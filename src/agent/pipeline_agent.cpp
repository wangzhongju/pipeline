#include "common/model_package.h"
#include "pipeline_agent/AlarmRelay.h"
#include "pipeline_agent/IpcClient.h"
#include "pipeline_agent/Logger.h"
#include "pipeline_agent/MessageMapper.h"
#include "pl_launch.h"

#include <atomic>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <csignal>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>

#include <yaml-cpp/yaml.h>

namespace {

struct Options {
    std::string pipeline_root = "/opt/demo/pipeline";
    std::string runtime_dir = "/tmp/pipeline-agent";
    std::string platform_socket =
        "/opt/smart-guard/run/media-agent/media_agent.sock";
    std::string relay_socket = "/tmp/pipeline-agent/alarm.sock";
    std::string agent_id = "agent_001";
    int heartbeat_ms = 10000;
    int config_debounce_ms = 2000;
    int config_max_wait_ms = 10000;
    int worker_stop_timeout_ms = 20000;
    int vdec_pool_size = 2;
    int mux_pool_size = 8;
    int preprocess_pool_size = 12;
    int infer_output_pool_size = 8;
    int model_queue_depth = 2;
    std::string model_group_config;
};

struct DesiredGroup {
    std::string key;
    std::string stream_id;
    // Retained for the legacy single-model launcher; stream workers do not use
    // these fields.
    std::string scenario;
    std::string package_path;
    AgentConfig config;
};

struct Worker {
    pid_t pid = -1;
    int vdec_offset = 0;
    int vdec_count = 0;
    std::string signature;
    DesiredGroup desired;
};

std::atomic<bool> g_stop{false};

void signalHandler(int) {
    g_stop = true;
}

std::string safeName(const std::string& value) {
    std::string result;
    for (const unsigned char ch : value) {
        result.push_back(std::isalnum(ch) || ch == '-' || ch == '_'
                             ? static_cast<char>(ch)
                             : '_');
    }
    return result.empty() ? "unknown" : result;
}

std::string stableId(const std::string& value) {
    std::ostringstream output;
    output << std::hex << std::hash<std::string>{}(value);
    return output.str();
}

std::string contentFingerprint(const std::vector<unsigned char>& bytes) {
    uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char value : bytes) {
        hash ^= value;
        hash *= 1099511628211ULL;
    }
    std::ostringstream output;
    output << std::hex << std::setw(16) << std::setfill('0') << hash;
    return output.str();
}

void writeText(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output || !(output << text)) {
        throw std::runtime_error("failed to write " + path.string());
    }
}

std::string jsonArray(const nlohmann::json& value,
                      const std::string& fallback) {
    return value.is_array() ? value.dump() : fallback;
}

std::filesystem::path findExtractedFile(
    const std::filesystem::path& root, const std::string& name) {
    const auto direct = root / name;
    if (std::filesystem::exists(direct)) {
        return direct;
    }
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file() && entry.path().filename() == name) {
            return entry.path();
        }
    }
    return {};
}

class TaskManager {
public:
    TaskManager(Options options)
        : options_(std::move(options)) {
        if (options_.model_group_config.empty()) {
            options_.model_group_config =
                (std::filesystem::path(options_.pipeline_root) /
                 "config/ModelGroups.yaml").string();
        }
        loadModelGroups();
    }

    ~TaskManager() {
        stopAll();
    }

    void submit(const AgentConfig& config) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto now = std::chrono::steady_clock::now();
        if (pending_configs_.empty()) {
            first_pending_config_at_ = now;
        }
        last_pending_config_at_ = now;
        pending_configs_.push_back(config);
    }

    void requestModelReload(const AlgorithmModelUpdate& update) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (update.model_scenario_codes().empty()) {
            pending_reload_all_ = true;
            pending_reload_scenarios_.clear();
            return;
        }
        for (const auto& scenario : update.model_scenario_codes()) {
            if (std::find(pending_reload_scenarios_.begin(),
                          pending_reload_scenarios_.end(),
                          scenario) == pending_reload_scenarios_.end()) {
                pending_reload_scenarios_.push_back(scenario);
            }
        }
    }

    void tick() {
        const bool worker_exited = reapExited();
        std::deque<AgentConfig> updates;
        bool reload_all = false;
        std::vector<std::string> reload_scenarios;
        bool config_waiting = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!pending_configs_.empty()) {
                const auto now = std::chrono::steady_clock::now();
                const auto idle_ms =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - last_pending_config_at_).count();
                const auto age_ms =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - first_pending_config_at_).count();
                if (idle_ms >= options_.config_debounce_ms ||
                    age_ms >= options_.config_max_wait_ms) {
                    updates.swap(pending_configs_);
                } else {
                    config_waiting = true;
                }
            }
            if (!config_waiting) {
                reload_all = pending_reload_all_;
                pending_reload_all_ = false;
                reload_scenarios.swap(pending_reload_scenarios_);
            }
        }

        bool config_changed = false;
        for (const auto& update : updates) {
            if (!update.agent_id().empty() &&
                update.agent_id() != options_.agent_id) {
                LOG_WARN("[TaskManager] ignore config for other agent "
                         "local={} target={} config_id={}",
                         options_.agent_id, update.agent_id(),
                         update.config_id());
                continue;
            }
            for (const auto& stream : update.streams()) {
                if (stream.stream_id().empty()) {
                    LOG_WARN("[TaskManager] ignore config_id={} stream with "
                             "empty stream_id", update.config_id());
                    continue;
                }
                if (stream.enabled()) {
                    active_streams_[stream.stream_id()] = stream;
                } else {
                    active_streams_.erase(stream.stream_id());
                }
                config_changed = true;
            }
            LOG_INFO("[TaskManager] merged config_id={} updates={} "
                     "active_streams={}",
                     update.config_id(), update.streams_size(),
                     active_streams_.size());
        }

        if (!config_waiting &&
            (config_changed || reload_all || !reload_scenarios.empty() ||
             worker_exited)) {
            AgentConfig aggregate;
            aggregate.set_agent_id(options_.agent_id);
            for (const auto& entry : active_streams_) {
                aggregate.add_streams()->CopyFrom(entry.second);
            }
            apply(aggregate, reload_all, reload_scenarios);
        }
    }

    int activeWorkers() const {
        return static_cast<int>(workers_.size());
    }

    int activeStreams() const {
        return static_cast<int>(active_streams_.size());
    }

    void stopAll() {
        for (auto& entry : workers_) {
            stopWorker(entry.second);
        }
        workers_.clear();
    }

