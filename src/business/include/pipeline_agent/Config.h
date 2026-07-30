#pragma once

#include <cstdint>
#include <string>

namespace pipeline::agent {

struct SocketConfig {
    std::string socket_path = "/tmp/pipeline_agent.sock";
    int send_queue_size = 100;
    int heartbeat_interval_ms = 10000;
    std::string agent_id;
};

constexpr uint32_t kMagic = 0xDEADBEEF;

}  // namespace pipeline::agent
