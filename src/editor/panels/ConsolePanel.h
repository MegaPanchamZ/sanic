/**
 * ConsolePanel.h
 * 
 * Console/log output panel.
 */

#pragma once

#include "../EditorWindow.h"
#include <vector>
#include <string>
#include <mutex>

namespace Sanic::Editor {

enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warning,
    Error
};

struct LogMessage {
    LogLevel level;
    std::string message;
    std::string category;
    float timestamp;
    int count = 1;  // For collapsed duplicates
};

class ConsolePanel : public EditorWindow {
public:
    void initialize(Editor* editor) override;
    void update(float deltaTime) override;
    void draw() override;
    const char* getName() const override { return "Console"; }
    
    // Logging
    static void log(LogLevel level, const std::string& message, const std::string& category = "");
    static void logTrace(const std::string& message, const std::string& category = "");
    static void logDebug(const std::string& message, const std::string& category = "");
    static void logInfo(const std::string& message, const std::string& category = "");
    static void logWarning(const std::string& message, const std::string& category = "");
    static void logError(const std::string& message, const std::string& category = "");
    
    // Clear log
    void clear();
    
private:
    void drawToolbar();
    void drawLogEntry(const LogMessage& msg, int index);
    ImVec4 getLevelColor(LogLevel level);
    const char* getLevelIcon(LogLevel level);
    
    static ConsolePanel* instance_;
    
    Editor* editor_ = nullptr;
    
    std::vector<LogMessage> messages_;
    std::mutex messagesMutex_;
    
    // Filters
    bool showTrace_ = false;
    bool showDebug_ = true;
    bool showInfo_ = true;
    bool showWarning_ = true;
    bool showError_ = true;
    
    char filterBuffer_[256] = "";
    bool autoScroll_ = true;
    bool collapse_ = true;
    
    // Selection
    int selectedMessage_ = -1;
};

// Convenience macros
#define SANIC_LOG_TRACE(msg) Sanic::Editor::ConsolePanel::logTrace(msg)
#define SANIC_LOG_DEBUG(msg) Sanic::Editor::ConsolePanel::logDebug(msg)
#define SANIC_LOG_INFO(msg) Sanic::Editor::ConsolePanel::logInfo(msg)
#define SANIC_LOG_WARN(msg) Sanic::Editor::ConsolePanel::logWarning(msg)
#define SANIC_LOG_ERROR(msg) Sanic::Editor::ConsolePanel::logError(msg)

} // namespace Sanic::Editor
