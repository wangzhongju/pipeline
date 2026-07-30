#include "pipeline_agent/IpcClient.h"
#include "pipeline_agent/Logger.h"
#include "pipeline_agent/MessageMapper.h"

namespace pipeline::agent {

IpcClient::IpcClient(SocketConfig cfg)
    : cfg_(std::move(cfg)) {}

IpcClient::~IpcClient() { stop(); }

bool IpcClient::start() {
    sender_ = std::make_unique<SocketSender>(cfg_);
    sender_->setRecvCallback([this](const std::string& data) { onRecv(data); });
    return sender_->start();
}

void IpcClient::stop() {
    if (sender_) sender_->stop();
}

bool IpcClient::pushAlarm(AlarmInfo alarm) {
    return sender_->sendFrame(buildEnvelopePayload(alarm, seq_++));
}

bool IpcClient::pushHeartbeat(HeartBeat heartbeat) {
    return sender_->sendFrame(buildEnvelopePayload(heartbeat, seq_++));
}

void IpcClient::setConfigCallback(ConfigCallback cb) {
    config_cb_ = std::move(cb);
}

void IpcClient::setModelUpdateCallback(ModelUpdateCallback cb) {
    model_update_cb_ = std::move(cb);
}

bool IpcClient::sendAck(uint32_t seq, MessageType ack_for, bool success, const std::string& message) {
    auto ack = buildAck(cfg_.agent_id, ack_for, success, message);
    return sender_ && sender_->sendFrame(buildEnvelopePayload(ack, seq));
}

// ── 接收分发 ──────────────────────────────────────────────
void IpcClient::onRecv(const std::string& data) {
    Envelope env;
    if (!env.ParseFromString(data)) {
        LOG_ERROR("[IpcClient] parse Envelope failed");
        return;
    }

    if (env.version() != PROTO_VERSION_CURRENT) {
        LOG_ERROR("[IpcClient] version mismatch: recv={} current={}", 
            static_cast<int>(env.version()), static_cast<int>(PROTO_VERSION_CURRENT));
        return;
    }

    switch (env.type()) {
    case MSG_CONFIG: {
        if (!env.has_config()) {
            LOG_ERROR("[IpcClient] MSG_CONFIG missing config body");
            return;
        }
        handleConfig(env);
        break;
    }
    case MSG_ALG_MODEL_UPDATE: {
        if (!env.has_model_update()) {
            LOG_ERROR("[IpcClient] MSG_ALG_MODEL_UPDATE missing model_update body");
            return;
        }
        handleAlgorithmModelUpdate(env);
        break;
    }
    default:
        LOG_DEBUG("[IpcClient] recv unhandled msg type={}", static_cast<int>(env.type()));
        break;
    }
}

void IpcClient::handleConfig(const Envelope& env) {
    AgentConfig cfg = env.config();
    LOG_INFO("[IpcClient] received config agent_id={} config_id={} streams={}",
             cfg.agent_id(), cfg.config_id(), cfg.streams_size());
    for (int i = 0; i < cfg.streams_size(); ++i) {
        const auto& stream = cfg.streams(i);
        LOG_INFO("[IpcClient]   stream[{}] enabled={} id={} rtsp_url={} new_rtsp_url={} reconnect_s={} "
                 "snapshot_dir={} record_dir={} record_duration_s={} dedup_interval_s={} algorithms={}",
                 i,
                 stream.enabled(),
                 stream.stream_id(),
                 stream.rtsp_url(),
                 stream.new_rtsp_url(),
                 stream.reconnect_interval_s(),
                 stream.alarm_snapshot_dir(),
                 stream.alarm_record_dir(),
                 stream.alarm_record_duration_s(),
                 stream.alarm_dedup_interval_s(),
                 stream.algorithms_size());
        for (int j = 0; j < stream.algorithms_size(); ++j) {
            const auto& algo = stream.algorithms(j);
            LOG_INFO("[IpcClient]     algo[{}] scenario={} model={} ver={} threshold={} alarm_level={} date={}/{} rois={}",
                     j,
                     algo.model_scenario_code(),
                     algo.model_config_name(),
                     algo.model_version(),
                     algo.threshold(),
                     algo.alarm_level(),
                     algo.start_date(),
                     algo.end_date(),
                     algo.rois_size());
            for (int k = 0; k < algo.rois_size(); ++k) {
                const auto& roi = algo.rois(k);
                LOG_INFO("[IpcClient]       roi[{}] enabled={} mode={} shape_case={}",
                         k,
                         roi.enabled(),
                         RoiArea::RoiMode_Name(roi.mode()),
                         static_cast<int>(roi.shape_case()));

                if (roi.has_rect()) {
                    const auto& rect = roi.rect();
                    LOG_INFO("[IpcClient]         rect cx={} cy={} width={} height={} angle={}",
                             rect.cx(), rect.cy(), rect.width(), rect.height(), rect.angle());
                }

                if (roi.has_poly()) {
                    const auto& poly = roi.poly();
                    LOG_INFO("[IpcClient]         poly points={}", poly.points_size());
                    for (int p = 0; p < poly.points_size(); ++p) {
                        const auto& point = poly.points(p);
                        LOG_INFO("[IpcClient]           point[{}] x={} y={}", p, point.x(), point.y());
                    }
                }
            }
        }
    }

    if (config_cb_) {
        config_cb_(cfg);
    }
    sendAck(env.seq(), MSG_CONFIG, true, "");
}

void IpcClient::handleAlgorithmModelUpdate(const Envelope& env) {
    const auto& update = env.model_update();
    LOG_INFO("[IpcClient] received algorithm model update agent_id={} keys={}",
             update.agent_id(),
             update.model_scenario_codes_size());
    for (int i = 0; i < update.model_scenario_codes_size(); ++i) {
        LOG_INFO("[IpcClient]   model_scenario_code[{}]={}", i, update.model_scenario_codes(i));
    }

    AckResult result;
    if (model_update_cb_) {
        result = model_update_cb_(update);
    } else {
        result.success = false;
        result.message = "algorithm model update callback is not configured";
        LOG_WARN("[IpcClient] {}", result.message);
    }

    sendAck(env.seq(),
            MSG_ALG_MODEL_UPDATE,
            result.success,
            result.message);
}

}  // namespace pipeline::agent
