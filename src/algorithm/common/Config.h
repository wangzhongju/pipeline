#pragma once

#include <string>
#include <vector>

namespace algorithm::cdky {

struct LogConfig {
    std::string level = "info";     // trace/debug/info/warn/error/critical/off
    std::string output = "file";    // file/console
    std::string log_dir = "log";
    int max_file_mb = 50;
    int max_files = 5;
};

struct TrackConfig {
    bool enabled = true;
    std::string tracker_type = "bytetrack";
    float min_thresh = 0.5F;
    float high_thresh = 0.7F;
    float max_iou_distance = 0.8F;
    float high_thresh_person = 0.8F;
    float high_thresh_motorbike = 0.8F;
    int max_age = 70;
    int n_init = 3;
};

struct EventConfig {
    std::string config_path;
};

struct AlgoConfig {
    LogConfig log;
    TrackConfig track;
    EventConfig event;

    static AlgoConfig loadFromFile(const std::string& path);
    static AlgoConfig loadFromString(const std::string& yaml_str);

    std::string toYamlString() const;
};

std::vector<std::string> getConfigPathCandidates(
    const std::string& file_name,
    const std::string& explicit_path = std::string(),
    const std::vector<std::string>& extra_fallbacks = {});

std::string resolveConfigPath(
    const std::string& file_name,
    const std::string& explicit_path = std::string(),
    const std::vector<std::string>& extra_fallbacks = {});

} // namespace algorithm::cdky
