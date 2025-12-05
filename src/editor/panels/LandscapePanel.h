#pragma once

#include "../EditorWindow.h"
#include "engine/LandscapeSystem.h"
#include <glm/glm.hpp>

namespace Sanic::Editor {

class LandscapePanel : public EditorWindow {
public:
    void initialize(Editor* editor) override;
    void update(float deltaTime) override;
    void draw() override;
    const char* getName() const override { return "Landscape"; }
    
private:
    void drawCreateTab();
    void drawSculptTab();
    void drawPaintTab();
    
    void handleBrushInput();
    
    Editor* editor_ = nullptr;
    LandscapeSystem* landscapeSystem_ = nullptr;
    
    // Active landscape
    uint32_t activeLandscapeId_ = 0;
    
    // Creation settings
    LandscapeConfig createConfig_;
    
    // Brush settings
    LandscapeBrush brush_;
    bool isPainting_ = false;
    
    // UI State
    int activeTab_ = 0; // 0=Create, 1=Sculpt, 2=Paint
    
    // Layer UI
    char newLayerName_[64] = "New Layer";
};

} // namespace Sanic::Editor
