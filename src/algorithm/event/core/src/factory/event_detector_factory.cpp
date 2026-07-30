#define PL_LOG_ID PL_LOG_OTHERS

#define MEDIA_AGENT_LOGGER_NAME "cdky.event"
#include "common/Logger.h"
#include "event_detector_factory.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace algorithm::cdky {

namespace {

std::string toLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

RoiMode parseRoiMode(const YAML::Node& modeNode) {
    if (!modeNode || modeNode.IsNull()) {
        return RoiMode::Unknown;
    }

    if (modeNode.IsScalar()) {
        const std::string raw = modeNode.as<std::string>();
        const std::string lower = toLowerCopy(raw);

        if (lower == "1" || lower == "include" || lower == "roi_include") {
            return RoiMode::Include;
        }
        if (lower == "2" || lower == "exclude" || lower == "roi_exclude") {
            return RoiMode::Exclude;
        }
        if (lower == "0" || lower == "unknown" || lower == "roi_mode_unknown") {
            return RoiMode::Unknown;
        }
    }

    try {
        const int modeValue = modeNode.as<int>();
        if (modeValue == 1) {
            return RoiMode::Include;
        }
        if (modeValue == 2) {
            return RoiMode::Exclude;
        }
    } catch (const std::exception&) {
        // keep unknown
    }

    return RoiMode::Unknown;
}

bool isDefinedNode(const YAML::Node& node);

bool parsePointNode(const YAML::Node& pointNode, RoiPoint2D& outPoint) {
    if (!pointNode || !pointNode.IsMap()) {
        return false;
    }
    if (!isDefinedNode(pointNode["x"]) || !isDefinedNode(pointNode["y"])) {
        return false;
    }

    outPoint.x = clampNormalized(pointNode["x"].as<float>());
    outPoint.y = clampNormalized(pointNode["y"].as<float>());
    return true;
}

bool isDefinedNode(const YAML::Node& node) {
    try {
        return node.IsDefined() && !node.IsNull();
    } catch (const std::exception&) {
        return false;
    }
}

YAML::Node selectConfigNode(const YAML::Node& config,
                            const YAML::Node& defaultConfig,
                            const char* key) {
    if (config) {
        const YAML::Node node = config[key];
        if (isDefinedNode(node)) {
            return node;
        }
    }
    if (defaultConfig) {
        const YAML::Node node = defaultConfig[key];
        if (isDefinedNode(node)) {
            return node;
        }
    }
    return YAML::Node();
}

std::vector<std::string> parseLabelListNode(const YAML::Node& node) {
    if (!isDefinedNode(node)) {
        return {};
    }
    if (node.IsSequence()) {
        return node.as<std::vector<std::string>>();
    }
    if (node.IsScalar()) {
        return {node.as<std::string>()};
    }
    return {};
}

std::vector<std::string> selectLabelList(const YAML::Node& config,
                                         const YAML::Node& defaultConfig,
                                         const char* preferredKey,
                                         const char* legacyKey,
                                         const std::vector<std::string>& fallback = {}) {
    if (const YAML::Node node = selectConfigNode(config, defaultConfig, preferredKey);
        isDefinedNode(node)) {
        return parseLabelListNode(node);
    }
    if (legacyKey != nullptr) {
        if (const YAML::Node node = selectConfigNode(config, defaultConfig, legacyKey);
            isDefinedNode(node)) {
            return parseLabelListNode(node);
        }
    }
    return fallback;
}

} // namespace

std::map<std::string, std::vector<std::unique_ptr<BaseEventDetector>>> EventDetectorFactory::createAllDetectors(
    const std::string& configFile) {
    (void)configFile;
    std::map<std::string, std::vector<std::unique_ptr<BaseEventDetector>>> allDetectors;
    return allDetectors;
}

