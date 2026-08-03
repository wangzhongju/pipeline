#pragma once

#include "media-agent.pb.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

extern "C" {
#include "es_comm_video.h"
}

struct AVFormatContext;
struct AVPacket;
struct AVStream;

namespace pipeline::evidence {

struct EncodedVideoPacket {
    const uint8_t* data = nullptr;
    int size = 0;
    int codec_id = 0;
    int width = 0;
    int height = 0;
    int64_t pts = 0;
    int64_t dts = 0;
    int64_t duration = 0;
    int time_base_num = 1;
    int time_base_den = 1000;
    int64_t frame_index = 0;
    bool key_frame = false;
};

class EvidenceService {
public:
    static EvidenceService& instance();

    void applyConfig(const AgentConfig& config);
    void updateDetections(const std::string& stream_id,
                          int64_t frame_index,
                          const std::vector<DetectionObject>& objects);
    bool appendPacket(const std::string& stream_id,
                      const EncodedVideoPacket& packet);
    std::string triggerRecording(
        const std::string& stream_id,
        const std::vector<DetectionObject>& alarm_objects);
    std::string saveSnapshot(const std::string& stream_id,
                             const VIDEO_FRAME_INFO_S& frame,
                             const std::vector<DetectionObject>& objects,
                             const std::string& scenario);
    void close();

private:
    class Recorder;

    EvidenceService() = default;
    ~EvidenceService();

    std::mutex mutex_;
    std::unordered_map<std::string, StreamConfig> configs_;
    std::unordered_map<std::string, std::unique_ptr<Recorder>> recorders_;
};

}  // namespace pipeline::evidence
