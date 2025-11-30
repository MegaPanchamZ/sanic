/**
 * Toolbar.cpp
 * 
 * Main toolbar implementation.
 */

#include "Toolbar.h"
#include "../Editor.h"
#include "../viewport/Gizmo.h"
#include <imgui.h>

namespace Sanic::Editor {

void Toolbar::initialize(Editor* editor) {
    editor_ = editor;
}

void Toolbar::update(float deltaTime) {
    // Nothing to update
}

void Toolbar::draw() {
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
    
    if (ImGui::Begin("##Toolbar", nullptr, flags)) {
        drawGizmoModeButtons();
        
        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();
        
        drawSnapSettings();
        
        ImGui::SameLine();
        
        // Center the play controls
        float windowWidth = ImGui::GetWindowWidth();
        float playControlsWidth = 150.0f;
        float cursorX = (windowWidth - playControlsWidth) * 0.5f;
        ImGui::SetCursorPosX(cursorX);
        
        drawPlayControls();
        
        ImGui::SameLine();
        
        // Right-aligned layout buttons
        float rightOffset = windowWidth - 150.0f;
        ImGui::SetCursorPosX(rightOffset);
        
        drawLayoutButtons();
    }
    ImGui::End();
    
    ImGui::PopStyleVar(2);
}

void Toolbar::drawPlayControls() {
    static bool isPlaying = false;
    static bool isPaused = false;
    
    ImVec4 playColor = isPlaying ? ImVec4(0.2f, 0.8f, 0.2f, 1.0f) : ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
    ImVec4 pauseColor = isPaused ? ImVec4(0.8f, 0.8f, 0.2f, 1.0f) : ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
    
    ImGui::PushStyleColor(ImGuiCol_Text, playColor);
    if (ImGui::Button(isPlaying ? "[||]" : "[>]", ImVec2(40, 0))) {
        isPlaying = !isPlaying;
        if (isPlaying) {
            isPaused = false;
            // TODO: Enter play mode
        } else {
            // TODO: Exit play mode
        }
    }
    ImGui::PopStyleColor();
    
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(isPlaying ? "Stop (Ctrl+P)" : "Play (Ctrl+P)");
    }
    
    ImGui::SameLine();
    
    ImGui::PushStyleColor(ImGuiCol_Text, pauseColor);
    ImGui::BeginDisabled(!isPlaying);
    if (ImGui::Button("[=]", ImVec2(40, 0))) {
        isPaused = !isPaused;
        // TODO: Pause/resume
    }
    ImGui::EndDisabled();
    ImGui::PopStyleColor();
    
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Pause");
    }
    
    ImGui::SameLine();
    
    ImGui::BeginDisabled(!isPlaying);
    if (ImGui::Button("[>|]", ImVec2(40, 0))) {
        // TODO: Step one frame
    }
    ImGui::EndDisabled();
    
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Step Frame");
    }
}

void Toolbar::drawGizmoModeButtons() {
    // Get current gizmo from editor
    // For now use static state
    static int gizmoMode = 0;  // 0=Translate, 1=Rotate, 2=Scale
    static int coordSpace = 0; // 0=World, 1=Local
    
    // Translate
    bool isTranslate = (gizmoMode == 0);
    if (isTranslate) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
    }
    if (ImGui::Button("[+]", ImVec2(30, 0))) {
        gizmoMode = 0;
    }
    if (isTranslate) {
        ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Translate (W)");
    }
    
    ImGui::SameLine();
    
    // Rotate
    bool isRotate = (gizmoMode == 1);
    if (isRotate) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
    }
    if (ImGui::Button("[O]", ImVec2(30, 0))) {
        gizmoMode = 1;
    }
    if (isRotate) {
        ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Rotate (E)");
    }
    
    ImGui::SameLine();
    
    // Scale
    bool isScale = (gizmoMode == 2);
    if (isScale) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
    }
    if (ImGui::Button("[#]", ImVec2(30, 0))) {
        gizmoMode = 2;
    }
    if (isScale) {
        ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Scale (R)");
    }
    
    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();
    
    // Coordinate space toggle
    const char* spaceLabel = (coordSpace == 0) ? "World" : "Local";
    if (ImGui::Button(spaceLabel, ImVec2(50, 0))) {
        coordSpace = 1 - coordSpace;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Toggle World/Local Space");
    }
}

void Toolbar::drawSnapSettings() {
    static bool snapEnabled = false;
    static float snapValue = 1.0f;
    
    if (snapEnabled) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
    }
    if (ImGui::Button("Snap", ImVec2(40, 0))) {
        snapEnabled = !snapEnabled;
    }
    if (snapEnabled) {
        ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Toggle Snap (Hold Ctrl)");
    }
    
    ImGui::SameLine();
    
    ImGui::PushItemWidth(50);
    ImGui::DragFloat("##SnapValue", &snapValue, 0.1f, 0.1f, 100.0f, "%.1f");
    ImGui::PopItemWidth();
    
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Snap Increment");
    }
}

void Toolbar::drawLayoutButtons() {
    static int viewMode = 0;  // 0=Editor, 1=Game, 2=Split
    
    if (ImGui::Button("Editor", ImVec2(50, 0))) {
        viewMode = 0;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Editor View");
    }
    
    ImGui::SameLine();
    
    if (ImGui::Button("Game", ImVec2(50, 0))) {
        viewMode = 1;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Game View");
    }
}

} // namespace Sanic::Editor
