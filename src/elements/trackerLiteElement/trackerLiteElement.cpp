#define PL_LOG_ID PL_LOG_TRACKER
#include "trackerLiteElement.h"

#include <algorithm>
#include <chrono>
#include <vector>

#include <yaml-cpp/yaml.h>

namespace {

template <typename T>
T yamlValue(const YAML::Node &node, const char *key, const T &fallback) {
    return node[key].IsDefined() ? node[key].as<T>() : fallback;
}

std::string frameStreamId(const CFrameMeta &frame) {
    if (!frame.streamId.empty()) {
        return frame.streamId;
    }
    return frame.source;
}

}  // namespace

TrackerLiteElement::~TrackerLiteElement() { destroyTrackers(); }

app_ret TrackerLiteElement::Init() {
    config_.enabled = 1;
    config_.tracker_type = "bytetrack";
    config_.min_thresh = 0.1F;
    config_.high_thresh = 0.5F;
    config_.max_iou_distance = 0.7F;
    config_.high_thresh_person = 0.5F;
    config_.high_thresh_motorbike = 0.5F;
    config_.max_age = 30;
    config_.n_init = 1;

    if (!m_configFile.empty()) {
        const YAML::Node config = YAML::LoadFile(m_configFile);
        config_.min_thresh = yamlValue(config, "min-thresh", config_.min_thresh);
        config_.high_thresh = yamlValue(config, "high-thresh", config_.high_thresh);
        config_.max_iou_distance =
            yamlValue(config, "max-iou-distance", config_.max_iou_distance);
        config_.high_thresh_person =
            yamlValue(config, "high-thresh-person", config_.high_thresh_person);
        config_.high_thresh_motorbike =
            yamlValue(config, "high-thresh-motorbike", config_.high_thresh_motorbike);
        config_.max_age = yamlValue(config, "max-age", config_.max_age);
        config_.n_init = yamlValue(config, "n-init", config_.n_init);
    }

    return APP_SUCCESS;
}

tracker_handle_t *TrackerLiteElement::getOrCreateTracker(
    const std::string &streamId) {
    auto it = trackers_.find(streamId);
    if (it != trackers_.end()) {
        return it->second;
    }

    tracker_handle_t *tracker = nullptr;
    if (tracker_create(&config_, &tracker) != 0 || tracker == nullptr) {
        app_error("%s create tracker failed for stream %s\n",
                  mName.c_str(), streamId.c_str());
        return nullptr;
    }
    trackers_.emplace(streamId, tracker);
    return tracker;
}

app_ret TrackerLiteElement::ProcessData(
    CBaseMeta *baseMeta, CElement const *previousElement) {
    (void)previousElement;
    if (baseMeta == nullptr || baseMeta->mMetaType != BATCH_META) {
        return APP_FAILURE;
    }

    auto *batch = static_cast<CBatchMeta *>(baseMeta);
    std::lock_guard<std::mutex> lock(mutex_);
    for (int frameIndex = 0; frameIndex < batch->getFrameMetaSize(); ++frameIndex) {
        CFrameMeta *frame = batch->getFrameMeta(frameIndex);
        if (frame == nullptr || frame->eosFlag) {
            continue;
        }

        const std::string streamId = frameStreamId(*frame);
        tracker_handle_t *tracker = getOrCreateTracker(streamId);
        if (tracker == nullptr) {
            continue;
        }

        int width = 0;
        int height = 0;
        if (!frame->images.empty() && frame->images.front() != nullptr &&
            frame->images.front()->mPic != nullptr) {
            width = frame->images.front()->mPic->videoFrame.width;
            height = frame->images.front()->mPic->videoFrame.height;
        }

        std::vector<tracker_detection_t> detections(frame->objs.size());
        std::vector<tracker_output_t> outputs(frame->objs.size());
        for (size_t index = 0; index < frame->objs.size(); ++index) {
            const CObjectMeta *object = frame->objs[index];
            const CBboxInfo &box = object->detectorBboxInfo;
            detections[index].x = box.left + box.width * 0.5F;
            detections[index].y = box.top + box.height * 0.5F;
            detections[index].width = box.width;
            detections[index].height = box.height;
            detections[index].confidence = object->detectorConfidence;
            detections[index].class_id = object->classId;
        }

        tracker_frame_desc_t descriptor{};
        descriptor.width = width;
        descriptor.height = height;
        descriptor.timestamp_ms = frame->pts > 0
            ? static_cast<int64_t>(frame->pts)
            : std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::system_clock::now().time_since_epoch()).count();
        if (tracker_process(tracker, &descriptor,
                            detections.empty() ? nullptr : detections.data(),
                            detections.size(),
                            outputs.empty() ? nullptr : outputs.data()) != 0) {
            app_error("%s tracker process failed for stream %s\n",
                      mName.c_str(), streamId.c_str());
            continue;
        }

        for (size_t index = 0; index < frame->objs.size(); ++index) {
            if (!outputs[index].matched || outputs[index].track_id < 0) {
                continue;
            }
            CObjectMeta *object = frame->objs[index];
            object->trackerId = outputs[index].track_id;
            object->trackerConfidence = outputs[index].confidence;
            object->trackerBboxInfo.left =
                outputs[index].x - outputs[index].width * 0.5F;
            object->trackerBboxInfo.top =
                outputs[index].y - outputs[index].height * 0.5F;
            object->trackerBboxInfo.width = outputs[index].width;
            object->trackerBboxInfo.height = outputs[index].height;
        }
    }
    return APP_SUCCESS;
}

void TrackerLiteElement::destroyTrackers() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &entry : trackers_) {
        tracker_destroy(entry.second);
    }
    trackers_.clear();
}

app_ret TrackerLiteElement::Finish() {
    destroyTrackers();
    return APP_SUCCESS;
}

extern "C" CElement *createEsTrackerLiteElement(
    const char *name, const char *path, int dieIndex) {
    return new TrackerLiteElement(name, path, dieIndex);
}
