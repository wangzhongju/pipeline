#pragma once

#include "pipeline_agent/Config.h"
#include "pipeline_agent/SocketSender.h"
#include "media-agent.pb.h"
#include <functional>
#include <memory>
#include <string>
#include <atomic>

namespace pipeline::agent {

/**
 * IPC 业务层客户端
 *
 * 职责：Protobuf Envelope 的解析、配置下发分发与消息发送。
 *       业务消息体和 Envelope 的组装由 protocol mapper 负责，底层传输委托给 SocketSender。
 *
 * 调用方（Pipeline）通过 pushAlarm() / pushHeartbeat() 发送业务消息，
 * 通过 setConfigCallback() 接收对端下发的配置。
 */
class IpcClient {
public:
    struct AckResult {
        bool success = true;
        std::string message;
    };

    explicit IpcClient(SocketConfig cfg);
    ~IpcClient();

    IpcClient(const IpcClient&) = delete;
    IpcClient& operator=(const IpcClient&) = delete;

    bool start();
    void stop();

    /** 推入报警（非阻塞，队满自动丢弃最旧条目） */
    bool pushAlarm(AlarmInfo alarm);

    /** 推入心跳（由 Pipeline 定期调用） */
    bool pushHeartbeat(HeartBeat heartbeat);

    bool isRunning() const { return sender_ && sender_->isRunning(); }

    // 统计（透传给传输层）
    uint64_t sentCount()      const { return sender_ ? sender_->sentCount()      : 0; }
    uint64_t failedCount()    const { return sender_ ? sender_->failedCount()    : 0; }
    uint64_t reconnectCount() const { return sender_ ? sender_->reconnectCount() : 0; }

    /**
     * 注册配置下发回调（需在 start() 前设置）
     * 对端发来 MSG_CONFIG 时在接收线程上调用，回调应尽快返回
     */
    using ConfigCallback = std::function<void(const AgentConfig&)>;
    using ModelUpdateCallback = std::function<AckResult(const AlgorithmModelUpdate&)>;
    void setConfigCallback(ConfigCallback cb);
    void setModelUpdateCallback(ModelUpdateCallback cb);

private:
    // 被传输层 recvLoop 调用
    void onRecv(const std::string& payload);
    bool sendAck(uint32_t seq, MessageType ack_for, bool success, const std::string& message);
    void handleConfig(const Envelope& env);
    void handleAlgorithmModelUpdate(const Envelope& env);

    SocketConfig               cfg_;
    std::unique_ptr<SocketSender> sender_;

    ConfigCallback config_cb_;
    ModelUpdateCallback model_update_cb_;
    std::atomic<uint32_t> seq_{0};
};

}  // namespace pipeline::agent
