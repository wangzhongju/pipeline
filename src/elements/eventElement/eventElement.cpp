#define PL_LOG_ID PL_LOG_OTHERS
#include "eventElement.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <unordered_set>
#include <vector>

#include <yaml-cpp/yaml.h>

#include "pipeline_agent/Logger.h"
#include "pipeline_agent/MessageMapper.h"
#include "EvidenceService.h"

namespace {

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string frameStreamId(const CFrameMeta &frame) {
    return frame.streamId.empty() ? frame.source : frame.streamId;
}

const AlgorithmConfig *findAlgorithm(
    const StreamConfig &config, const std::string &scenario) {
    for (const auto &algorithm : config.algorithms()) {
        if (algorithm.model_scenario_code() == scenario) {
            return &algorithm;
        }
    }
    return nullptr;
}

event_roi_mode_t roiMode(const RoiArea &roi) {
    if (roi.mode() == RoiArea::ROI_INCLUDE) {
        return EVENT_ROI_INCLUDE;
    }
    if (roi.mode() == RoiArea::ROI_EXCLUDE) {
        return EVENT_ROI_EXCLUDE;
    }
    return EVENT_ROI_MODE_UNKNOWN;
}

DetectionObject toProtoObject(const event_object_t &object) {
    DetectionObject result;
    result.set_track_id(
        object.tracker_id > 0
            ? static_cast<uint32_t>(object.tracker_id) : 0U);
    result.set_class_id(object.class_id >= 0 ? object.class_id : 0);
    result.set_class_name(object.class_name == nullptr ? "" : object.class_name);
    result.set_confidence(object.confidence);
    Box *box = result.mutable_bbox();
    box->set_cx(object.x);
    box->set_cy(object.y);
    box->set_width(object.width);
    box->set_height(object.height);
    return result;
}

DetectionObject toProtoObject(const CObjectMeta &object) {
    DetectionObject result;
    result.set_track_id(
        object.trackerId > 0
            ? static_cast<uint32_t>(object.trackerId) : 0U);
    result.set_class_id(object.classId >= 0 ? object.classId : 0);
    result.set_class_name(object.objLable);
    result.set_confidence(
        object.trackerId >= 0 && object.trackerConfidence > 0.0F
            ? object.trackerConfidence
            : object.detectorConfidence);
    const CBboxInfo &source = object.trackerId >= 0
        ? object.trackerBboxInfo : object.detectorBboxInfo;
    Box *box = result.mutable_bbox();
    box->set_cx(source.left + source.width * 0.5F);
    box->set_cy(source.top + source.height * 0.5F);
    box->set_width(source.width);
    box->set_height(source.height);
    return result;
}

}  // namespace

EventElement::~EventElement() {
    stop_ = true;
    if (heartbeatThread_.joinable()) {
        heartbeatThread_.join();
    }
    if (ipc_) {
        ipc_->stop();
    }
    destroyStates();
}

app_ret EventElement::Init() {
    const YAML::Node config = YAML::LoadFile(m_configFile);
    socketPath_ = config["socket-path"].as<std::string>(
        "/opt/smart-guard/run/media-agent/media_agent.sock");
    agentId_ = config["agent-id"].as<std::string>("agent_001");
    eventConfigPath_ = config["event-config"].as<std::string>("");
    fixedConfigPath_ = config["fixed-agent-config"].as<std::string>("");
    alarmRelayPath_ = config["alarm-relay-socket"].as<std::string>("");
    scenarioFilter_ = config["scenario-code"].as<std::string>("");
    modelGroupId_ = config["model-group-id"].as<std::string>(
        scenarioFilter_.empty() ? mName : scenarioFilter_);
    modelGroupCount_ = std::max(
        1, config["model-group-count"].as<int>(1));
    sendQueueSize_ = config["send-queue-size"].as<int>(100);
    heartbeatIntervalMs_ = config["heartbeat-interval-ms"].as<int>(10000);

    if (!fixedConfigPath_.empty() && !loadFixedConfig(fixedConfigPath_)) {
        app_error("%s failed to load fixed agent config %s\n",
                  mName.c_str(), fixedConfigPath_.c_str());
        return APP_FAILURE;
    }

    if (!alarmRelayPath_.empty()) {
        alarmRelay_ = std::make_unique<pipeline::agent::AlarmRelayClient>(
            alarmRelayPath_);
    } else {
        pipeline::agent::SocketConfig socketConfig;
        socketConfig.socket_path = socketPath_;
        socketConfig.agent_id = agentId_;
        socketConfig.send_queue_size = std::max(1, sendQueueSize_);
        socketConfig.heartbeat_interval_ms = std::max(1000, heartbeatIntervalMs_);
        ipc_ = std::make_unique<pipeline::agent::IpcClient>(
            std::move(socketConfig));
        ipc_->setConfigCallback(
            [this](const AgentConfig &agentConfig) { applyConfig(agentConfig); });
    }
    return APP_SUCCESS;
}

