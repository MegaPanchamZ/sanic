/**
 * Grid.cpp
 * 
 * Implementation of viewport grid rendering.
 */

#include "Grid.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace Sanic::Editor {

Grid::Grid() {
}

Grid::~Grid() {
}

std::vector<Grid::GridVertex> Grid::generateLines(const glm::vec3& cameraPos) const {
    std::vector<GridVertex> vertices;
    
    if (!visible_) return vertices;
    
    int lineCount = static_cast<int>(size_ / spacing_);
    float halfSize = size_ * 0.5f;
    
    // Snap grid to camera position for infinite grid effect
    float snapX = std::floor(cameraPos.x / spacing_) * spacing_;
    float snapZ = std::floor(cameraPos.z / spacing_) * spacing_;
    
    // X-axis lines (parallel to Z)
    for (int i = -lineCount; i <= lineCount; ++i) {
        float x = snapX + i * spacing_;
        
        // Determine color
        glm::vec4 lineColor;
        if (std::abs(x) < spacing_ * 0.5f) {
            lineColor = zAxisColor_;  // Z axis
        } else if (i % majorLineInterval_ == 0) {
            lineColor = majorColor_;
        } else {
            lineColor = color_;
        }
        
        // Calculate fade based on distance from camera
        glm::vec3 lineCenter(x, 0, cameraPos.z);
        float dist = glm::length(lineCenter - cameraPos);
        float fade = 1.0f - std::clamp((dist - nearFade_) / (farFade_ - nearFade_), 0.0f, 1.0f);
        lineColor.a *= fade;
        
        if (lineColor.a > 0.01f) {
            vertices.push_back({glm::vec3(x, 0, snapZ - halfSize), lineColor});
            vertices.push_back({glm::vec3(x, 0, snapZ + halfSize), lineColor});
        }
    }
    
    // Z-axis lines (parallel to X)
    for (int i = -lineCount; i <= lineCount; ++i) {
        float z = snapZ + i * spacing_;
        
        // Determine color
        glm::vec4 lineColor;
        if (std::abs(z) < spacing_ * 0.5f) {
            lineColor = xAxisColor_;  // X axis
        } else if (i % majorLineInterval_ == 0) {
            lineColor = majorColor_;
        } else {
            lineColor = color_;
        }
        
        // Calculate fade
        glm::vec3 lineCenter(cameraPos.x, 0, z);
        float dist = glm::length(lineCenter - cameraPos);
        float fade = 1.0f - std::clamp((dist - nearFade_) / (farFade_ - nearFade_), 0.0f, 1.0f);
        lineColor.a *= fade;
        
        if (lineColor.a > 0.01f) {
            vertices.push_back({glm::vec3(snapX - halfSize, 0, z), lineColor});
            vertices.push_back({glm::vec3(snapX + halfSize, 0, z), lineColor});
        }
    }
    
    return vertices;
}

void Grid::drawImGui(const glm::mat4& viewProj, const glm::vec2& viewportSize,
                      const glm::vec3& cameraPos) {
    if (!visible_) return;
    
    auto vertices = generateLines(cameraPos);
    if (vertices.empty()) return;
    
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 windowPos = ImGui::GetWindowPos();
    
    // Project and draw lines
    for (size_t i = 0; i < vertices.size(); i += 2) {
        const auto& v0 = vertices[i];
        const auto& v1 = vertices[i + 1];
        
        // Project to screen space
        glm::vec4 clip0 = viewProj * glm::vec4(v0.position, 1.0f);
        glm::vec4 clip1 = viewProj * glm::vec4(v1.position, 1.0f);
        
        // Clip against near plane (simple check)
        if (clip0.w <= 0.0f && clip1.w <= 0.0f) continue;
        
        // Perspective divide
        if (clip0.w > 0.0f && clip1.w > 0.0f) {
            glm::vec3 ndc0 = glm::vec3(clip0) / clip0.w;
            glm::vec3 ndc1 = glm::vec3(clip1) / clip1.w;
            
            // Convert to screen coordinates
            ImVec2 screen0(
                windowPos.x + (ndc0.x * 0.5f + 0.5f) * viewportSize.x,
                windowPos.y + (1.0f - (ndc0.y * 0.5f + 0.5f)) * viewportSize.y
            );
            ImVec2 screen1(
                windowPos.x + (ndc1.x * 0.5f + 0.5f) * viewportSize.x,
                windowPos.y + (1.0f - (ndc1.y * 0.5f + 0.5f)) * viewportSize.y
            );
            
            // Convert color
            ImU32 color = IM_COL32(
                static_cast<int>(v0.color.r * 255),
                static_cast<int>(v0.color.g * 255),
                static_cast<int>(v0.color.b * 255),
                static_cast<int>(v0.color.a * 255)
            );
            
            drawList->AddLine(screen0, screen1, color, 1.0f);
        }
    }
}

} // namespace Sanic::Editor
