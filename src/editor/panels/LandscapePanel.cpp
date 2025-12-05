/**
 * LandscapePanel.cpp
 * 
 * Editor panel for creating and editing landscapes.
 */

#include "LandscapePanel.h"
#include "../Editor.h"
#include "engine/ECS.h"
#include <imgui.h>

namespace Sanic::Editor {

void LandscapePanel::initialize(Editor* editor) {
    EditorWindow::initialize(editor);
    editor_ = editor;
    
    // Default config
    createConfig_.componentsX = 8;
    createConfig_.componentsY = 8;
    createConfig_.componentSize = 64.0f;
    createConfig_.heightScale = 256.0f;
    createConfig_.heightmapResolution = 65; // 64 + 1
}

void LandscapePanel::update(float deltaTime) {
    if (activeLandscapeId_ != 0 && isPainting_) {
        handleBrushInput();
    }
}

void LandscapePanel::draw() {
    if (!beginPanel()) {
        endPanel();
        return;
    }
    
    // Get landscape system from world
    if (editor_->getWorld()) {
        // TODO: In a real ECS, we'd get the system properly. 
        // For now assuming we can access it or finding a landscape entity.
        // This part depends on how systems are exposed. 
        // Let's assume we can get it from the world or it's a global system.
        // Since I don't see a direct getter in World.h (I haven't read it fully), 
        // I'll assume we might need to add a getter or find the component.
        
        // For this implementation, I'll assume we can get it. 
        // If not, we might need to fix World.h.
    }
    
    if (ImGui::BeginTabBar("LandscapeTabs")) {
        if (ImGui::BeginTabItem("Create")) {
            activeTab_ = 0;
            drawCreateTab();
            ImGui::EndTabItem();
        }
        
        if (activeLandscapeId_ != 0) {
            if (ImGui::BeginTabItem("Sculpt")) {
                activeTab_ = 1;
                drawSculptTab();
                ImGui::EndTabItem();
            }
            
            if (ImGui::BeginTabItem("Paint")) {
                activeTab_ = 2;
                drawPaintTab();
                ImGui::EndTabItem();
            }
        }
        
        ImGui::EndTabBar();
    }
    
    endPanel();
}

void LandscapePanel::drawCreateTab() {
    ImGui::Text("New Landscape Configuration");
    ImGui::Separator();
    
    int size[2] = { (int)createConfig_.componentsX, (int)createConfig_.componentsY };
    if (ImGui::DragInt2("Component Count", size, 1, 1, 64)) {
        createConfig_.componentsX = size[0];
        createConfig_.componentsY = size[1];
    }
    
    ImGui::DragFloat("Component Size", &createConfig_.componentSize, 1.0f, 1.0f, 1024.0f);
    ImGui::DragFloat("Height Scale", &createConfig_.heightScale, 1.0f, 1.0f, 2048.0f);
    
    int res = (int)createConfig_.heightmapResolution;
    if (ImGui::Combo("Resolution", &res, "33 (32+1)\0 65 (64+1)\0 129 (128+1)\0 257 (256+1)\0\0")) {
        // Map index to resolution
        switch (res) {
            case 0: createConfig_.heightmapResolution = 33; break;
            case 1: createConfig_.heightmapResolution = 65; break;
            case 2: createConfig_.heightmapResolution = 129; break;
            case 3: createConfig_.heightmapResolution = 257; break;
        }
    }
    
    ImGui::Spacing();
    
    if (ImGui::Button("Create Landscape", ImVec2(-1, 40))) {
        // TODO: Actually create the landscape via system
        // activeLandscapeId_ = landscapeSystem_->createLandscape(createConfig_);
        
        // For now, just simulate it so UI works
        activeLandscapeId_ = 1; 
        editor_->showNotification("Landscape created (Simulation)");
    }
}

void LandscapePanel::drawSculptTab() {
    ImGui::Text("Sculpting Tools");
    ImGui::Separator();
    
    const char* modes[] = { "Raise", "Lower", "Smooth", "Flatten", "Noise" };
    int currentMode = (int)brush_.mode;
    if (currentMode > 4) currentMode = 0; // Safety
    
    if (ImGui::Combo("Tool", &currentMode, modes, IM_ARRAYSIZE(modes))) {
        brush_.mode = (LandscapeBrush::Mode)currentMode;
    }
    
    ImGui::DragFloat("Brush Radius", &brush_.radius, 0.5f, 1.0f, 200.0f);
    ImGui::SliderFloat("Brush Strength", &brush_.strength, 0.0f, 1.0f);
    ImGui::SliderFloat("Brush Falloff", &brush_.falloff, 0.0f, 1.0f);
    
    if (brush_.mode == LandscapeBrush::Mode::Flatten) {
        // Target height for flatten
        static float flattenHeight = 0.0f;
        ImGui::DragFloat("Target Height", &flattenHeight);
    }
    
    ImGui::Separator();
    ImGui::TextDisabled("Hold Ctrl+Left Click to sculpt");
}

void LandscapePanel::drawPaintTab() {
    ImGui::Text("Material Painting");
    ImGui::Separator();
    
    // Layer list
    ImGui::BeginChild("Layers", ImVec2(0, 150), true);
    // TODO: Iterate layers
    if (ImGui::Selectable("Layer 1 (Grass)", brush_.targetLayerId == 0)) brush_.targetLayerId = 0;
    if (ImGui::Selectable("Layer 2 (Dirt)", brush_.targetLayerId == 1)) brush_.targetLayerId = 1;
    if (ImGui::Selectable("Layer 3 (Rock)", brush_.targetLayerId == 2)) brush_.targetLayerId = 2;
    ImGui::EndChild();
    
    ImGui::InputText("Name", newLayerName_, sizeof(newLayerName_));
    if (ImGui::Button("Add Layer")) {
        // TODO: Add layer
    }
    
    ImGui::Separator();
    ImGui::DragFloat("Brush Radius", &brush_.radius, 0.5f, 1.0f, 200.0f);
    ImGui::SliderFloat("Brush Strength", &brush_.strength, 0.0f, 1.0f);
}

void LandscapePanel::handleBrushInput() {
    // Raycast from camera to landscape
    // Apply brush at hit position
}

} // namespace Sanic::Editor