app_ret EventElement::Start() {
    if (ipc_ && !ipc_->start()) {
        app_error("%s failed to start platform IPC\n", mName.c_str());
        return APP_FAILURE;
    }
    stop_ = false;
    if (ipc_) {
        heartbeatThread_ = std::thread(&EventElement::heartbeatLoop, this);
    }
    return APP_SUCCESS;
}

bool EventElement::loadFixedConfig(const std::string &path) {
    std::ifstream input(path, std::ios::binary);
    AgentConfig config;
    if (!input.is_open() || !config.ParseFromIstream(&input)) {
        return false;
    }
    applyConfig(config);
    return true;
}

bool EventElement::sendAlarm(AlarmInfo alarm) {
    if (alarmRelay_) {
        return alarmRelay_->send(alarm);
    }
    return ipc_ && ipc_->pushAlarm(std::move(alarm));
}

void EventElement::applyConfig(const AgentConfig &config) {
    if (!config.agent_id().empty() && config.agent_id() != agentId_) {
        app_warn("%s ignore config for agent %s\n",
                 mName.c_str(), config.agent_id().c_str());
        return;
    }

    std::unordered_map<std::string, StreamConfig> next;
    std::unordered_set<std::string> activeStreamIds;
    for (const auto &stream : config.streams()) {
        if (!stream.stream_id().empty()) {
            next[stream.stream_id()] = stream;
            activeStreamIds.insert(stream.stream_id());
        }
    }

    {
        std::lock_guard<std::mutex> lock(configMutex_);
        streamConfigs_.swap(next);
    }
    pipeline::evidence::EvidenceService::instance().applyConfig(config);

    std::lock_guard<std::mutex> lock(eventMutex_);
    for (auto it = eventStates_.begin(); it != eventStates_.end();) {
        if (activeStreamIds.count(it->first) == 0) {
            event_destroy(it->second.handle);
            it = eventStates_.erase(it);
        } else {
            event_reset(it->second.handle);
            it->second.lastAlarmMs.clear();
            ++it;
        }
    }
}

EventElement::StreamEventState *EventElement::getOrCreateState(
    const std::string &streamId) {
    auto it = eventStates_.find(streamId);
    if (it != eventStates_.end()) {
        return &it->second;
    }

    event_config_t config{};
    config.config_path =
        eventConfigPath_.empty() ? nullptr : eventConfigPath_.c_str();
    event_handle_t *handle = nullptr;
    if (event_create(&config, &handle) != 0 || handle == nullptr) {
        app_error("%s create event state failed for stream %s\n",
                  mName.c_str(), streamId.c_str());
        return nullptr;
    }
    auto inserted = eventStates_.emplace(streamId, StreamEventState{handle, {}});
    return &inserted.first->second;
}