std::unique_ptr<BaseEventDetector> EventDetectorFactory::createDetectorByTag(const std::string& tag,
                                                                              const std::string& cameraId,
                                                                              const YAML::Node& config) {
    try {
        if (!config["event_detectors"]) {
            LOG_ERROR("EventDetectorFactory: no 'event_detectors' section in config");
            return nullptr;
        }

        const YAML::Node detectorsNode = config["event_detectors"];
        if (!detectorsNode[tag]) {
            LOG_DEBUG("EventDetectorFactory: tag '{}' not found in config", tag.c_str());
            return nullptr;
        }

        const YAML::Node detectorConfig = detectorsNode[tag];
        if (isDefinedNode(detectorConfig["enabled"]) && !detectorConfig["enabled"].as<bool>()) {
            LOG_DEBUG("EventDetectorFactory: tag '{}' is disabled", tag.c_str());
            return nullptr;
        }

        std::string eventType = "target_detection";
        if (isDefinedNode(detectorConfig["event_type"])) {
            eventType = detectorConfig["event_type"].as<std::string>();
        }
        const YAML::Node defaultConfig = findEventTypeDefaults(config, eventType);

        if (eventType == "target_detection") {
            return createTargetDetectionDetector(tag, cameraId, defaultConfig, detectorConfig);
        }
        if (eventType == "person_leave") {
            return createPersonLeaveDetector(tag, cameraId, defaultConfig, detectorConfig);
        }
        if (eventType == "crowd_gather") {
            return createCrowdGatherDetector(tag, cameraId, defaultConfig, detectorConfig);
        }
        if (eventType == "person_running") {
            return createPersonRunningDetector(tag, cameraId, defaultConfig, detectorConfig);
        }
        if (eventType == "vehicle_reverse") {
            return createVehicleReverseDetector(tag, cameraId, defaultConfig, detectorConfig);
        }
        if (eventType == "vehicle_parking") {
            return createVehicleParkingDetector(tag, cameraId, defaultConfig, detectorConfig);
        }

        LOG_ERROR("EventDetectorFactory: unknown event type '{}'", eventType.c_str());
        return nullptr;
    } catch (const std::exception& ex) {
        LOG_ERROR("EventDetectorFactory: failed to create detector for tag '{}': {}",
                  tag.c_str(),
                  ex.what());
        return nullptr;
    }
}

std::unique_ptr<BaseEventDetector> EventDetectorFactory::createDynamicTargetDetectionDetector(
    const std::string& tag,
    const std::string& cameraId,
    const YAML::Node& config,
    const std::vector<std::string>& targetLabels,
    const std::string& englishDescription,
    const std::string& chineseDescription) {
    if (targetLabels.empty()) {
        LOG_WARN("EventDetectorFactory: dynamic target_detection rejected empty labels tag={}", tag.c_str());
        return nullptr;
    }

    try {
        const YAML::Node defaultConfig = findEventTypeDefaults(config, "target_detection");
        TargetDetectionConfig detectorConfig;
        applyBaseConfig(YAML::Node(), defaultConfig, detectorConfig, "target_detection");
        detectorConfig.eventType = "target_detection";
        detectorConfig.detectLabels = targetLabels;

        const std::string english = englishDescription.empty() ? tag : englishDescription;
        const std::string chinese = chineseDescription.empty() ? english : chineseDescription;
        detectorConfig.labelDescMap["default"] = {english, chinese};

        return std::make_unique<TargetDetectionEventDetector>(tag, cameraId, detectorConfig);
    } catch (const std::exception& ex) {
        LOG_ERROR("EventDetectorFactory: failed to create dynamic target_detection tag='{}': {}",
                  tag.c_str(),
                  ex.what());
        return nullptr;
    }
}

YAML::Node EventDetectorFactory::findEventTypeDefaults(const YAML::Node& rootConfig,
                                                       const std::string& eventType) {
    if (!rootConfig || !isDefinedNode(rootConfig["event_type_defaults"])) {
        return YAML::Node();
    }
    const YAML::Node defaultsRoot = rootConfig["event_type_defaults"];
    if (!defaultsRoot || !defaultsRoot.IsMap()) {
        return YAML::Node();
    }
    return defaultsRoot[eventType];
}

std::set<std::string> EventDetectorFactory::parseDeviceModel(const std::string& deviceModel) {
    std::set<std::string> tags;
    if (deviceModel.empty()) {
        return tags;
    }

    std::istringstream input(deviceModel);
    std::string token;
    while (std::getline(input, token, ',')) {
        const size_t first = token.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            continue;
        }
        const size_t last = token.find_last_not_of(" \t\r\n");
        token = token.substr(first, last - first + 1);
        if (!token.empty()) {
            tags.insert(token);
        }
    }
    return tags;
}

