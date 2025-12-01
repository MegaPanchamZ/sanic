/**
 * Viewport.cpp
 * 
 * Implementation of the 3D viewport panel.
 */

#include "Viewport.h"
#include "../Editor.h"
#include "../core/Selection.h"
#include "../core/UndoSystem.h"
#include <ImGuizmo.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace Sanic::Editor {

Viewport::Viewport() {
    // Default camera position
    camera_.lookAt(glm::vec3(5, 5, 5), glm::vec3(0, 0, 0));
}

Viewport::~Viewport() {
}

void Viewport::initialize(Editor* editor) {
    EditorWindow::initialize(editor);
    editor_ = editor;
    
    // Setup grid
    grid_.setSize(100.0f);
    grid_.setSpacing(1.0f);
    grid_.setMajorLineInterval(10);
    
    // Setup gizmo defaults from config
    const auto& config = editor_->getConfig();
    gizmo_.setSnapTranslation(config.snapTranslate);
    gizmo_.setSnapRotation(config.snapRotate);
    gizmo_.setSnapScale(config.snapScale);
    gizmo_.setSnapEnabled(config.snapToGrid);
}

void Viewport::shutdown() {
}

void Viewport::update(float deltaTime) {
    if (isFocused_) {
        camera_.update(deltaTime);
    }
}

void Viewport::draw() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    
    // Make the viewport window completely transparent so the 3D scene shows through
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground;
    
    if (beginPanel(flags)) {
        // Get viewport dimensions
        ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
        viewportSize_ = glm::vec2(viewportPanelSize.x, viewportPanelSize.y);
        
        ImVec2 viewportPanelPos = ImGui::GetCursorScreenPos();
        viewportPos_ = glm::vec2(viewportPanelPos.x, viewportPanelPos.y);
        
        // Update camera aspect ratio
        if (viewportSize_.y > 0) {
            camera_.setAspectRatio(viewportSize_.x / viewportSize_.y);
        }
        
        // Check focus and hover
        isFocused_ = ImGui::IsWindowFocused();
        isHovered_ = ImGui::IsWindowHovered();
        
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        
        // The 3D scene is rendered underneath - we just draw UI overlays here
        // Draw a subtle border to show the viewport bounds when focused
        if (isFocused_) {
            drawList->AddRect(
                ImVec2(viewportPos_.x, viewportPos_.y),
                ImVec2(viewportPos_.x + viewportSize_.x, viewportPos_.y + viewportSize_.y),
                IM_COL32(100, 150, 255, 100),
                0.0f, 0, 1.0f
            );
        }
        
        // Draw selection outlines
        drawSelectionOutlines();
        
        // Handle input
        handleInput(ImGui::GetIO().DeltaTime);
        
        // Handle gizmo
        handleGizmoInteraction();
        
        // Draw box selection
        if (boxSelecting_) {
            ImVec2 min(
                std::min(boxSelectStart_.x, boxSelectEnd_.x),
                std::min(boxSelectStart_.y, boxSelectEnd_.y)
            );
            ImVec2 max(
                std::max(boxSelectStart_.x, boxSelectEnd_.x),
                std::max(boxSelectStart_.y, boxSelectEnd_.y)
            );
            
            drawList->AddRectFilled(min, max, IM_COL32(100, 150, 255, 50));
            drawList->AddRect(min, max, IM_COL32(100, 150, 255, 255));
        }
        
        // Draw viewport overlay (tool info, camera info)
        drawViewportOverlay();
        
        // Draw toolbar
        drawToolbar();
    }
    endPanel();
    
    ImGui::PopStyleColor(2);  // WindowBg, ChildBg
    ImGui::PopStyleVar();
}

