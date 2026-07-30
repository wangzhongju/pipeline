#include "pipeline_event.h"

#include "common/Config.h"
#define PIPELINE_ALGORITHM_LOGGER_NAME "pipeline.event"
#include "common/Logger.h"
#include "RequestModelPackage.h"
#include "event_detector_factory.h"

#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr const char* kDefaultEventConfigName = "Event.yaml";
constexpr const char* kRepoFallbackConfigPath =
    "src/algorithm/config/Event.yaml";

float clampUnit(float value) {
    return std::max(0.0F, std::min(1.0F, value));
}

algorithm::cdky::RoiMode toCoreRoiMode(event_roi_mode_t mode) {
    switch (mode) {
    case EVENT_ROI_INCLUDE:
        return algorithm::cdky::RoiMode::Include;
    case EVENT_ROI_EXCLUDE:
        return algorithm::cdky::RoiMode::Exclude;
    case EVENT_ROI_MODE_UNKNOWN:
    default:
        return algorithm::cdky::RoiMode::Unknown;
    }
}

algorithm::cdky::RoiArea toCoreRoiArea(const event_roi_area_t& area) {
    algorithm::cdky::RoiArea output;
    output.enabled = area.enabled != 0;
    output.mode = toCoreRoiMode(area.mode);
    output.confidence = clampUnit(area.confidence);

    if (area.shape_type == EVENT_ROI_SHAPE_RECT) {
        output.hasRect = true;
        output.rect.cx = clampUnit(area.rect.cx);
        output.rect.cy = clampUnit(area.rect.cy);
        output.rect.width = clampUnit(area.rect.width);
        output.rect.height = clampUnit(area.rect.height);
        output.rect.angle = area.rect.angle;
    } else if (area.shape_type == EVENT_ROI_SHAPE_POLY) {
        if (area.poly.points && area.poly.point_count >= 3) {
            output.hasPoly = true;
            output.poly.points.reserve(area.poly.point_count);
            for (size_t idx = 0; idx < area.poly.point_count; ++idx) {
                algorithm::cdky::RoiPoint2D point;
                point.x = clampUnit(area.poly.points[idx].x);
                point.y = clampUnit(area.poly.points[idx].y);
                output.poly.points.push_back(point);
            }
        }
    }

    return output;
}

std::vector<algorithm::cdky::RoiArea> toCoreRoiAreas(const event_roi_area_t* roiAreas,
                                                      size_t roiAreaCount) {
    std::vector<algorithm::cdky::RoiArea> output;
    if (!roiAreas || roiAreaCount == 0) {
        return output;
    }

    output.reserve(roiAreaCount);
    for (size_t idx = 0; idx < roiAreaCount; ++idx) {
        algorithm::cdky::RoiArea area = toCoreRoiArea(roiAreas[idx]);
        if (!area.hasRect && !area.hasPoly) {
            continue;
        }
        output.push_back(std::move(area));
    }
    return output;
}

struct RequestTargetDetectionOverrides {
    std::vector<std::string> targetLabels;
    std::string englishDescription;
    std::string chineseDescription;
};

struct RequestConfigFileIdentity {
    uintmax_t size = 0;
    std::filesystem::file_time_type lastWriteTime{};

    bool operator==(const RequestConfigFileIdentity& other) const {
        return size == other.size && lastWriteTime == other.lastWriteTime;
    }
};

struct RequestConfigCacheEntry {
    RequestConfigFileIdentity identity;
    RequestTargetDetectionOverrides overrides;
};

using RequestConfigCache = std::map<std::string, RequestConfigCacheEntry>;

std::string makeRequestConfigCacheKey(const std::string& path) {
    std::error_code ec;
    const std::filesystem::path absolutePath = std::filesystem::absolute(path, ec);
    if (ec) {
        return path;
    }
    return absolutePath.lexically_normal().string();
}

