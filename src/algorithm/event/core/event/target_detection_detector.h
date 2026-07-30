#ifndef _TARGET_DETECTION_DETECTOR_H__
#define _TARGET_DETECTION_DETECTOR_H__

#include "event_detector_base.h"

#include <deque>
#include <map>

namespace algorithm::cdky {

/* 目标检测事件配置。 */
struct TargetDetectionConfig : public BaseEventConfig {
    std::vector<std::string> detectLabels;
    float targetRequiredTime = 2.0F;
    int targetRequiredFrame = 12;
    float maxSearchTime = 4.0F;
};

/* 类别事件状态。 */
struct ClassEventState {
    std::string labelName;
    int internalIndex = -1;
    bool isWaitingForTarget = false;
    uint64_t eventStartTimeMs = 0;
    uint64_t lastTargetTimeMs = 0;
    std::vector<CObjectMeta*> currentFrameObjects;
    bool hasValidTargetsInCurrentFrame = false;
    uint64_t currentFrameIndex = 0;
    std::deque<uint64_t> detectionTimestampsMs;
};

class TargetDetectionEventDetector : public BaseEventDetector {
public:
    TargetDetectionEventDetector(const std::string& name,
                                 const std::string& cameraId,
                                 const TargetDetectionConfig& config)
        : BaseEventDetector(name, cameraId), m_config(config), m_nextInternalIndex(0) {
        for (const std::string& label : m_config.detectLabels) {
            if (m_labelToIndexMap.find(label) == m_labelToIndexMap.end()) {
                m_labelToIndexMap[label] = m_nextInternalIndex++;
            }
        }
    }

    bool detectEvent(CFrameMeta* frameMeta, EventInfo& eventInfo) override;
    BaseEventConfig* getConfig() override { return &m_config; }
    void reset() override;

private:
    int ensureLabelIndex(const std::string& labelName);
    bool isActiveDetectLabel(const std::string& labelName) const;
    void handleNewTarget(CFrameMeta* frameMeta);
    void checkAndTriggerEvent(CFrameMeta* frameMeta, EventInfo& eventInfo, bool& triggered);
    void resetClassState(ClassEventState& classState);
    bool hasValidTargets(const std::vector<CObjectMeta*>& objects);

private:
    TargetDetectionConfig m_config;
    std::map<std::string, ClassEventState> m_classStates;
    std::map<std::string, int> m_labelToIndexMap;
    int m_nextInternalIndex;
};

} // namespace algorithm::cdky

#endif // _TARGET_DETECTION_DETECTOR_H__
