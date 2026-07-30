#include "pipeline_agent/MessageMapper.h"

#include "pipeline_agent/Time.h"
#include "pipeline_agent/Uuid.h"

#include <algorithm>

namespace pipeline::agent {

namespace {

Envelope makeEnvelope(MessageType type, uint32_t seq) {
    Envelope env;
    env.set_version(PROTO_VERSION_CURRENT);
    env.set_type(type);
    env.set_seq(seq);
    return env;
}

std::string serializeEnvelope(Envelope env) {
    std::string out;
    env.SerializeToString(&out);
    return out;
}

} // namespace

AlarmInfo buildAlarmInfo(const std::string& stream_id,
                         const AlgorithmConfig& detector_cfg,
                         const std::vector<DetectionObject>& targets,
                         const std::string& description,
                         const std::string& snapshot_name,
                         const std::string& record_name) {
    AlarmInfo alarm;
    alarm.set_alarm_id(generateUuidV4());
    alarm.set_stream_id(stream_id);
    alarm.set_timestamp_ms(systemNowMs());
    const DetectionObject* primary_target = targets.empty() ? nullptr : &targets.front();
    alarm.set_model_scenario_code(detector_cfg.model_scenario_code().empty() && primary_target != nullptr
        ? primary_target->class_name()
        : detector_cfg.model_scenario_code());
    alarm.set_level(detector_cfg.alarm_level());
    float max_confidence = 0.0f;
    for (const auto& target : targets) {
        max_confidence = std::max(max_confidence, target.confidence());
        alarm.add_target()->CopyFrom(target);
    }
    alarm.set_confidence(max_confidence);
    alarm.set_description(description);
    alarm.set_snapshot_name(snapshot_name);
    alarm.set_record_name(record_name);
    return alarm;
}

OcrInfo buildOcrInfo(const std::string& stream_id,
                     const AlgorithmConfig& ocr_cfg,
                     const std::vector<DetectionObject>& targets) {
    OcrInfo ocr;
    ocr.set_ocr_id(generateUuidV4());
    ocr.set_stream_id(stream_id);
    ocr.set_timestamp_ms(systemNowMs());
    ocr.set_model_scenario_code(ocr_cfg.model_scenario_code());
    for (const auto& target : targets) {
        ocr.add_target()->CopyFrom(target);
    }
    return ocr;
}

HeartBeat buildHeartbeat(const std::string& agent_id,
                         int total_stream_count,
                         int success_stream_count,
                         int failed_stream_count,
                         int64_t uptime_s) {
    HeartBeat heartbeat;
    heartbeat.set_agent_id(agent_id);
    heartbeat.set_total_stream_count(total_stream_count);
    heartbeat.set_success_stream_count(success_stream_count);
    heartbeat.set_failed_stream_count(failed_stream_count);
    heartbeat.set_uptime_s(uptime_s);
    return heartbeat;
}

Ack buildAck(const std::string& agent_id,
             MessageType ack_for,
             bool success,
             const std::string& message) {
    Ack ack;
    ack.set_agent_id(agent_id);
    ack.set_ack_for(ack_for);
    ack.set_success(success);
    ack.set_message(message);
    return ack;
}

std::string buildEnvelopePayload(const AlarmInfo& alarm, uint32_t seq) {
    auto env = makeEnvelope(MSG_ALARM, seq);
    env.mutable_alarm()->CopyFrom(alarm);
    return serializeEnvelope(std::move(env));
}

std::string buildEnvelopePayload(const OcrInfo& ocr, uint32_t seq) {
    auto env = makeEnvelope(MSG_OCR, seq);
    env.mutable_ocr()->CopyFrom(ocr);
    return serializeEnvelope(std::move(env));
}

std::string buildEnvelopePayload(const HeartBeat& heartbeat, uint32_t seq) {
    auto env = makeEnvelope(MSG_HEARTBEAT, seq);
    env.mutable_heartbeat()->CopyFrom(heartbeat);
    return serializeEnvelope(std::move(env));
}

std::string buildEnvelopePayload(const Ack& ack, uint32_t seq) {
    auto env = makeEnvelope(MSG_ACK, seq);
    env.mutable_ack()->CopyFrom(ack);
    return serializeEnvelope(std::move(env));
}

}  // namespace pipeline::agent
