/**
 * EditorWindow.h
 * 
 * Base class for all editor panels/windows.
 */

#pragma once

#include <string>
#include <imgui.h>

namespace Sanic::Editor {

// Forward declaration
class Editor;
enum class EditorMode;

class EditorWindow {
public:
    virtual ~EditorWindow() = default;
    
    // Lifecycle
    virtual void initialize(Editor* editor) { editor_ = editor; }
    virtual void shutdown() {}
    
    // Per-frame
    virtual void update(float deltaTime) {}
    virtual void draw() = 0;
    
    // Identity
    virtual const char* getName() const = 0;
    virtual const char* getIcon() const { return nullptr; }
    
    // Visibility
    bool isVisible() const { return visible_; }
    void setVisible(bool visible) { visible_ = visible; }
    
    // Focus
    bool isFocused() const { return focused_; }
    bool isHovered() const { return hovered_; }
    
    // Mode changes
    virtual void onModeChanged(EditorMode oldMode, EditorMode newMode) {}
    
    // Selection changes
    virtual void onSelectionChanged() {}
    
protected:
    // Helper to begin a standard panel window
    bool beginPanel(ImGuiWindowFlags extraFlags = 0) {
        ImGui::SetNextWindowSize(ImVec2(300, 400), ImGuiCond_FirstUseEver);
        
        bool open = visible_;
        bool result = ImGui::Begin(getName(), &open, extraFlags);
        
        visible_ = open;
        focused_ = ImGui::IsWindowFocused();
        hovered_ = ImGui::IsWindowHovered();
        
        return result;
    }
    
    void endPanel() {
        ImGui::End();
    }
    
    Editor* editor_ = nullptr;
    bool visible_ = true;
    bool focused_ = false;
    bool hovered_ = false;
};

} // namespace Sanic::Editor