std::vector<RoiArea> EventDetectorFactory::parseRoiAreas(const YAML::Node& roiAreasNode) {
    std::vector<RoiArea> roiAreas;
    if (!roiAreasNode || !roiAreasNode.IsSequence()) {
        return roiAreas;
    }

    for (const YAML::Node& areaNode : roiAreasNode) {
        if (!areaNode || !areaNode.IsMap()) {
            continue;
        }

        RoiArea area;
        area.enabled = isDefinedNode(areaNode["enabled"]) ? areaNode["enabled"].as<bool>() : true;
        area.mode = parseRoiMode(areaNode["mode"]);
        area.confidence =
            clampNormalized(isDefinedNode(areaNode["confidence"]) ? areaNode["confidence"].as<float>() : 0.0F);

        if (const YAML::Node rectNode = areaNode["rect"]; isDefinedNode(rectNode)) {
            area.hasRect = true;
            area.rect.cx = clampNormalized(isDefinedNode(rectNode["cx"]) ? rectNode["cx"].as<float>() : 0.0F);
            area.rect.cy = clampNormalized(isDefinedNode(rectNode["cy"]) ? rectNode["cy"].as<float>() : 0.0F);
            area.rect.width =
                clampNormalized(isDefinedNode(rectNode["width"]) ? rectNode["width"].as<float>() : 0.0F);
            area.rect.height =
                clampNormalized(isDefinedNode(rectNode["height"]) ? rectNode["height"].as<float>() : 0.0F);
            area.rect.angle = isDefinedNode(rectNode["angle"]) ? rectNode["angle"].as<float>() : 0.0F;
        }

        if (!area.hasRect) {
            YAML::Node pointsNode;
            if (isDefinedNode(areaNode["poly"])) {
                pointsNode = isDefinedNode(areaNode["poly"]["points"]) ? areaNode["poly"]["points"] : areaNode["poly"];
            }

            if (pointsNode && pointsNode.IsSequence()) {
                for (const YAML::Node& pointNode : pointsNode) {
                    RoiPoint2D point;
                    if (parsePointNode(pointNode, point)) {
                        area.poly.points.push_back(point);
                    }
                }
                if (area.poly.points.size() >= 3) {
                    area.hasPoly = true;
                }
            }
        }

        if (!area.hasRect && !area.hasPoly) {
            continue;
        }

        roiAreas.push_back(std::move(area));
    }

    return roiAreas;
}

void EventDetectorFactory::applyBaseConfig(const YAML::Node& config,
                                           const YAML::Node& defaultConfig,
                                           BaseEventConfig& detectorConfig,
                                           const std::string& fallbackEventType) {
    detectorConfig.eventType = fallbackEventType;
    if (const YAML::Node node = selectConfigNode(config, defaultConfig, "event_type"); isDefinedNode(node)) {
        detectorConfig.eventType = node.as<std::string>();
    }
    if (const YAML::Node node = selectConfigNode(config, defaultConfig, "enabled"); isDefinedNode(node)) {
        detectorConfig.enabled = node.as<bool>();
    }
    if (const YAML::Node node = selectConfigNode(config, defaultConfig, "event_interval_ms"); isDefinedNode(node)) {
        detectorConfig.eventIntervalMs = node.as<int>();
    }
    if (const YAML::Node node = selectConfigNode(config, defaultConfig, "confidence_threshold"); isDefinedNode(node)) {
        detectorConfig.confidenceThreshold = clampNormalized(node.as<float>());
    }
    if (const YAML::Node node = selectConfigNode(config, defaultConfig, "confidence_grace_ms"); isDefinedNode(node)) {
        detectorConfig.confidenceGraceMs = std::max(0, node.as<int>());
    }
    if (const YAML::Node node = selectConfigNode(config, defaultConfig, "roi_areas"); isDefinedNode(node)) {
        detectorConfig.roiAreas = parseRoiAreas(node);
    }
    if (const YAML::Node labelDescs = selectConfigNode(config, defaultConfig, "label_descriptions");
        isDefinedNode(labelDescs) && labelDescs.IsMap()) {
        for (YAML::const_iterator it = labelDescs.begin(); it != labelDescs.end(); ++it) {
            const std::string label = it->first.as<std::string>();
            if (!it->second || !it->second.IsMap()) {
                continue;
            }
            const std::string english =
                it->second["english"] ? it->second["english"].as<std::string>() : "";
            const std::string chinese =
                it->second["chinese"] ? it->second["chinese"].as<std::string>() : "";
            detectorConfig.labelDescMap[label] = {english, chinese};
        }
    }
}