app_ret EventElement::ProcessData(
    CBaseMeta *baseMeta, CElement const *previousElement) {
    (void)previousElement;
    if (baseMeta == nullptr || baseMeta->mMetaType != BATCH_META) {
        return APP_FAILURE;
    }

    auto *batch = static_cast<CBatchMeta *>(baseMeta);
    for (int frameIndex = 0; frameIndex < batch->getFrameMetaSize(); ++frameIndex) {
        CFrameMeta *frame = batch->getFrameMeta(frameIndex);
        if (frame == nullptr || frame->eosFlag) {
            continue;
        }
        const std::string streamId = frameStreamId(*frame);

        StreamConfig streamConfig;
        {
            std::lock_guard<std::mutex> lock(configMutex_);
            auto configIt = streamConfigs_.find(streamId);
            if (configIt == streamConfigs_.end() || !configIt->second.enabled()) {
                continue;
            }
            streamConfig = configIt->second;
        }

        std::vector<event_object_t> objects(frame->objs.size());
        std::vector<DetectionObject> detections;
        detections.reserve(frame->objs.size());
        for (size_t index = 0; index < frame->objs.size(); ++index) {
            const CObjectMeta *object = frame->objs[index];
            const CBboxInfo &box = object->trackerId >= 0
                ? object->trackerBboxInfo : object->detectorBboxInfo;
            objects[index].class_name = object->objLable.c_str();
            objects[index].class_id = object->classId;
            objects[index].tracker_id = object->trackerId;
            objects[index].confidence = object->trackerId >= 0
                ? object->trackerConfidence : object->detectorConfidence;
            objects[index].x = box.left + box.width * 0.5F;
            objects[index].y = box.top + box.height * 0.5F;
            objects[index].width = box.width;
            objects[index].height = box.height;
            detections.push_back(toProtoObject(*object));
        }
        pipeline::evidence::EvidenceService::instance().updateDetections(
            streamId, static_cast<int64_t>(frame->pts), modelGroupId_,
            modelGroupCount_, detections);

        std::vector<AlgorithmConfig> algorithms;
        std::vector<std::vector<event_point2d_t>> pointStorage;
        std::vector<std::vector<event_roi_area_t>> roiStorage;
        std::vector<event_request_t> requests;
        algorithms.reserve(streamConfig.algorithms_size());
        pointStorage.reserve(streamConfig.algorithms_size());
        roiStorage.reserve(streamConfig.algorithms_size());
        requests.reserve(streamConfig.algorithms_size());

        for (const auto &algorithm : streamConfig.algorithms()) {
            if (!scenarioFilter_.empty() &&
                algorithm.model_scenario_code() != scenarioFilter_) {
                continue;
            }
            algorithms.push_back(algorithm);
            pointStorage.emplace_back();
            roiStorage.emplace_back();
            auto &points = pointStorage.back();
            auto &rois = roiStorage.back();
            size_t totalPointCount = 0;
            for (const auto &roi : algorithms.back().rois()) {
                if (roi.enabled() && roi.has_poly()) {
                    totalPointCount += static_cast<size_t>(
                        roi.poly().points_size());
                }
            }
            points.reserve(totalPointCount);
            rois.reserve(static_cast<size_t>(algorithms.back().rois_size()));

            for (const auto &roi : algorithms.back().rois()) {
                if (!roi.enabled()) {
                    continue;
                }
                event_roi_area_t nativeRoi{};
                nativeRoi.enabled = 1;
                nativeRoi.mode = roiMode(roi);
                if (roi.has_rect()) {
                    nativeRoi.shape_type = EVENT_ROI_SHAPE_RECT;
                    nativeRoi.rect.cx = roi.rect().cx();
                    nativeRoi.rect.cy = roi.rect().cy();
                    nativeRoi.rect.width = roi.rect().width();
                    nativeRoi.rect.height = roi.rect().height();
                    nativeRoi.rect.angle = roi.rect().angle();
                } else if (roi.has_poly() && roi.poly().points_size() >= 3) {
                    nativeRoi.shape_type = EVENT_ROI_SHAPE_POLY;
                    const size_t firstPoint = points.size();
                    for (const auto &point : roi.poly().points()) {
                        points.push_back({point.x(), point.y()});
                    }
                    nativeRoi.poly.points = points.data() + firstPoint;
                    nativeRoi.poly.point_count = roi.poly().points_size();
                } else {
                    continue;
                }
                rois.push_back(nativeRoi);
            }

            event_request_t request{};
            request.event_name =
                algorithms.back().model_scenario_code().c_str();
            request.has_roi_override = rois.empty() ? 0 : 1;
            request.roi_areas = rois.empty() ? nullptr : rois.data();
            request.roi_area_count = rois.size();
            request.confidence_threshold = algorithms.back().threshold();
            request.config_path =
                algorithms.back().model_config_name().empty()
                    ? nullptr
                    : algorithms.back().model_config_name().c_str();
            requests.push_back(request);
        }

        event_frame_desc_t descriptor{};
        descriptor.camera_id = streamId.c_str();
        descriptor.timestamp_ms = nowMs();

        const event_alarm_t *alarms = nullptr;
        size_t alarmCount = 0;
        std::lock_guard<std::mutex> eventLock(eventMutex_);
        StreamEventState *state = getOrCreateState(streamId);
        if (state == nullptr ||
            event_process(state->handle, &descriptor,
                          objects.empty() ? nullptr : objects.data(),
                          objects.size(),
                          requests.empty() ? nullptr : requests.data(),
                          requests.size(), &alarms, &alarmCount) != 0) {
            continue;
        }

        const int64_t currentMs = nowMs();
        const int configuredDedupSeconds =
            streamConfig.alarm_dedup_interval_s();
        const int64_t dedupMs =
            (configuredDedupSeconds > 0 ? configuredDedupSeconds : 5) * 1000LL;
        for (size_t alarmIndex = 0; alarmIndex < alarmCount; ++alarmIndex) {
            const event_alarm_t &alarm = alarms[alarmIndex];
            const std::string scenario =
                alarm.event_name == nullptr ? "" : alarm.event_name;
            auto previous = state->lastAlarmMs.find(scenario);
            if (previous != state->lastAlarmMs.end() &&
                currentMs - previous->second < dedupMs) {
                continue;
            }
            state->lastAlarmMs[scenario] = currentMs;

            const AlgorithmConfig *algorithm =
                findAlgorithm(streamConfig, scenario);
            if (algorithm == nullptr) {
                continue;
            }
            std::vector<DetectionObject> targets;
            targets.reserve(alarm.object_count);
            for (size_t objectIndex = 0;
                 objectIndex < alarm.object_count; ++objectIndex) {
                targets.push_back(toProtoObject(alarm.objects[objectIndex]));
            }
            std::string snapshotName;
            CImage *snapshotImage = nullptr;
            if (frame->images.size() > 1 && frame->images[1] &&
                frame->images[1]->mPic) {
                snapshotImage = frame->images[1];
            } else if (!frame->images.empty() && frame->images.front() &&
                       frame->images.front()->mPic) {
                snapshotImage = frame->images.front();
            }
            if (snapshotImage != nullptr) {
                snapshotName =
                    pipeline::evidence::EvidenceService::instance().saveSnapshot(
                        streamId, *snapshotImage->mPic, targets, scenario);
            }
            const std::string recordName =
                pipeline::evidence::EvidenceService::instance().triggerRecording(
                    streamId);
            const bool queued = sendAlarm(pipeline::agent::buildAlarmInfo(
                streamId, *algorithm, targets,
                alarm.description == nullptr ? "" : alarm.description,
                snapshotName, recordName));
            if (queued) {
                LOG_INFO(
                    "[EventElement] alarm queued stream={} scenario={} targets={}",
                    streamId, scenario, targets.size());
            } else {
                LOG_ERROR(
                    "[EventElement] alarm queue failed stream={} scenario={} targets={}",
                    streamId, scenario, targets.size());
            }
        }
    }
    return APP_SUCCESS;
}

