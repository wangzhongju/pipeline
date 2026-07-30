#pragma once

#include "media-agent.pb.h"

#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace pipeline::agent {

class AlarmRelayClient {
public:
    explicit AlarmRelayClient(std::string socket_path);
    bool send(const AlarmInfo& alarm) const;

private:
    std::string socket_path_;
};

class AlarmRelayServer {
public:
    using Callback = std::function<void(AlarmInfo)>;

    AlarmRelayServer(std::string socket_path, Callback callback);
    ~AlarmRelayServer();

    AlarmRelayServer(const AlarmRelayServer&) = delete;
    AlarmRelayServer& operator=(const AlarmRelayServer&) = delete;

    bool start();
    void stop();

private:
    void receiveLoop();

    std::string socket_path_;
    Callback callback_;
    std::atomic<bool> stop_{false};
    int socket_fd_ = -1;
    std::thread thread_;
};

}  // namespace pipeline::agent
