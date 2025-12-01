/**
 * Menubar.cpp
 * 
 * Main menu bar implementation.
 */

#include "Menubar.h"
#include "../Editor.h"
#include "../core/Commands.h"
#include "../core/UndoSystem.h"
#include "../core/Selection.h"
#include <imgui.h>

namespace Sanic::Editor {

void Menubar::initialize(Editor* editor) {
    editor_ = editor;
}

void Menubar::update(float deltaTime) {
    // Nothing to update
}

void Menubar::draw() {
    if (ImGui::BeginMainMenuBar()) {
        drawFileMenu();
        drawEditMenu();
        drawViewMenu();
        drawGameObjectMenu();
        drawComponentMenu();
        drawWindowMenu();
        drawToolsMenu();
        drawHelpMenu();
        
        // Right-aligned info
        float rightOffset = ImGui::GetWindowWidth() - 200.0f;
        ImGui::SetCursorPosX(rightOffset);
        ImGui::TextDisabled("Sanic Editor v0.1");
        
        ImGui::EndMainMenuBar();
    }
    
    // Modal dialogs
    if (showAbout_) {
        showAboutDialog();
    }
    if (showProjectSettings_) {
        showProjectSettings();
    }
}

void Menubar::drawFileMenu() {
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New Scene", "Ctrl+N")) {
            showNewSceneDialog();
        }
        if (ImGui::MenuItem("Open Scene...", "Ctrl+O")) {
            showOpenSceneDialog();
        }
        
        ImGui::Separator();
        
        if (ImGui::MenuItem("Save", "Ctrl+S")) {
            // TODO: Save current scene
        }
        if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) {
            showSaveSceneDialog();
        }
        
        ImGui::Separator();
        
        if (ImGui::BeginMenu("Recent Scenes")) {
            // TODO: Recent scenes list
            ImGui::MenuItem("(No recent scenes)", nullptr, false, false);
            ImGui::EndMenu();
        }
        
        ImGui::Separator();
        
        if (ImGui::MenuItem("Project Settings...")) {
            showProjectSettings_ = true;
        }
        
        ImGui::Separator();
        
        if (ImGui::MenuItem("Exit", "Alt+F4")) {
            // TODO: Prompt save if dirty
            // glfwSetWindowShouldClose(window, true);
        }
        
        ImGui::EndMenu();
    }
}

void Menubar::drawEditMenu() {
    if (ImGui::BeginMenu("Edit")) {
        auto& undoSystem = editor_->getUndoSystem();
        
        bool canUndo = undoSystem.canUndo();
        bool canRedo = undoSystem.canRedo();
        
        std::string undoLabel = "Undo";
        std::string redoLabel = "Redo";
        
        if (canUndo) {
            undoLabel += " " + undoSystem.getUndoDescription();
        }
        if (canRedo) {
            redoLabel += " " + undoSystem.getRedoDescription();
        }
        
        if (ImGui::MenuItem(undoLabel.c_str(), "Ctrl+Z", false, canUndo)) {
            undoSystem.undo();
        }
        if (ImGui::MenuItem(redoLabel.c_str(), "Ctrl+Y", false, canRedo)) {
            undoSystem.redo();
        }
        
        ImGui::Separator();
        
        if (ImGui::MenuItem("Cut", "Ctrl+X", false, false)) {
            // TODO: Cut selected entities
        }
        if (ImGui::MenuItem("Copy", "Ctrl+C", false, false)) {
            // TODO: Copy selected entities
        }
        if (ImGui::MenuItem("Paste", "Ctrl+V", false, false)) {
            // TODO: Paste entities
        }
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, false)) {
            // TODO: Duplicate selected entities
        }
        if (ImGui::MenuItem("Delete", "Delete", false, false)) {
            // TODO: Delete selected entities
        }
        
        ImGui::Separator();
        
        if (ImGui::MenuItem("Select All", "Ctrl+A")) {
            // TODO: Select all entities
        }
        if (ImGui::MenuItem("Deselect All", "Escape")) {
            editor_->getSelection().clearSelection();
        }
        
        ImGui::Separator();
        
        if (ImGui::MenuItem("Find...", "Ctrl+F")) {
            // TODO: Open find dialog
        }
        
        ImGui::EndMenu();
    }
}

