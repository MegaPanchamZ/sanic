/**
 * Viewport.cpp
 * 
 * Implementation of the 3D viewport panel.
 * 
 * The viewport renders the 3D scene to an offscreen texture using EditorRenderer,
 * then displays that texture in the ImGui panel. This is the same approach used
 * by Unreal Engine (FSceneViewport -> SViewport -> ImGui equivalent).
 */

#include "Viewport.h"
#include "../Editor.h"
#include "../EditorRenderer.h"
#include "../core/Selection.h"
#include "../core/UndoSystem.h"
#include <ImGuizmo.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <algorithm>

namespace Sanic::Editor {

Viewport::Viewport() {
    // Default camera position
    camera_.lookAt(glm::vec3(5, 5, 5), glm::vec3(0, 0, 0));
    
    // Initialize show flags for default view mode
    ApplyViewMode(viewMode_, showFlags_);
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
    
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    
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
        
        // Request resize if viewport size changed
        uint32_t newWidth = static_cast<uint32_t>(viewportSize_.x);
        uint32_t newHeight = static_cast<uint32_t>(viewportSize_.y);
        if (newWidth > 0 && newHeight > 0 && (newWidth != viewportWidth_ || newHeight != viewportHeight_)) {
            viewportWidth_ = newWidth;
            viewportHeight_ = newHeight;
            // Resize will be handled by the editor's EditorRenderer
        }
        
        // Draw the rendered scene texture
        // The EditorRenderer provides the texture via getViewportTexture()
        if (viewportTexture_ != VK_NULL_HANDLE) {
            // ImGui::Image displays the offscreen rendered scene
            ImGui::Image(
                (ImTextureID)viewportTexture_,
                viewportPanelSize,
                ImVec2(0, 0),  // UV start
                ImVec2(1, 1)   // UV end
            );
        } else {
            // Fallback: show a placeholder with instructions
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.1f, 0.12f, 1.0f));
            ImGui::BeginChild("ViewportPlaceholder", viewportPanelSize, false);
            
            // Center the text
            ImVec2 textSize = ImGui::CalcTextSize("Scene rendering to viewport...");
            ImGui::SetCursorPos(ImVec2(
                (viewportPanelSize.x - textSize.x) * 0.5f,
                (viewportPanelSize.y - textSize.y) * 0.5f
            ));
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1.0f), "Scene rendering to viewport...");
            
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }
        
        // Now draw overlays on top of the image
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        
        // Draw a subtle border when focused
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

