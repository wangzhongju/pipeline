#include "common/Config.h"

#include <yaml-cpp/yaml.h>

#include <cstdlib>
#include <filesystem>
#include <stdexcept>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#ifndef PIPELINE_ALGORITHM_SOURCE_CONFIG_DIR
#define PIPELINE_ALGORITHM_SOURCE_CONFIG_DIR ""
#endif

namespace algorithm::cdky {
namespace {

namespace fs = std::filesystem;

template <typename T>
T yamlGet(const YAML::Node& node, const char* key, const T& fallback) {
    if (!node || !node[key] || node[key].IsNull()) {
        return fallback;
    }

    try {
        return node[key].as<T>();
    } catch (const std::exception&) {
        return fallback;
    }
}

bool yamlGetBoolAlias(const YAML::Node& node,
                      const char* primary_key,
                      const char* alias_key,
                      bool fallback) {
    if (node && node[primary_key] && !node[primary_key].IsNull()) {
        return yamlGet<bool>(node, primary_key, fallback);
    }
    return yamlGet<bool>(node, alias_key, fallback);
}

std::string normalizePath(const fs::path& path) {
    return path.lexically_normal().generic_string();
}

bool pathExists(const std::string& path) {
    std::error_code ec;
    return !path.empty() && fs::exists(fs::path(path), ec) && !ec;
}

void appendCandidate(std::vector<std::string>& candidates, const fs::path& path) {
    if (path.empty()) {
        return;
    }

    const std::string normalized = normalizePath(path);
    for (const std::string& candidate : candidates) {
        if (candidate == normalized) {
            return;
        }
    }
    candidates.push_back(normalized);
}

fs::path getSharedLibraryConfigDir() {
#if defined(_WIN32)
    HMODULE module = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(&getSharedLibraryConfigDir),
                            &module)) {
        return {};
    }

    char path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameA(module, path, MAX_PATH);
    if (length == 0 || length == MAX_PATH) {
        return {};
    }
    return fs::path(path).parent_path().parent_path() / "config";
#else
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(&getSharedLibraryConfigDir), &info) == 0 ||
        !info.dli_fname) {
        return {};
    }
    return fs::path(info.dli_fname).parent_path().parent_path() / "config";
#endif
}

LogConfig parseLog(const YAML::Node& node) {
    LogConfig config;
    if (!node || !node.IsMap()) {
        return config;
    }

    config.level = yamlGet<std::string>(node, "level", config.level);
    config.output = yamlGet<std::string>(node, "output", config.output);
    config.log_dir = yamlGet<std::string>(node, "log_dir", config.log_dir);
    config.max_file_mb = yamlGet<int>(node, "max_file_mb", config.max_file_mb);
    config.max_files = yamlGet<int>(node, "max_files", config.max_files);
    if (config.max_file_mb <= 0) {
        config.max_file_mb = LogConfig{}.max_file_mb;
    }
    if (config.max_files <= 0) {
        config.max_files = LogConfig{}.max_files;
    }
    return config;
}

TrackConfig parseTrack(const YAML::Node& node) {
    TrackConfig config;
    if (!node || !node.IsMap()) {
        return config;
    }

    config.enabled = yamlGetBoolAlias(node, "enabled", "enable", config.enabled);
    config.tracker_type = yamlGet<std::string>(node, "tracker_type", config.tracker_type);
    config.min_thresh = yamlGet<float>(node, "min_thresh", config.min_thresh);
    config.high_thresh = yamlGet<float>(node, "high_thresh", config.high_thresh);
    config.max_iou_distance = yamlGet<float>(node, "max_iou_distance", config.max_iou_distance);
    config.high_thresh_person =
        yamlGet<float>(node, "high_thresh_person", config.high_thresh_person);
    config.high_thresh_motorbike =
        yamlGet<float>(node, "high_thresh_motorbike", config.high_thresh_motorbike);
    config.max_age = yamlGet<int>(node, "max_age", config.max_age);
    config.n_init = yamlGet<int>(node, "n_init", config.n_init);
    return config;
}

