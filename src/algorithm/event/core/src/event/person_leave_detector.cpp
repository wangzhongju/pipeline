#define PL_LOG_ID PL_LOG_OTHERS

#define MEDIA_AGENT_LOGGER_NAME "cdky.event"
#include "common/Logger.h"
#include "person_leave_detector.h"

#include <ctime>

namespace algorithm::cdky {

bool PersonLeaveEventDetector::detectEvent(CFrameMeta* frameMeta, EventInfo& eventInfo) {
    if (!m_config.enabled) {
        LOG_DEBUG("person_leave disabled detector={}", m_name.c_str());
        return false;
    }
    if (!isInActiveTimeRange()) {
        LOG_TRACE("person_leave outside active time detector={} camera_id={}",
                  m_name.c_str(),
                  m_cameraId);
        return false;
    }

    const uint64_t currentTimeMs = getEventTimestamp(frameMeta);
    const bool personInRoi = isPersonInRoi(frameMeta->objs);

    if (personInRoi) {
        m_state.personPresent = true;
        m_state.lastPresentTimeMs = currentTimeMs;
        m_state.isLeaving = false;
        m_state.leaveStartTimeMs = 0;
        return false;
    }

    if (m_state.personPresent) {
        if (isWithinConfidenceGrace(currentTimeMs, m_state.lastPresentTimeMs)) {
            m_state.isLeaving = false;
            m_state.leaveStartTimeMs = 0;
            return false;
        }

        if (!m_state.isLeaving) {
            m_state.isLeaving = true;
            m_state.leaveStartTimeMs = currentTimeMs;
        }

        const uint64_t leaveTimeMs = currentTimeMs - m_state.leaveStartTimeMs;
        const uint64_t thresholdMs = static_cast<uint64_t>(m_config.leaveTimeThreshold * 1000);
        if (leaveTimeMs >= thresholdMs && checkEventInterval(currentTimeMs)) {
            fillBaseEventInfo(frameMeta, eventInfo, currentTimeMs, 0);
            fillEventDescription(eventInfo, firstTargetLabel(m_config.targetLabels));
            eventInfo.extraInfo["leave_duration"] = std::to_string(leaveTimeMs / 1000) + "s";

            updateLastEventTime(currentTimeMs);
            m_state.personPresent = false;
            m_state.isLeaving = false;
            LOG_INFO("person_leave triggered detector={} camera_id={} leave_ms={}",
                     m_name.c_str(),
                     m_cameraId,
                     leaveTimeMs);
            return true;
        }
    }

    return false;
}

bool PersonLeaveEventDetector::isInActiveTimeRange() {
    if (m_config.activeTimeRanges.empty()) {
        return true;
    }

    const auto now = std::chrono::system_clock::now();
    const auto timeValue = std::chrono::system_clock::to_time_t(now);
    const std::tm localTm = *std::localtime(&timeValue);

    for (const auto& range : m_config.activeTimeRanges) {
        if (range.isInRange(localTm.tm_hour, localTm.tm_min)) {
            return true;
        }
    }
    return false;
}

bool PersonLeaveEventDetector::isPersonInRoi(const std::vector<CObjectMeta*>& objects) {
    for (const CObjectMeta* obj : objects) {
        if (!obj || !labelMatches(m_config.targetLabels, obj->objLable)) {
            continue;
        }
        if (objectPassesRoi(obj)) {
            return true;
        }
    }
    return false;
}

void PersonLeaveEventDetector::reset() {
    m_state.personPresent = false;
    m_state.lastPresentTimeMs = 0;
    m_state.leaveStartTimeMs = 0;
    m_state.isLeaving = false;
    LOG_DEBUG("person_leave reset detector={} camera_id={}", m_name.c_str(), m_cameraId);
}

} // namespace algorithm::cdky