void Viewport::setViewMode(Sanic::EViewMode mode) {
    if (viewMode_ != mode) {
        viewMode_ = mode;
        ApplyViewMode(viewMode_, showFlags_);
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
    
    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();
    
    // View Mode dropdown
    drawViewModeMenu();
    
    ImGui::PopStyleVar(2);
}

void Viewport::drawViewModeMenu() {
    const char* currentModeName = Sanic::GetViewModeName(viewMode_);
    
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
    
    // Calculate width based on text
    float buttonWidth = ImGui::CalcTextSize(currentModeName).x + 30.0f;
    buttonWidth = std::max(buttonWidth, 100.0f);
    
    if (ImGui::BeginCombo("##ViewMode", currentModeName, ImGuiComboFlags_WidthFitPreview)) {
        // Standard modes
        if (ImGui::BeginMenu("Standard")) {
            if (ImGui::MenuItem("Lit", nullptr, viewMode_ == Sanic::EViewMode::Lit)) 
                setViewMode(Sanic::EViewMode::Lit);
            if (ImGui::MenuItem("Unlit", nullptr, viewMode_ == Sanic::EViewMode::Unlit)) 
                setViewMode(Sanic::EViewMode::Unlit);
            if (ImGui::MenuItem("Wireframe", nullptr, viewMode_ == Sanic::EViewMode::Wireframe)) 
                setViewMode(Sanic::EViewMode::Wireframe);
            if (ImGui::MenuItem("Lit Wireframe", nullptr, viewMode_ == Sanic::EViewMode::LitWireframe)) 
                setViewMode(Sanic::EViewMode::LitWireframe);
            ImGui::EndMenu();
        }
        
        // Lighting modes
        if (ImGui::BeginMenu("Lighting")) {
            if (ImGui::MenuItem("Lighting Only", nullptr, viewMode_ == Sanic::EViewMode::LightingOnly)) 
                setViewMode(Sanic::EViewMode::LightingOnly);
            if (ImGui::MenuItem("Detail Lighting", nullptr, viewMode_ == Sanic::EViewMode::DetailLighting)) 
                setViewMode(Sanic::EViewMode::DetailLighting);
            if (ImGui::MenuItem("Light Complexity", nullptr, viewMode_ == Sanic::EViewMode::LightComplexity)) 
                setViewMode(Sanic::EViewMode::LightComplexity);
            ImGui::EndMenu();
        }
        
        // Buffer Visualization
        if (ImGui::BeginMenu("Buffer Visualization")) {
            if (ImGui::MenuItem("Base Color", nullptr, viewMode_ == Sanic::EViewMode::BaseColor)) 
                setViewMode(Sanic::EViewMode::BaseColor);
            if (ImGui::MenuItem("Metallic", nullptr, viewMode_ == Sanic::EViewMode::Metallic)) 
                setViewMode(Sanic::EViewMode::Metallic);
            if (ImGui::MenuItem("Roughness", nullptr, viewMode_ == Sanic::EViewMode::Roughness)) 
                setViewMode(Sanic::EViewMode::Roughness);
            if (ImGui::MenuItem("Specular", nullptr, viewMode_ == Sanic::EViewMode::Specular)) 
                setViewMode(Sanic::EViewMode::Specular);
            ImGui::Separator();
            if (ImGui::MenuItem("World Normals", nullptr, viewMode_ == Sanic::EViewMode::WorldNormal)) 
                setViewMode(Sanic::EViewMode::WorldNormal);
            if (ImGui::MenuItem("Ambient Occlusion", nullptr, viewMode_ == Sanic::EViewMode::AmbientOcclusion)) 
                setViewMode(Sanic::EViewMode::AmbientOcclusion);
            if (ImGui::MenuItem("Scene Depth", nullptr, viewMode_ == Sanic::EViewMode::SceneDepth)) 
                setViewMode(Sanic::EViewMode::SceneDepth);
            ImGui::EndMenu();
        }
        
        // Material
        if (ImGui::BeginMenu("Material")) {
            if (ImGui::MenuItem("Reflections", nullptr, viewMode_ == Sanic::EViewMode::Reflections)) 
                setViewMode(Sanic::EViewMode::Reflections);
            if (ImGui::MenuItem("Reflection Override", nullptr, viewMode_ == Sanic::EViewMode::ReflectionOverride)) 
                setViewMode(Sanic::EViewMode::ReflectionOverride);
            ImGui::EndMenu();
        }
        
        // Mesh visualization
        if (ImGui::BeginMenu("Mesh")) {
            if (ImGui::MenuItem("Vertex Colors", nullptr, viewMode_ == Sanic::EViewMode::VertexColors)) 
                setViewMode(Sanic::EViewMode::VertexColors);
            if (ImGui::MenuItem("Mesh UVs", nullptr, viewMode_ == Sanic::EViewMode::MeshUVs)) 
                setViewMode(Sanic::EViewMode::MeshUVs);
            if (ImGui::MenuItem("LOD Coloration", nullptr, viewMode_ == Sanic::EViewMode::LODColoration)) 
                setViewMode(Sanic::EViewMode::LODColoration);
            if (ImGui::MenuItem("Triangle Density", nullptr, viewMode_ == Sanic::EViewMode::TriangleDensity)) 
                setViewMode(Sanic::EViewMode::TriangleDensity);
            ImGui::EndMenu();
        }
        
        // Advanced visualization
        if (ImGui::BeginMenu("Advanced")) {
            if (ImGui::MenuItem("Nanite", nullptr, viewMode_ == Sanic::EViewMode::Nanite)) 
                setViewMode(Sanic::EViewMode::Nanite);
            if (ImGui::MenuItem("Virtual Shadow Map", nullptr, viewMode_ == Sanic::EViewMode::VirtualShadowMap)) 
                setViewMode(Sanic::EViewMode::VirtualShadowMap);
            if (ImGui::MenuItem("Lumen", nullptr, viewMode_ == Sanic::EViewMode::Lumen)) 
                setViewMode(Sanic::EViewMode::Lumen);
            if (ImGui::MenuItem("DDGI", nullptr, viewMode_ == Sanic::EViewMode::DDGI)) 
                setViewMode(Sanic::EViewMode::DDGI);
            if (ImGui::MenuItem("SSR", nullptr, viewMode_ == Sanic::EViewMode::SSR)) 
                setViewMode(Sanic::EViewMode::SSR);
            if (ImGui::MenuItem("Motion Vectors", nullptr, viewMode_ == Sanic::EViewMode::MotionVectors)) 
                setViewMode(Sanic::EViewMode::MotionVectors);
            ImGui::EndMenu();
        }
        
        // Geometry Inspection
        if (ImGui::BeginMenu("Geometry Inspection")) {
            if (ImGui::MenuItem("Clay", nullptr, viewMode_ == Sanic::EViewMode::Clay)) 
                setViewMode(Sanic::EViewMode::Clay);
            if (ImGui::MenuItem("Front/Back Face", nullptr, viewMode_ == Sanic::EViewMode::FrontBackFace)) 
                setViewMode(Sanic::EViewMode::FrontBackFace);
            ImGui::EndMenu();
        }
        
        // Ray Tracing
        if (ImGui::BeginMenu("Ray Tracing")) {
            if (ImGui::MenuItem("Path Tracing", nullptr, viewMode_ == Sanic::EViewMode::PathTracing)) 
                setViewMode(Sanic::EViewMode::PathTracing);
            if (ImGui::MenuItem("Ray Tracing Debug", nullptr, viewMode_ == Sanic::EViewMode::RayTracingDebug)) 
                setViewMode(Sanic::EViewMode::RayTracingDebug);
            ImGui::EndMenu();
        }
        
        // Performance
        if (ImGui::BeginMenu("Performance")) {
            if (ImGui::MenuItem("Shader Complexity", nullptr, viewMode_ == Sanic::EViewMode::ShaderComplexity)) 
                setViewMode(Sanic::EViewMode::ShaderComplexity);
            if (ImGui::MenuItem("Quad Overdraw", nullptr, viewMode_ == Sanic::EViewMode::QuadOverdraw)) 
                setViewMode(Sanic::EViewMode::QuadOverdraw);
            ImGui::EndMenu();
        }
        
        ImGui::EndCombo();
    }
    
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("View Mode: %s\nCategory: %s", 
                          Sanic::GetViewModeName(viewMode_), 
                          Sanic::GetViewModeCategory(viewMode_));
    }
    
    ImGui::PopStyleVar();
}

