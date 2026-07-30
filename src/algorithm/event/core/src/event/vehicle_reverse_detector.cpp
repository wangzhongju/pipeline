#define PL_LOG_ID PL_LOG_OTHERS

#define MEDIA_AGENT_LOGGER_NAME "cdky.event"
#include "common/Logger.h"
#include "vehicle_reverse_detector.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace algorithm::cdky {

bool VehicleReverseEventDetector::detectEvent(CFrameMeta* frameMeta, EventInfo& eventInfo) {
    if (!m_config.enabled) {
        LOG_DEBUG("vehicle_reverse disabled detector={}", m_name.c_str());
        return false;
    }

    const uint64_t currentTimeMs = getEventTimestamp(frameMeta);
    cleanupOldTracks(currentTimeMs);
    std::set<uint64_t> seenTrackIds;

    for (CObjectMeta* obj : frameMeta->objs) {
        if (!obj) {
            continue;
        }
        if (std::find(m_config.targetLabels.begin(), m_config.targetLabels.end(), obj->objLable) ==
            m_config.targetLabels.end()) {
            continue;
        }
        if (!objectPassesRoi(obj)) {
            continue;
        }

        const uint64_t trackId =
            (obj->trackerId > 0) ? obj->trackerId : static_cast<uint64_t>(obj->classId);
        if (trackId == 0) {
            continue;
        }
        seenTrackIds.insert(trackId);

        const float centerX = obj->detectorBboxInfo.left + obj->detectorBboxInfo.width * 0.5F;
        const float centerY = obj->detectorBboxInfo.top + obj->detectorBboxInfo.height * 0.5F;

        auto it = m_trackInfoMap.find(trackId);
        if (it == m_trackInfoMap.end()) {
            VehicleTrackInfo trackInfo;
            trackInfo.trackId = trackId;
            trackInfo.startX = centerX;
            trackInfo.startY = centerY;
            trackInfo.currentX = centerX;
            trackInfo.currentY = centerY;
            trackInfo.startTimeMs = currentTimeMs;
            trackInfo.lastTimeMs = currentTimeMs;
            trackInfo.isReversing = false;
            m_trackInfoMap[trackId] = trackInfo;
            continue;
        }

        VehicleTrackInfo& trackInfo = it->second;
        if (trackInfo.lastTimeMs == 0) {
            trackInfo.startX = centerX;
            trackInfo.startY = centerY;
            trackInfo.currentX = centerX;
            trackInfo.currentY = centerY;
            trackInfo.startTimeMs = currentTimeMs;
            trackInfo.lastTimeMs = currentTimeMs;
            trackInfo.isReversing = false;
            continue;
        }

        trackInfo.currentX = centerX;
        trackInfo.currentY = centerY;
        trackInfo.lastTimeMs = currentTimeMs;

        if (isReverse(trackInfo)) {
            if (!trackInfo.isReversing) {
                trackInfo.isReversing = true;
            }

            const uint64_t duration = currentTimeMs - trackInfo.startTimeMs;
            if (duration >= static_cast<uint64_t>(m_config.durationThreshold * 1000) &&
                checkEventInterval(currentTimeMs)) {
                fillBaseEventInfo(frameMeta, eventInfo, currentTimeMs, 0);
                eventInfo.objects.push_back(obj);
                fillEventDescription(eventInfo, obj->objLable);
                eventInfo.extraInfo["track_id"] = std::to_string(trackId);
                eventInfo.extraInfo["normal_direction"] = m_config.normalDirection;

                updateLastEventTime(currentTimeMs);
                m_trackInfoMap.erase(trackId);
                LOG_INFO("vehicle_reverse triggered detector={} camera_id={} track_id={} direction={}",
                         m_name.c_str(),
                         m_cameraId,
                         trackId,
                         m_config.normalDirection.c_str());
                return true;
            }
        } else {
            trackInfo.isReversing = false;
        }
    }

    for (auto& kv : m_trackInfoMap) {
        VehicleTrackInfo& trackInfo = kv.second;
        if (seenTrackIds.find(trackInfo.trackId) != seenTrackIds.end()) {
            continue;
        }
        if (!isWithinConfidenceGrace(currentTimeMs, trackInfo.lastTimeMs)) {
            trackInfo.isReversing = false;
            trackInfo.lastTimeMs = 0;
        }
    }

    return false;
}

bool VehicleReverseEventDetector::isReverse(const VehicleTrackInfo& trackInfo) {
    const float dx = trackInfo.currentX - trackInfo.startX;
    const float dy = trackInfo.currentY - trackInfo.startY;
    const float distance = std::sqrt(dx * dx + dy * dy);
    if (distance < 0.05F) {
        return false;
    }

    if (m_config.normalDirection == "left_to_right") {
        return dx < -m_config.reverseThreshold * distance;
    }
    if (m_config.normalDirection == "right_to_left") {
        return dx > m_config.reverseThreshold * distance;
    }
    if (m_config.normalDirection == "top_to_bottom") {
        return dy < -m_config.reverseThreshold * distance;
    }
    if (m_config.normalDirection == "bottom_to_top") {
        return dy > m_config.reverseThreshold * distance;
    }
    return false;
}

void VehicleReverseEventDetector::cleanupOldTracks(uint64_t currentTimeMs) {
    const uint64_t maxTrackAgeMs = 10000;
    for (auto it = m_trackInfoMap.begin(); it != m_trackInfoMap.end();) {
        if (currentTimeMs - it->second.lastTimeMs > maxTrackAgeMs) {
            it = m_trackInfoMap.erase(it);
        } else {
            ++it;
        }
    }
}

void VehicleReverseEventDetector::reset() {
    m_trackInfoMap.clear();
    LOG_DEBUG("vehicle_reverse reset detector={} camera_id={}", m_name.c_str(), m_cameraId);
}

} // namespace algorithm::cdky
