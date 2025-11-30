/**
 * Gizmo.h
 * 
 * Transform gizmos for the editor viewport.
 * Uses ImGuizmo for rendering and interaction.
 */

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include "../../engine/ECS.h"

namespace Sanic::Editor {

enum class GizmoAxis {
    None,
    X, Y, Z,
    XY, XZ, YZ,
    XYZ,
    Screen,     // View-aligned plane
    Trackball   // Free rotation
};

enum class GizmoType {
    Translate,
    Rotate,
    Scale,
    Universal,   // All three combined
    Bounds       // Scale by bounds handles
};

enum class GizmoSpace {
    World,
    Local
};

struct GizmoResult {
    bool active = false;
    bool changed = false;
    glm::vec3 deltaTranslation = glm::vec3(0);
    glm::quat deltaRotation = glm::quat();
    glm::vec3 deltaScale = glm::vec3(0);
    glm::mat4 newMatrix = glm::mat4(1);
    GizmoAxis axis = GizmoAxis::None;
};

class Gizmo {
public:
    Gizmo();
    
    // Set gizmo type
    void setType(GizmoType type) { type_ = type; }
    GizmoType getType() const { return type_; }
    
    // Set space (world/local)
    void setSpace(GizmoSpace space) { space_ = space; }
    GizmoSpace getSpace() const { return space_; }
    
    // Snapping
    void setSnapTranslation(float snap) { snapTranslation_ = snap; }
    void setSnapRotation(float snap) { snapRotation_ = snap; }
    void setSnapScale(float snap) { snapScale_ = snap; }
    void setSnapEnabled(bool enabled) { snapEnabled_ = enabled; }
    bool isSnapEnabled() const { return snapEnabled_; }
    
    float getSnapTranslation() const { return snapTranslation_; }
    float getSnapRotation() const { return snapRotation_; }
    float getSnapScale() const { return snapScale_; }
    
    // Manipulate a single transform
    // Call this inside ImGui window after setting up gizmo
    GizmoResult manipulate(const glm::mat4& view, const glm::mat4& projection,
                           glm::mat4& matrix, const glm::vec2& viewportPos,
                           const glm::vec2& viewportSize);
    
    // Manipulate with transform components
    GizmoResult manipulate(const glm::mat4& view, const glm::mat4& projection,
                           Transform& transform, const glm::vec2& viewportPos,
                           const glm::vec2& viewportSize);
    
    // Check if gizmo is being used (for input handling)
    bool isOver() const;
    bool isUsing() const;
    
    // Set bounds for bounds mode
    void setBounds(const glm::vec3& min, const glm::vec3& max);
    
    // Enable/disable gizmo
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }
    
private:
    GizmoType type_ = GizmoType::Translate;
    GizmoSpace space_ = GizmoSpace::World;
    
    float snapTranslation_ = 1.0f;
    float snapRotation_ = 15.0f;
    float snapScale_ = 0.1f;
    bool snapEnabled_ = false;
    
    glm::vec3 boundsMin_ = glm::vec3(-0.5f);
    glm::vec3 boundsMax_ = glm::vec3(0.5f);
    
    bool enabled_ = true;
    
    // Cache last manipulation state
    glm::mat4 lastMatrix_ = glm::mat4(1);
};

} // namespace Sanic::Editor
