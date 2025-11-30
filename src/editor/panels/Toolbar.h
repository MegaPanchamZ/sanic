/**
 * Toolbar.h
 * 
 * Main toolbar with play/stop controls and gizmo mode buttons.
 */

#pragma once

#include "../EditorWindow.h"

namespace Sanic::Editor {

class Toolbar : public EditorWindow {
public:
    void initialize(Editor* editor) override;
    void update(float deltaTime) override;
    void draw() override;
    const char* getName() const override { return "Toolbar"; }
    
private:
    void drawPlayControls();
    void drawGizmoModeButtons();
    void drawSnapSettings();
    void drawLayoutButtons();
    
    Editor* editor_ = nullptr;
};

} // namespace Sanic::Editor
