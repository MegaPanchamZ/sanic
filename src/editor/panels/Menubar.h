/**
 * Menubar.h
 * 
 * Main menu bar for the editor.
 */

#pragma once

#include "../EditorWindow.h"
#include <functional>
#include <vector>
#include <string>

namespace Sanic::Editor {

class Menubar : public EditorWindow {
public:
    void initialize(Editor* editor) override;
    void update(float deltaTime) override;
    void draw() override;
    const char* getName() const override { return "Menubar"; }
    
private:
    void drawFileMenu();
    void drawEditMenu();
    void drawViewMenu();
    void drawGameObjectMenu();
    void drawComponentMenu();
    void drawWindowMenu();
    void drawToolsMenu();
    void drawHelpMenu();
    
    // Dialogs
    void showNewSceneDialog();
    void showOpenSceneDialog();
    void showSaveSceneDialog();
    void showProjectSettings();
    void showAboutDialog();
    
    Editor* editor_ = nullptr;
    
    bool showAbout_ = false;
    bool showProjectSettings_ = false;
};

} // namespace Sanic::Editor
