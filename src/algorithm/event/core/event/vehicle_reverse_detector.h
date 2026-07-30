#ifndef _VEHICLE_REVERSE_DETECTOR_H__
#define _VEHICLE_REVERSE_DETECTOR_H__

#include "event_detector_base.h"

#include <map>

namespace algorithm::cdky {

/* 车辆逆行事件配置。 */
struct VehicleReverseConfig : public BaseEventConfig {
    std::vector<std::string> targetLabels{"car", "truck", "bus"};
    std::string normalDirection;
    float reverseThreshold = 0.8F;
    float durationThreshold = 2.0F;
};

/* 车辆轨迹信息。 */
struct VehicleTrackInfo {
    uint64_t trackId = 0;
    float startX = 0.0F;
    float startY = 0.0F;
    float currentX = 0.0F;
    float currentY = 0.0F;
    uint64_t startTimeMs = 0;
    uint64_t lastTimeMs = 0;
    bool isReversing = false;
};

class VehicleReverseEventDetector : public BaseEventDetector {
public:
    VehicleReverseEventDetector(const std::string& name,
                                const std::string& cameraId,
                                const VehicleReverseConfig& config)
        : BaseEventDetector(name, cameraId), m_config(config) {}

    bool detectEvent(CFrameMeta* frameMeta, EventInfo& eventInfo) override;
    BaseEventConfig* getConfig() override { return &m_config; }
    void reset() override;

private:
    bool isReverse(const VehicleTrackInfo& trackInfo);
    void cleanupOldTracks(uint64_t currentTimeMs);

private:
    VehicleReverseConfig m_config;
    std::map<uint64_t, VehicleTrackInfo> m_trackInfoMap;
};

} // namespace algorithm::cdky

#endif // _VEHICLE_REVERSE_DETECTOR_H__
