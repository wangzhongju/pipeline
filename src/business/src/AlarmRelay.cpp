#include "pipeline_agent/AlarmRelay.h"

#include "pipeline_agent/Logger.h"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>

namespace pipeline::agent {
namespace {

bool makeAddress(const std::string& path, sockaddr_un& address) {
    if (path.empty() || path.size() >= sizeof(address.sun_path)) {
        return false;
    }
    std::memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    return true;
}

}  // namespace

AlarmRelayClient::AlarmRelayClient(std::string socket_path)
    : socket_path_(std::move(socket_path)) {}

bool AlarmRelayClient::send(const AlarmInfo& alarm) const {
    sockaddr_un address{};
    if (!makeAddress(socket_path_, address)) {
        return false;
    }

    std::string payload;
    if (!alarm.SerializeToString(&payload)) {
        return false;
    }

    const int fd = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return false;
    }
    const ssize_t sent = ::sendto(fd, payload.data(), payload.size(), MSG_NOSIGNAL,
                                  reinterpret_cast<const sockaddr*>(&address),
                                  sizeof(address));
    ::close(fd);
    return sent == static_cast<ssize_t>(payload.size());
}

AlarmRelayServer::AlarmRelayServer(std::string socket_path, Callback callback)
    : socket_path_(std::move(socket_path)), callback_(std::move(callback)) {}

AlarmRelayServer::~AlarmRelayServer() {
    stop();
}

bool AlarmRelayServer::start() {
    if (thread_.joinable()) {
        return true;
    }

    sockaddr_un address{};
    if (!makeAddress(socket_path_, address)) {
        LOG_ERROR("[AlarmRelay] invalid socket path={}", socket_path_);
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::path(socket_path_).parent_path(), ec);
    ::unlink(socket_path_.c_str());

    socket_fd_ = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (socket_fd_ < 0 ||
        ::bind(socket_fd_, reinterpret_cast<const sockaddr*>(&address),
               sizeof(address)) != 0) {
        LOG_ERROR("[AlarmRelay] bind failed path={} error={}",
                  socket_path_, std::strerror(errno));
        if (socket_fd_ >= 0) {
            ::close(socket_fd_);
            socket_fd_ = -1;
        }
        return false;
    }

    stop_ = false;
    thread_ = std::thread(&AlarmRelayServer::receiveLoop, this);
    return true;
}

void AlarmRelayServer::stop() {
    stop_ = true;
    if (socket_fd_ >= 0) {
        ::shutdown(socket_fd_, SHUT_RDWR);
        ::close(socket_fd_);
        socket_fd_ = -1;
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    ::unlink(socket_path_.c_str());
}

void AlarmRelayServer::receiveLoop() {
    std::vector<char> buffer(1024 * 1024);
    while (!stop_) {
        const ssize_t size = ::recv(socket_fd_, buffer.data(), buffer.size(), 0);
        if (size < 0) {
            if (!stop_ && errno != EINTR) {
                LOG_WARN("[AlarmRelay] receive failed error={}", std::strerror(errno));
            }
            continue;
        }

        AlarmInfo alarm;
        if (!alarm.ParseFromArray(buffer.data(), static_cast<int>(size))) {
            LOG_WARN("[AlarmRelay] discarded invalid alarm payload bytes={}", size);
            continue;
        }
        if (callback_) {
            callback_(std::move(alarm));
        }
    }
}

}  // namespace pipeline::agent