std::unique_ptr<BaseEventDetector> EventDetectorFactory::createTargetDetectionDetector(
    const std::string& tag,
    const std::string& cameraId,
    const YAML::Node& defaultConfig,
    const YAML::Node& config) {
    TargetDetectionConfig detectorConfig;
    applyBaseConfig(config, defaultConfig, detectorConfig, tag);

    const auto targetLabels = selectLabelList(config, defaultConfig, "target_labels", "detect_labels");
    if (!targetLabels.empty()) {
        detectorConfig.detectLabels = targetLabels;
    }
    if (const YAML::Node node = selectConfigNode(config, defaultConfig, "target_required_time"); isDefinedNode(node)) {
        detectorConfig.targetRequiredTime = node.as<float>();
    }
    if (const YAML::Node node = selectConfigNode(config, defaultConfig, "target_required_frame"); isDefinedNode(node)) {
        detectorConfig.targetRequiredFrame = node.as<int>();
    }
    if (const YAML::Node node = selectConfigNode(config, defaultConfig, "max_search_time"); isDefinedNode(node)) {
        detectorConfig.maxSearchTime = node.as<float>();
    }

    return std::make_unique<TargetDetectionEventDetector>(tag, cameraId, detectorConfig);
}

std::unique_ptr<BaseEventDetector> EventDetectorFactory::createPersonLeaveDetector(
    const std::string& tag,
    const std::string& cameraId,
    const YAML::Node& defaultConfig,
    const YAML::Node& config) {
    PersonLeaveConfig detectorConfig;
    applyBaseConfig(config, defaultConfig, detectorConfig, tag);

    if (const YAML::Node node = selectConfigNode(config, defaultConfig, "leave_time_threshold"); isDefinedNode(node)) {
        detectorConfig.leaveTimeThreshold = node.as<float>();
    }
    const auto targetLabels =
        selectLabelList(config, defaultConfig, "target_labels", "target_label", detectorConfig.targetLabels);
    if (!targetLabels.empty()) {
        detectorConfig.targetLabels = targetLabels;
    }
    if (const YAML::Node ranges = selectConfigNode(config, defaultConfig, "active_time_ranges"); isDefinedNode(ranges)) {
        for (size_t i = 0; i < ranges.size(); ++i) {
            TimeRange timeRange;
            timeRange.startHour = ranges[i]["start_hour"].as<int>();
            timeRange.startMinute = ranges[i]["start_minute"].as<int>();
            timeRange.endHour = ranges[i]["end_hour"].as<int>();
            timeRange.endMinute = ranges[i]["end_minute"].as<int>();
            detectorConfig.activeTimeRanges.push_back(timeRange);
        }
    }

    return std::make_unique<PersonLeaveEventDetector>(tag, cameraId, detectorConfig);
}

std::unique_ptr<BaseEventDetector> EventDetectorFactory::createCrowdGatherDetector(
    const std::string& tag,
    const std::string& cameraId,
    const YAML::Node& defaultConfig,
    const YAML::Node& config) {
    CrowdGatherConfig detectorConfig;
    applyBaseConfig(config, defaultConfig, detectorConfig, tag);

    if (const YAML::Node node = selectConfigNode(config, defaultConfig, "person_count_threshold"); isDefinedNode(node)) {
        detectorConfig.personCountThreshold = node.as<int>();
    }
    if (const YAML::Node node = selectConfigNode(config, defaultConfig, "duration_threshold"); isDefinedNode(node)) {
        detectorConfig.durationThreshold = node.as<float>();
    }
    const auto targetLabels =
        selectLabelList(config, defaultConfig, "target_labels", "target_label", detectorConfig.targetLabels);
    if (!targetLabels.empty()) {
        detectorConfig.targetLabels = targetLabels;
    }
    return std::make_unique<CrowdGatherEventDetector>(tag, cameraId, detectorConfig);
}

