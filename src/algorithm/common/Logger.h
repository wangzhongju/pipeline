#pragma once

#include "common/Config.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>

namespace algorithm::cdky {

class Logger {
   public:
    static void init(
        const LogConfig &config,
        const std::string & = "pipeline.algorithm") {
        std::string level = config.level;
        std::transform(level.begin(), level.end(), level.begin(),
                       [](unsigned char value) {
                           return static_cast<char>(std::tolower(value));
                       });
        threshold().store(parseLevel(level), std::memory_order_relaxed);
    }

    static bool enabled(const char *level) {
        return parseLevel(level) >=
            threshold().load(std::memory_order_relaxed);
    }

   private:
    static int parseLevel(const std::string &level) {
        if (level == "trace" || level == "TRACE") return 0;
        if (level == "debug" || level == "DEBUG") return 1;
        if (level == "warn" || level == "WARN") return 3;
        if (level == "error" || level == "ERROR") return 4;
        if (level == "critical" || level == "CRITICAL") return 5;
        if (level == "off" || level == "OFF") return 6;
        return 2;
    }

    static std::atomic<int> &threshold() {
        static std::atomic<int> value{2};
        return value;
    }
};

template <typename... Args>
void logMessage(const char *level, const char *message, Args &&...args) {
    if (!Logger::enabled(level)) {
        return;
    }
    static std::mutex mutex;
    std::ostringstream line;
    line << message;
    ((line << ' ' << std::forward<Args>(args)), ...);
    std::lock_guard<std::mutex> lock(mutex);
    std::clog << '[' << level << "] " << line.str() << '\n';
}

}  // namespace algorithm::cdky

#define LOG_TRACE(...) ::algorithm::cdky::logMessage("TRACE", __VA_ARGS__)
#define LOG_DEBUG(...) ::algorithm::cdky::logMessage("DEBUG", __VA_ARGS__)
#define LOG_INFO(...) ::algorithm::cdky::logMessage("INFO", __VA_ARGS__)
#define LOG_WARN(...) ::algorithm::cdky::logMessage("WARN", __VA_ARGS__)
#define LOG_ERROR(...) ::algorithm::cdky::logMessage("ERROR", __VA_ARGS__)
#define LOG_CRITICAL(...) ::algorithm::cdky::logMessage("CRITICAL", __VA_ARGS__)
