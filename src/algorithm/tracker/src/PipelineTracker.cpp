#include "pipeline_tracker.h"

#include "byte_track.h"
#include "common/Config.h"
#define PIPELINE_ALGORITHM_LOGGER_NAME "pipeline.tracker"
#include "common/Logger.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr float kMatchIouThreshold = 0.3F;

struct NormalizedBox {
    float left = 0.0F;
    float top = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
};

struct TrackerMatchCandidate {
    size_t detection_index = 0;
    size_t track_index = 0;
    float iou = 0.0F;
};

struct TrackerHandleImpl {
    tracker_config_t config{};
    std::string tracker_type;
    std::unique_ptr<algorithm::cdky::ByteTracker> tracker;
    algorithm::cdky::TrackList track_list;
    int width = 0;
    int height = 0;
};

float clampUnit(float value) {
    return std::max(0.0F, std::min(1.0F, value));
}

float clampCoord(float value, float lower, float upper) {
    return std::max(lower, std::min(value, upper));
}

bool isValidFrameDesc(const tracker_frame_desc_t& frame_desc) {
    return frame_desc.width > 0 && frame_desc.height > 0;
}

tracker_config_t toTrackerConfig(const algorithm::cdky::TrackConfig& config) {
    tracker_config_t output{};
    output.enabled = config.enabled ? 1 : 0;
    output.tracker_type = nullptr;
    output.min_thresh = config.min_thresh;
    output.high_thresh = config.high_thresh;
    output.max_iou_distance = config.max_iou_distance;
    output.high_thresh_person = config.high_thresh_person;
    output.high_thresh_motorbike = config.high_thresh_motorbike;
    output.max_age = config.max_age;
    output.n_init = config.n_init;
    return output;
}

void applyTrackerConfigOverride(const tracker_config_t& override_config,
                                tracker_config_t& effective_config,
                                std::string& tracker_type) {
    effective_config.enabled = override_config.enabled;
    if (override_config.tracker_type && override_config.tracker_type[0] != '\0') {
        tracker_type = override_config.tracker_type;
    }

    if (override_config.min_thresh > 0.0F) {
        effective_config.min_thresh = override_config.min_thresh;
    }
    if (override_config.high_thresh > 0.0F) {
        effective_config.high_thresh = override_config.high_thresh;
    }
    if (override_config.max_iou_distance > 0.0F) {
        effective_config.max_iou_distance = override_config.max_iou_distance;
    }
    if (override_config.high_thresh_person > 0.0F) {
        effective_config.high_thresh_person = override_config.high_thresh_person;
    }
    if (override_config.high_thresh_motorbike > 0.0F) {
        effective_config.high_thresh_motorbike = override_config.high_thresh_motorbike;
    }
    if (override_config.max_age > 0) {
        effective_config.max_age = override_config.max_age;
    }
    if (override_config.n_init > 0) {
        effective_config.n_init = override_config.n_init;
    }
}

algorithm::cdky::AlgoConfig loadDefaultTrackerConfig(std::string& loaded_path,
                                                     std::string& error_message) {
    algorithm::cdky::AlgoConfig config;
    loaded_path = algorithm::cdky::resolveConfigPath("EsTracker.yaml");
    if (loaded_path.empty()) {
        return config;
    }

    try {
        config = algorithm::cdky::AlgoConfig::loadFromFile(loaded_path);
    } catch (const std::exception& ex) {
        error_message = ex.what();
    }
    return config;
}

float normalizedIou(const NormalizedBox& lhs, const NormalizedBox& rhs) {
    const float inter_left = std::max(lhs.left, rhs.left);
    const float inter_top = std::max(lhs.top, rhs.top);
    const float inter_right = std::min(lhs.left + lhs.width, rhs.left + rhs.width);
    const float inter_bottom = std::min(lhs.top + lhs.height, rhs.top + rhs.height);

    const float inter_width = std::max(0.0F, inter_right - inter_left);
    const float inter_height = std::max(0.0F, inter_bottom - inter_top);
    const float inter_area = inter_width * inter_height;
    const float lhs_area = lhs.width * lhs.height;
    const float rhs_area = rhs.width * rhs.height;
    const float union_area = lhs_area + rhs_area - inter_area;
    return union_area > 0.0F ? inter_area / union_area : 0.0F;
}