bool getRequestConfigFileIdentity(const std::string& path, RequestConfigFileIdentity& identity) {
    std::error_code ec;
    const std::filesystem::path filePath(path);
    const auto status = std::filesystem::status(filePath, ec);
    if (ec || !std::filesystem::is_regular_file(status)) {
        return false;
    }

    const uintmax_t size = std::filesystem::file_size(filePath, ec);
    if (ec) {
        return false;
    }
    const std::filesystem::file_time_type lastWriteTime =
        std::filesystem::last_write_time(filePath, ec);
    if (ec) {
        return false;
    }

    identity.size = size;
    identity.lastWriteTime = lastWriteTime;
    return true;
}

std::vector<std::string> parseJsonStringList(const nlohmann::json& root, const char* key) {
    std::vector<std::string> output;
    const auto it = root.find(key);
    if (it == root.end() || it->is_null()) {
        return output;
    }

    if (it->is_array()) {
        output.reserve(it->size());
        for (const auto& value : *it) {
            if (value.is_string()) {
                const std::string item = value.get<std::string>();
                if (!item.empty()) {
                    output.push_back(item);
                }
            }
        }
    } else if (it->is_string()) {
        const std::string item = it->get<std::string>();
        if (!item.empty()) {
            output.push_back(item);
        }
    }

    return output;
}

std::string parseJsonString(const nlohmann::json& root, const char* key) {
    const auto it = root.find(key);
    if (it == root.end() || !it->is_string()) {
        return {};
    }
    return it->get<std::string>();
}

bool loadRequestConfigJson(const char* path, nlohmann::json& root, std::string& errorMessage) {
    const std::string configPath = path ? path : "";
    if (algorithm::cdky::event_request_package::isEncryptedPackage(configPath)) {
        return algorithm::cdky::event_request_package::loadRootConfigJson(
            configPath,
            root,
            &errorMessage);
    }

    std::ifstream input(configPath);
    if (!input.is_open()) {
        errorMessage = "open failed";
        return false;
    }

    try {
        root = nlohmann::json::parse(input);
    } catch (const std::exception& ex) {
        errorMessage = ex.what();
        return false;
    }
    return true;
}

RequestTargetDetectionOverrides parseRequestTargetDetectionOverrides(const event_request_t& request,
                                                                     const nlohmann::json& root) {
    RequestTargetDetectionOverrides overrides;

    try {
        std::vector<std::string> configLabels = parseJsonStringList(root, "target_labels");
        if (!configLabels.empty()) {
            const auto countIt = root.find("target_label_count");
            if (countIt != root.end() && countIt->is_number_unsigned()) {
                const size_t configuredCount = countIt->get<size_t>();
                if (configuredCount != configLabels.size()) {
                    LOG_WARN("event request config target_label_count mismatch path={} count={} labels={}",
                             request.config_path,
                             configuredCount,
                             configLabels.size());
                }
            }
            overrides.targetLabels = std::move(configLabels);
        }

        const std::string english = parseJsonString(root, "english_description");
        if (!english.empty()) {
            overrides.englishDescription = english;
        }
        const std::string chinese = parseJsonString(root, "chinese_description");
        if (!chinese.empty()) {
            overrides.chineseDescription = chinese;
        }
    } catch (const std::exception& ex) {
        LOG_WARN("event request config parse failed path={} err={}", request.config_path, ex.what());
    }

    return overrides;
}

RequestTargetDetectionOverrides resolveRequestTargetDetectionOverrides(const event_request_t& request,
                                                                       RequestConfigCache& cache) {
    RequestTargetDetectionOverrides overrides;

    if (!request.config_path || request.config_path[0] == '\0') {
        return overrides;
    }

    const std::string configPath = request.config_path;
    const std::string cacheKey = makeRequestConfigCacheKey(configPath);
    RequestConfigFileIdentity identity;
    const bool hasIdentity = getRequestConfigFileIdentity(configPath, identity);
    if (hasIdentity) {
        const auto it = cache.find(cacheKey);
        if (it != cache.end() && it->second.identity == identity) {
            return it->second.overrides;
        }
    } else {
        cache.erase(cacheKey);
    }

    nlohmann::json root;
    std::string errorMessage;
    if (!loadRequestConfigJson(request.config_path, root, errorMessage)) {
        LOG_WARN("event request config load failed path={} err={}", request.config_path, errorMessage);
        cache.erase(cacheKey);
        return overrides;
    }

    overrides = parseRequestTargetDetectionOverrides(request, root);

    RequestConfigFileIdentity loadedIdentity;
    if (getRequestConfigFileIdentity(configPath, loadedIdentity)) {
        cache[cacheKey] = RequestConfigCacheEntry{loadedIdentity, overrides};
    }

    return overrides;
}

