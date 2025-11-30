/**
 * ConsolePanel.cpp
 * 
 * Console/log output panel implementation.
 */

#include "ConsolePanel.h"
#include "../Editor.h"
#include <imgui.h>
#include <chrono>

namespace Sanic::Editor {

ConsolePanel* ConsolePanel::instance_ = nullptr;

void ConsolePanel::initialize(Editor* editor) {
    editor_ = editor;
    instance_ = this;
}

void ConsolePanel::update(float deltaTime) {
    // Nothing to update
}

void ConsolePanel::draw() {
    drawToolbar();
    
    ImGui::Separator();
    
    // Log area
    ImGui::BeginChild("LogArea", ImVec2(0, 0), false);
    
    std::lock_guard<std::mutex> lock(messagesMutex_);
    
    for (int i = 0; i < static_cast<int>(messages_.size()); ++i) {
        const auto& msg = messages_[i];
        
        // Apply level filter
        bool show = true;
        switch (msg.level) {
            case LogLevel::Trace:   show = showTrace_; break;
            case LogLevel::Debug:   show = showDebug_; break;
            case LogLevel::Info:    show = showInfo_; break;
            case LogLevel::Warning: show = showWarning_; break;
            case LogLevel::Error:   show = showError_; break;
        }
        
        if (!show) continue;
        
        // Apply text filter
        if (filterBuffer_[0] != '\0') {
            if (msg.message.find(filterBuffer_) == std::string::npos &&
                msg.category.find(filterBuffer_) == std::string::npos) {
                continue;
            }
        }
        
        drawLogEntry(msg, i);
    }
    
    // Auto scroll
    if (autoScroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }
    
    ImGui::EndChild();
}

void ConsolePanel::drawToolbar() {
    // Clear button
    if (ImGui::Button("Clear")) {
        clear();
    }
    
    ImGui::SameLine();
    
    // Collapse toggle
    ImGui::Checkbox("Collapse", &collapse_);
    
    ImGui::SameLine();
    
    // Auto-scroll toggle
    ImGui::Checkbox("Auto-scroll", &autoScroll_);
    
    ImGui::SameLine();
    ImGui::Separator();
    ImGui::SameLine();
    
    // Level filters with colored buttons
    ImGui::PushStyleColor(ImGuiCol_Text, getLevelColor(LogLevel::Trace));
    ImGui::Checkbox("Trace", &showTrace_);
    ImGui::PopStyleColor();
    
    ImGui::SameLine();
    
    ImGui::PushStyleColor(ImGuiCol_Text, getLevelColor(LogLevel::Debug));
    ImGui::Checkbox("Debug", &showDebug_);
    ImGui::PopStyleColor();
    
    ImGui::SameLine();
    
    ImGui::PushStyleColor(ImGuiCol_Text, getLevelColor(LogLevel::Info));
    ImGui::Checkbox("Info", &showInfo_);
    ImGui::PopStyleColor();
    
    ImGui::SameLine();
    
    ImGui::PushStyleColor(ImGuiCol_Text, getLevelColor(LogLevel::Warning));
    ImGui::Checkbox("Warning", &showWarning_);
    ImGui::PopStyleColor();
    
    ImGui::SameLine();
    
    ImGui::PushStyleColor(ImGuiCol_Text, getLevelColor(LogLevel::Error));
    ImGui::Checkbox("Error", &showError_);
    ImGui::PopStyleColor();
    
    ImGui::SameLine();
    ImGui::Separator();
    ImGui::SameLine();
    
    // Search filter
    ImGui::PushItemWidth(200.0f);
    ImGui::InputTextWithHint("##Filter", "Filter...", filterBuffer_, sizeof(filterBuffer_));
    ImGui::PopItemWidth();
    
    // Message count
    ImGui::SameLine();
    ImGui::Text("| %zu messages", messages_.size());
}

void ConsolePanel::drawLogEntry(const LogMessage& msg, int index) {
    ImGui::PushID(index);
    
    ImVec4 color = getLevelColor(msg.level);
    
    // Background for selected
    bool isSelected = (index == selectedMessage_);
    if (isSelected) {
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.3f, 0.3f, 0.5f, 1.0f));
    }
    
    // Selectable row
    char label[32];
    snprintf(label, sizeof(label), "##log%d", index);
    if (ImGui::Selectable(label, isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
        selectedMessage_ = index;
    }
    
    if (isSelected) {
        ImGui::PopStyleColor();
    }
    
    ImGui::SameLine();
    
    // Level icon
    ImGui::TextColored(color, "%s", getLevelIcon(msg.level));
    
    ImGui::SameLine();
    
    // Timestamp
    ImGui::TextDisabled("[%.2f]", msg.timestamp);
    
    ImGui::SameLine();
    
    // Category
    if (!msg.category.empty()) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.8f, 1.0f), "[%s]", msg.category.c_str());
        ImGui::SameLine();
    }
    
    // Message
    ImGui::TextColored(color, "%s", msg.message.c_str());
    
    // Count badge for collapsed duplicates
    if (collapse_ && msg.count > 1) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%d)", msg.count);
    }
    
    // Context menu
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Copy Message")) {
            ImGui::SetClipboardText(msg.message.c_str());
        }
        if (ImGui::MenuItem("Copy All")) {
            std::string allText;
            for (const auto& m : messages_) {
                allText += m.message + "\n";
            }
            ImGui::SetClipboardText(allText.c_str());
        }
        ImGui::EndPopup();
    }
    
    ImGui::PopID();
}

