/**
 * Editor.h
 * 
 * Main editor application class for the Sanic Engine.
 * 
 * Features:
 * - ImGui-based docking interface
 * - Panel management system
 * - Mode control (Edit/Play/Pause/Simulate)
 * - Integration with engine systems
 */

#pragma once

#include "EditorWindow.h"
#include "core/Selection.h"
#include "core/UndoSystem.h"
#include "core/Shortcuts.h"
#include "../engine/ECS.h"
#include "../engine/VulkanContext.h"

#include <imgui.h>
#include <memory>
#include <vector>
#include <functional>
#include <string>

// Forward declarations
class Renderer;

namespace Sanic::Editor {

// Editor mode
enum class EditorMode {
    Edit,       // Normal editing
    Play,       // Playing in editor
    Paused,     // Paused during play
    Simulate    // Physics simulation without player
};

// Editor configuration
struct EditorConfig {
    std::string layoutPath = "editor_layout.ini";
    std::string recentProjectsPath = "recent_projects.json";
    
    // Viewport
    float gizmoSize = 100.0f;
    float gridSize = 100.0f;
    float gridStep = 1.0f;
    bool snapToGrid = true;
    float snapTranslate = 1.0f;
    float snapRotate = 15.0f;
    float snapScale = 0.1f;
    
    // Colors
    ImVec4 selectionColor = ImVec4(1.0f, 0.6f, 0.1f, 1.0f);
    ImVec4 gridColor = ImVec4(0.3f, 0.3f, 0.3f, 1.0f);
    ImVec4 xAxisColor = ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
    ImVec4 yAxisColor = ImVec4(0.2f, 1.0f, 0.2f, 1.0f);
    ImVec4 zAxisColor = ImVec4(0.2f, 0.2f, 1.0f, 1.0f);
    
    // Performance
    bool limitEditorFPS = true;
    int editorFPSLimit = 60;
    
    // Theme
    bool darkTheme = true;
};

class Editor {
public:
    Editor();
    ~Editor();
    
    // Lifecycle
    bool initialize(VulkanContext* vulkanContext, Sanic::World* world);
    void shutdown();
    
    // ImGui Vulkan resources
    bool initializeImGui(VkRenderPass renderPass, uint32_t imageCount);
    void shutdownImGui();
    
    // Main loop
    void beginFrame();
    void update(float deltaTime);
    void render(VkCommandBuffer commandBuffer);
    void endFrame();
    
    // Mode control
    void setMode(EditorMode mode);
    EditorMode getMode() const { return mode_; }
    void play();
    void pause();
    void stop();
    bool isPlaying() const { return mode_ == EditorMode::Play || mode_ == EditorMode::Simulate; }
    
    // Panel management
    template<typename T>
    T* getPanel() {
        for (auto& panel : panels_) {
            if (auto* p = dynamic_cast<T*>(panel.get())) {
                return p;
            }
        }
        return nullptr;
    }
    
    void openPanel(const std::string& name);
    void closePanel(const std::string& name);
    bool isPanelOpen(const std::string& name) const;
    
    // Core systems access
    Selection& getSelection() { return *selection_; }
    const Selection& getSelection() const { return *selection_; }
    UndoSystem& getUndoSystem() { return *undoSystem_; }
    ShortcutManager& getShortcuts() { return *shortcuts_; }
    
    // Scene access
    Sanic::World* getWorld() { return world_; }
    const Sanic::World* getWorld() const { return world_; }
    VulkanContext* getVulkanContext() { return vulkanContext_; }
    
    // Configuration
    EditorConfig& getConfig() { return config_; }
    const EditorConfig& getConfig() const { return config_; }
    
    // Notifications
    void showNotification(const std::string& message, float duration = 3.0f);
    void showError(const std::string& message);
    void showWarning(const std::string& message);
    
    // File operations
    void newScene();
    void openScene(const std::string& path = "");
    void saveScene();
    void saveSceneAs();
    
    // Static access
    static Editor* getInstance() { return instance_; }
    
private:
    void setupImGuiStyle();
    void setupDocking();
    void drawMainMenuBar();
    void drawToolbar();
    void drawStatusBar();
    void drawNotifications();
    void handleGlobalShortcuts();
    
    void createDefaultPanels();
    void saveLayout();
    void loadLayout();
    void saveConfig();
    void loadConfig();
    
    static Editor* instance_;
    
    VulkanContext* vulkanContext_ = nullptr;
    Sanic::World* world_ = nullptr;
    
    EditorConfig config_;
    EditorMode mode_ = EditorMode::Edit;
    
    std::unique_ptr<Selection> selection_;
    std::unique_ptr<UndoSystem> undoSystem_;
    std::unique_ptr<ShortcutManager> shortcuts_;
    
    std::vector<std::unique_ptr<EditorWindow>> panels_;
    
    // ImGui Vulkan resources
    VkDescriptorPool imguiDescriptorPool_ = VK_NULL_HANDLE;
    bool imguiInitialized_ = false;
    
    // Notification system
    struct Notification {
        std::string message;
        float timeRemaining;
        enum class Type { Info, Warning, Error } type;
    };
    std::vector<Notification> notifications_;
    
    // Scene state
    std::string currentScenePath_;
    bool sceneDirty_ = false;
    
    // Debug
    bool showDemoWindow_ = false;
    bool showMetricsWindow_ = false;
};

} // namespace Sanic::Editor