std::string resolveEventConfigPath(const event_config_t* config) {
    const std::string explicitPath =
        (config && config->config_path && config->config_path[0] != '\0')
            ? std::string(config->config_path)
            : std::string();
    return algorithm::cdky::resolveConfigPath(
        kDefaultEventConfigName,
        explicitPath,
        {kRepoFallbackConfigPath});
}

std::string requestedEventConfigPath(const event_config_t* config) {
    if (config && config->config_path && config->config_path[0] != '\0') {
        return std::string(config->config_path);
    }
    return std::string(kDefaultEventConfigName);
}

std::string resolveEventDescription(const algorithm::cdky::EventInfo& eventInfo) {
    const auto englishIt = eventInfo.extraInfo.find("english_desc");
    if (englishIt != eventInfo.extraInfo.end() && !englishIt->second.empty()) {
        return englishIt->second;
    }

    const auto chineseIt = eventInfo.extraInfo.find("chinese_desc");
    if (chineseIt != eventInfo.extraInfo.end() && !chineseIt->second.empty()) {
        return chineseIt->second;
    }

    return eventInfo.eventType;
}

struct DetectorConfigBackup {
    bool valid = false;
    std::vector<algorithm::cdky::RoiArea> roiAreas;
    float confidenceThreshold = 0.0F;
    int eventIntervalMs = 0;
    bool hasTargetDetectionConfig = false;
    std::vector<std::string> detectLabels;
    std::map<std::string, std::pair<std::string, std::string>> labelDescMap;
};

DetectorConfigBackup backupDetectorConfig(algorithm::cdky::BaseEventDetector* detector) {
    DetectorConfigBackup backup;
    if (!detector) {
        return backup;
    }

    algorithm::cdky::BaseEventConfig* config = detector->getConfig();
    if (!config) {
        return backup;
    }

    backup.valid = true;
    backup.roiAreas = config->roiAreas;
    backup.confidenceThreshold = config->confidenceThreshold;
    backup.eventIntervalMs = config->eventIntervalMs;
    backup.labelDescMap = config->labelDescMap;
    if (auto* targetConfig = dynamic_cast<algorithm::cdky::TargetDetectionConfig*>(config)) {
        backup.hasTargetDetectionConfig = true;
        backup.detectLabels = targetConfig->detectLabels;
    }
    return backup;
}

void restoreDetectorConfig(algorithm::cdky::BaseEventDetector* detector, const DetectorConfigBackup& backup) {
    if (!detector || !backup.valid) {
        return;
    }

    algorithm::cdky::BaseEventConfig* config = detector->getConfig();
    if (!config) {
        return;
    }

    config->roiAreas = backup.roiAreas;
    config->confidenceThreshold = backup.confidenceThreshold;
    config->eventIntervalMs = backup.eventIntervalMs;
    config->labelDescMap = backup.labelDescMap;
    if (backup.hasTargetDetectionConfig) {
        if (auto* targetConfig = dynamic_cast<algorithm::cdky::TargetDetectionConfig*>(config)) {
            targetConfig->detectLabels = backup.detectLabels;
        }
    }
}

