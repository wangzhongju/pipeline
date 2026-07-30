#ifndef _VEHICLE_PARKING_DETECTOR_H__
#define _VEHICLE_PARKING_DETECTOR_H__

#include "event_detector_base.h"

#include <map>

namespace algorithm::cdky {

/* 车辆违停事件配置。 */
struct VehicleParkingConfig : public BaseEventConfig {
    float parkingTimeThreshold = 30.0F;
    std::vector<TimeRange> activeTimeRanges;
    std::vector<std::string> targetLabels{"car", "truck", "bus"};
};

/* 车辆违停状态。 */
struct VehicleParkingState {
    bool vehiclePresent = false;
    uint64_t parkingStartTimeMs = 0;
    uint64_t lastVehicleTimeMs = 0;
    bool isParking = false;
    std::string currentVehicleLabel;
};

class VehicleParkingEventDetector : public BaseEventDetector {
public:
    VehicleParkingEventDetector(const std::string& name,
                                const std::string& cameraId,
                                const VehicleParkingConfig& config)
        : BaseEventDetector(name, cameraId), m_config(config) {}

    bool detectEvent(CFrameMeta* frameMeta, EventInfo& eventInfo) override;
    BaseEventConfig* getConfig() override { return &m_config; }
    void reset() override;

private:
    bool isInActiveTimeRange();
    bool isVehicleInRoi(const std::vector<CObjectMeta*>& objects, std::string& vehicleLabel);

private:
    VehicleParkingConfig m_config;
    VehicleParkingState m_state;
};

} // namespace algorithm::cdky

#endif // _VEHICLE_PARKING_DETECTOR_H__
