#ifndef _PERSON_RUNNING_DETECTOR_H__
#define _PERSON_RUNNING_DETECTOR_H__

#include "event_detector_base.h"

#include <deque>
#include <map>

namespace algorithm::cdky {

/* 人员奔跑事件配置。 */
struct PersonRunningConfig : public BaseEventConfig {
    float bodyHeightSpeedThreshold = 2.0F;
    float durationThreshold = 1.0F;
    std::vector<std::string> targetLabels{"person"};
};

/* 目标跟踪信息。 */
struct TargetTrackInfo {
    uint64_t trackId = 0;
    float lastX = 0.0F;
    float lastY = 0.0F;
    float lastHeight = 0.0F;
    uint64_t lastTimeMs = 0;
    std::deque<float> speedHistory;
    bool isRunning = false;
    uint64_t runningStartTimeMs = 0;
};

class PersonRunningEventDetector : public BaseEventDetector {
public:
    PersonRunningEventDetector(const std::string& name,
                               const std::string& cameraId,
                               const PersonRunningConfig& config)
        : BaseEventDetector(name, cameraId), m_config(config) {}

    bool detectEvent(CFrameMeta* frameMeta, EventInfo& eventInfo) override;
    BaseEventConfig* getConfig() override { return &m_config; }
    void reset() override;

private:
    float calculateSpeed(const CObjectMeta* obj, TargetTrackInfo& trackInfo, uint64_t currentTimeMs);
    void cleanupOldTracks(uint64_t currentTimeMs);

private:
    PersonRunningConfig m_config;
    std::map<uint64_t, TargetTrackInfo> m_trackInfoMap;
};

} // namespace algorithm::cdky

#endif // _PERSON_RUNNING_DETECTOR_H__
