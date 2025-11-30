/**
 * PropertyWidgets.cpp
 * 
 * Property widget implementations.
 */

#include "PropertyWidgets.h"
#include <imgui_internal.h>
#include <cstring>

namespace Sanic::Editor {

void PropertyWidgets::drawLabel(const char* label, float width) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(width);
    ImGui::SetNextItemWidth(-1);
}

bool PropertyWidgets::drawFloat(const char* label, float& value, float speed, 
                                float min, float max) {
    drawLabel(label);
    
    ImGui::PushID(label);
    bool changed = ImGui::DragFloat("##float", &value, speed, min, max, "%.3f");
    ImGui::PopID();
    
    return changed;
}

bool PropertyWidgets::drawInt(const char* label, int& value, int speed,
                              int min, int max) {
    drawLabel(label);
    
    ImGui::PushID(label);
    bool changed = ImGui::DragInt("##int", &value, static_cast<float>(speed), min, max);
    ImGui::PopID();
    
    return changed;
}

bool PropertyWidgets::drawBool(const char* label, bool& value) {
    drawLabel(label);
    
    ImGui::PushID(label);
    bool changed = ImGui::Checkbox("##bool", &value);
    ImGui::PopID();
    
    return changed;
}

bool PropertyWidgets::drawString(const char* label, std::string& value) {
    drawLabel(label);
    
    char buffer[256];
    strncpy(buffer, value.c_str(), sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    
    ImGui::PushID(label);
    bool changed = ImGui::InputText("##string", buffer, sizeof(buffer));
    if (changed) {
        value = buffer;
    }
    ImGui::PopID();
    
    return changed;
}

bool PropertyWidgets::drawVec2(const char* label, glm::vec2& value, float speed) {
    drawLabel(label);
    
    ImGui::PushID(label);
    bool changed = ImGui::DragFloat2("##vec2", &value.x, speed, 0.0f, 0.0f, "%.3f");
    ImGui::PopID();
    
    return changed;
}

bool PropertyWidgets::drawVec3(const char* label, glm::vec3& value, float speed) {
    ImGui::PushID(label);
    
    bool changed = false;
    
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(100.0f);
    
    float width = (ImGui::GetContentRegionAvail().x - 48.0f) / 3.0f;
    
    // X (Red)
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
    if (ImGui::Button("X", ImVec2(16, 0))) {
        value.x = 0.0f;
        changed = true;
    }
    ImGui::PopStyleColor(3);
    
    ImGui::SameLine();
    ImGui::SetNextItemWidth(width);
    if (ImGui::DragFloat("##x", &value.x, speed, 0.0f, 0.0f, "%.3f")) {
        changed = true;
    }
    
    // Y (Green)
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.8f, 0.3f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.9f, 0.2f, 1.0f));
    if (ImGui::Button("Y", ImVec2(16, 0))) {
        value.y = 0.0f;
        changed = true;
    }
    ImGui::PopStyleColor(3);
    
    ImGui::SameLine();
    ImGui::SetNextItemWidth(width);
    if (ImGui::DragFloat("##y", &value.y, speed, 0.0f, 0.0f, "%.3f")) {
        changed = true;
    }
    
    // Z (Blue)
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.8f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.9f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.2f, 1.0f, 1.0f));
    if (ImGui::Button("Z", ImVec2(16, 0))) {
        value.z = 0.0f;
        changed = true;
    }
    ImGui::PopStyleColor(3);
    
    ImGui::SameLine();
    ImGui::SetNextItemWidth(width);
    if (ImGui::DragFloat("##z", &value.z, speed, 0.0f, 0.0f, "%.3f")) {
        changed = true;
    }
    
    ImGui::PopID();
    
    return changed;
}

bool PropertyWidgets::drawVec4(const char* label, glm::vec4& value, float speed) {
    drawLabel(label);
    
    ImGui::PushID(label);
    bool changed = ImGui::DragFloat4("##vec4", &value.x, speed, 0.0f, 0.0f, "%.3f");
    ImGui::PopID();
    
    return changed;
}

bool PropertyWidgets::drawRotation(const char* label, glm::vec3& eulerDegrees, float speed) {
    return drawVec3(label, eulerDegrees, speed);
}

