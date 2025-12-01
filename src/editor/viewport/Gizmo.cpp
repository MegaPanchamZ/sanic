/**
 * Gizmo.cpp
 * 
 * Implementation of transform gizmos using ImGuizmo.
 */

#include "Gizmo.h"
#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace Sanic::Editor {

Gizmo::Gizmo() {
}

GizmoResult Gizmo::manipulate(const glm::mat4& view, const glm::mat4& projection,
                               glm::mat4& matrix, const glm::vec2& viewportPos,
                               const glm::vec2& viewportSize) {
    GizmoResult result;
    
    if (!enabled_) return result;
    
    // Set ImGuizmo viewport
    ImGuizmo::SetRect(viewportPos.x, viewportPos.y, viewportSize.x, viewportSize.y);
    ImGuizmo::SetOrthographic(false);
    
    // Convert gizmo type
    ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
    switch (type_) {
        case GizmoType::Translate: operation = ImGuizmo::TRANSLATE; break;
        case GizmoType::Rotate: operation = ImGuizmo::ROTATE; break;
        case GizmoType::Scale: operation = ImGuizmo::SCALE; break;
        case GizmoType::Universal: 
            operation = ImGuizmo::TRANSLATE | ImGuizmo::ROTATE | ImGuizmo::SCALE;
            break;
        case GizmoType::Bounds: operation = ImGuizmo::BOUNDS; break;
    }
    
    // Convert space
    ImGuizmo::MODE mode = (space_ == GizmoSpace::World) ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
    
    // Snap values
    float* snapValues = nullptr;
    float translateSnap[3] = { snapTranslation_, snapTranslation_, snapTranslation_ };
    float rotateSnap[3] = { snapRotation_, snapRotation_, snapRotation_ };
    float scaleSnap[3] = { snapScale_, snapScale_, snapScale_ };
    
    if (snapEnabled_) {
        switch (type_) {
            case GizmoType::Translate: snapValues = translateSnap; break;
            case GizmoType::Rotate: snapValues = rotateSnap; break;
            case GizmoType::Scale: snapValues = scaleSnap; break;
            default: break;
        }
    }
    
    // Bounds for bounds mode
    float bounds[6] = { boundsMin_.x, boundsMin_.y, boundsMin_.z, 
                        boundsMax_.x, boundsMax_.y, boundsMax_.z };
    
    // Manipulate
    glm::mat4 deltaMatrix(1.0f);
    
    bool manipulated = false;
    if (type_ == GizmoType::Bounds) {
        manipulated = ImGuizmo::Manipulate(
            &view[0][0], &projection[0][0],
            operation, mode,
            &matrix[0][0],
            &deltaMatrix[0][0],
            snapValues,
            bounds,
            snapEnabled_ ? translateSnap : nullptr
        );
    } else {
        manipulated = ImGuizmo::Manipulate(
            &view[0][0], &projection[0][0],
            operation, mode,
            &matrix[0][0],
            &deltaMatrix[0][0],
            snapValues
        );
    }
    
    result.active = ImGuizmo::IsUsing();
    result.changed = manipulated && result.active;
    
    if (result.changed) {
        result.newMatrix = matrix;
        
        // Calculate delta from lastMatrix
        glm::mat4 invLast = glm::inverse(lastMatrix_);
        glm::mat4 delta = matrix * invLast;
        
        // Decompose delta
        glm::vec3 scale, translation, skew;
        glm::vec4 perspective;
        glm::quat rotation;
        glm::decompose(delta, scale, rotation, translation, skew, perspective);
        
        result.deltaTranslation = translation;
        result.deltaRotation = rotation;
        result.deltaScale = scale - glm::vec3(1.0f);
    }
    
    lastMatrix_ = matrix;
    
    return result;
}

GizmoResult Gizmo::manipulate(const glm::mat4& view, const glm::mat4& projection,
                               Transform& transform, const glm::vec2& viewportPos,
                               const glm::vec2& viewportSize) {
    // Build matrix from transform
    glm::mat4 matrix = transform.getLocalMatrix();
    
    GizmoResult result = manipulate(view, projection, matrix, viewportPos, viewportSize);
    
    if (result.changed) {
        // Decompose matrix back to transform
        glm::vec3 scale, translation, skew;
        glm::vec4 perspective;
        glm::quat rotation;
        glm::decompose(matrix, scale, rotation, translation, skew, perspective);
        
        transform.position = translation;
        transform.rotation = rotation;
        transform.scale = scale;
    }
    
    return result;
}

bool Gizmo::isOver() const {
    return ImGuizmo::IsOver();
}

bool Gizmo::isUsing() const {
    return ImGuizmo::IsUsing();
}

void Gizmo::setBounds(const glm::vec3& min, const glm::vec3& max) {
    boundsMin_ = min;
    boundsMax_ = max;
}

} // namespace Sanic::Editor