void Viewport::handleInput(float deltaTime) {
    if (!isHovered_ && !isFocused_) return;
    
    ImGuiIO& io = ImGui::GetIO();
    
    glm::vec2 mousePos(io.MousePos.x, io.MousePos.y);
    glm::vec2 mouseDelta = mousePos - lastMousePos_;
    lastMousePos_ = mousePos;
    
    // Check if mouse is in viewport
    mouseInViewport_ = mousePos.x >= viewportPos_.x && 
                       mousePos.x <= viewportPos_.x + viewportSize_.x &&
                       mousePos.y >= viewportPos_.y && 
                       mousePos.y <= viewportPos_.y + viewportSize_.y;
    
    if (!mouseInViewport_) return;
    
    // Don't process input if gizmo is being used
    if (gizmo_.isUsing()) return;
    
    // Camera controls
    bool leftMouse = io.MouseDown[ImGuiMouseButton_Left];
    bool middleMouse = io.MouseDown[ImGuiMouseButton_Middle];
    bool rightMouse = io.MouseDown[ImGuiMouseButton_Right];
    bool altKey = io.KeyAlt;
    bool shiftKey = io.KeyShift;
    bool ctrlKey = io.KeyCtrl;
    
    // Only handle camera if not interacting with gizmo
    if (!gizmo_.isOver()) {
        camera_.onMouseMove(mouseDelta.x, mouseDelta.y, leftMouse, middleMouse, rightMouse, altKey, shiftKey);
    }
    
    // Mouse scroll for zoom
    if (std::abs(io.MouseWheel) > 0.01f) {
        camera_.onMouseScroll(io.MouseWheel);
    }
    
    // Keyboard shortcuts for viewport
    handleKeyboardShortcuts();
    
    // Handle picking and box selection
    if (leftMouse && !altKey && !gizmo_.isOver()) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            // Start box selection or single pick
            boxSelectStart_ = mousePos;
            boxSelectEnd_ = mousePos;
        }
        
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 5.0f)) {
            // Box selection
            boxSelecting_ = true;
            boxSelectEnd_ = mousePos;
        }
    }
    
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (boxSelecting_) {
            handleBoxSelection();
            boxSelecting_ = false;
        } else if (!gizmo_.isOver() && !altKey) {
            handleMousePicking();
        }
    }
    
    // FPS camera keys
    if (isFocused_ && rightMouse) {
        for (int key = GLFW_KEY_A; key <= GLFW_KEY_Z; ++key) {
            if (ImGui::IsKeyPressed((ImGuiKey)key)) camera_.onKeyDown(key);
            if (ImGui::IsKeyReleased((ImGuiKey)key)) camera_.onKeyUp(key);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_LeftShift)) camera_.onKeyDown(GLFW_KEY_LEFT_SHIFT);
        if (ImGui::IsKeyReleased(ImGuiKey_LeftShift)) camera_.onKeyUp(GLFW_KEY_LEFT_SHIFT);
    }
}

void Viewport::handleMousePicking() {
    if (!editor_) return;
    
    Entity picked = pickEntityAtMouse();
    
    ImGuiIO& io = ImGui::GetIO();
    bool additive = io.KeyCtrl || io.KeyShift;
    
    auto& selection = editor_->getSelection();
    
    if (picked != INVALID_ENTITY) {
        if (additive) {
            selection.toggleSelection(picked);
        } else {
            selection.select(picked);
        }
    } else if (!additive) {
        selection.clearSelection();
    }
}

void Viewport::handleBoxSelection() {
    if (!editor_ || !editor_->getWorld()) return;
    
    ImGuiIO& io = ImGui::GetIO();
    bool additive = io.KeyCtrl || io.KeyShift;
    
    glm::vec2 min(
        std::min(boxSelectStart_.x, boxSelectEnd_.x) - viewportPos_.x,
        std::min(boxSelectStart_.y, boxSelectEnd_.y) - viewportPos_.y
    );
    glm::vec2 max(
        std::max(boxSelectStart_.x, boxSelectEnd_.x) - viewportPos_.x,
        std::max(boxSelectStart_.y, boxSelectEnd_.y) - viewportPos_.y
    );
    
    editor_->getSelection().selectInRect(
        *editor_->getWorld(),
        min, max,
        camera_.getViewProjectionMatrix(),
        viewportSize_,
        additive
    );
}

void Viewport::handleGizmoInteraction() {
    if (!editor_ || !editor_->getWorld()) return;
    
    auto& selection = editor_->getSelection();
    if (!selection.hasSelection()) return;
    
    Entity focused = selection.getFocused();
    if (focused == INVALID_ENTITY) return;
    
    Sanic::World* world = editor_->getWorld();
    if (!world->hasComponent<Transform>(focused)) return;
    
    Transform& transform = world->getComponent<Transform>(focused);
    
    // Store transform before gizmo manipulation starts
    if (gizmo_.isUsing() && !gizmoWasUsing_) {
        transformBeforeGizmo_ = transform;
    }
    
    // Manipulate gizmo
    GizmoResult result = gizmo_.manipulate(
        camera_.getViewMatrix(),
        camera_.getProjectionMatrix(),
        transform,
        viewportPos_,
        viewportSize_
    );
    
    // Record undo when gizmo manipulation ends
    if (gizmoWasUsing_ && !gizmo_.isUsing()) {
        if (transformBeforeGizmo_.position != transform.position ||
            transformBeforeGizmo_.rotation != transform.rotation ||
            transformBeforeGizmo_.scale != transform.scale) {
            editor_->getUndoSystem().record(
                std::make_unique<TransformAction>(world, focused, transformBeforeGizmo_, transform)
            );
        }
    }
    
    gizmoWasUsing_ = gizmo_.isUsing();
}

