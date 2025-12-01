/**
 * Viewport.h
 * 
 * 3D viewport panel for the editor.
 * 
 * Features:
 * - Scene rendering
 * - Camera controls (orbit, FPS, pan)
 * - Transform gizmos
 * - Entity picking
 * - Box selection
 * - Grid rendering
 */

#pragma once

#include "../EditorWindow.h"
#include "ViewportCamera.h"
#include "Gizmo.h"
#include "Grid.h"
#include "../../engine/VulkanContext.h"
#include "../../engine/core/ViewMode.h"
#include <imgui.h>
#include <memory>

namespace Sanic::Editor {

enum class ViewportTool {
    Select,
    Translate,
    Rotate,
    Scale,
    Universal
};

class Viewport : public EditorWindow {
public:
    Viewport();
    ~Viewport() override;
    
    // EditorWindow interface
    void initialize(Editor* editor) override;
    void shutdown() override;
    void update(float deltaTime) override;
    void draw() override;
    const char* getName() const override { return "Viewport"; }
    
    // Camera access
    ViewportCamera& getCamera() { return camera_; }
    const ViewportCamera& getCamera() const { return camera_; }
    
    // Tool settings
    void setTool(ViewportTool tool);
    ViewportTool getTool() const { return currentTool_; }
    
    void setGizmoSpace(GizmoSpace space) { gizmo_.setSpace(space); }
    GizmoSpace getGizmoSpace() const { return gizmo_.getSpace(); }
    
    // Snapping
    void setSnapEnabled(bool enabled) { gizmo_.setSnapEnabled(enabled); }
    bool isSnapEnabled() const { return gizmo_.isSnapEnabled(); }
    
    void setSnapTranslation(float snap) { gizmo_.setSnapTranslation(snap); }
    void setSnapRotation(float snap) { gizmo_.setSnapRotation(snap); }
    void setSnapScale(float snap) { gizmo_.setSnapScale(snap); }
    
    // Focus on selection
    void focusOnSelection();
    
    // Grid
    void setGridVisible(bool visible) { grid_.setVisible(visible); }
    bool isGridVisible() const { return grid_.isVisible(); }
    
    // View Mode
    void setViewMode(Sanic::EViewMode mode);
    Sanic::EViewMode getViewMode() const { return viewMode_; }
    const Sanic::ShowFlags& getShowFlags() const { return showFlags_; }
    Sanic::ShowFlags& getShowFlags() { return showFlags_; }
    
    // 3D cursor
    void set3DCursor(const glm::vec3& position) { cursor3D_ = position; }
    glm::vec3 get3DCursor() const { return cursor3D_; }
    void show3DCursor(bool show) { showCursor3D_ = show; }
    
    // Viewport info
    glm::vec2 getViewportPos() const { return viewportPos_; }
    glm::vec2 getViewportSize() const { return viewportSize_; }
    
    // Render target texture (set by EditorRenderer)
    void setViewportTexture(VkDescriptorSet texture) { viewportTexture_ = texture; }
    VkDescriptorSet getViewportTexture() const { return viewportTexture_; }
    
    // Current viewport dimensions (for render target sizing)
    uint32_t getViewportWidth() const { return viewportWidth_; }
    uint32_t getViewportHeight() const { return viewportHeight_; }
    
private:
    void handleInput(float deltaTime);
    void handleMousePicking();
    void handleBoxSelection();
    void handleGizmoInteraction();
    void handleKeyboardShortcuts();
    
    void drawToolbar();
    void drawViewModeMenu();
    void drawViewportOverlay();
    void drawViewCube();
    void drawGizmoControls();
    void drawCameraInfo();
    void drawSelectionOutlines();
    
    // Picking
    Entity pickEntityAtMouse();
    
    Editor* editor_ = nullptr;
    
    // Viewport state
    glm::vec2 viewportPos_ = glm::vec2(0);
    glm::vec2 viewportSize_ = glm::vec2(800, 600);
    bool isFocused_ = false;
    bool isHovered_ = false;
    
    // Camera
    ViewportCamera camera_;
    
    // Tool state
    ViewportTool currentTool_ = ViewportTool::Translate;
    
    // Gizmo
    Gizmo gizmo_;
    bool gizmoWasUsing_ = false;
    Transform transformBeforeGizmo_;
    
    // Grid
    Grid grid_;
    
    // View Mode
    Sanic::EViewMode viewMode_ = Sanic::EViewMode::Lit;
    Sanic::ShowFlags showFlags_;
    
    // Box selection
    bool boxSelecting_ = false;
    glm::vec2 boxSelectStart_ = glm::vec2(0);
    glm::vec2 boxSelectEnd_ = glm::vec2(0);
    
    // 3D cursor
    glm::vec3 cursor3D_ = glm::vec3(0);
    bool showCursor3D_ = false;
    
    // Mouse state
    glm::vec2 lastMousePos_ = glm::vec2(0);
    bool mouseInViewport_ = false;
    
    // Render target (for offscreen rendering)
    VkDescriptorSet viewportTexture_ = VK_NULL_HANDLE;
    uint32_t viewportWidth_ = 0;
    uint32_t viewportHeight_ = 0;
};

} // namespace Sanic::Editor