NormalizedBox detectionToNormalizedBox(const tracker_detection_t& detection) {
    const float width = clampUnit(detection.width);
    const float height = clampUnit(detection.height);
    const float center_x = clampUnit(detection.x);
    const float center_y = clampUnit(detection.y);

    NormalizedBox box;
    box.left = clampUnit(center_x - width * 0.5F);
    box.top = clampUnit(center_y - height * 0.5F);
    box.width = std::max(0.0F, std::min(width, 1.0F - box.left));
    box.height = std::max(0.0F, std::min(height, 1.0F - box.top));
    return box;
}

NormalizedBox trackToNormalizedBox(const algorithm::cdky::TrackNode& track, int width, int height) {
    const float track_cx = track.cywh(0, 0);
    const float track_ymax = track.cywh(0, 1);
    const float track_w = track.cywh(0, 2);
    const float track_h = track.cywh(0, 3);

    NormalizedBox box;
    box.left = clampUnit((track_cx - track_w * 0.5F) / static_cast<float>(width));
    box.top = clampUnit((track_ymax - track_h) / static_cast<float>(height));
    box.width = std::max(0.0F, std::min(track_w / static_cast<float>(width), 1.0F - box.left));
    box.height = std::max(0.0F, std::min(track_h / static_cast<float>(height), 1.0F - box.top));
    return box;
}

algorithm::cdky::DetectionRow toByteTrackDetection(const tracker_detection_t& detection,
                                          const tracker_frame_desc_t& frame_desc) {
    const float frame_width = static_cast<float>(frame_desc.width);
    const float frame_height = static_cast<float>(frame_desc.height);

    const float center_x = clampUnit(detection.x) * frame_width;
    const float center_y = clampUnit(detection.y) * frame_height;
    const float width = std::max(1.0F, clampUnit(detection.width) * frame_width);
    const float height = std::max(1.0F, clampUnit(detection.height) * frame_height);

    const float left = clampCoord(center_x - width * 0.5F, 0.0F, std::max(0.0F, frame_width - 1.0F));
    const float top = clampCoord(center_y - height * 0.5F, 0.0F, std::max(0.0F, frame_height - 1.0F));
    const float clamped_width = std::max(1.0F, std::min(width, frame_width - left));
    const float clamped_height = std::max(1.0F, std::min(height, frame_height - top));

    algorithm::cdky::DetectionRow output;
    output.tlwh(0, 0) = left;
    output.tlwh(0, 1) = top;
    output.tlwh(0, 2) = clamped_width;
    output.tlwh(0, 3) = clamped_height;
    output.confidence = detection.confidence;
    output.type = detection.class_id;
    return output;
}

void clearDeletedTracks(algorithm::cdky::TrackList& track_list) {
    for (auto it = track_list.begin(); it != track_list.end();) {
        if (it->state == algorithm::cdky::e_State::DELETE) {
            it = track_list.erase(it);
        } else {
            ++it;
        }
    }
    track_list.swap(track_list);
}

void initializeOutputs(const tracker_detection_t* detections,
                       size_t detection_count,
                       tracker_output_t* outputs) {
    if (!outputs) {
        return;
    }

    for (size_t index = 0; index < detection_count; ++index) {
        outputs[index].x = detections[index].x;
        outputs[index].y = detections[index].y;
        outputs[index].width = detections[index].width;
        outputs[index].height = detections[index].height;
        outputs[index].confidence = detections[index].confidence;
        outputs[index].class_id = detections[index].class_id;
        outputs[index].track_id = -1;
        outputs[index].matched = 0;
    }
}

float resolveFloat(float value, float fallback) {
    return value > 0.0F ? value : fallback;
}

int resolveInt(int value, int fallback) {
    return value > 0 ? value : fallback;
}