void Viewport::drawViewportOverlay() {
    // View mode indicator in top-left (below toolbar)
    if (viewMode_ != Sanic::EViewMode::Lit) {
        ImGui::SetCursorPos(ImVec2(8, 48));
        
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.15f, 0.35f, 0.55f, 0.85f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
        
        const char* modeName = Sanic::GetViewModeName(viewMode_);
        ImVec2 textSize = ImGui::CalcTextSize(modeName);
        
        ImGui::BeginChild("ViewModeIndicator", ImVec2(textSize.x + 16, textSize.y + 8), false, ImGuiWindowFlags_NoScrollbar);
        ImGui::SetCursorPos(ImVec2(8, 4));
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", modeName);
        ImGui::EndChild();
        
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }
    
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
            // Vulkan NDC Y is -1 at top, +1 at bottom. Screen Y is 0 at top, H at bottom.
            // So -1 -> 0, +1 -> 1.
            // Formula: (ndc.y + 1) * 0.5 maps [-1, 1] to [0, 1]
            viewportPos_.y + (ndcPos.y * 0.5f + 0.5f) * viewportSize_.y
        );
        
        // Draw selection indicator (simple circle for now)
        float radius = 20.0f / clipPos.w * 10.0f;  // Scale with distance
        radius = std::clamp(radius, 8.0f, 50.0f);
        
        drawList->AddCircle(screenPos, radius, selectionColor, 16, 2.0f);
    }
}

} // namespace Sanic::Editor