bool PropertyWidgets::drawColor3(const char* label, glm::vec3& color) {
    drawLabel(label);
    
    ImGui::PushID(label);
    bool changed = ImGui::ColorEdit3("##color3", &color.x, 
        ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
    ImGui::PopID();
    
    return changed;
}

bool PropertyWidgets::drawColor4(const char* label, glm::vec4& color) {
    drawLabel(label);
    
    ImGui::PushID(label);
    bool changed = ImGui::ColorEdit4("##color4", &color.x,
        ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_AlphaPreview);
    ImGui::PopID();
    
    return changed;
}

bool PropertyWidgets::drawRange(const char* label, float& min, float& max,
                                float rangeMin, float rangeMax) {
    drawLabel(label);
    
    ImGui::PushID(label);
    
    float width = ImGui::GetContentRegionAvail().x * 0.5f - 4.0f;
    
    ImGui::SetNextItemWidth(width);
    bool changed = ImGui::DragFloat("##min", &min, 0.01f, rangeMin, max, "%.3f");
    
    ImGui::SameLine();
    ImGui::TextUnformatted("-");
    ImGui::SameLine();
    
    ImGui::SetNextItemWidth(width);
    if (ImGui::DragFloat("##max", &max, 0.01f, min, rangeMax, "%.3f")) {
        changed = true;
    }
    
    ImGui::PopID();
    
    return changed;
}

bool PropertyWidgets::drawAssetPath(const char* label, std::string& path,
                                    const char* assetType) {
    drawLabel(label);
    
    ImGui::PushID(label);
    
    bool changed = false;
    
    // Path display
    float buttonWidth = 60.0f;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - buttonWidth - 4.0f);
    
    char buffer[256];
    strncpy(buffer, path.c_str(), sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    
    if (ImGui::InputText("##path", buffer, sizeof(buffer))) {
        path = buffer;
        changed = true;
    }
    
    // Drag-drop target
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
            path = static_cast<const char*>(payload->Data);
            changed = true;
        }
        ImGui::EndDragDropTarget();
    }
    
    ImGui::SameLine();
    
    // Browse button
    if (ImGui::Button("Browse", ImVec2(buttonWidth, 0))) {
        // TODO: Open file dialog
    }
    
    ImGui::PopID();
    
    return changed;
}

bool PropertyWidgets::drawCurve(const char* label, std::vector<glm::vec2>& points,
                                const glm::vec2& rangeMin, const glm::vec2& rangeMax) {
    ImGui::PushID(label);
    
    ImGui::TextUnformatted(label);
    
    ImVec2 canvasSize(ImGui::GetContentRegionAvail().x, 150.0f);
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    
    // Background
    drawList->AddRectFilled(canvasPos, 
        ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
        IM_COL32(30, 30, 35, 255));
    
    // Grid lines
    for (int i = 1; i < 4; i++) {
        float x = canvasPos.x + canvasSize.x * (i / 4.0f);
        float y = canvasPos.y + canvasSize.y * (i / 4.0f);
        drawList->AddLine(ImVec2(x, canvasPos.y), ImVec2(x, canvasPos.y + canvasSize.y),
            IM_COL32(50, 50, 55, 255));
        drawList->AddLine(ImVec2(canvasPos.x, y), ImVec2(canvasPos.x + canvasSize.x, y),
            IM_COL32(50, 50, 55, 255));
    }
    
    // Border
    drawList->AddRect(canvasPos,
        ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
        IM_COL32(60, 60, 65, 255));
    
    // Draw curve
    if (points.size() >= 2) {
        for (size_t i = 0; i < points.size() - 1; i++) {
            auto toScreen = [&](const glm::vec2& p) -> ImVec2 {
                float x = (p.x - rangeMin.x) / (rangeMax.x - rangeMin.x);
                float y = 1.0f - (p.y - rangeMin.y) / (rangeMax.y - rangeMin.y);
                return ImVec2(canvasPos.x + x * canvasSize.x, canvasPos.y + y * canvasSize.y);
            };
            
            drawList->AddLine(toScreen(points[i]), toScreen(points[i + 1]),
                IM_COL32(100, 180, 255, 255), 2.0f);
        }
    }
    
    // Draw points
    bool changed = false;
    for (size_t i = 0; i < points.size(); i++) {
        auto toScreen = [&](const glm::vec2& p) -> ImVec2 {
            float x = (p.x - rangeMin.x) / (rangeMax.x - rangeMin.x);
            float y = 1.0f - (p.y - rangeMin.y) / (rangeMax.y - rangeMin.y);
            return ImVec2(canvasPos.x + x * canvasSize.x, canvasPos.y + y * canvasSize.y);
        };
        
        ImVec2 pointPos = toScreen(points[i]);
        
        ImGui::SetCursorScreenPos(ImVec2(pointPos.x - 5, pointPos.y - 5));
        ImGui::InvisibleButton(("point" + std::to_string(i)).c_str(), ImVec2(10, 10));
        
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
            ImVec2 delta = ImGui::GetMouseDragDelta(0);
            ImGui::ResetMouseDragDelta(0);
            
            points[i].x += delta.x / canvasSize.x * (rangeMax.x - rangeMin.x);
            points[i].y -= delta.y / canvasSize.y * (rangeMax.y - rangeMin.y);
            
            points[i].x = glm::clamp(points[i].x, rangeMin.x, rangeMax.x);
            points[i].y = glm::clamp(points[i].y, rangeMin.y, rangeMax.y);
            
            changed = true;
        }
        
        bool hovered = ImGui::IsItemHovered();
        drawList->AddCircleFilled(pointPos, hovered ? 6.0f : 5.0f,
            hovered ? IM_COL32(255, 200, 100, 255) : IM_COL32(255, 255, 255, 255));
    }
    
    ImGui::SetCursorScreenPos(ImVec2(canvasPos.x, canvasPos.y + canvasSize.y + 4.0f));
    
    ImGui::PopID();
    
    return changed;
}

