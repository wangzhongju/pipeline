#pragma once

#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>

namespace pipeline::agent {

inline int64_t steadyNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

inline int64_t systemNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

inline std::string makeTimestamp(const char* format, bool with_milliseconds = false) {
    const auto now = std::chrono::system_clock::now();
    const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
    localtime_r(&now_time, &tm_buf);

    char time_buf[32] = {0};
    std::strftime(time_buf, sizeof(time_buf), format, &tm_buf);
    if (!with_milliseconds) {
        return std::string(time_buf);
    }

    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    const int64_t millisecond_part = now_ms % 1000;
    char buf[40] = {0};
    std::snprintf(buf,
                  sizeof(buf),
                  "%s_%03lld",
                  time_buf,
                  static_cast<long long>(millisecond_part >= 0 ? millisecond_part : millisecond_part + 1000));
    return std::string(buf);
}

inline std::string makeOsdTimeText() {
    return makeTimestamp("%Y-%m-%d %H:%M:%S");
}

inline std::string makeFileTimeText() {
    return makeTimestamp("%Y%m%d_%H%M%S", true);
}

}  // namespace pipeline::agent
