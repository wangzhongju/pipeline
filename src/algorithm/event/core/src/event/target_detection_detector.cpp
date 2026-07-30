#define PL_LOG_ID PL_LOG_OTHERS

#define MEDIA_AGENT_LOGGER_NAME "cdky.event"
#include "common/Logger.h"
#include "target_detection_detector.h"

#include <algorithm>

namespace algorithm::cdky {

int TargetDetectionEventDetector::ensureLabelIndex(const std::string& labelName) {
    const auto it = m_labelToIndexMap.find(labelName);
    if (it != m_labelToIndexMap.end()) {
        return it->second;
    }

    const int index = m_nextInternalIndex++;
    m_labelToIndexMap[labelName] = index;
    return index;
}

bool TargetDetectionEventDetector::isActiveDetectLabel(const std::string& labelName) const {
    return std::find(m_config.detectLabels.begin(), m_config.detectLabels.end(), labelName) !=
           m_config.detectLabels.end();
}

bool TargetDetectionEventDetector::detectEvent(CFrameMeta* frameMeta, EventInfo& eventInfo) {
    if (!m_config.enabled) {
        LOG_DEBUG("target_detection disabled detector={}", m_name.c_str());
        return false;
    }

    if (!frameMeta->objs.empty() && hasValidTargets(frameMeta->objs)) {
        handleNewTarget(frameMeta);
    }

    bool triggered = false;
    checkAndTriggerEvent(frameMeta, eventInfo, triggered);
    return triggered;
}

void TargetDetectionEventDetector::handleNewTarget(CFrameMeta* frameMeta) {
    const uint64_t currentTimeMs = getEventTimestamp(frameMeta);

    for (CObjectMeta* obj : frameMeta->objs) {
        if (!obj || obj->objLable.empty()) {
            continue;
        }
        if (!isActiveDetectLabel(obj->objLable)) {
            continue;
        }
        if (!objectPassesRoi(obj)) {
            continue;
        }

        const std::string labelName = obj->objLable;
        if (m_classStates.find(labelName) == m_classStates.end()) {
            ClassEventState newState;
            newState.labelName = labelName;
            newState.internalIndex = ensureLabelIndex(labelName);
            m_classStates[labelName] = newState;
        }

        auto& classState = m_classStates[labelName];
        if (!checkEventInterval(currentTimeMs)) {
            continue;
        }

        if (!classState.isWaitingForTarget) {
            classState.isWaitingForTarget = true;
            classState.eventStartTimeMs = currentTimeMs;
            classState.currentFrameObjects.clear();
            classState.hasValidTargetsInCurrentFrame = false;
            classState.currentFrameIndex = frameMeta->index;
            classState.lastTargetTimeMs = currentTimeMs;
            classState.detectionTimestampsMs.clear();
            LOG_DEBUG("target_detection start waiting detector={} label={} camera_id={}",
                      m_name.c_str(),
                      labelName.c_str(),
                      m_cameraId);
        }
    }
}

void TargetDetectionEventDetector::checkAndTriggerEvent(CFrameMeta* frameMeta,
                                                        EventInfo& eventInfo,
                                                        bool& triggered) {
    const uint64_t currentTimeMs = getEventTimestamp(frameMeta);
    triggered = false;

    for (auto& kv : m_classStates) {
        const std::string labelName = kv.first;
        auto& classState = kv.second;
        if (!isActiveDetectLabel(labelName)) {
            resetClassState(classState);
            continue;
        }
        if (!classState.isWaitingForTarget) {
            continue;
        }

        classState.currentFrameIndex = frameMeta->index;
        classState.currentFrameObjects.clear();
        classState.hasValidTargetsInCurrentFrame = false;

        for (CObjectMeta* obj : frameMeta->objs) {
            if (!obj || obj->objLable != labelName) {
                continue;
            }
            if (!objectPassesRoi(obj)) {
                continue;
            }
            classState.currentFrameObjects.push_back(obj);
            classState.hasValidTargetsInCurrentFrame = true;
            classState.lastTargetTimeMs = currentTimeMs;
        }

        if (classState.hasValidTargetsInCurrentFrame) {
            classState.detectionTimestampsMs.push_back(currentTimeMs);
        }

        const uint64_t windowMs = static_cast<uint64_t>(m_config.targetRequiredTime * 1000);
        while (!classState.detectionTimestampsMs.empty() &&
               currentTimeMs - classState.detectionTimestampsMs.front() > windowMs) {
            classState.detectionTimestampsMs.pop_front();
        }

        if (!classState.hasValidTargetsInCurrentFrame) {
            if (!isWithinConfidenceGrace(currentTimeMs, classState.lastTargetTimeMs)) {
                resetClassState(classState);
            }
            continue;
        }

        if (!classState.detectionTimestampsMs.empty()) {
            const uint64_t windowSpan =
                classState.detectionTimestampsMs.back() - classState.detectionTimestampsMs.front();
            if (windowSpan >= windowMs &&
                static_cast<int>(classState.detectionTimestampsMs.size()) < m_config.targetRequiredFrame) {
                resetClassState(classState);
                continue;
            }
        }

        if (!classState.detectionTimestampsMs.empty()) {
            const uint64_t timeSinceLastTarget = currentTimeMs - classState.detectionTimestampsMs.back();
            const uint64_t maxNoTargetAfterWindow =
                static_cast<uint64_t>((m_config.maxSearchTime - m_config.targetRequiredTime) * 1000);
            if (timeSinceLastTarget > maxNoTargetAfterWindow) {
                resetClassState(classState);
                continue;
            }
        }

        const uint64_t noTargetTimeMs = currentTimeMs - classState.lastTargetTimeMs;
        if (noTargetTimeMs > static_cast<uint64_t>(m_config.maxSearchTime * 1000)) {
            resetClassState(classState);
            continue;
        }

        if (static_cast<int>(classState.detectionTimestampsMs.size()) >= m_config.targetRequiredFrame &&
            classState.hasValidTargetsInCurrentFrame) {
            fillBaseEventInfo(frameMeta, eventInfo, currentTimeMs, classState.internalIndex);
            eventInfo.frameIndex = classState.currentFrameIndex;
            eventInfo.labelName = labelName;
            eventInfo.objects = classState.currentFrameObjects;
            fillEventDescription(eventInfo, labelName);

            updateLastEventTime(currentTimeMs);
            resetClassState(classState);
            triggered = true;
            LOG_INFO("target_detection triggered detector={} label={} camera_id={}",
                     m_name.c_str(),
                     labelName.c_str(),
                     m_cameraId);
            return;
        }
    }
}

void TargetDetectionEventDetector::resetClassState(ClassEventState& classState) {
    classState.isWaitingForTarget = false;
    classState.eventStartTimeMs = 0;
    classState.lastTargetTimeMs = 0;
    classState.currentFrameObjects.clear();
    classState.hasValidTargetsInCurrentFrame = false;
    classState.detectionTimestampsMs.clear();
}

bool TargetDetectionEventDetector::hasValidTargets(const std::vector<CObjectMeta*>& objects) {
    for (const CObjectMeta* obj : objects) {
        if (!obj || obj->objLable.empty()) {
            continue;
        }
        if (!isActiveDetectLabel(obj->objLable)) {
            continue;
        }
        if (!objectPassesRoi(obj)) {
            continue;
        }
        return true;
    }
    return false;
}

void TargetDetectionEventDetector::reset() {
    m_classStates.clear();
    LOG_DEBUG("target_detection reset detector={} camera_id={}", m_name.c_str(), m_cameraId);
}

} // namespace algorithm::cdky