bool PropertyWidgets::drawGradient(const char* label, std::vector<GradientKey>& keys) {
    ImGui::PushID(label);
    
    ImGui::TextUnformatted(label);
    
    ImVec2 barSize(ImGui::GetContentRegionAvail().x, 24.0f);
    ImVec2 barPos = ImGui::GetCursorScreenPos();
    
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    
    // Draw gradient bar
    if (keys.size() >= 2) {
        // Sort keys by position
        std::vector<GradientKey> sortedKeys = keys;
        std::sort(sortedKeys.begin(), sortedKeys.end(), 
            [](const GradientKey& a, const GradientKey& b) { return a.position < b.position; });
        
        for (size_t i = 0; i < sortedKeys.size() - 1; i++) {
            float x1 = barPos.x + sortedKeys[i].position * barSize.x;
            float x2 = barPos.x + sortedKeys[i + 1].position * barSize.x;
            
            ImU32 col1 = ImGui::ColorConvertFloat4ToU32(ImVec4(
                sortedKeys[i].color.r, sortedKeys[i].color.g, 
                sortedKeys[i].color.b, sortedKeys[i].color.a));
            ImU32 col2 = ImGui::ColorConvertFloat4ToU32(ImVec4(
                sortedKeys[i + 1].color.r, sortedKeys[i + 1].color.g,
                sortedKeys[i + 1].color.b, sortedKeys[i + 1].color.a));
            
            drawList->AddRectFilledMultiColor(
                ImVec2(x1, barPos.y), ImVec2(x2, barPos.y + barSize.y),
                col1, col2, col2, col1);
        }
    }
    
    // Border
    drawList->AddRect(barPos, ImVec2(barPos.x + barSize.x, barPos.y + barSize.y),
        IM_COL32(60, 60, 65, 255));
    
    // Draw key markers
    bool changed = false;
    for (size_t i = 0; i < keys.size(); i++) {
        float x = barPos.x + keys[i].position * barSize.x;
        
        ImVec2 markerPos(x, barPos.y + barSize.y);
        
        drawList->AddTriangleFilled(
            ImVec2(markerPos.x - 5, markerPos.y + 8),
            ImVec2(markerPos.x + 5, markerPos.y + 8),
            ImVec2(markerPos.x, markerPos.y),
            IM_COL32(255, 255, 255, 255));
    }
    
    ImGui::SetCursorScreenPos(ImVec2(barPos.x, barPos.y + barSize.y + 12.0f));
    
    ImGui::PopID();
    
    return changed;
}

bool PropertyWidgets::drawHeader(const char* label, bool* open, bool defaultOpen) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed |
                               ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_FramePadding;
    
    if (!defaultOpen && open && !*open) {
        flags &= ~ImGuiTreeNodeFlags_DefaultOpen;
    }
    
    bool isOpen = ImGui::TreeNodeEx(label, flags);
    
    if (open) {
        *open = isOpen;
    }
    
    return isOpen;
}

bool PropertyWidgets::beginSection(const char* label, bool defaultOpen) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed;
    
    if (!defaultOpen) {
        flags &= ~ImGuiTreeNodeFlags_DefaultOpen;
    }
    
    return ImGui::TreeNodeEx(label, flags);
}

void PropertyWidgets::endSection() {
    ImGui::TreePop();
}

void PropertyWidgets::helpMarker(const char* desc) {
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

} // namespace Sanic::Editor
