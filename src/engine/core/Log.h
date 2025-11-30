/**
 * Log.h - Logging utilities stub
 */
#pragma once

#include <cstdio>
#include <string>

namespace Sanic {

// Simple logging stub - replace with actual implementation
#define SANIC_LOG_INFO(fmt, ...) printf("[INFO] " fmt "\n", ##__VA_ARGS__)
#define SANIC_LOG_WARN(fmt, ...) printf("[WARN] " fmt "\n", ##__VA_ARGS__)
#define SANIC_LOG_ERROR(fmt, ...) printf("[ERROR] " fmt "\n", ##__VA_ARGS__)
#define SANIC_LOG_DEBUG(fmt, ...) printf("[DEBUG] " fmt "\n", ##__VA_ARGS__)

// LOG_* macros with format string support (printf-style)
#define LOG_INFO(fmt, ...) printf("[INFO] " fmt "\n", ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) printf("[WARN] " fmt "\n", ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) printf("[ERROR] " fmt "\n", ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) printf("[DEBUG] " fmt "\n", ##__VA_ARGS__)
#define LOG_TRACE(fmt, ...) printf("[TRACE] " fmt "\n", ##__VA_ARGS__)

inline void LogInfo(const std::string& msg) { printf("[INFO] %s\n", msg.c_str()); }
inline void LogWarn(const std::string& msg) { printf("[WARN] %s\n", msg.c_str()); }
inline void LogError(const std::string& msg) { printf("[ERROR] %s\n", msg.c_str()); }
inline void LogDebug(const std::string& msg) { printf("[DEBUG] %s\n", msg.c_str()); }

} // namespace Sanic