void applyRequestOverrides(algorithm::cdky::BaseEventDetector* detector,
                           const event_request_t& request,
                           const RequestTargetDetectionOverrides& targetOverrides) {
    if (!detector) {
        return;
    }

    algorithm::cdky::BaseEventConfig* config = detector->getConfig();
    if (!config) {
        return;
    }

    if (request.has_roi_override != 0) {
        config->roiAreas = toCoreRoiAreas(request.roi_areas, request.roi_area_count);
    }
    if (request.confidence_threshold > 0.0F) {
        config->confidenceThreshold = clampUnit(request.confidence_threshold);
    }
    if (request.event_interval_ms > 0) {
        config->eventIntervalMs = request.event_interval_ms;
    }

    auto* targetConfig = dynamic_cast<algorithm::cdky::TargetDetectionConfig*>(config);
    if (!targetConfig || config->eventType != "target_detection") {
        return;
    }

    if (!targetOverrides.targetLabels.empty()) {
        targetConfig->detectLabels = targetOverrides.targetLabels;
    }

    if (!targetOverrides.englishDescription.empty() || !targetOverrides.chineseDescription.empty()) {
        config->labelDescMap.clear();
        config->labelDescMap["default"] = {
            targetOverrides.englishDescription,
            targetOverrides.chineseDescription};
    }
}

} // namespace

struct EventAlarmBuffers {
    std::vector<std::string> alarmEventNames;
    std::vector<std::string> alarmDescriptions;
    std::vector<std::vector<event_object_t>> alarmObjects;
    std::vector<std::vector<std::string>> alarmObjectClassNames;
    std::vector<event_alarm_t> alarmViews;

    void reset() {
        alarmEventNames.clear();
        alarmDescriptions.clear();
        alarmObjects.clear();
        alarmObjectClassNames.clear();
        alarmViews.clear();
    }
};

struct event_handle_t {
    std::string configPath;
    YAML::Node config;
    std::mutex apiMutex;

    std::vector<std::string> supportedNames;
    std::vector<const char*> supportedNamePointers;

    std::map<std::string, std::map<std::string, std::unique_ptr<algorithm::cdky::BaseEventDetector>>> detectors;
    RequestConfigCache requestConfigCache;
    std::map<std::thread::id, EventAlarmBuffers> alarmBuffersByThread;

    bool loadConfig(const std::string& path) {
        configPath = path;
        try {
            config = YAML::LoadFile(path);
            algorithm::cdky::Logger::init(algorithm::cdky::AlgoConfig::loadFromFile(path).log,
                                          PIPELINE_ALGORITHM_LOGGER_NAME);
            LOG_INFO("event config loaded path={}", path.c_str());
            return true;
        } catch (const std::exception& ex) {
            LOG_ERROR("event config load failed path={} err={}", path.c_str(), ex.what());
        }

        return false;
    }

    void refreshSupportedNames() {
        supportedNames.clear();
        supportedNamePointers.clear();

        if (!config || !config["event_detectors"]) {
            LOG_WARN("event config has no event_detectors section path={}", configPath.c_str());
            return;
        }

        const YAML::Node detectorsNode = config["event_detectors"];
        for (YAML::const_iterator it = detectorsNode.begin(); it != detectorsNode.end(); ++it) {
            const std::string tag = it->first.as<std::string>();
            const YAML::Node detectorCfg = it->second;
            if (detectorCfg["enabled"] && !detectorCfg["enabled"].as<bool>()) {
                LOG_DEBUG("event detector disabled name={}", tag.c_str());
                continue;
            }
            supportedNames.push_back(tag);
        }

        std::sort(supportedNames.begin(), supportedNames.end());
        supportedNames.erase(std::unique(supportedNames.begin(), supportedNames.end()), supportedNames.end());
        for (const std::string& name : supportedNames) {
            supportedNamePointers.push_back(name.c_str());
        }
        LOG_INFO("event supported names refreshed count={}", supportedNames.size());
    }

    algorithm::cdky::BaseEventDetector* getOrCreateDetector(const std::string& cameraId,
                                                            const std::string& eventName,
                                                            const std::vector<std::string>& requestTargetLabels,
                                                            const std::string& englishDescription,
                                                            const std::string& chineseDescription) {
        auto& cameraDetectors = detectors[cameraId];
        auto it = cameraDetectors.find(eventName);
        if (it != cameraDetectors.end()) {
            return it->second.get();
        }

        bool isConfiguredEvent = false;
        if (config && config["event_detectors"]) {
            const YAML::Node detectorsNode = config["event_detectors"];
            isConfiguredEvent = static_cast<bool>(detectorsNode[eventName]);
        }

        auto detector = algorithm::cdky::EventDetectorFactory::createDetectorByTag(eventName, cameraId, config);
        if (!detector && !isConfiguredEvent && !requestTargetLabels.empty()) {
            detector = algorithm::cdky::EventDetectorFactory::createDynamicTargetDetectionDetector(
                eventName,
                cameraId,
                config,
                requestTargetLabels,
                englishDescription,
                chineseDescription);
        }
        if (!detector) {
            LOG_WARN("event_name={} not registered or disabled", eventName.c_str());
            return nullptr;
        }

        algorithm::cdky::BaseEventDetector* rawDetector = detector.get();
        cameraDetectors.emplace(eventName, std::move(detector));
        LOG_INFO("event detector created camera_id={} event_name={}", cameraId.c_str(), eventName.c_str());
        return rawDetector;
    }