void EventElement::heartbeatLoop() {
    const auto start = std::chrono::steady_clock::now();
    while (!stop_) {
        int total = 0;
        int enabled = 0;
        {
            std::lock_guard<std::mutex> lock(configMutex_);
            total = static_cast<int>(streamConfigs_.size());
            for (const auto &entry : streamConfigs_) {
                enabled += entry.second.enabled() ? 1 : 0;
            }
        }
        const int64_t uptime = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start).count();
        if (ipc_) {
            ipc_->pushHeartbeat(pipeline::agent::buildHeartbeat(
                agentId_, total, enabled, total - enabled, uptime));
        }

        const int slices = std::max(1, heartbeatIntervalMs_ / 100);
        for (int slice = 0; slice < slices && !stop_; ++slice) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void EventElement::destroyStates() {
    std::lock_guard<std::mutex> lock(eventMutex_);
    for (auto &entry : eventStates_) {
        event_destroy(entry.second.handle);
    }
    eventStates_.clear();
}

app_ret EventElement::Finish() {
    stop_ = true;
    if (heartbeatThread_.joinable()) {
        heartbeatThread_.join();
    }
    if (ipc_) {
        ipc_->stop();
    }
    destroyStates();
    return APP_SUCCESS;
}

extern "C" CElement *createEsEventElement(
    const char *name, const char *path, int dieIndex) {
    return new EventElement(name, path, dieIndex);
}
