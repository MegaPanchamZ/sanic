/**
 * PropertyWidgets.h
 * 
 * Reusable property editing widgets for the inspector.
 */

#pragma once

#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <functional>

namespace Sanic::Editor {

class PropertyWidgets {
public:
    // Basic types
    static bool drawFloat(const char* label, float& value, float speed = 0.1f, 
                         float min = 0.0f, float max = 0.0f);
    
    static bool drawInt(const char* label, int& value, int speed = 1,
                       int min = 0, int max = 0);
    
    static bool drawBool(const char* label, bool& value);
    
    static bool drawString(const char* label, std::string& value);
    
    // Vector types
    static bool drawVec2(const char* label, glm::vec2& value, float speed = 0.1f);
    static bool drawVec3(const char* label, glm::vec3& value, float speed = 0.1f);
    static bool drawVec4(const char* label, glm::vec4& value, float speed = 0.1f);
    
    // Rotation (euler angles in degrees)
    static bool drawRotation(const char* label, glm::vec3& eulerDegrees, float speed = 1.0f);
    
    // Color
    static bool drawColor3(const char* label, glm::vec3& color);
    static bool drawColor4(const char* label, glm::vec4& color);
    
    // Range slider
    static bool drawRange(const char* label, float& min, float& max, 
                         float rangeMin = 0.0f, float rangeMax = 1.0f);
    
    // Enum dropdown
    template<typename T>
    static bool drawEnum(const char* label, T& value, 
                        const char* const* names, int count);
    
    // Asset reference
    static bool drawAssetPath(const char* label, std::string& path, 
                             const char* assetType);
    
    // Curve editor
    static bool drawCurve(const char* label, std::vector<glm::vec2>& points,
                         const glm::vec2& rangeMin = glm::vec2(0, 0),
                         const glm::vec2& rangeMax = glm::vec2(1, 1));
    
    // Gradient editor
    struct GradientKey {
        float position;
        glm::vec4 color;
    };
    static bool drawGradient(const char* label, std::vector<GradientKey>& keys);
    
    // Header with collapse/expand
    static bool drawHeader(const char* label, bool* open = nullptr, bool defaultOpen = true);
    
    // Collapsing section
    static bool beginSection(const char* label, bool defaultOpen = true);
    static void endSection();
    
    // Tooltip helper
    static void helpMarker(const char* desc);
    
private:
    static void drawLabel(const char* label, float width = 100.0f);
};

// Template implementations
template<typename T>
bool PropertyWidgets::drawEnum(const char* label, T& value, 
                               const char* const* names, int count) {
    drawLabel(label);
    
    int currentIndex = static_cast<int>(value);
    bool changed = false;
    
    ImGui::PushID(label);
    if (ImGui::BeginCombo("##enum", names[currentIndex])) {
        for (int i = 0; i < count; i++) {
            bool isSelected = (currentIndex == i);
            if (ImGui::Selectable(names[i], isSelected)) {
                value = static_cast<T>(i);
                changed = true;
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();
    
    return changed;
}

} // namespace Sanic::Editor
