#ifndef _CROWD_GATHER_DETECTOR_H__
#define _CROWD_GATHER_DETECTOR_H__

#include "event_detector_base.h"

namespace algorithm::cdky {

/* 人群聚集事件配置。 */
struct CrowdGatherConfig : public BaseEventConfig {
    int personCountThreshold = 5;
    float durationThreshold = 3.0F;
    std::vector<std::string> targetLabels{"person"};
};

/* 人群聚集状态。 */
struct CrowdGatherState {
    uint64_t crowdStartTimeMs = 0;
    uint64_t lastQualifiedTimeMs = 0;
    int currentPersonCount = 0;
    bool isCrowdGathering = false;
};

class CrowdGatherEventDetector : public BaseEventDetector {
public:
    CrowdGatherEventDetector(const std::string& name,
                             const std::string& cameraId,
                             const CrowdGatherConfig& config)
        : BaseEventDetector(name, cameraId), m_config(config) {}

    bool detectEvent(CFrameMeta* frameMeta, EventInfo& eventInfo) override;
    BaseEventConfig* getConfig() override { return &m_config; }
    void reset() override;

private:
    int countPersonInRoi(const std::vector<CObjectMeta*>& objects);

private:
    CrowdGatherConfig m_config;
    CrowdGatherState m_state;
};

} // namespace algorithm::cdky

#endif // _CROWD_GATHER_DETECTOR_H__