void Viewport::handleKeyboardShortcuts() {
    if (!isFocused_) return;
    
    // Tool shortcuts
    if (ImGui::IsKeyPressed(ImGuiKey_Q)) setTool(ViewportTool::Select);
    if (ImGui::IsKeyPressed(ImGuiKey_W)) setTool(ViewportTool::Translate);
    if (ImGui::IsKeyPressed(ImGuiKey_E)) setTool(ViewportTool::Rotate);
    if (ImGui::IsKeyPressed(ImGuiKey_R)) setTool(ViewportTool::Scale);
    
    // Space toggles between world/local
    if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
        setGizmoSpace(getGizmoSpace() == GizmoSpace::World ? GizmoSpace::Local : GizmoSpace::World);
    }
    
    // F to focus on selection
    if (ImGui::IsKeyPressed(ImGuiKey_F)) {
        focusOnSelection();
    }
    
    // Numpad for views
    if (ImGui::IsKeyPressed(ImGuiKey_Keypad1)) camera_.snapToFront();
    if (ImGui::IsKeyPressed(ImGuiKey_Keypad3)) camera_.snapToRight();
    if (ImGui::IsKeyPressed(ImGuiKey_Keypad7)) camera_.snapToTop();
    if (ImGui::IsKeyPressed(ImGuiKey_Keypad5)) camera_.togglePerspective();
}

void Viewport::setTool(ViewportTool tool) {
    currentTool_ = tool;
    
    switch (tool) {
        case ViewportTool::Select:
            gizmo_.setEnabled(false);
            break;
        case ViewportTool::Translate:
            gizmo_.setEnabled(true);
            gizmo_.setType(GizmoType::Translate);
            break;
        case ViewportTool::Rotate:
            gizmo_.setEnabled(true);
            gizmo_.setType(GizmoType::Rotate);
            break;
        case ViewportTool::Scale:
            gizmo_.setEnabled(true);
            gizmo_.setType(GizmoType::Scale);
            break;
        case ViewportTool::Universal:
            gizmo_.setEnabled(true);
            gizmo_.setType(GizmoType::Universal);
            break;
    }
}

void Viewport::focusOnSelection() {
    if (!editor_ || !editor_->getWorld()) return;
    
    auto& selection = editor_->getSelection();
    if (!selection.hasSelection()) return;
    
    glm::vec3 center = selection.getSelectionCenter(*editor_->getWorld());
    glm::vec3 min = selection.getSelectionBoundsMin(*editor_->getWorld());
    glm::vec3 max = selection.getSelectionBoundsMax(*editor_->getWorld());
    
    float radius = glm::length(max - min) * 0.5f;
    camera_.focusOn(center, std::max(radius, 1.0f));
}

Entity Viewport::pickEntityAtMouse() {
    if (!editor_ || !editor_->getWorld()) return INVALID_ENTITY;
    
    ImGuiIO& io = ImGui::GetIO();
    glm::vec2 mousePos(io.MousePos.x - viewportPos_.x, io.MousePos.y - viewportPos_.y);
    
    auto ray = camera_.screenToRay(mousePos, viewportSize_);
    
    Sanic::World* world = editor_->getWorld();
    Entity closest = INVALID_ENTITY;
    float closestDist = std::numeric_limits<float>::max();
    
    // Simple sphere-based picking
    auto query = world->query<Transform>();
    for (auto [entity, transform] : query) {
        // Simple sphere intersection test
        glm::vec3 toEntity = transform.position - ray.origin;
        float t = glm::dot(toEntity, ray.direction);
        
        if (t < 0) continue;  // Behind camera
        
        glm::vec3 closestPoint = ray.origin + ray.direction * t;
        float dist = glm::length(closestPoint - transform.position);
        
        // Assume 0.5 unit picking radius
        float pickRadius = 0.5f * glm::length(transform.scale);
        
        if (dist < pickRadius && t < closestDist) {
            closestDist = t;
            closest = entity;
        }
    }
    
    return closest;
}

void Viewport::drawToolbar() {
    ImGui::SetCursorPos(ImVec2(8, 8));
    
    // Tool buttons
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 2));
    
    auto toolButton = [this](const char* label, ViewportTool tool) {
        bool selected = currentTool_ == tool;
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }
        if (ImGui::Button(label, ImVec2(28, 28))) {
            setTool(tool);
        }
        if (selected) {
            ImGui::PopStyleColor();
        }
        ImGui::SameLine();
    };
    
    toolButton("Q", ViewportTool::Select);
    toolButton("W", ViewportTool::Translate);
    toolButton("E", ViewportTool::Rotate);
    toolButton("R", ViewportTool::Scale);
    
    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();
    
    // World/Local toggle
    const char* spaceLabel = gizmo_.getSpace() == GizmoSpace::World ? "World" : "Local";
    if (ImGui::Button(spaceLabel, ImVec2(50, 28))) {
        setGizmoSpace(gizmo_.getSpace() == GizmoSpace::World ? GizmoSpace::Local : GizmoSpace::World);
    }
    
    ImGui::SameLine();
    
    // Snap toggle
    bool snapEnabled = gizmo_.isSnapEnabled();
    if (ImGui::Checkbox("Snap", &snapEnabled)) {
        gizmo_.setSnapEnabled(snapEnabled);
    }
    
    ImGui::PopStyleVar(2);
}