    EventAlarmBuffers& alarmBuffersForCurrentThread() {
        return alarmBuffersByThread[std::this_thread::get_id()];
    }

    void resetAlarmBuffers() {
        for (auto& [threadId, buffers] : alarmBuffersByThread) {
            (void)threadId;
            buffers.reset();
        }
    }
};

extern "C" int event_create(const event_config_t* config, event_handle_t** outHandle) {
    if (!outHandle) {
        return -1;
    }
    *outHandle = nullptr;

    auto handle = std::make_unique<event_handle_t>();
    const std::string configPath = resolveEventConfigPath(config);
    if (configPath.empty()) {
        LOG_ERROR("event_create failed to resolve config requested={}",
                  requestedEventConfigPath(config).c_str());
        return -1;
    }
    if (!handle->loadConfig(configPath)) {
        return -1;
    }

    handle->refreshSupportedNames();
    *outHandle = handle.release();
    LOG_INFO("event_create success config_path={}", configPath.c_str());
    return 0;
}

extern "C" void event_destroy(event_handle_t* handle) {
    LOG_DEBUG("event_destroy handle={}", static_cast<const void*>(handle));
    delete handle;
}

extern "C" int event_reset(event_handle_t* handle) {
    if (!handle) {
        LOG_WARN("event_reset rejected null handle");
        return -1;
    }

    std::lock_guard<std::mutex> lock(handle->apiMutex);
    for (auto& [cameraId, detectorMap] : handle->detectors) {
        (void)cameraId;
        for (auto& [eventName, detector] : detectorMap) {
            (void)eventName;
            if (detector) {
                detector->reset();
                detector->resetEventInterval();
            }
        }
    }

    handle->resetAlarmBuffers();
    LOG_INFO("event_reset success");
    return 0;
}

extern "C" int event_list_supported_names(event_handle_t* handle,
                                           const char*** outNames,
                                           size_t* outCount) {
    if (!handle || !outNames || !outCount) {
        LOG_WARN("event_list_supported_names rejected invalid args handle={} out_names={} out_count={}",
                 static_cast<const void*>(handle),
                 static_cast<const void*>(outNames),
                 static_cast<const void*>(outCount));
        return -1;
    }

    *outNames = handle->supportedNamePointers.empty() ? nullptr : handle->supportedNamePointers.data();
    *outCount = handle->supportedNamePointers.size();
    return 0;
}

