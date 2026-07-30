#pragma once

#include <iostream>
#include <mutex>
#include <sstream>
#include <utility>

namespace pipeline::agent {

template <typename... Args>
void logMessage(const char *level, const char *message, Args &&...args) {
    static std::mutex mutex;
    std::ostringstream line;
    line << message;
    ((line << ' ' << std::forward<Args>(args)), ...);
    std::lock_guard<std::mutex> lock(mutex);
    std::clog << '[' << level << "] " << line.str() << '\n';
}

}  // namespace pipeline::agent

#define PIPELINE_LOG_TRACE(...) \
    ::pipeline::agent::logMessage("TRACE", __VA_ARGS__)
#define PIPELINE_LOG_DEBUG(...) \
    ::pipeline::agent::logMessage("DEBUG", __VA_ARGS__)
#define PIPELINE_LOG_INFO(...) \
    ::pipeline::agent::logMessage("INFO", __VA_ARGS__)
#define PIPELINE_LOG_WARN(...) \
    ::pipeline::agent::logMessage("WARN", __VA_ARGS__)
#define PIPELINE_LOG_ERROR(...) \
    ::pipeline::agent::logMessage("ERROR", __VA_ARGS__)

#define LOG_TRACE(...) PIPELINE_LOG_TRACE(__VA_ARGS__)
#define LOG_DEBUG(...) PIPELINE_LOG_DEBUG(__VA_ARGS__)
#define LOG_INFO(...) PIPELINE_LOG_INFO(__VA_ARGS__)
#define LOG_WARN(...) PIPELINE_LOG_WARN(__VA_ARGS__)
#define LOG_ERROR(...) PIPELINE_LOG_ERROR(__VA_ARGS__)
