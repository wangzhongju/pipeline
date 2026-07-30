#define PL_LOG_ID PL_LOG_OTHERS

#define MEDIA_AGENT_LOGGER_NAME "cdky.event"
#include "common/Logger.h"
#include "person_running_detector.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace algorithm::cdky {

bool PersonRunningEventDetector::detectEvent(CFrameMeta* frameMeta, EventInfo& eventInfo) {
    if (!m_config.enabled) {
        LOG_DEBUG("person_running disabled detector={}", m_name.c_str());
        return false;
    }

    const uint64_t currentTimeMs = getEventTimestamp(frameMeta);
    cleanupOldTracks(currentTimeMs);
    std::set<uint64_t> seenTrackIds;

    for (CObjectMeta* obj : frameMeta->objs) {
        if (!obj || !labelMatches(m_config.targetLabels, obj->objLable)) {
            continue;
        }
        if (!objectPassesRoi(obj)) {
            continue;
        }

        const uint64_t trackId = (obj->trackerId > 0) ? obj->trackerId : static_cast<uint64_t>(obj->classId);
        if (trackId == 0) {
            continue;
        }
        seenTrackIds.insert(trackId);

        auto it = m_trackInfoMap.find(trackId);
        if (it == m_trackInfoMap.end()) {
            TargetTrackInfo trackInfo;
            trackInfo.trackId = trackId;
            trackInfo.lastX = obj->detectorBboxInfo.left + obj->detectorBboxInfo.width * 0.5F;
            trackInfo.lastY = obj->detectorBboxInfo.top + obj->detectorBboxInfo.height * 0.5F;
            trackInfo.lastHeight = obj->detectorBboxInfo.height;
            trackInfo.lastTimeMs = currentTimeMs;
            m_trackInfoMap[trackId] = trackInfo;
            continue;
        }

        TargetTrackInfo& trackInfo = it->second;
        if (trackInfo.lastTimeMs == 0) {
            trackInfo.lastX = obj->detectorBboxInfo.left + obj->detectorBboxInfo.width * 0.5F;
            trackInfo.lastY = obj->detectorBboxInfo.top + obj->detectorBboxInfo.height * 0.5F;
            trackInfo.lastHeight = obj->detectorBboxInfo.height;
            trackInfo.lastTimeMs = currentTimeMs;
            continue;
        }

        const float speed = calculateSpeed(obj, trackInfo, currentTimeMs);
        if (speed < 0.0F) {
            continue;
        }

        trackInfo.speedHistory.push_back(speed);
        if (trackInfo.speedHistory.size() > 10) {
            trackInfo.speedHistory.pop_front();
        }

        float avgSpeed = 0.0F;
        for (const float historySpeed : trackInfo.speedHistory) {
            avgSpeed += historySpeed;
        }
        avgSpeed /= static_cast<float>(trackInfo.speedHistory.size());

        if (avgSpeed >= m_config.bodyHeightSpeedThreshold) {
            if (!trackInfo.isRunning) {
                trackInfo.isRunning = true;
                trackInfo.runningStartTimeMs = currentTimeMs;
            }

            const uint64_t runningDuration = currentTimeMs - trackInfo.runningStartTimeMs;
            const uint64_t durationThresholdMs = static_cast<uint64_t>(m_config.durationThreshold * 1000);
            if (runningDuration >= durationThresholdMs && checkEventInterval(currentTimeMs)) {
                fillBaseEventInfo(frameMeta, eventInfo, currentTimeMs, 0);
                eventInfo.objects.push_back(obj);
                fillEventDescription(eventInfo, obj->objLable);
                eventInfo.extraInfo["speed"] = std::to_string(avgSpeed) + " body/s";
                eventInfo.extraInfo["speed_unit"] = "body_height_per_s";
                eventInfo.extraInfo["track_id"] = std::to_string(trackId);

                updateLastEventTime(currentTimeMs);
                trackInfo.isRunning = false;
                LOG_INFO("person_running triggered detector={} camera_id={} track_id={} speed={}",
                         m_name.c_str(),
                         m_cameraId,
                         trackId,
                         avgSpeed);
                return true;
            }
        } else {
            trackInfo.isRunning = false;
            trackInfo.runningStartTimeMs = 0;
        }
    }

    for (auto& kv : m_trackInfoMap) {
        TargetTrackInfo& trackInfo = kv.second;
        if (seenTrackIds.find(trackInfo.trackId) != seenTrackIds.end()) {
            continue;
        }
        if (!isWithinConfidenceGrace(currentTimeMs, trackInfo.lastTimeMs)) {
            trackInfo.isRunning = false;
            trackInfo.runningStartTimeMs = 0;
            trackInfo.speedHistory.clear();
            trackInfo.lastTimeMs = 0;
        }
    }

    return false;
}

float PersonRunningEventDetector::calculateSpeed(const CObjectMeta* obj,
                                                 TargetTrackInfo& trackInfo,
                                                 uint64_t currentTimeMs) {
    const float currentX = obj->detectorBboxInfo.left + obj->detectorBboxInfo.width * 0.5F;
    const float currentY = obj->detectorBboxInfo.top + obj->detectorBboxInfo.height * 0.5F;
    const float currentHeight = obj->detectorBboxInfo.height;

    const float dx = currentX - trackInfo.lastX;
    const float dy = currentY - trackInfo.lastY;
    const float distance = std::sqrt(dx * dx + dy * dy);

    const uint64_t dt = currentTimeMs - trackInfo.lastTimeMs;
    if (dt == 0) {
        return -1.0F;
    }
    const float dtSec = static_cast<float>(dt) / 1000.0F;
    const float refHeight = std::max(0.05F, (trackInfo.lastHeight + currentHeight) * 0.5F);
    const float speed = (distance / refHeight) / dtSec;

    trackInfo.lastX = currentX;
    trackInfo.lastY = currentY;
    trackInfo.lastHeight = currentHeight;
    trackInfo.lastTimeMs = currentTimeMs;
    return speed;
}

void PersonRunningEventDetector::cleanupOldTracks(uint64_t currentTimeMs) {
    const uint64_t maxTrackAgeMs = 5000;
    for (auto it = m_trackInfoMap.begin(); it != m_trackInfoMap.end();) {
        if (currentTimeMs - it->second.lastTimeMs > maxTrackAgeMs) {
            it = m_trackInfoMap.erase(it);
        } else {
            ++it;
        }
    }
}

void PersonRunningEventDetector::reset() {
    m_trackInfoMap.clear();
    LOG_DEBUG("person_running reset detector={} camera_id={}", m_name.c_str(), m_cameraId);
}

} // namespace algorithm::cdky