private:
    struct PreparedModel {
        std::string group_id;
        std::string fingerprint;
        int queue_depth = 2;
        AgentConfig config;
        nlohmann::json root;
        std::filesystem::path directory;
        std::filesystem::path model_path;
        int width = 512;
        int height = 512;
        int infer_interval = 3;
    };

    void loadModelGroups() {
        try {
            const YAML::Node root =
                YAML::LoadFile(options_.model_group_config);
            options_.model_queue_depth = std::max(
                1, root["default_queue_depth"].as<int>(
                       options_.model_queue_depth));
            const YAML::Node groups = root["groups"];
            if (!groups || !groups.IsMap()) {
                return;
            }
            for (const auto& entry : groups) {
                const std::string group_id = entry.first.as<std::string>();
                const YAML::Node value = entry.second;
                group_queue_depths_[group_id] = std::max(
                    1, value["queue_depth"].as<int>(
                           options_.model_queue_depth));
                group_infer_intervals_[group_id] = std::max(
                    1, value["infer_interval"].as<int>(3));
                group_require_identical_model_[group_id] =
                    value["require_identical_model"].as<bool>(true);
                group_canonical_scenarios_[group_id] =
                    value["canonical_scenario"].as<std::string>("");
                const YAML::Node scenarios = value["scenarios"];
                if (!scenarios || !scenarios.IsSequence()) {
                    continue;
                }
                for (const auto& scenario : scenarios) {
                    scenario_model_groups_[scenario.as<std::string>()] =
                        group_id;
                }
            }
            LOG_INFO("[TaskManager] loaded model groups file={} scenarios={}",
                     options_.model_group_config,
                     scenario_model_groups_.size());
        } catch (const std::exception& error) {
            LOG_WARN("[TaskManager] model group config unavailable file={} "
                     "error={}; grouping by model content",
                     options_.model_group_config, error.what());
        }
    }

    std::string declaredModelGroup(const std::string& scenario,
                                   const std::string& fingerprint) const {
        const auto it = scenario_model_groups_.find(scenario);
        return it == scenario_model_groups_.end()
                   ? "model-" + fingerprint : it->second;
    }

    int modelQueueDepth(const std::string& group_id) const {
        const auto it = group_queue_depths_.find(group_id);
        return it == group_queue_depths_.end()
                   ? options_.model_queue_depth : it->second;
    }

    int modelInferInterval(const std::string& group_id) const {
        const auto it = group_infer_intervals_.find(group_id);
        return it == group_infer_intervals_.end() ? 3 : it->second;
    }

    bool requireIdenticalModel(const std::string& group_id) const {
        const auto it = group_require_identical_model_.find(group_id);
        return it == group_require_identical_model_.end() || it->second;
    }

    std::map<std::string, DesiredGroup> buildDesired(
        const AgentConfig& config) const {
        std::map<std::string, DesiredGroup> result;
        for (const auto& stream : config.streams()) {
            if (!stream.enabled() || stream.stream_id().empty() ||
                stream.rtsp_url().empty()) {
                continue;
            }
            bool has_algorithm = false;
            for (const auto& algorithm : stream.algorithms()) {
                if (algorithm.model_scenario_code().empty() ||
                    algorithm.model_config_name().empty()) {
                    continue;
                }
                has_algorithm = true;
            }
            if (has_algorithm) {
                DesiredGroup group;
                group.key = stream.stream_id();
                group.stream_id = stream.stream_id();
                group.config.set_agent_id(config.agent_id());
                group.config.add_streams()->CopyFrom(stream);
                result.emplace(group.key, std::move(group));
            }
        }
        return result;
    }

    void apply(const AgentConfig& config, bool reload_all,
               const std::vector<std::string>& reload_scenarios) {
        auto desired = buildDesired(config);
        std::vector<std::string> remove;
        for (const auto& entry : workers_) {
            if (desired.find(entry.first) == desired.end()) {
                remove.push_back(entry.first);
            }
        }
        for (const auto& key : remove) {
            stopWorker(workers_.at(key));
            workers_.erase(key);
        }

        for (auto& entry : desired) {
            const std::string signature = entry.second.config.SerializeAsString();
            auto current = workers_.find(entry.first);
            bool reload = reload_all;
            for (const auto& scenario : reload_scenarios) {
                for (const auto& algorithm :
                     entry.second.config.streams(0).algorithms()) {
                    reload = reload ||
                             scenario == algorithm.model_scenario_code();
                }
            }
            if (current != workers_.end() &&
                current->second.signature == signature && !reload &&
                current->second.pid > 0) {
                continue;
            }
            if (current != workers_.end()) {
                stopWorker(current->second);
                workers_.erase(current);
            }

            Worker worker;
            worker.signature = signature;
            worker.desired = entry.second;
            worker.vdec_count = 1;
            try {
                worker.vdec_offset = allocateVdecOffset(worker.vdec_count);
                worker.pid =
                    startStreamWorker(worker.desired, worker.vdec_offset);
            } catch (const std::exception& error) {
                LOG_ERROR("[TaskManager] start failed stream={} error={}",
                          entry.second.stream_id, error.what());
                continue;
            }
            workers_.emplace(entry.first, std::move(worker));
        }
        LOG_INFO("[TaskManager] reconciled streams={} stream_workers={}",
                 config.streams_size(), workers_.size());
    }

    int allocateVdecOffset(int count) const {
        constexpr int kMaxVdecGroups = 128;
        for (int offset = 0; offset + count <= kMaxVdecGroups; ++offset) {
            bool available = true;
            for (const auto& entry : workers_) {
                const Worker& worker = entry.second;
                if (worker.pid <= 0 || worker.vdec_count <= 0) {
                    continue;
                }
                const int worker_end =
                    worker.vdec_offset + worker.vdec_count;
                const int requested_end = offset + count;
                if (offset < worker_end &&
                    worker.vdec_offset < requested_end) {
                    available = false;
                    break;
                }
            }
            if (available) {
                return offset;
            }
        }
        throw std::runtime_error("no contiguous VDEC group range available");
    }

    std::vector<PreparedModel> prepareModels(
        const DesiredGroup& desired,
        const std::filesystem::path& group_dir) {
        using algorithm::cdky::model_package::Package;
        std::map<std::string, PreparedModel> grouped;
        const auto& stream = desired.config.streams(0);
        std::vector<const AlgorithmConfig*> algorithms;
        for (const auto& item : stream.algorithms()) {
            algorithms.push_back(&item);
        }
        std::stable_sort(
            algorithms.begin(), algorithms.end(),
            [this](const AlgorithmConfig* first,
                   const AlgorithmConfig* second) {
                auto canonical = [this](const AlgorithmConfig* item) {
                    const auto group = scenario_model_groups_.find(
                        item->model_scenario_code());
                    if (group == scenario_model_groups_.end()) {
                        return false;
                    }
                    const auto preferred =
                        group_canonical_scenarios_.find(group->second);
                    return preferred != group_canonical_scenarios_.end() &&
                           preferred->second ==
                               item->model_scenario_code();
                };
                return canonical(first) && !canonical(second);
            });
        for (const AlgorithmConfig* algorithm_ptr : algorithms) {
            const auto& algorithm_config = *algorithm_ptr;
            if (algorithm_config.model_scenario_code().empty() ||
                algorithm_config.model_config_name().empty()) {
                continue;
            }
            Package package;
            std::string error;
            if (!algorithm::cdky::model_package::loadPackage(
                    algorithm_config.model_config_name(), package, &error)) {
                throw std::runtime_error(
                    "package decrypt failed scenario=" +
                    algorithm_config.model_scenario_code() + ": " + error);
            }
            const nlohmann::json root = package.root_config;
            const bool nested =
                root.contains("model_config") &&
                root["model_config"].contains("model_name");
            const std::string model_name = nested
                ? root["model_config"]["model_name"].get<std::string>()
                : root.value("model_name", std::string{});
            const auto* model_bytes =
                algorithm::cdky::model_package::findFile(
                    package.files, model_name);
            if (model_name.empty() || model_bytes == nullptr) {
                algorithm::cdky::model_package::wipePackageFiles(&package);
                throw std::runtime_error(
                    "package model file missing scenario=" +
                    algorithm_config.model_scenario_code());
            }
            const std::string fingerprint =
                contentFingerprint(*model_bytes);
            const std::string group_id = declaredModelGroup(
                algorithm_config.model_scenario_code(), fingerprint);
            auto [it, inserted] = grouped.try_emplace(group_id);
            PreparedModel& model = it->second;
            if (inserted) {
                model.group_id = group_id;
                model.fingerprint = fingerprint;
                model.queue_depth = modelQueueDepth(group_id);
                model.infer_interval = modelInferInterval(group_id);
                model.root = root;
                model.config.set_agent_id(desired.config.agent_id());
                StreamConfig* model_stream = model.config.add_streams();
                model_stream->CopyFrom(stream);
                model_stream->clear_algorithms();
                const int number = static_cast<int>(grouped.size());
                model.directory =
                    group_dir / ("model_" + std::to_string(number));
                std::filesystem::create_directories(model.directory);
                if (!algorithm::cdky::model_package::
                        writePackageFilesToDirectory(
                            package, model.directory.string(), &error)) {
                    algorithm::cdky::model_package::wipePackageFiles(
                        &package);
                    throw std::runtime_error(
                        "package extract failed: " + error);
                }
                model.model_path =
                    findExtractedFile(model.directory, model_name);
                if (model.model_path.empty()) {
                    algorithm::cdky::model_package::wipePackageFiles(
                        &package);
                    throw std::runtime_error(
                        "extracted model not found: " + model_name);
                }
            } else if (model.fingerprint != fingerprint) {
                if (requireIdenticalModel(group_id)) {
                    algorithm::cdky::model_package::wipePackageFiles(
                        &package);
                    throw std::runtime_error(
                        "model group '" + group_id +
                        "' contains different .model files");
                }
                LOG_WARN("[TaskManager] model group={} scenario={} has "
                         "fingerprint={} but canonical fingerprint={}; "
                         "reusing canonical model by configuration",
                         group_id,
                         algorithm_config.model_scenario_code(),
                         fingerprint, model.fingerprint);
            }
            model.config.mutable_streams(0)->add_algorithms()->CopyFrom(
                algorithm_config);
            algorithm::cdky::model_package::wipePackageFiles(&package);
        }
        std::vector<PreparedModel> result;
        for (auto& entry : grouped) {
            result.push_back(std::move(entry.second));
        }
        return result;
    }

    void writeModelConfigs(PreparedModel& model, int number,
                           int model_count,
                           const std::filesystem::path& group_dir) {
        const nlohmann::json& root = model.root;
        const bool nested =
            root.contains("model_config") &&
            root["model_config"].contains("model_name");
        const auto preprocess = nested
            ? root.value("preprocess_config", nlohmann::json::object())
            : root.value("preprocess", nlohmann::json::object());
        const auto postprocess = nested
            ? root.value("postprocess_config", nlohmann::json::object())
            : nlohmann::json::object();
        const auto classes = nested
            ? root.value("class_config", nlohmann::json::object())
            : nlohmann::json::object();
        const auto shape = nested
            ? preprocess.value(
                  "output_shape", nlohmann::json::array({1, 3, 512, 512}))
            : nlohmann::json::array(
                  {1, 3, root.value("input_h", 512),
                   root.value("input_w", 512)});
        model.width = shape.is_array() && shape.size() >= 4
                          ? shape[3].get<int>() : 512;
        model.height = shape.is_array() && shape.size() >= 4
                           ? shape[2].get<int>() : 512;
        const int class_count = nested
            ? postprocess.value(
                  "class_num", classes.value("classes_nums", 1))
            : root.value("num_class", 1);
        const auto labels = nested
            ? classes.value(
                  "class_names", nlohmann::json::array({"object"}))
            : root.value(
                  "class_names", nlohmann::json::array({"object"}));
        const std::string suffix = std::to_string(number);
        const auto label_path = group_dir / ("labels_" + suffix + ".txt");
        std::ostringstream label_text;
        for (const auto& label : labels) {
            label_text << label.get<std::string>() << "\n";
        }
        writeText(label_path, label_text.str());

        const auto configured_padding = preprocess.value(
            "padding_data", nlohmann::json::array({114, 114, 114}));
        nlohmann::json padding = nlohmann::json::array();
        for (size_t index = 0; index < 3; ++index) {
            padding.push_back(
                configured_padding.is_array() &&
                        index < configured_padding.size() &&
                        configured_padding[index].is_number()
                    ? static_cast<int>(
                          configured_padding[index].get<double>())
                    : 114);
        }
        std::ostringstream pre;
        pre << "%YAML:1.0\ndie-id: 0\ntarget-infer-ids: ["
            << number << "]\nselect-class-ids: [1, 2, 3]\ninterval: ["
            << model.infer_interval
            << ", 1]\noutput-order: 0\noutput-format: 3\noutput-shape: "
            << shape.dump() << "\ndata-type: 4\npoolsize: "
            << options_.preprocess_pool_size
            << "\nchannel: 0\n\nmaintain_aspect_ratio:\n  enable: true\n"
            << "  padding-value: " << padding.dump()
            << "\n\nnormalize:\n  enable: true\n  normalizationmode: 1\n"
            << "  maxminreciprocal: [0.00392157, 0.00392157, 0.00392157]\n"
            << "  minvalue: [0.0, 0.0, 0.0]\n"
            << "  stdreciprocal: [1, 1, 1]\n"
            << "  meanvalue: [0, 0, 0]\n"
            << "  bypassstepquantization: false\n  stepreciprocal: 127\n\n"
            << "crop:\n  enable: false\ndump:\n  enable: false\n";
        writeText(group_dir / ("EsPreProcess_" + suffix + ".yaml"),
                  pre.str());

        std::ostringstream infer;
        infer << "model-filepath: \"" << model.model_path.string()
              << "\"\nunique-id: " << number
              << "\ndie-id: 0\ninferOutputPoolSize: "
              << options_.infer_output_pool_size
              << "\ndumpflag: 0\nisAsync: 1\n";
        writeText(group_dir / ("EsInfer_" + suffix + ".yaml"),
                  infer.str());

        nlohmann::json thresholds = postprocess.value(
            "score_thresholds", nlohmann::json::array());
        if (thresholds.empty()) {
            thresholds = nlohmann::json::array();
            for (int index = 0; index < class_count; ++index) {
                thresholds.push_back(root.value("conf_thresh", 0.5F));
            }
        }
        const auto img_wh = postprocess.value(
            "img_wh", nlohmann::json::array(
                          {model.width, model.height}));
        nlohmann::json input_scale = postprocess.value(
            "input_scale",
            root.value("input_scale", nlohmann::json::array()));
        if (input_scale.empty()) {
            const auto quant_path = model.directory / "esquant/table.json";
            std::ifstream quant_input(quant_path);
            nlohmann::json quant;
            if (quant_input) {
                quant_input >> quant;
            }
            if (quant.contains("model_info") &&
                quant["model_info"].contains("outputs") &&
                quant.contains("blob_info")) {
                for (const auto& output_name :
                     quant["model_info"]["outputs"]) {
                    const std::string name =
                        output_name.get<std::string>();
                    if (quant["blob_info"].contains(name) &&
                        quant["blob_info"][name].contains("int8_step")) {
                        input_scale.push_back(
                            quant["blob_info"][name]["int8_step"]);
                    }
                }
            }
        }
        if (input_scale.empty()) {
            throw std::runtime_error(
                "package config missing postprocess output scales group=" +
                model.group_id);
        }
        const std::string model_type =
            root.value("model_type", std::string{"yolov8_det"});
        const int network_name =
            model_type.find("yolov8") != std::string::npos ? 5 : 3;
        std::ostringstream post;
        post << "%YAML:1.0\npostprocess-params:\n  die-id: 0\n"
             << "  dsp-id: 3\n  network-type: 0\n  target-inferId: "
             << number << "\n  optype: 0\n  softmaxScale: 0.188668\n\n"
             << "detector-params:\n  labelfile-path: \""
             << label_path.string()
             << "\"\n  cluster-mode: 0\n  attach-class-ids: 0;1\n"
             << "  Per_scoreThreshold: " << thresholds.dump()
             << "\n\nclass-attrs-all:\n  detected-min-w: 5\n"
             << "  detected-min-h: 5\n  detected-max-w: "
             << model.width << "\n  detected-max-h: " << model.height
             << "\n  minBoxes: 3\n  pre-cluster-threshold: 0.2\n"
             << "  post-cluster-threshold: 0.5\n"
             << "  dbscan-min-score: 0.7\n  nms-iou-threshold: 0.1\n"
             << "  topk: 1\n  roi-top-offset: 0\n  roi-bottom-offset: "
             << model.height
             << "\n\nclass-attrs-0:\n  topk: 2\n"
             << "  nms-iou-threshold: 0.5\n"
             << "  pre-cluster-threshold: 0.2\n\ndetection-params:\n"
             << "  network-name: " << network_name
             << "\n  nms-method: 0\n  iou-method: 0\n  imgWH: "
             << img_wh.dump() << "\n  classNum: " << class_count
             << "\n  anchorNum: 3\n"
             << "  anchorScale: [[10, 13, 16, 30, 33, 23], "
             << "[30, 61, 62, 45, 59, 119], "
             << "[116, 90, 156, 198, 373, 326]]\n"
             << "  input-scale: " << input_scale.dump()
             << "\n  maxbboxperclass: 100\n  maxbboxperimg: 200\n"
             << "  scoreThreshold: "
             << root.value("conf_thresh", 0.5F)
             << "\n  iouThreshold: "
             << root.value("iou_thresh", 0.45F)
             << "\n  softnmssigma: 0.6\n  imgOffset: [0, 0]\n";
        writeText(group_dir / ("EsPostProcess_" + suffix + ".yaml"),
                  post.str());
        writeText(group_dir / ("EsTrackerLite_" + suffix + ".yaml"),
                  "min-thresh: 0.1\nhigh-thresh: 0.5\n"
                  "max-iou-distance: 0.7\nhigh-thresh-person: 0.5\n"
                  "high-thresh-motorbike: 0.5\nmax-age: 30\nn-init: 1\n");

        const auto fixed_config =
            group_dir / ("agent-config_" + suffix + ".pb");
        {
            std::ofstream output(
                fixed_config, std::ios::binary | std::ios::trunc);
            if (!output || !model.config.SerializeToOstream(&output)) {
                throw std::runtime_error(
                    "failed to write model group config");
            }
        }
        std::ostringstream event;
        event << "agent-id: " << options_.agent_id << "\nevent-config: "
              << options_.pipeline_root
              << "/config/Event.yaml\nfixed-agent-config: "
              << fixed_config.string() << "\nalarm-relay-socket: "
              << options_.relay_socket << "\nmodel-group-id: "
              << model.group_id << "\nmodel-group-count: "
              << model_count << "\n";
        writeText(group_dir / ("EsEvent_" + suffix + ".yaml"),
                  event.str());
    }

    pid_t startStreamWorker(const DesiredGroup& desired,
                            int vdec_offset) {
        if (desired.config.streams_size() != 1) {
            throw std::runtime_error(
                "stream worker requires exactly one stream");
        }
        const std::filesystem::path group_dir =
            std::filesystem::path(options_.runtime_dir) /
            ("stream_" + safeName(desired.stream_id) + "_" +
             stableId(desired.key));
        std::error_code ec;
        std::filesystem::remove_all(group_dir, ec);
        std::filesystem::create_directories(group_dir);
        auto models = prepareModels(desired, group_dir);
        if (models.empty()) {
            throw std::runtime_error("stream has no usable model package");
        }
        for (size_t index = 0; index < models.size(); ++index) {
            writeModelConfigs(models[index], static_cast<int>(index + 1),
                              static_cast<int>(models.size()), group_dir);
        }

        std::ostringstream vdec;
        vdec << "%YAML:1.0\ninput:\n  type: h264\nparam:\n"
             << "  die-id: 0\n  align: 1\n  poolsize: "
             << options_.vdec_pool_size
             << "\noutput:\n  picture-0:\n    video-format: nv12\n"
             << "dump:\n  enable: false\n";
        writeText(group_dir / "EsVdec.yaml", vdec.str());

        const auto& stream = desired.config.streams(0);
        std::ostringstream demux;
        demux << "url: " << stream.rtsp_url() << "\nstream-id: "
              << stream.stream_id()
              << "\noutfps: 25\ntotalframe: 0\ndump:\n  enable: false\n";
        writeText(group_dir / "EsAvDemux.yaml", demux.str());

        std::vector<std::string> arguments;
        auto add = [&arguments](std::initializer_list<std::string> values) {
            arguments.insert(arguments.end(), values.begin(), values.end());
        };
        const std::string launcher =
            (std::filesystem::path(options_.pipeline_root) /
             "bin/pipeline_agent").string();
        add({launcher, "--worker", "perfstat_interval", "10000000",
             "config_path", group_dir.string() + "/", "lib_path",
             options_.pipeline_root + "/lib/", "EsAvDemux", "-path",
             "EsAvDemux.yaml", "-loopnum", "200000000", "-", "!",
             "EsEvidenceRecorder", "-name", "record1", "-", "!", "EsVdec",
             "-name", "decoder1", "-path", "EsVdec.yaml", "-", "!", "EsMux",
             "-name", "mux1", "-timeout", "40", "-poolsize",
             std::to_string(options_.mux_pool_size), "-", "!", "EsFrameFork",
             "-name", "fork1", "-"});
        for (size_t index = 0; index < models.size(); ++index) {
            const std::string number = std::to_string(index + 1);
            if (index > 0) {
                add({"element", "-name", "fork1", "-"});
            }
            add({"!", "EsQueue", "-name", "queue_model_" + number,
                 "-type", "1", "-deepth",
                 std::to_string(models[index].queue_depth), "-", "!",
                 "EsPreProcess", "-name", "preproc_" + number, "-path",
                 "EsPreProcess_" + number + ".yaml", "-", "!", "EsInfer",
                 "-name", "infer_" + number, "-path",
                 "EsInfer_" + number + ".yaml", "-", "!", "EsQueue",
                 "-name", "queue_post_" + number, "-type", "1", "-deepth",
                 std::to_string(models[index].queue_depth), "-", "!",
                 "EsPostProcess", "-name", "post_" + number, "-path",
                 "EsPostProcess_" + number + ".yaml", "-", "!",
                 "EsTrackerLite", "-name", "track_" + number, "-path",
                 "EsTrackerLite_" + number + ".yaml", "-", "!", "EsEvent",
                 "-name", "event_" + number, "-path",
                 "EsEvent_" + number + ".yaml", "-", "!", "EsTestSink",
                 "-name", "sink_" + number, "-"});
        }

        const pid_t pid = ::fork();
        if (pid < 0) {
            throw std::runtime_error("fork failed");
        }
        if (pid == 0) {
            const std::string offset = std::to_string(vdec_offset);
            ::setenv("LD_LIBRARY_PATH",
                     (options_.pipeline_root + "/lib").c_str(), 1);
            const char* worker_perf =
                ::getenv("PIPELINE_WORKER_PERF_STATIC_FLAG");
            if (worker_perf != nullptr && worker_perf[0] != '\0') {
                ::setenv("PERF_STATIC_FLAG", worker_perf, 1);
            }
            const char* worker_log_level =
                ::getenv("PIPELINE_WORKER_LOG_LEVEL");
            if (worker_log_level != nullptr &&
                worker_log_level[0] != '\0') {
                ::setenv("PL_LOG_LEVEL", worker_log_level, 1);
            }
            ::setenv("PIPELINE_VDEC_GROUP_OFFSET", offset.c_str(), 1);
            std::vector<char*> argv;
            for (auto& argument : arguments) {
                argv.push_back(argument.data());
            }
            argv.push_back(nullptr);
            ::execv(argv[0], argv.data());
            _exit(127);
        }
        std::ostringstream summary;
        for (size_t index = 0; index < models.size(); ++index) {
            if (index > 0) {
                summary << ',';
            }
            summary << models[index].group_id << ':'
                    << models[index].config.streams(0).algorithms_size();
        }
        LOG_INFO("[TaskManager] stream worker started pid={} stream={} "
                 "vdec_offset={} model_groups={} groups=[{}]",
                 pid, desired.stream_id, vdec_offset, models.size(),
                 summary.str());
        return pid;
    }

    pid_t startWorker(const DesiredGroup& group, int vdec_offset) {
        using algorithm::cdky::model_package::Package;
        Package package;
        std::string error;
        if (!algorithm::cdky::model_package::loadPackage(
                group.package_path, package, &error)) {
            throw std::runtime_error("package decrypt failed: " + error);
        }

        const std::filesystem::path group_dir =
            std::filesystem::path(options_.runtime_dir) /
            (safeName(group.scenario) + "_" + stableId(group.key));
        const std::filesystem::path model_dir = group_dir / "model";
        std::error_code ec;
        std::filesystem::remove_all(group_dir, ec);
        std::filesystem::create_directories(model_dir);
        if (!algorithm::cdky::model_package::writePackageFilesToDirectory(
                package, model_dir.string(), &error)) {
            algorithm::cdky::model_package::wipePackageFiles(&package);
            throw std::runtime_error("package extract failed: " + error);
        }

        const nlohmann::json package_root = package.root_config;
        algorithm::cdky::model_package::wipePackageFiles(&package);
        const bool package_nested_schema =
            package_root.contains("model_config") &&
            package_root["model_config"].contains("model_name");
        const std::string package_model_name = package_nested_schema
            ? package_root["model_config"]["model_name"].get<std::string>()
            : package_root.value("model_name", std::string{});
        if (package_model_name.empty()) {
            throw std::runtime_error("package root config missing model_name");
        }
        const std::filesystem::path model_path =
            findExtractedFile(model_dir, package_model_name);
        if (model_path.empty()) {
            throw std::runtime_error(
                "package model file not found: " + package_model_name);
        }

        const nlohmann::json& root = package_root;
        const bool nested_schema = package_nested_schema;

        const auto preprocess = nested_schema
            ? root.value("preprocess_config", nlohmann::json::object())
            : root.value("preprocess", nlohmann::json::object());
        const auto postprocess = nested_schema
            ? root.value("postprocess_config", nlohmann::json::object())
            : nlohmann::json::object();
        const auto classes = nested_schema
            ? root.value("class_config", nlohmann::json::object())
            : nlohmann::json::object();
        const auto shape = nested_schema
            ? preprocess.value(
                  "output_shape", nlohmann::json::array({1, 3, 512, 512}))
            : nlohmann::json::array(
                  {1, 3, root.value("input_h", 512),
                   root.value("input_w", 512)});
        const int width = shape.is_array() && shape.size() >= 4
                              ? shape[3].get<int>() : 512;
        const int height = shape.is_array() && shape.size() >= 4
                               ? shape[2].get<int>() : 512;
        const int class_count = nested_schema
            ? postprocess.value(
                  "class_num", classes.value("classes_nums", 1))
            : root.value("num_class", 1);
        const auto labels = nested_schema
            ? classes.value(
                  "class_names", nlohmann::json::array({"object"}))
            : root.value(
                  "class_names", nlohmann::json::array({"object"}));

        std::ostringstream label_text;
        for (const auto& label : labels) {
            label_text << label.get<std::string>() << "\n";
        }
        writeText(group_dir / "labels.txt", label_text.str());

        std::ostringstream vdec;
        vdec << "%YAML:1.0\ninput:\n  type: h264\nparam:\n"
             << "  die-id: 0\n  align: 1\n  poolsize: "
             << options_.vdec_pool_size << "\noutput:\n"
             << "  picture-0:\n    video-format: nv12\n    scale: ["
             << width << ", " << height << "]\n"
             << "  picture-1:\n    video-format: nv12\ndump:\n  enable: false\n";
        writeText(group_dir / "EsVdec.yaml", vdec.str());

        const auto configured_padding = preprocess.value(
            "padding_data", nlohmann::json::array({114, 114, 114}));
        nlohmann::json padding = nlohmann::json::array();
        for (size_t index = 0; index < 3; ++index) {
            const int value =
                configured_padding.is_array() &&
                        index < configured_padding.size() &&
                        configured_padding[index].is_number()
                    ? static_cast<int>(
                          configured_padding[index].get<double>())
                    : 114;
            padding.push_back(value);
        }
        std::ostringstream pre;
        pre << "%YAML:1.0\ndie-id: 0\ntarget-infer-ids: [1, 3]\n"
            << "select-class-ids: [1, 2, 3]\ninterval: [3, 1]\n"
            << "output-order: 0\noutput-format: 3\noutput-shape: "
            << shape.dump() << "\ndata-type: 4\npoolsize: "
            << options_.preprocess_pool_size << "\nchannel: 0\n\n"
            << "maintain_aspect_ratio:\n  enable: true\n"
            << "  padding-value: " << padding.dump() << "\n\nnormalize:\n"
            << "  enable: true\n  normalizationmode: 1\n"
            << "  maxminreciprocal: [0.00392157, 0.00392157, 0.00392157]\n"
            << "  minvalue: [0.0, 0.0, 0.0]\n"
            << "  stdreciprocal: [1, 1, 1]\n  meanvalue: [0, 0, 0]\n"
            << "  bypassstepquantization: false\n  stepreciprocal: 127\n\n"
            << "crop:\n  enable: false\ndump:\n  enable: false\n";
        writeText(group_dir / "EsPreProcess.yaml", pre.str());

        std::ostringstream infer;
        const int infer_output_pool_size = std::max(
            options_.infer_output_pool_size,
            group.config.streams_size() + 1);
        infer << "model-filepath: \"" << model_path.string()
               << "\"\nunique-id: 1\ndie-id: 0\ninferOutputPoolSize: "
              << infer_output_pool_size << "\n"
              << "dumpflag: 0\nisAsync: 1\n";
        writeText(group_dir / "EsInfer.yaml", infer.str());

        nlohmann::json thresholds = postprocess.value(
            "score_thresholds", nlohmann::json::array());
        if (thresholds.empty()) {
            thresholds = nlohmann::json::array();
            const float threshold = root.value("conf_thresh", 0.5F);
            for (int index = 0; index < class_count; ++index) {
                thresholds.push_back(threshold);
            }
        }
        const auto img_wh = postprocess.value(
            "img_wh", nlohmann::json::array({width, height}));
        nlohmann::json input_scale = postprocess.value(
            "input_scale", root.value(
                "input_scale", nlohmann::json::array()));
        if (input_scale.empty()) {
            const auto quant_path = model_dir / "esquant/table.json";
            std::ifstream quant_input(quant_path);
            nlohmann::json quant;
            if (quant_input) {
                quant_input >> quant;
            }
            if (quant.contains("model_info") &&
                quant["model_info"].contains("outputs") &&
                quant.contains("blob_info")) {
                for (const auto& output_name :
                     quant["model_info"]["outputs"]) {
                    const std::string name = output_name.get<std::string>();
                    if (quant["blob_info"].contains(name) &&
                        quant["blob_info"][name].contains("int8_step")) {
                        input_scale.push_back(
                            quant["blob_info"][name]["int8_step"]);
                    }
                }
            }
        }
        if (input_scale.empty()) {
            throw std::runtime_error(
                "package config missing postprocess output scales");
        }
        const std::string model_type =
            root.value("model_type", std::string{"yolov8_det"});
        const int network_name =
            model_type.find("yolov8") != std::string::npos ? 5 : 3;
        std::ostringstream post;
        post << "%YAML:1.0\npostprocess-params:\n  die-id: 0\n  dsp-id: 3\n"
             << "  network-type: 0\n  target-inferId: 1\n  optype: 0\n"
             << "  softmaxScale: 0.188668\n\ndetector-params:\n"
             << "  labelfile-path: \"" << (group_dir / "labels.txt").string()
             << "\"\n  cluster-mode: 0\n  attach-class-ids: 0;1\n"
             << "  Per_scoreThreshold: " << thresholds.dump() << "\n\n"
             << "class-attrs-all:\n  detected-min-w: 5\n  detected-min-h: 5\n"
             << "  detected-max-w: " << width << "\n  detected-max-h: "
             << height << "\n  minBoxes: 3\n  pre-cluster-threshold: 0.2\n"
             << "  post-cluster-threshold: 0.5\n  dbscan-min-score: 0.7\n"
             << "  nms-iou-threshold: 0.1\n  topk: 1\n  roi-top-offset: 0\n"
             << "  roi-bottom-offset: " << height << "\n\n"
             << "class-attrs-0:\n  topk: 2\n  nms-iou-threshold: 0.5\n"
             << "  pre-cluster-threshold: 0.2\n\ndetection-params:\n"
             << "  network-name: " << network_name
             << "\n  nms-method: 0\n  iou-method: 0\n"
             << "  imgWH: " << img_wh.dump() << "\n  classNum: "
             << class_count << "\n  anchorNum: 3\n"
             << "  anchorScale: [[10, 13, 16, 30, 33, 23], "
             << "[30, 61, 62, 45, 59, 119], "
             << "[116, 90, 156, 198, 373, 326]]\n"
             << "  input-scale: " << input_scale.dump() << "\n"
             << "  maxbboxperclass: 100\n  maxbboxperimg: 200\n"
             << "  scoreThreshold: " << root.value("conf_thresh", 0.5F)
             << "\n  iouThreshold: " << root.value("iou_thresh", 0.45F)
             << "\n"
             << "  softnmssigma: 0.6\n  imgOffset: [0, 0]\n";
        writeText(group_dir / "EsPostProcess.yaml", post.str());

        writeText(group_dir / "EsTrackerLite.yaml",
                  "min-thresh: 0.1\nhigh-thresh: 0.5\n"
                  "max-iou-distance: 0.7\nhigh-thresh-person: 0.5\n"
                  "high-thresh-motorbike: 0.5\nmax-age: 30\nn-init: 1\n");

        const auto fixed_config_path = group_dir / "agent-config.pb";
        {
            std::ofstream output(fixed_config_path,
                                 std::ios::binary | std::ios::trunc);
            if (!output || !group.config.SerializeToOstream(&output)) {
                throw std::runtime_error("failed to write worker config");
            }
        }
        std::ostringstream event;
        event << "agent-id: " << options_.agent_id << "\n"
              << "event-config: " << options_.pipeline_root
              << "/config/Event.yaml\nfixed-agent-config: "
              << fixed_config_path.string() << "\nalarm-relay-socket: "
              << options_.relay_socket << "\nscenario-code: "
              << group.scenario << "\n";
        writeText(group_dir / "EsEvent.yaml", event.str());

        for (int index = 0; index < group.config.streams_size(); ++index) {
            const auto& stream = group.config.streams(index);
            std::ostringstream demux;
            demux << "url: " << stream.rtsp_url() << "\nstream-id: "
                  << stream.stream_id()
                  << "\noutfps: 25\ntotalframe: 0\ndump:\n  enable: false\n";
            writeText(group_dir /
                          ("EsAvDemux_" + std::to_string(index + 1) + ".yaml"),
                      demux.str());
        }

        std::vector<std::string> arguments;
        auto add = [&arguments](std::initializer_list<std::string> values) {
            arguments.insert(arguments.end(), values.begin(), values.end());
        };
        const std::string launcher =
            (std::filesystem::path(options_.pipeline_root) /
             "bin/pipeline_agent").string();
        add({launcher, "--worker", "perfstat_interval", "10000000", "config_path",
             group_dir.string() + "/", "lib_path",
             options_.pipeline_root + "/lib/"});
        for (int index = 0; index < group.config.streams_size(); ++index) {
            const std::string number = std::to_string(index + 1);
            add({"EsAvDemux", "-path", "EsAvDemux_" + number + ".yaml",
                 "-loopnum", "200000000", "-", "!", "EsEvidenceRecorder",
                 "-name", "record" + number, "-", "!", "EsVdec", "-name",
                 "decoder" + number, "-path", "EsVdec.yaml", "-"});
            if (index == 0) {
                add({"!", "EsMux", "-name", "mux1", "-timeout", "40",
                     "-poolsize", std::to_string(options_.mux_pool_size), "-"});
            } else {
                add({"!", "element", "-name", "mux1", "-"});
            }
        }
        add({"!", "EsQueue", "-name", "queuepreproc1", "-type", "0",
             "-deepth", "5", "-", "!", "EsPreProcess", "-name", "preproc1",
             "-path", "EsPreProcess.yaml", "-", "!", "EsInfer", "-name",
             "infer1", "-path", "EsInfer.yaml", "-", "!", "EsQueue",
             "-name", "queuepost1", "-type", "0", "-deepth", "5", "-", "!",
             "EsPostProcess", "-name", "post1", "-path",
             "EsPostProcess.yaml", "-", "!", "EsTrackerLite", "-name",
             "track1", "-path", "EsTrackerLite.yaml", "-", "!", "EsEvent",
             "-name", "event1", "-path", "EsEvent.yaml", "-", "!",
             "EsTestSink", "-name", "sink1", "-"});

        const pid_t pid = ::fork();
        if (pid < 0) {
            throw std::runtime_error("fork failed");
        }
        if (pid == 0) {
            const std::string offset = std::to_string(vdec_offset);
            ::setenv("LD_LIBRARY_PATH",
                     (options_.pipeline_root + "/lib").c_str(), 1);
            const char* worker_perf =
                ::getenv("PIPELINE_WORKER_PERF_STATIC_FLAG");
            if (worker_perf != nullptr && worker_perf[0] != '\0') {
                ::setenv("PERF_STATIC_FLAG", worker_perf, 1);
            }
            const char* worker_log_level =
                ::getenv("PIPELINE_WORKER_LOG_LEVEL");
            if (worker_log_level != nullptr && worker_log_level[0] != '\0') {
                ::setenv("PL_LOG_LEVEL", worker_log_level, 1);
            }
            ::setenv("PIPELINE_VDEC_GROUP_OFFSET", offset.c_str(), 1);
            std::vector<char*> argv;
            for (auto& argument : arguments) {
                argv.push_back(argument.data());
            }
            argv.push_back(nullptr);
            ::execv(argv[0], argv.data());
            _exit(127);
        }
        LOG_INFO("[TaskManager] worker started pid={} stream={} scenario={} "
                 "streams={} vdec_offset={} infer_output_pool={} model={}",
                 pid, group.stream_id, group.scenario,
                 group.config.streams_size(), vdec_offset,
                 infer_output_pool_size, model_path.string());
        return pid;
    }

    void stopWorker(Worker& worker) {
        if (worker.pid <= 0) {
            return;
        }
        const pid_t pid = worker.pid;
        const auto started = std::chrono::steady_clock::now();
        LOG_INFO("[TaskManager] stopping stream worker pid={} stream={}",
                 pid, worker.desired.stream_id);
        ::kill(worker.pid, SIGTERM);
        const int attempts =
            std::max(1, options_.worker_stop_timeout_ms / 100);
        for (int attempt = 0; attempt < attempts; ++attempt) {
            int status = 0;
            if (::waitpid(worker.pid, &status, WNOHANG) == worker.pid) {
                const auto elapsed_ms =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - started).count();
                LOG_INFO("[TaskManager] worker stopped pid={} stream={} "
                         "elapsed_ms={} status={}",
                         pid, worker.desired.stream_id, elapsed_ms, status);
                worker.pid = -1;
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        LOG_WARN("[TaskManager] worker pid={} did not stop within {} ms; "
                 "sending SIGKILL",
                 pid, options_.worker_stop_timeout_ms);
        ::kill(worker.pid, SIGKILL);
        ::waitpid(worker.pid, nullptr, 0);
        worker.pid = -1;
    }

    bool reapExited() {
        bool exited = false;
        for (auto& entry : workers_) {
            if (entry.second.pid <= 0) {
                continue;
            }
            int status = 0;
            if (::waitpid(entry.second.pid, &status, WNOHANG) ==
                entry.second.pid) {
                LOG_ERROR("[TaskManager] stream worker exited pid={} "
                          "stream={} status={}",
                          entry.second.pid, entry.second.desired.stream_id,
                          status);
                entry.second.pid = -1;
                exited = true;
            }
        }
        return exited;
    }

    Options options_;
    mutable std::mutex mutex_;
    std::deque<AgentConfig> pending_configs_;
    std::chrono::steady_clock::time_point first_pending_config_at_;
    std::chrono::steady_clock::time_point last_pending_config_at_;
    bool pending_reload_all_ = false;
    std::vector<std::string> pending_reload_scenarios_;
    std::unordered_map<std::string, std::string> scenario_model_groups_;
    std::unordered_map<std::string, int> group_queue_depths_;
    std::unordered_map<std::string, int> group_infer_intervals_;
    std::unordered_map<std::string, bool>
        group_require_identical_model_;
    std::unordered_map<std::string, std::string>
        group_canonical_scenarios_;
    std::map<std::string, StreamConfig> active_streams_;
    std::map<std::string, Worker> workers_;
};