EventConfig parseEvent(const YAML::Node& node) {
    EventConfig config;
    if (!node || !node.IsMap()) {
        return config;
    }

    config.config_path = yamlGet<std::string>(node, "config_path", config.config_path);
    return config;
}

AlgoConfig parseYaml(const YAML::Node& root) {
    AlgoConfig config;
    if (!root || !root.IsMap()) {
        return config;
    }

    if (root["log"]) {
        config.log = parseLog(root["log"]);
    }
    if (root["track"]) {
        config.track = parseTrack(root["track"]);
    }
    if (root["event"]) {
        config.event = parseEvent(root["event"]);
    }
    return config;
}

} // namespace

AlgoConfig AlgoConfig::loadFromFile(const std::string& path) {
    if (!pathExists(path)) {
        throw std::runtime_error("Cannot open config file: " + path);
    }

    return parseYaml(YAML::LoadFile(path));
}

AlgoConfig AlgoConfig::loadFromString(const std::string& yaml_str) {
    return parseYaml(YAML::Load(yaml_str));
}

std::string AlgoConfig::toYamlString() const {
    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << "log" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "level" << YAML::Value << log.level;
    out << YAML::Key << "output" << YAML::Value << log.output;
    out << YAML::Key << "log_dir" << YAML::Value << log.log_dir;
    out << YAML::Key << "max_file_mb" << YAML::Value << log.max_file_mb;
    out << YAML::Key << "max_files" << YAML::Value << log.max_files;
    out << YAML::EndMap;

    out << YAML::Key << "track" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "enabled" << YAML::Value << track.enabled;
    out << YAML::Key << "tracker_type" << YAML::Value << track.tracker_type;
    out << YAML::Key << "min_thresh" << YAML::Value << track.min_thresh;
    out << YAML::Key << "high_thresh" << YAML::Value << track.high_thresh;
    out << YAML::Key << "max_iou_distance" << YAML::Value << track.max_iou_distance;
    out << YAML::Key << "high_thresh_person" << YAML::Value << track.high_thresh_person;
    out << YAML::Key << "high_thresh_motorbike" << YAML::Value << track.high_thresh_motorbike;
    out << YAML::Key << "max_age" << YAML::Value << track.max_age;
    out << YAML::Key << "n_init" << YAML::Value << track.n_init;
    out << YAML::EndMap;
    out << YAML::EndMap;
    return out.c_str();
}

std::vector<std::string> getConfigPathCandidates(
    const std::string& file_name,
    const std::string& explicit_path,
    const std::vector<std::string>& extra_fallbacks) {
    std::vector<std::string> candidates;

    if (!explicit_path.empty()) {
        appendCandidate(candidates, explicit_path);
    }

    if (const char* env_dir = std::getenv("PIPELINE_ALGORITHM_CONFIG_DIR")) {
        appendCandidate(candidates, fs::path(env_dir) / file_name);
    }

    appendCandidate(candidates, fs::current_path() / "config" / file_name);

    const fs::path shared_library_config_dir = getSharedLibraryConfigDir();
    if (!shared_library_config_dir.empty()) {
        appendCandidate(candidates, shared_library_config_dir / file_name);
    }

    const std::string source_config_dir = PIPELINE_ALGORITHM_SOURCE_CONFIG_DIR;
    if (!source_config_dir.empty()) {
        appendCandidate(candidates, fs::path(source_config_dir) / file_name);
    }

    for (const std::string& fallback : extra_fallbacks) {
        appendCandidate(candidates, fallback);
    }

    return candidates;
}

std::string resolveConfigPath(const std::string& file_name,
                              const std::string& explicit_path,
                              const std::vector<std::string>& extra_fallbacks) {
    const std::vector<std::string> candidates =
        getConfigPathCandidates(file_name, explicit_path, extra_fallbacks);
    for (const std::string& candidate : candidates) {
        if (pathExists(candidate)) {
            return candidate;
        }
    }
    return {};
}

} // namespace algorithm::cdky