void Viewport::drawViewportOverlay() {
    // Camera info in bottom-left
    ImGui::SetCursorPos(ImVec2(8, viewportSize_.y - 60));
    
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0.5f));
    ImGui::BeginChild("CameraInfo", ImVec2(200, 50), false, ImGuiWindowFlags_NoScrollbar);
    
    const auto& pos = camera_.getPosition();
    ImGui::Text("Camera: %.1f, %.1f, %.1f", pos.x, pos.y, pos.z);
    ImGui::Text("%s | FOV: %.0f", camera_.isPerspective() ? "Perspective" : "Ortho", camera_.getFov());
    
    ImGui::EndChild();
    ImGui::PopStyleColor();
    
    // View Cube / Camera Controls in top-right
    drawViewCube();
}

void Viewport::drawViewCube() {
    float cubeSize = 100.0f;
    float padding = 10.0f;
    
    ImGui::SetCursorPos(ImVec2(viewportSize_.x - cubeSize - padding, padding + 35)); // Below toolbar
    
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
    ImGui::BeginChild("ViewCube", ImVec2(cubeSize, cubeSize + 30), false, ImGuiWindowFlags_NoScrollbar);
    
    // Perspective/Ortho toggle
    if (ImGui::Button(camera_.isPerspective() ? "Persp" : "Ortho", ImVec2(cubeSize - 8, 20))) {
        camera_.togglePerspective();
    }
    
    ImGui::Spacing();
    
    // View buttons in a 3x3 grid-ish layout
    float btnSize = 28.0f;
    float spacing = 2.0f;
    
    // Row 1: Top view in center
    ImGui::SetCursorPosX((cubeSize - btnSize) * 0.5f);
    if (ImGui::Button("T", ImVec2(btnSize, btnSize))) {
        camera_.snapToTop();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Top (Numpad 7)");
    
    // Row 2: Left, Front, Right
    if (ImGui::Button("L", ImVec2(btnSize, btnSize))) {
        camera_.snapToLeft();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Left");
    ImGui::SameLine(0, spacing);
    
    if (ImGui::Button("F", ImVec2(btnSize, btnSize))) {
        camera_.snapToFront();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Front (Numpad 1)");
    ImGui::SameLine(0, spacing);
    
    if (ImGui::Button("R", ImVec2(btnSize, btnSize))) {
        camera_.snapToRight();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Right (Numpad 3)");
    
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void Viewport::drawSelectionOutlines() {
    if (!editor_ || !editor_->getWorld()) return;
    
    auto& selection = editor_->getSelection();
    if (!selection.hasSelection()) return;
    
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    glm::mat4 viewProj = camera_.getViewProjectionMatrix();
    
    const auto& config = editor_->getConfig();
    ImU32 selectionColor = IM_COL32(
        static_cast<int>(config.selectionColor.x * 255),
        static_cast<int>(config.selectionColor.y * 255),
        static_cast<int>(config.selectionColor.z * 255),
        static_cast<int>(config.selectionColor.w * 255)
    );
    
    Sanic::World* world = editor_->getWorld();
    
    for (Entity entity : selection.getSelection()) {
        if (!world->hasComponent<Transform>(entity)) continue;
        
        const Transform& transform = world->getComponent<Transform>(entity);
        
        // Project position to screen
        glm::vec4 clipPos = viewProj * glm::vec4(transform.position, 1.0f);
        if (clipPos.w <= 0.0f) continue;
        
        glm::vec3 ndcPos = glm::vec3(clipPos) / clipPos.w;
        
        ImVec2 screenPos(
            viewportPos_.x + (ndcPos.x * 0.5f + 0.5f) * viewportSize_.x,
            viewportPos_.y + (1.0f - (ndcPos.y * 0.5f + 0.5f)) * viewportSize_.y
        );
        
        // Draw selection indicator (simple circle for now)
        float radius = 20.0f / clipPos.w * 10.0f;  // Scale with distance
        radius = std::clamp(radius, 8.0f, 50.0f);
        
        drawList->AddCircle(screenPos, radius, selectionColor, 16, 2.0f);
    }
}

} // namespace Sanic::Editor