extern "C" int event_process(event_handle_t* handle,
                              const event_frame_desc_t* frameDesc,
                              const event_object_t* objects,
                              size_t objectCount,
                              const event_request_t* requests,
                              size_t requestCount,
                              const event_alarm_t** outAlarms,
                              size_t* outAlarmCount) {
    if (!handle || !frameDesc || !outAlarms || !outAlarmCount) {
        LOG_WARN("event_process rejected invalid args handle={} frame={} out_alarms={} out_count={}",
                 static_cast<const void*>(handle),
                 static_cast<const void*>(frameDesc),
                 static_cast<const void*>(outAlarms),
                 static_cast<const void*>(outAlarmCount));
        return -1;
    }
    if (objectCount > 0 && !objects) {
        LOG_WARN("event_process rejected null objects object_count={}", objectCount);
        return -1;
    }
    if (requestCount > 0 && !requests) {
        LOG_WARN("event_process rejected null requests request_count={}", requestCount);
        return -1;
    }

    std::lock_guard<std::mutex> lock(handle->apiMutex);
    EventAlarmBuffers& alarmBuffers = handle->alarmBuffersForCurrentThread();
    alarmBuffers.reset();

    std::vector<algorithm::cdky::CObjectMeta> objectPool;
    objectPool.reserve(objectCount);

    algorithm::cdky::CFrameMeta frameMeta;
    frameMeta.index = 0;
    frameMeta.timestampMs = frameDesc->timestamp_ms;
    const std::string cameraId =
        (frameDesc->camera_id && frameDesc->camera_id[0] != '\0') ? frameDesc->camera_id : "0";
    frameMeta.cameraId = cameraId;

    for (size_t idx = 0; idx < objectCount; ++idx) {
        const event_object_t& inputObject = objects[idx];

        algorithm::cdky::CObjectMeta objectMeta;
        objectMeta.objLable = inputObject.class_name ? inputObject.class_name : "";
        objectMeta.classId = inputObject.class_id;
        objectMeta.trackerId = inputObject.tracker_id > 0 ? static_cast<uint64_t>(inputObject.tracker_id) : 0;
        objectMeta.detectorConfidence = inputObject.confidence;
        objectMeta.detectorBboxInfo.left = clampUnit(inputObject.x - inputObject.width * 0.5F);
        objectMeta.detectorBboxInfo.top = clampUnit(inputObject.y - inputObject.height * 0.5F);
        objectMeta.detectorBboxInfo.width = clampUnit(inputObject.width);
        objectMeta.detectorBboxInfo.height = clampUnit(inputObject.height);

        if (objectMeta.detectorBboxInfo.left + objectMeta.detectorBboxInfo.width > 1.0F) {
            objectMeta.detectorBboxInfo.width =
                std::max(0.0F, 1.0F - objectMeta.detectorBboxInfo.left);
        }
        if (objectMeta.detectorBboxInfo.top + objectMeta.detectorBboxInfo.height > 1.0F) {
            objectMeta.detectorBboxInfo.height =
                std::max(0.0F, 1.0F - objectMeta.detectorBboxInfo.top);
        }

        objectPool.push_back(std::move(objectMeta));
    }

    frameMeta.objs.reserve(objectPool.size());
    for (algorithm::cdky::CObjectMeta& objectMeta : objectPool) {
        frameMeta.objs.push_back(&objectMeta);
    }

    std::set<std::string> processedNames;
    LOG_TRACE("event_process camera_id={} timestamp_ms={} object_count={} request_count={}",
              cameraId,
              frameDesc->timestamp_ms,
              objectCount,
              requestCount);

    for (size_t idx = 0; idx < requestCount; ++idx) {
        const event_request_t& request = requests[idx];
        const std::string eventName = request.event_name ? request.event_name : "";
        if (eventName.empty()) {
            LOG_DEBUG("event_process skipped empty event_name request_index={}", idx);
            continue;
        }
        if (!processedNames.insert(eventName).second) {
            LOG_DEBUG("event_process skipped duplicate event_name={}", eventName.c_str());
            continue;
        }

        const RequestTargetDetectionOverrides targetOverrides =
            resolveRequestTargetDetectionOverrides(request, handle->requestConfigCache);

        algorithm::cdky::BaseEventDetector* detector =
            handle->getOrCreateDetector(cameraId,
                                        eventName,
                                        targetOverrides.targetLabels,
                                        targetOverrides.englishDescription,
                                        targetOverrides.chineseDescription);
        if (!detector) {
            continue;
        }
        detector->setCameraId(cameraId);

        const bool hasRoiOverride = request.has_roi_override != 0;
        const bool hasConfidenceOverride = request.confidence_threshold > 0.0F;
        const bool hasEventIntervalOverride = request.event_interval_ms > 0;
        const algorithm::cdky::BaseEventConfig* detectorConfig = detector->getConfig();
        const bool isTargetDetection =
            detectorConfig && detectorConfig->eventType == "target_detection";
        const bool hasTargetLabelOverride = isTargetDetection && !targetOverrides.targetLabels.empty();
        const bool hasDescriptionOverride =
            isTargetDetection &&
            (!targetOverrides.englishDescription.empty() || !targetOverrides.chineseDescription.empty());
        DetectorConfigBackup configBackup;
        if (hasRoiOverride || hasConfidenceOverride || hasEventIntervalOverride ||
            hasTargetLabelOverride || hasDescriptionOverride) {
            configBackup = backupDetectorConfig(detector);
            applyRequestOverrides(detector, request, targetOverrides);
            LOG_DEBUG("event_process applied request override event_name={} roi_count={} confidence_override={} event_interval_override={} target_label_count={} description_override={}",
                      eventName.c_str(),
                      hasRoiOverride ? request.roi_area_count : 0,
                      hasConfidenceOverride,
                      hasEventIntervalOverride ? request.event_interval_ms : 0,
                      hasTargetLabelOverride ? targetOverrides.targetLabels.size() : 0,
                      hasDescriptionOverride);
        }

        algorithm::cdky::EventInfo eventInfo;
        const bool triggered = detector->detectEvent(&frameMeta, eventInfo);

        if (hasRoiOverride || hasConfidenceOverride || hasEventIntervalOverride ||
            hasTargetLabelOverride || hasDescriptionOverride) {
            restoreDetectorConfig(detector, configBackup);
            LOG_DEBUG("event_process restored request override event_name={}", eventName.c_str());
        }
        if (!triggered) {
            continue;
        }

        LOG_INFO("event triggered camera_id={} event_name={} object_count={}",
                 cameraId,
                 eventName.c_str(),
                 eventInfo.objects.size());
        alarmBuffers.alarmEventNames.push_back(eventName);
        alarmBuffers.alarmDescriptions.push_back(resolveEventDescription(eventInfo));
        alarmBuffers.alarmObjects.emplace_back();
        alarmBuffers.alarmObjectClassNames.emplace_back();

        std::vector<event_object_t>& alarmObjects = alarmBuffers.alarmObjects.back();
        std::vector<std::string>& classNames = alarmBuffers.alarmObjectClassNames.back();

        alarmObjects.reserve(eventInfo.objects.size());
        classNames.reserve(eventInfo.objects.size());

        for (const algorithm::cdky::CObjectMeta* objectMeta : eventInfo.objects) {
            if (!objectMeta) {
                continue;
            }

            classNames.push_back(objectMeta->objLable);
            event_object_t outputObject{};
            outputObject.class_name = classNames.back().c_str();
            outputObject.class_id = objectMeta->classId;
            outputObject.tracker_id = static_cast<int>(objectMeta->trackerId);
            outputObject.confidence = objectMeta->detectorConfidence;
            outputObject.width = objectMeta->detectorBboxInfo.width;
            outputObject.height = objectMeta->detectorBboxInfo.height;
            outputObject.x = objectMeta->detectorBboxInfo.left + objectMeta->detectorBboxInfo.width * 0.5F;
            outputObject.y = objectMeta->detectorBboxInfo.top + objectMeta->detectorBboxInfo.height * 0.5F;
            alarmObjects.push_back(outputObject);
        }
    }

    alarmBuffers.alarmViews.reserve(alarmBuffers.alarmEventNames.size());
    for (size_t idx = 0; idx < alarmBuffers.alarmEventNames.size(); ++idx) {
        event_alarm_t alarm{};
        alarm.event_name = alarmBuffers.alarmEventNames[idx].c_str();
        alarm.description = alarmBuffers.alarmDescriptions[idx].c_str();
        alarm.objects = alarmBuffers.alarmObjects[idx].empty() ? nullptr : alarmBuffers.alarmObjects[idx].data();
        alarm.object_count = alarmBuffers.alarmObjects[idx].size();
        alarmBuffers.alarmViews.push_back(alarm);
    }

    *outAlarms = alarmBuffers.alarmViews.empty() ? nullptr : alarmBuffers.alarmViews.data();
    *outAlarmCount = alarmBuffers.alarmViews.size();
    LOG_TRACE("event_process success alarm_count={}", *outAlarmCount);
    return 0;
}