void Menubar::drawViewMenu() {
    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Frame Selected", "F")) {
            // TODO: Frame selection in viewport
        }
        if (ImGui::MenuItem("Frame All", "Shift+F")) {
            // TODO: Frame all objects
        }
        
        ImGui::Separator();
        
        if (ImGui::BeginMenu("Gizmos")) {
            static bool showGrid = true;
            static bool showIcons = true;
            static bool showBounds = false;
            static bool showColliders = true;
            
            ImGui::MenuItem("Grid", nullptr, &showGrid);
            ImGui::MenuItem("Icons", nullptr, &showIcons);
            ImGui::MenuItem("Selection Bounds", nullptr, &showBounds);
            ImGui::MenuItem("Colliders", nullptr, &showColliders);
            
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Camera")) {
            if (ImGui::MenuItem("Perspective")) {}
            if (ImGui::MenuItem("Orthographic")) {}
            ImGui::Separator();
            if (ImGui::MenuItem("Top")) {}
            if (ImGui::MenuItem("Bottom")) {}
            if (ImGui::MenuItem("Front")) {}
            if (ImGui::MenuItem("Back")) {}
            if (ImGui::MenuItem("Left")) {}
            if (ImGui::MenuItem("Right")) {}
            
            ImGui::EndMenu();
        }
        
        ImGui::Separator();
        
        if (ImGui::BeginMenu("Layout")) {
            if (ImGui::MenuItem("Default")) {
                // TODO: Load default layout
            }
            if (ImGui::MenuItem("Wide")) {}
            if (ImGui::MenuItem("Tall")) {}
            ImGui::Separator();
            if (ImGui::MenuItem("Save Layout...")) {}
            if (ImGui::MenuItem("Load Layout...")) {}
            
            ImGui::EndMenu();
        }
        
        ImGui::EndMenu();
    }
}

void Menubar::drawGameObjectMenu() {
    if (ImGui::BeginMenu("GameObject")) {
        if (ImGui::MenuItem("Create Empty", "Ctrl+Shift+N")) {
            // TODO: Create empty entity
        }
        
        ImGui::Separator();
        
        if (ImGui::BeginMenu("3D Object")) {
            if (ImGui::MenuItem("Cube")) {}
            if (ImGui::MenuItem("Sphere")) {}
            if (ImGui::MenuItem("Cylinder")) {}
            if (ImGui::MenuItem("Capsule")) {}
            if (ImGui::MenuItem("Plane")) {}
            if (ImGui::MenuItem("Quad")) {}
            
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Light")) {
            if (ImGui::MenuItem("Directional Light")) {}
            if (ImGui::MenuItem("Point Light")) {}
            if (ImGui::MenuItem("Spot Light")) {}
            if (ImGui::MenuItem("Area Light")) {}
            
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Audio")) {
            if (ImGui::MenuItem("Audio Source")) {}
            if (ImGui::MenuItem("Audio Listener")) {}
            
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Effects")) {
            if (ImGui::MenuItem("Particle System")) {}
            if (ImGui::MenuItem("Trail Renderer")) {}
            
            ImGui::EndMenu();
        }
        
        if (ImGui::MenuItem("Camera")) {
            // TODO: Create camera entity
        }
        
        ImGui::Separator();
        
        if (ImGui::MenuItem("Group Selected", "Ctrl+G")) {
            // TODO: Create parent for selection
        }
        if (ImGui::MenuItem("Ungroup", "Ctrl+Shift+G")) {
            // TODO: Remove parent
        }
        
        ImGui::EndMenu();
    }
}

void Menubar::drawComponentMenu() {
    if (ImGui::BeginMenu("Component")) {
        if (ImGui::BeginMenu("Physics")) {
            if (ImGui::MenuItem("Rigidbody")) {}
            if (ImGui::MenuItem("Box Collider")) {}
            if (ImGui::MenuItem("Sphere Collider")) {}
            if (ImGui::MenuItem("Capsule Collider")) {}
            if (ImGui::MenuItem("Mesh Collider")) {}
            
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Rendering")) {
            if (ImGui::MenuItem("Mesh Renderer")) {}
            if (ImGui::MenuItem("Skinned Mesh Renderer")) {}
            if (ImGui::MenuItem("Sprite Renderer")) {}
            
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Audio")) {
            if (ImGui::MenuItem("Audio Source")) {}
            if (ImGui::MenuItem("Audio Listener")) {}
            
            ImGui::EndMenu();
        }
        
        if (ImGui::MenuItem("Script")) {
            // TODO: Add script component
        }
        
        if (ImGui::MenuItem("Animator")) {
            // TODO: Add animator
        }
        
        ImGui::EndMenu();
    }
}

