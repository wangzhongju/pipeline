#ifndef _EVENT_DETECTOR_FACTORY_H__
#define _EVENT_DETECTOR_FACTORY_H__

#include "crowd_gather_detector.h"
#include "event_detector_base.h"
#include "person_leave_detector.h"
#include "person_running_detector.h"
#include "target_detection_detector.h"
#include "vehicle_parking_detector.h"
#include "vehicle_reverse_detector.h"

#include <map>
#include <memory>
#include <set>
#include <vector>

#include <yaml-cpp/yaml.h>

namespace algorithm::cdky {

class EventDetectorFactory {
public:
    static std::map<std::string, std::vector<std::unique_ptr<BaseEventDetector>>> createAllDetectors(
        const std::string& configFile);

    static std::unique_ptr<BaseEventDetector> createDetectorByTag(const std::string& tag,
                                                                   const std::string& cameraId,
                                                                   const YAML::Node& config);

    static std::unique_ptr<BaseEventDetector> createDynamicTargetDetectionDetector(
        const std::string& tag,
        const std::string& cameraId,
        const YAML::Node& config,
        const std::vector<std::string>& targetLabels,
        const std::string& englishDescription,
        const std::string& chineseDescription);

    static std::set<std::string> parseDeviceModel(const std::string& deviceModel);

private:
    static YAML::Node findEventTypeDefaults(const YAML::Node& rootConfig,
                                            const std::string& eventType);
    static std::vector<RoiArea> parseRoiAreas(const YAML::Node& roiAreasNode);
    static void applyBaseConfig(const YAML::Node& config,
                                const YAML::Node& defaultConfig,
                                BaseEventConfig& detectorConfig,
                                const std::string& fallbackEventType);

    static std::unique_ptr<BaseEventDetector> createTargetDetectionDetector(const std::string& tag,
                                                                            const std::string& cameraId,
                                                                            const YAML::Node& defaultConfig,
                                                                            const YAML::Node& config);

    static std::unique_ptr<BaseEventDetector> createPersonLeaveDetector(const std::string& tag,
                                                                        const std::string& cameraId,
                                                                        const YAML::Node& defaultConfig,
                                                                        const YAML::Node& config);

    static std::unique_ptr<BaseEventDetector> createCrowdGatherDetector(const std::string& tag,
                                                                        const std::string& cameraId,
                                                                        const YAML::Node& defaultConfig,
                                                                        const YAML::Node& config);

    static std::unique_ptr<BaseEventDetector> createPersonRunningDetector(const std::string& tag,
                                                                          const std::string& cameraId,
                                                                          const YAML::Node& defaultConfig,
                                                                          const YAML::Node& config);

    static std::unique_ptr<BaseEventDetector> createVehicleReverseDetector(const std::string& tag,
                                                                           const std::string& cameraId,
                                                                           const YAML::Node& defaultConfig,
                                                                           const YAML::Node& config);

    static std::unique_ptr<BaseEventDetector> createVehicleParkingDetector(const std::string& tag,
                                                                           const std::string& cameraId,
                                                                           const YAML::Node& defaultConfig,
                                                                           const YAML::Node& config);
};

} // namespace algorithm::cdky

#endif // _EVENT_DETECTOR_FACTORY_H__