bool createTrackerIfNeeded(TrackerHandleImpl& handle, const tracker_frame_desc_t& frame_desc) {
    if (handle.tracker && handle.width == frame_desc.width && handle.height == frame_desc.height) {
        return true;
    }

    LOG_INFO("tracker initializing frame_width={} frame_height={} min_thresh={} high_thresh={} max_iou_distance={} max_age={} n_init={}",
             frame_desc.width,
             frame_desc.height,
             handle.config.min_thresh,
             handle.config.high_thresh,
             handle.config.max_iou_distance,
             handle.config.max_age,
             handle.config.n_init);

    algorithm::cdky::TrackList empty_track_list;
    handle.track_list.swap(empty_track_list);
    handle.tracker.reset();

    handle.tracker = std::make_unique<algorithm::cdky::ByteTracker>(
        resolveFloat(handle.config.min_thresh, algorithm::cdky::TrackConfig{}.min_thresh),
        resolveFloat(handle.config.high_thresh, algorithm::cdky::TrackConfig{}.high_thresh),
        resolveFloat(handle.config.max_iou_distance, algorithm::cdky::TrackConfig{}.max_iou_distance),
        frame_desc.width,
        frame_desc.height,
        resolveFloat(handle.config.high_thresh_person, algorithm::cdky::TrackConfig{}.high_thresh_person),
        resolveFloat(handle.config.high_thresh_motorbike, algorithm::cdky::TrackConfig{}.high_thresh_motorbike),
        resolveInt(handle.config.max_age, algorithm::cdky::TrackConfig{}.max_age),
        resolveInt(handle.config.n_init, algorithm::cdky::TrackConfig{}.n_init));
    handle.width = frame_desc.width;
    handle.height = frame_desc.height;
    LOG_DEBUG("tracker initialized success={}", static_cast<bool>(handle.tracker));
    return static_cast<bool>(handle.tracker);
}

void applyTrackMatches(const algorithm::cdky::TrackList& track_list,
                       const tracker_detection_t* detections,
                       size_t detection_count,
                       const tracker_frame_desc_t& frame_desc,
                       tracker_output_t* outputs) {
    if (!outputs || !detections || detection_count == 0 || track_list.empty()) {
        return;
    }

    std::vector<NormalizedBox> detection_boxes;
    detection_boxes.reserve(detection_count);
    for (size_t index = 0; index < detection_count; ++index) {
        detection_boxes.push_back(detectionToNormalizedBox(detections[index]));
    }

    std::vector<TrackerMatchCandidate> candidates;
    for (size_t track_index = 0; track_index < track_list.size(); ++track_index) {
        const auto& track = track_list[track_index];
        const NormalizedBox track_box = trackToNormalizedBox(track, frame_desc.width, frame_desc.height);
        for (size_t detection_index = 0; detection_index < detection_count; ++detection_index) {
            if (detections[detection_index].class_id != track.type) {
                continue;
            }

            const float iou = normalizedIou(detection_boxes[detection_index], track_box);
            if (iou >= kMatchIouThreshold) {
                candidates.push_back(TrackerMatchCandidate{detection_index, track_index, iou});
            }
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const TrackerMatchCandidate& lhs,
                                                       const TrackerMatchCandidate& rhs) {
        return lhs.iou > rhs.iou;
    });

    std::vector<bool> detection_used(detection_count, false);
    std::vector<bool> track_used(track_list.size(), false);
    for (const auto& candidate : candidates) {
        if (detection_used[candidate.detection_index] || track_used[candidate.track_index]) {
            continue;
        }

        detection_used[candidate.detection_index] = true;
        track_used[candidate.track_index] = true;
        outputs[candidate.detection_index].track_id = track_list[candidate.track_index].id;
        outputs[candidate.detection_index].matched = 1;
    }
}

} // namespace

struct tracker_handle_t {
    TrackerHandleImpl impl;
};