Options parseOptions(int argc, char** argv) {
    Options options;
    for (int index = 1; index + 1 < argc; index += 2) {
        const std::string key = argv[index];
        const std::string value = argv[index + 1];
        if (key == "--pipeline-root") {
            options.pipeline_root = value;
        } else if (key == "--runtime-dir") {
            options.runtime_dir = value;
        } else if (key == "--platform-socket") {
            options.platform_socket = value;
        } else if (key == "--relay-socket") {
            options.relay_socket = value;
        } else if (key == "--agent-id") {
            options.agent_id = value;
        } else if (key == "--heartbeat-ms") {
            options.heartbeat_ms = std::max(1000, std::stoi(value));
        } else if (key == "--config-debounce-ms") {
            options.config_debounce_ms = std::max(0, std::stoi(value));
        } else if (key == "--config-max-wait-ms") {
            options.config_max_wait_ms =
                std::max(options.config_debounce_ms, std::stoi(value));
        } else if (key == "--worker-stop-timeout-ms") {
            options.worker_stop_timeout_ms = std::max(1000, std::stoi(value));
        } else if (key == "--vdec-pool-size") {
            options.vdec_pool_size = std::max(2, std::stoi(value));
        } else if (key == "--mux-pool-size") {
            options.mux_pool_size = std::max(2, std::stoi(value));
        } else if (key == "--preprocess-pool-size") {
            options.preprocess_pool_size = std::max(3, std::stoi(value));
        } else if (key == "--infer-output-pool-size") {
            options.infer_output_pool_size = std::max(3, std::stoi(value));
        } else if (key == "--model-group-config") {
            options.model_group_config = value;
        } else if (key == "--model-queue-depth") {
            options.model_queue_depth = std::max(1, std::stoi(value));
        } else {
            throw std::runtime_error("unknown option: " + key);
        }
    }
    return options;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--worker") {
        return pipelineWorkerMain(argc - 1, argv + 1);
    }

    Options options;
    try {
        options = parseOptions(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 2;
    }

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    std::filesystem::create_directories(options.runtime_dir);

    TaskManager task_manager(options);
    pipeline::agent::SocketConfig socket_config;
    socket_config.socket_path = options.platform_socket;
    socket_config.agent_id = options.agent_id;
    socket_config.send_queue_size = 1000;
    socket_config.heartbeat_interval_ms = options.heartbeat_ms;
    pipeline::agent::IpcClient ipc(std::move(socket_config));
    ipc.setConfigCallback(
        [&task_manager](const AgentConfig& config) {
            task_manager.submit(config);
        });
    ipc.setModelUpdateCallback(
        [&task_manager](const AlgorithmModelUpdate& update) {
            task_manager.requestModelReload(update);
            return pipeline::agent::IpcClient::AckResult{
                true, "model reload scheduled"};
        });

    pipeline::agent::AlarmRelayServer relay(
        options.relay_socket,
        [&ipc](AlarmInfo alarm) {
            if (!ipc.pushAlarm(std::move(alarm))) {
                LOG_ERROR("[pipeline_agent] alarm relay queue full");
            }
        });
    if (!relay.start() || !ipc.start()) {
        LOG_ERROR("[pipeline_agent] failed to start IPC");
        return 1;
    }

    const auto started = std::chrono::steady_clock::now();
    auto next_heartbeat = started;
    while (!g_stop) {
        task_manager.tick();
        const auto now = std::chrono::steady_clock::now();
        if (now >= next_heartbeat) {
            const int active = task_manager.activeStreams();
            const int64_t uptime =
                std::chrono::duration_cast<std::chrono::seconds>(
                    now - started).count();
            ipc.pushHeartbeat(pipeline::agent::buildHeartbeat(
                options.agent_id, active, active, 0, uptime));
            next_heartbeat =
                now + std::chrono::milliseconds(options.heartbeat_ms);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    task_manager.stopAll();
    relay.stop();
    ipc.stop();
    return 0;
}
