#define PL_LOG_ID PL_LOG_OTHERS

#define MEDIA_AGENT_LOGGER_NAME "cdky.event"
#include "common/Logger.h"
#include "crowd_gather_detector.h"

namespace algorithm::cdky {

bool CrowdGatherEventDetector::detectEvent(CFrameMeta* frameMeta, EventInfo& eventInfo) {
    if (!m_config.enabled) {
        LOG_DEBUG("crowd_gather disabled detector={}", m_name.c_str());
        return false;
    }

    const uint64_t currentTimeMs = getEventTimestamp(frameMeta);
    const int personCount = countPersonInRoi(frameMeta->objs);
    m_state.currentPersonCount = personCount;
    const uint64_t durationMs = static_cast<uint64_t>(m_config.durationThreshold * 1000);

    if (personCount >= m_config.personCountThreshold) {
        m_state.lastQualifiedTimeMs = currentTimeMs;
        if (!m_state.isCrowdGathering) {
            m_state.isCrowdGathering = true;
            m_state.crowdStartTimeMs = currentTimeMs;
        }
    } else {
        if (m_state.isCrowdGathering &&
            isWithinConfidenceGrace(currentTimeMs, m_state.lastQualifiedTimeMs)) {
            return false;
        }
        m_state.isCrowdGathering = false;
        m_state.crowdStartTimeMs = 0;
        m_state.lastQualifiedTimeMs = 0;
    }

    if (personCount >= m_config.personCountThreshold &&
        m_state.isCrowdGathering && m_state.crowdStartTimeMs > 0) {
        const uint64_t gatherDuration = currentTimeMs - m_state.crowdStartTimeMs;
        if (gatherDuration >= durationMs && checkEventInterval(currentTimeMs)) {
            fillBaseEventInfo(frameMeta, eventInfo, currentTimeMs, 0);
            eventInfo.objects = frameMeta->objs;
            fillEventDescription(eventInfo, firstTargetLabel(m_config.targetLabels));
            eventInfo.extraInfo["person_count"] = std::to_string(personCount);
            eventInfo.extraInfo["duration"] = std::to_string(gatherDuration / 1000) + "s";

            updateLastEventTime(currentTimeMs);
            m_state.isCrowdGathering = false;
            m_state.crowdStartTimeMs = 0;
            LOG_INFO("crowd_gather triggered detector={} camera_id={} person_count={} duration_ms={}",
                     m_name.c_str(),
                     m_cameraId,
                     personCount,
                     gatherDuration);
            return true;
        }
    }

    return false;
}

int CrowdGatherEventDetector::countPersonInRoi(const std::vector<CObjectMeta*>& objects) {
    int count = 0;
    for (const CObjectMeta* obj : objects) {
        if (!obj || !labelMatches(m_config.targetLabels, obj->objLable)) {
            continue;
        }
        if (!objectPassesRoi(obj)) {
            continue;
        }
        ++count;
    }
    return count;
}

void CrowdGatherEventDetector::reset() {
    m_state.crowdStartTimeMs = 0;
    m_state.lastQualifiedTimeMs = 0;
    m_state.currentPersonCount = 0;
    m_state.isCrowdGathering = false;
    LOG_DEBUG("crowd_gather reset detector={} camera_id={}", m_name.c_str(), m_cameraId);
}

} // namespace algorithm::cdky