ImVec4 ConsolePanel::getLevelColor(LogLevel level) {
    switch (level) {
        case LogLevel::Trace:   return ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
        case LogLevel::Debug:   return ImVec4(0.6f, 0.8f, 1.0f, 1.0f);
        case LogLevel::Info:    return ImVec4(0.9f, 0.9f, 0.9f, 1.0f);
        case LogLevel::Warning: return ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
        case LogLevel::Error:   return ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
        default:                return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    }
}

const char* ConsolePanel::getLevelIcon(LogLevel level) {
    switch (level) {
        case LogLevel::Trace:   return "[T]";
        case LogLevel::Debug:   return "[D]";
        case LogLevel::Info:    return "[I]";
        case LogLevel::Warning: return "[W]";
        case LogLevel::Error:   return "[E]";
        default:                return "[?]";
    }
}

void ConsolePanel::clear() {
    std::lock_guard<std::mutex> lock(messagesMutex_);
    messages_.clear();
    selectedMessage_ = -1;
}

void ConsolePanel::log(LogLevel level, const std::string& message, const std::string& category) {
    if (!instance_) return;
    
    static auto startTime = std::chrono::high_resolution_clock::now();
    auto now = std::chrono::high_resolution_clock::now();
    float timestamp = std::chrono::duration<float>(now - startTime).count();
    
    std::lock_guard<std::mutex> lock(instance_->messagesMutex_);
    
    // Collapse duplicates if enabled
    if (instance_->collapse_ && !instance_->messages_.empty()) {
        auto& last = instance_->messages_.back();
        if (last.level == level && last.message == message && last.category == category) {
            last.count++;
            last.timestamp = timestamp;  // Update timestamp
            return;
        }
    }
    
    instance_->messages_.push_back({
        level,
        message,
        category,
        timestamp,
        1
    });
    
    // Limit message count
    const size_t maxMessages = 10000;
    if (instance_->messages_.size() > maxMessages) {
        instance_->messages_.erase(
            instance_->messages_.begin(),
            instance_->messages_.begin() + (instance_->messages_.size() - maxMessages)
        );
    }
}

void ConsolePanel::logTrace(const std::string& message, const std::string& category) {
    log(LogLevel::Trace, message, category);
}

void ConsolePanel::logDebug(const std::string& message, const std::string& category) {
    log(LogLevel::Debug, message, category);
}

void ConsolePanel::logInfo(const std::string& message, const std::string& category) {
    log(LogLevel::Info, message, category);
}

void ConsolePanel::logWarning(const std::string& message, const std::string& category) {
    log(LogLevel::Warning, message, category);
}

void ConsolePanel::logError(const std::string& message, const std::string& category) {
    log(LogLevel::Error, message, category);
}

} // namespace Sanic::Editor