std::unique_ptr<BaseEventDetector> EventDetectorFactory::createPersonRunningDetector(
    const std::string& tag,
    const std::string& cameraId,
    const YAML::Node& defaultConfig,
    const YAML::Node& config) {
    PersonRunningConfig detectorConfig;
    applyBaseConfig(config, defaultConfig, detectorConfig, tag);

    if (isDefinedNode(selectConfigNode(config, defaultConfig, "speed_threshold"))) {
        throw std::runtime_error(
            "person_running config uses removed key 'speed_threshold'; "
            "please use 'body_height_speed_threshold'");
    }
    if (const YAML::Node node = selectConfigNode(config, defaultConfig, "body_height_speed_threshold"); isDefinedNode(node)) {
        detectorConfig.bodyHeightSpeedThreshold = node.as<float>();
    }
    if (const YAML::Node node = selectConfigNode(config, defaultConfig, "duration_threshold"); isDefinedNode(node)) {
        detectorConfig.durationThreshold = node.as<float>();
    }
    const auto targetLabels =
        selectLabelList(config, defaultConfig, "target_labels", "target_label", detectorConfig.targetLabels);
    if (!targetLabels.empty()) {
        detectorConfig.targetLabels = targetLabels;
    }
    if (isDefinedNode(selectConfigNode(config, defaultConfig, "pixel_to_meter_ratio"))) {
        throw std::runtime_error(
            "person_running config uses removed key 'pixel_to_meter_ratio'; "
            "use 'body_height_speed_threshold' with unit body_height_per_s");
    }

    return std::make_unique<PersonRunningEventDetector>(tag, cameraId, detectorConfig);
}

std::unique_ptr<BaseEventDetector> EventDetectorFactory::createVehicleReverseDetector(
    const std::string& tag,
    const std::string& cameraId,
    const YAML::Node& defaultConfig,
    const YAML::Node& config) {
    VehicleReverseConfig detectorConfig;
    applyBaseConfig(config, defaultConfig, detectorConfig, tag);

    if (const YAML::Node node = selectConfigNode(config, defaultConfig, "target_labels"); isDefinedNode(node)) {
        detectorConfig.targetLabels = node.as<std::vector<std::string>>();
    }
    if (const YAML::Node node = selectConfigNode(config, defaultConfig, "normal_direction"); isDefinedNode(node)) {
        detectorConfig.normalDirection = node.as<std::string>();
    }
    if (const YAML::Node node = selectConfigNode(config, defaultConfig, "reverse_threshold"); isDefinedNode(node)) {
        detectorConfig.reverseThreshold = node.as<float>();
    }
    if (const YAML::Node node = selectConfigNode(config, defaultConfig, "duration_threshold"); isDefinedNode(node)) {
        detectorConfig.durationThreshold = node.as<float>();
    }

    return std::make_unique<VehicleReverseEventDetector>(tag, cameraId, detectorConfig);
}

std::unique_ptr<BaseEventDetector> EventDetectorFactory::createVehicleParkingDetector(
    const std::string& tag,
    const std::string& cameraId,
    const YAML::Node& defaultConfig,
    const YAML::Node& config) {
    VehicleParkingConfig detectorConfig;
    applyBaseConfig(config, defaultConfig, detectorConfig, tag);

    if (const YAML::Node node = selectConfigNode(config, defaultConfig, "parking_time_threshold"); isDefinedNode(node)) {
        detectorConfig.parkingTimeThreshold = node.as<float>();
    }
    if (const YAML::Node node = selectConfigNode(config, defaultConfig, "target_labels"); isDefinedNode(node)) {
        detectorConfig.targetLabels = node.as<std::vector<std::string>>();
    }
    if (const YAML::Node ranges = selectConfigNode(config, defaultConfig, "active_time_ranges"); isDefinedNode(ranges)) {
        for (size_t i = 0; i < ranges.size(); ++i) {
            TimeRange timeRange;
            timeRange.startHour = ranges[i]["start_hour"].as<int>();
            timeRange.startMinute = ranges[i]["start_minute"].as<int>();
            timeRange.endHour = ranges[i]["end_hour"].as<int>();
            timeRange.endMinute = ranges[i]["end_minute"].as<int>();
            detectorConfig.activeTimeRanges.push_back(timeRange);
        }
    }

    return std::make_unique<VehicleParkingEventDetector>(tag, cameraId, detectorConfig);
}

} // namespace algorithm::cdky
