#pragma once

#include "media-agent.pb.h"

#include <string>
#include <vector>

namespace pipeline::agent {

AlarmInfo buildAlarmInfo(const std::string& stream_id,
                         const AlgorithmConfig& detector_cfg,
                         const std::vector<DetectionObject>& targets,
                         const std::string& description,
                         const std::string& snapshot_name,
                         const std::string& record_name);

OcrInfo buildOcrInfo(const std::string& stream_id,
                     const AlgorithmConfig& ocr_cfg,
                     const std::vector<DetectionObject>& targets);


HeartBeat buildHeartbeat(const std::string& agent_id,
                         int total_stream_count,
                         int success_stream_count,
                         int failed_stream_count,
                         int64_t uptime_s);

Ack buildAck(const std::string& agent_id,
             MessageType ack_for,
             bool success,
             const std::string& message);

std::string buildEnvelopePayload(const AlarmInfo& alarm, uint32_t seq);

std::string buildEnvelopePayload(const OcrInfo& ocr, uint32_t seq);

std::string buildEnvelopePayload(const HeartBeat& heartbeat, uint32_t seq);

std::string buildEnvelopePayload(const Ack& ack, uint32_t seq);

}  // namespace pipeline::agent