extern "C" int tracker_create(const tracker_config_t* config, tracker_handle_t** out_handle) {
    if (!out_handle) {
        return -1;
    }
    *out_handle = nullptr;

    std::string config_path;
    std::string config_error;
    algorithm::cdky::AlgoConfig algo_config = loadDefaultTrackerConfig(config_path, config_error);
    algorithm::cdky::Logger::init(
        algo_config.log, PIPELINE_ALGORITHM_LOGGER_NAME);
    if (config_path.empty()) {
        LOG_WARN("tracker default config not found, using built-in defaults");
    } else if (!config_error.empty()) {
        LOG_WARN("tracker default config load failed path={} err={}, using built-in defaults",
                 config_path.c_str(),
                 config_error.c_str());
    } else {
        LOG_INFO("tracker default config loaded path={}", config_path.c_str());
    }

    auto handle = std::make_unique<tracker_handle_t>();
    handle->impl.config = toTrackerConfig(algo_config.track);
    handle->impl.tracker_type = algo_config.track.tracker_type;

    if (config) {
        LOG_DEBUG("tracker_create applying caller config override");
        applyTrackerConfigOverride(*config, handle->impl.config, handle->impl.tracker_type);
    } else {
        LOG_INFO("tracker_create using default config path={}",
                 config_path.empty() ? "<built-in>" : config_path.c_str());
    }

    handle->impl.config.tracker_type = handle->impl.tracker_type.c_str();
    *out_handle = handle.release();
    LOG_INFO("tracker_create success enabled={} tracker_type={} min_thresh={} high_thresh={}",
             (*out_handle)->impl.config.enabled,
             (*out_handle)->impl.tracker_type.c_str(),
             (*out_handle)->impl.config.min_thresh,
             (*out_handle)->impl.config.high_thresh);
    return 0;
}

extern "C" void tracker_destroy(tracker_handle_t* handle) {
    LOG_DEBUG("tracker_destroy handle={}", static_cast<const void*>(handle));
    delete handle;
}

extern "C" int tracker_reset(tracker_handle_t* handle) {
    if (!handle) {
        LOG_WARN("tracker_reset rejected null handle");
        return -1;
    }

    if (handle->impl.tracker) {
        handle->impl.tracker->Release();
    }
    algorithm::cdky::TrackList empty_track_list;
    handle->impl.track_list.swap(empty_track_list);
    LOG_INFO("tracker_reset success");
    return 0;
}

extern "C" int tracker_process(tracker_handle_t* handle,
                                  const tracker_frame_desc_t* frame_desc,
                                  const tracker_detection_t* detections,
                                  size_t detection_count,
                                  tracker_output_t* outputs) {
    if (!handle || !frame_desc || !isValidFrameDesc(*frame_desc)) {
        LOG_WARN("tracker_process rejected invalid args handle={} frame_desc={} width={} height={}",
                 static_cast<const void*>(handle),
                 static_cast<const void*>(frame_desc),
                 frame_desc ? frame_desc->width : 0,
                 frame_desc ? frame_desc->height : 0);
        return -1;
    }
    if (detection_count > 0 && (!detections || !outputs)) {
        LOG_WARN("tracker_process rejected invalid buffers detection_count={} detections={} outputs={}",
                 detection_count,
                 static_cast<const void*>(detections),
                 static_cast<const void*>(outputs));
        return -1;
    }

    initializeOutputs(detections, detection_count, outputs);

    if (!handle->impl.config.enabled) {
        LOG_DEBUG("tracker_process skipped because tracker disabled");
        return 0;
    }

    if (!createTrackerIfNeeded(handle->impl, *frame_desc)) {
        LOG_ERROR("tracker_process failed to initialize tracker");
        return -1;
    }

    LOG_TRACE("tracker_process frame_ts={} detection_count={} frame_width={} frame_height={}",
              frame_desc->timestamp_ms,
              detection_count,
              frame_desc->width,
              frame_desc->height);

    algorithm::cdky::Detections byte_track_detections;
    byte_track_detections.reserve(detection_count);
    for (size_t index = 0; index < detection_count; ++index) {
        byte_track_detections.push_back(toByteTrackDetection(detections[index], *frame_desc));
    }

    handle->impl.tracker->Process(byte_track_detections,
                                  static_cast<uint64_t>(frame_desc->timestamp_ms),
                                  handle->impl.track_list);
    clearDeletedTracks(handle->impl.track_list);
    applyTrackMatches(handle->impl.track_list, detections, detection_count, *frame_desc, outputs);
    LOG_TRACE("tracker_process success active_tracks={}", handle->impl.track_list.size());
    return 0;
}