void Menubar::drawWindowMenu() {
    if (ImGui::BeginMenu("Window")) {
        if (ImGui::MenuItem("Hierarchy", nullptr, true)) {}
        if (ImGui::MenuItem("Inspector", nullptr, true)) {}
        if (ImGui::MenuItem("Scene View", nullptr, true)) {}
        if (ImGui::MenuItem("Game View", nullptr, false)) {}
        if (ImGui::MenuItem("Asset Browser", nullptr, true)) {}
        if (ImGui::MenuItem("Console", nullptr, true)) {}
        
        ImGui::Separator();
        
        if (ImGui::MenuItem("Animation")) {}
        if (ImGui::MenuItem("Animator")) {}
        if (ImGui::MenuItem("Profiler")) {}
        if (ImGui::MenuItem("Audio Mixer")) {}
        
        ImGui::Separator();
        
        if (ImGui::BeginMenu("Rendering")) {
            if (ImGui::MenuItem("Lighting")) {}
            if (ImGui::MenuItem("Light Explorer")) {}
            if (ImGui::MenuItem("Occlusion Culling")) {}
            if (ImGui::MenuItem("Frame Debugger")) {}
            
            ImGui::EndMenu();
        }
        
        ImGui::EndMenu();
    }
}

void Menubar::drawToolsMenu() {
    if (ImGui::BeginMenu("Tools")) {
        if (ImGui::MenuItem("Build Settings...")) {}
        if (ImGui::MenuItem("Player Settings...")) {}
        
        ImGui::Separator();
        
        if (ImGui::MenuItem("Asset Import Settings")) {}
        if (ImGui::MenuItem("Shader Compiler")) {}
        
        ImGui::Separator();
        
        if (ImGui::BeginMenu("Diagnostics")) {
            if (ImGui::MenuItem("Profiler")) {}
            if (ImGui::MenuItem("Memory Profiler")) {}
            if (ImGui::MenuItem("GPU Profiler")) {}
            
            ImGui::EndMenu();
        }
        
        ImGui::EndMenu();
    }
}

void Menubar::drawHelpMenu() {
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("Documentation")) {
            // TODO: Open docs URL
        }
        if (ImGui::MenuItem("API Reference")) {}
        
        ImGui::Separator();
        
        if (ImGui::MenuItem("Check for Updates")) {}
        
        ImGui::Separator();
        
        if (ImGui::MenuItem("About Sanic Engine")) {
            showAbout_ = true;
        }
        
        ImGui::EndMenu();
    }
}

void Menubar::showNewSceneDialog() {
    // TODO: Confirm discard changes, create new scene
}

void Menubar::showOpenSceneDialog() {
    // TODO: NFD file dialog
}

void Menubar::showSaveSceneDialog() {
    // TODO: NFD save dialog
}

void Menubar::showProjectSettings() {
    if (ImGui::Begin("Project Settings", &showProjectSettings_)) {
        if (ImGui::BeginTabBar("SettingsTabs")) {
            if (ImGui::BeginTabItem("General")) {
                static char projectName[256] = "My Project";
                ImGui::InputText("Project Name", projectName, sizeof(projectName));
                
                ImGui::EndTabItem();
            }
            
            if (ImGui::BeginTabItem("Graphics")) {
                static int shadowQuality = 2;
                ImGui::Combo("Shadow Quality", &shadowQuality, "Low\0Medium\0High\0Ultra\0");
                
                static bool rayTracing = true;
                ImGui::Checkbox("Ray Tracing", &rayTracing);
                
                static bool vsync = true;
                ImGui::Checkbox("V-Sync", &vsync);
                
                ImGui::EndTabItem();
            }
            
            if (ImGui::BeginTabItem("Physics")) {
                static float gravity = -9.81f;
                ImGui::DragFloat("Gravity Y", &gravity, 0.1f);
                
                static int solverIterations = 6;
                ImGui::DragInt("Solver Iterations", &solverIterations, 1, 1, 20);
                
                ImGui::EndTabItem();
            }
            
            if (ImGui::BeginTabItem("Audio")) {
                static float masterVolume = 1.0f;
                ImGui::SliderFloat("Master Volume", &masterVolume, 0.0f, 1.0f);
                
                ImGui::EndTabItem();
            }
            
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

void Menubar::showAboutDialog() {
    ImGui::OpenPopup("About Sanic Engine");
    
    if (ImGui::BeginPopupModal("About Sanic Engine", &showAbout_, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Sanic Engine");
        ImGui::Text("Version 0.1.0 (Development)");
        ImGui::Separator();
        ImGui::Text("A Vulkan-based game engine with ray tracing support.");
        ImGui::Text("Built with ImGui, GLM, Jolt Physics, and more.");
        ImGui::Separator();
        ImGui::Text("(c) 2024");
        
        ImGui::Spacing();
        
        if (ImGui::Button("Close", ImVec2(120, 0))) {
            showAbout_ = false;
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::EndPopup();
    }
}

} // namespace Sanic::Editor
