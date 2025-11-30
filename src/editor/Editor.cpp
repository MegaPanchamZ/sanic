/**
 * Editor.cpp
 * 
 * Implementation of the main editor application class.
 */

#include "Editor.h"
#include "EditorLayout.h"
#include "viewport/Viewport.h"
#include "panels/HierarchyPanel.h"
#include "panels/InspectorPanel.h"
#include "panels/AssetBrowserPanel.h"
#include "panels/ConsolePanel.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <imgui_internal.h>

#include <fstream>
#include <algorithm>

namespace Sanic::Editor {

Editor* Editor::instance_ = nullptr;

Editor::Editor() {
    instance_ = this;
}

Editor::~Editor() {
    if (instance_ == this) {
        instance_ = nullptr;
    }
}

bool Editor::initialize(VulkanContext* vulkanContext, Sanic::World* world) {
    vulkanContext_ = vulkanContext;
    world_ = world;
    
    // Create core systems
    selection_ = std::make_unique<Selection>();
    undoSystem_ = std::make_unique<UndoSystem>();
    shortcuts_ = std::make_unique<ShortcutManager>();
    
    // Load configuration
    loadConfig();
    
    // Register default shortcuts
    shortcuts_->registerShortcut("Undo", {GLFW_KEY_Z, GLFW_MOD_CONTROL}, [this]() {
        if (undoSystem_->canUndo()) undoSystem_->undo();
    });
    shortcuts_->registerShortcut("Redo", {GLFW_KEY_Y, GLFW_MOD_CONTROL}, [this]() {
        if (undoSystem_->canRedo()) undoSystem_->redo();
    });
    shortcuts_->registerShortcut("Redo2", {GLFW_KEY_Z, GLFW_MOD_CONTROL | GLFW_MOD_SHIFT}, [this]() {
        if (undoSystem_->canRedo()) undoSystem_->redo();
    });
    shortcuts_->registerShortcut("Save", {GLFW_KEY_S, GLFW_MOD_CONTROL}, [this]() {
        saveScene();
    });
    shortcuts_->registerShortcut("SaveAs", {GLFW_KEY_S, GLFW_MOD_CONTROL | GLFW_MOD_SHIFT}, [this]() {
        saveSceneAs();
    });
    shortcuts_->registerShortcut("Open", {GLFW_KEY_O, GLFW_MOD_CONTROL}, [this]() {
        openScene();
    });
    shortcuts_->registerShortcut("New", {GLFW_KEY_N, GLFW_MOD_CONTROL}, [this]() {
        newScene();
    });
    shortcuts_->registerShortcut("Delete", {GLFW_KEY_DELETE, 0}, [this]() {
        auto& sel = getSelection();
        if (sel.hasSelection()) {
            for (Entity e : sel.getSelection()) {
                if (world_) world_->destroyEntity(e);
            }
            sel.clearSelection();
        }
    });
    shortcuts_->registerShortcut("Duplicate", {GLFW_KEY_D, GLFW_MOD_CONTROL}, [this]() {
        // Duplicate selected entities
        auto& sel = getSelection();
        if (sel.hasSelection() && world_) {
            std::vector<Entity> newEntities;
            for (Entity e : sel.getSelection()) {
                Entity newEntity = world_->instantiate(e);
                newEntities.push_back(newEntity);
            }
            sel.clearSelection();
            for (Entity e : newEntities) {
                sel.addToSelection(e);
            }
        }
    });
    shortcuts_->registerShortcut("SelectAll", {GLFW_KEY_A, GLFW_MOD_CONTROL}, [this]() {
        if (world_) selection_->selectAll(*world_);
    });
    shortcuts_->registerShortcut("Play", {GLFW_KEY_P, GLFW_MOD_CONTROL}, [this]() {
        if (mode_ == EditorMode::Edit) play();
        else stop();
    });
    
    return true;
}

void Editor::shutdown() {
    saveConfig();
    saveLayout();
    
    panels_.clear();
    shortcuts_.reset();
    undoSystem_.reset();
    selection_.reset();
}

bool Editor::initializeImGui(VkRenderPass renderPass, uint32_t imageCount) {
    if (imguiInitialized_) return true;
    
    VkDevice device = vulkanContext_->getDevice();
    
    // Create descriptor pool for ImGui
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };
    
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1000;
    poolInfo.poolSizeCount = std::size(poolSizes);
    poolInfo.pPoolSizes = poolSizes;
    
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &imguiDescriptorPool_) != VK_SUCCESS) {
        return false;
    }
    
    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    
    // Setup style
    setupImGuiStyle();
    
    // When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }
    
    // Initialize platform/renderer backends
    // Note: Caller should handle GLFW initialization
    
    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance = vulkanContext_->getInstance();
    initInfo.PhysicalDevice = vulkanContext_->getPhysicalDevice();
    initInfo.Device = device;
    initInfo.QueueFamily = vulkanContext_->getGraphicsQueueFamily();
    initInfo.Queue = vulkanContext_->getGraphicsQueue();
    initInfo.PipelineCache = VK_NULL_HANDLE;
    initInfo.DescriptorPool = imguiDescriptorPool_;
    initInfo.Subpass = 0;
    initInfo.MinImageCount = imageCount;
    initInfo.ImageCount = imageCount;
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.RenderPass = renderPass;
    
    ImGui_ImplVulkan_Init(&initInfo);
    
    // Upload fonts
    ImGui_ImplVulkan_CreateFontsTexture();
    
    // Create default panels
    createDefaultPanels();
    
    // Load layout
    loadLayout();
    
    imguiInitialized_ = true;
    return true;
}

void Editor::shutdownImGui() {
    if (!imguiInitialized_) return;
    
    VkDevice device = vulkanContext_->getDevice();
    vkDeviceWaitIdle(device);
    
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    
    if (imguiDescriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, imguiDescriptorPool_, nullptr);
        imguiDescriptorPool_ = VK_NULL_HANDLE;
    }
    
    imguiInitialized_ = false;
}

void Editor::setupImGuiStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    
    if (config_.darkTheme) {
        ImGui::StyleColorsDark();
        
        // Custom dark theme adjustments
        ImVec4* colors = style.Colors;
        colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
        colors[ImGuiCol_Header] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
        colors[ImGuiCol_Button] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.15f, 0.40f, 0.70f, 1.00f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
        colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
        colors[ImGuiCol_TabHovered] = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
        colors[ImGuiCol_TabActive] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
        colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
        colors[ImGuiCol_MenuBarBg] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
        colors[ImGuiCol_DockingPreview] = ImVec4(0.15f, 0.40f, 0.70f, 0.70f);
    } else {
        ImGui::StyleColorsLight();
    }
    
    // Common style settings
    style.WindowRounding = 4.0f;
    style.FrameRounding = 2.0f;
    style.GrabRounding = 2.0f;
    style.TabRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.WindowPadding = ImVec2(8, 8);
    style.FramePadding = ImVec2(4, 3);
    style.ItemSpacing = ImVec2(8, 4);
    style.ItemInnerSpacing = ImVec2(4, 4);
    style.IndentSpacing = 20.0f;
    style.ScrollbarSize = 14.0f;
    style.GrabMinSize = 10.0f;
}

void Editor::createDefaultPanels() {
    panels_.push_back(std::make_unique<Viewport>());
    panels_.push_back(std::make_unique<HierarchyPanel>());
    panels_.push_back(std::make_unique<InspectorPanel>());
    panels_.push_back(std::make_unique<AssetBrowserPanel>());
    panels_.push_back(std::make_unique<ConsolePanel>());
    
    // Initialize all panels
    for (auto& panel : panels_) {
        panel->initialize(this);
    }
}

void Editor::beginFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void Editor::update(float deltaTime) {
    // Handle global shortcuts
    handleGlobalShortcuts();
    
    // Setup docking
    setupDocking();
    
    // Draw main menu bar
    drawMainMenuBar();
    
    // Draw toolbar
    drawToolbar();
    
    // Update and draw all panels
    for (auto& panel : panels_) {
        if (panel->isVisible()) {
            panel->update(deltaTime);
            panel->draw();
        }
    }
    
    // Draw status bar
    drawStatusBar();
    
    // Draw notifications
    drawNotifications();
    
    // Update notification timers
    notifications_.erase(
        std::remove_if(notifications_.begin(), notifications_.end(),
            [deltaTime](Notification& n) {
                n.timeRemaining -= deltaTime;
                return n.timeRemaining <= 0.0f;
            }),
        notifications_.end()
    );
    
    // Debug windows
    if (showDemoWindow_) {
        ImGui::ShowDemoWindow(&showDemoWindow_);
    }
    if (showMetricsWindow_) {
        ImGui::ShowMetricsWindow(&showMetricsWindow_);
    }
}

void Editor::render(VkCommandBuffer commandBuffer) {
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
}

void Editor::endFrame() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }
}

void Editor::setupDocking() {
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
    
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    
    windowFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    windowFlags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    windowFlags |= ImGuiWindowFlags_NoBackground;
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    
    ImGui::Begin("DockSpace", nullptr, windowFlags);
    ImGui::PopStyleVar(3);
    
    ImGuiID dockspaceId = ImGui::GetID("MainDockSpace");
    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
    
    ImGui::End();
}

void Editor::drawMainMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Scene", "Ctrl+N")) {
                newScene();
            }
            if (ImGui::MenuItem("Open Scene...", "Ctrl+O")) {
                openScene();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save", "Ctrl+S")) {
                saveScene();
            }
            if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) {
                saveSceneAs();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) {
                // Request exit
            }
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, undoSystem_->canUndo())) {
                undoSystem_->undo();
            }
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, undoSystem_->canRedo())) {
                undoSystem_->redo();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Cut", "Ctrl+X", false, selection_->hasSelection())) {
                // Cut
            }
            if (ImGui::MenuItem("Copy", "Ctrl+C", false, selection_->hasSelection())) {
                // Copy
            }
            if (ImGui::MenuItem("Paste", "Ctrl+V")) {
                // Paste
            }
            if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, selection_->hasSelection())) {
                shortcuts_->triggerShortcut("Duplicate");
            }
            if (ImGui::MenuItem("Delete", "Del", false, selection_->hasSelection())) {
                shortcuts_->triggerShortcut("Delete");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Select All", "Ctrl+A")) {
                shortcuts_->triggerShortcut("SelectAll");
            }
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("View")) {
            for (auto& panel : panels_) {
                bool visible = panel->isVisible();
                if (ImGui::MenuItem(panel->getName(), nullptr, &visible)) {
                    panel->setVisible(visible);
                }
            }
            ImGui::Separator();
            ImGui::MenuItem("ImGui Demo", nullptr, &showDemoWindow_);
            ImGui::MenuItem("ImGui Metrics", nullptr, &showMetricsWindow_);
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Window")) {
            if (ImGui::MenuItem("Reset Layout")) {
                // Reset to default layout
            }
            if (ImGui::MenuItem("Save Layout")) {
                saveLayout();
            }
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About Sanic Editor")) {
                // Show about dialog
            }
            ImGui::EndMenu();
        }
        
        ImGui::EndMainMenuBar();
    }
}

void Editor::drawToolbar() {
    ImGuiWindowFlags toolbarFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;
    
    float toolbarHeight = 40.0f;
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
    
    if (ImGui::BeginViewportSideBar("##Toolbar", ImGui::GetMainViewport(), ImGuiDir_Up, toolbarHeight, toolbarFlags)) {
        // Transform tools
        ImGui::SameLine();
        
        // Play/Pause/Stop buttons (centered)
        float buttonSize = 32.0f;
        float centerX = ImGui::GetWindowWidth() / 2.0f - (buttonSize * 3 + 8) / 2.0f;
        ImGui::SetCursorPosX(centerX);
        
        bool isPlaying = mode_ == EditorMode::Play || mode_ == EditorMode::Simulate;
        bool isPaused = mode_ == EditorMode::Paused;
        
        // Play button
        if (isPlaying) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
        }
        if (ImGui::Button(isPlaying ? "||" : ">", ImVec2(buttonSize, buttonSize))) {
            if (isPlaying) pause();
            else play();
        }
        if (isPlaying) {
            ImGui::PopStyleColor();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(isPlaying ? "Pause" : "Play");
        }
        
        ImGui::SameLine();
        
        // Stop button
        bool canStop = isPlaying || isPaused;
        if (!canStop) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("[]", ImVec2(buttonSize, buttonSize))) {
            stop();
        }
        if (!canStop) {
            ImGui::EndDisabled();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Stop");
        }
        
        ImGui::End();
    }
    
    ImGui::PopStyleVar(2);
}

void Editor::drawStatusBar() {
    ImGuiWindowFlags statusFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;
    
    float statusHeight = 24.0f;
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));
    
    if (ImGui::BeginViewportSideBar("##StatusBar", ImGui::GetMainViewport(), ImGuiDir_Down, statusHeight, statusFlags)) {
        // Mode indicator
        const char* modeStr = "Edit";
        ImVec4 modeColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
        switch (mode_) {
            case EditorMode::Edit:
                modeStr = "Edit";
                break;
            case EditorMode::Play:
                modeStr = "Playing";
                modeColor = ImVec4(0.2f, 0.8f, 0.2f, 1.0f);
                break;
            case EditorMode::Paused:
                modeStr = "Paused";
                modeColor = ImVec4(0.8f, 0.8f, 0.2f, 1.0f);
                break;
            case EditorMode::Simulate:
                modeStr = "Simulating";
                modeColor = ImVec4(0.2f, 0.6f, 0.8f, 1.0f);
                break;
        }
        ImGui::TextColored(modeColor, "[%s]", modeStr);
        
        ImGui::SameLine();
        ImGui::Separator();
        ImGui::SameLine();
        
        // Selection info
        size_t selCount = selection_->getSelectionCount();
        if (selCount > 0) {
            ImGui::Text("%zu object(s) selected", selCount);
        } else {
            ImGui::TextDisabled("No selection");
        }
        
        // Right-aligned info
        ImGui::SameLine(ImGui::GetWindowWidth() - 200);
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        
        ImGui::End();
    }
    
    ImGui::PopStyleVar();
}

void Editor::drawNotifications() {
    if (notifications_.empty()) return;
    
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    float padding = 16.0f;
    float yOffset = 50.0f;  // Below toolbar
    
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - padding, 
                                    viewport->WorkPos.y + yOffset), 
                            ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 8));
    
    if (ImGui::Begin("##Notifications", nullptr, flags)) {
        for (size_t i = 0; i < notifications_.size(); ++i) {
            const auto& notif = notifications_[i];
            
            ImVec4 color;
            switch (notif.type) {
                case Notification::Type::Info:
                    color = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
                    break;
                case Notification::Type::Warning:
                    color = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
                    break;
                case Notification::Type::Error:
                    color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
                    break;
            }
            
            // Fade out effect
            float alpha = std::min(notif.timeRemaining, 1.0f);
            color.w = alpha;
            
            ImGui::TextColored(color, "%s", notif.message.c_str());
            
            if (i < notifications_.size() - 1) {
                ImGui::Separator();
            }
        }
    }
    ImGui::End();
    
    ImGui::PopStyleVar(2);
}

void Editor::handleGlobalShortcuts() {
    if (!ImGui::GetIO().WantCaptureKeyboard) {
        shortcuts_->update();
    }
}

void Editor::setMode(EditorMode mode) {
    if (mode_ == mode) return;
    
    EditorMode oldMode = mode_;
    mode_ = mode;
    
    // Notify panels of mode change
    for (auto& panel : panels_) {
        panel->onModeChanged(oldMode, mode);
    }
    
    switch (mode) {
        case EditorMode::Edit:
            showNotification("Stopped");
            break;
        case EditorMode::Play:
            showNotification("Playing");
            break;
        case EditorMode::Paused:
            showNotification("Paused");
            break;
        case EditorMode::Simulate:
            showNotification("Simulating");
            break;
    }
}

void Editor::play() {
    if (mode_ == EditorMode::Edit) {
        // Save scene state for restoration
        setMode(EditorMode::Play);
    } else if (mode_ == EditorMode::Paused) {
        setMode(EditorMode::Play);
    }
}

void Editor::pause() {
    if (mode_ == EditorMode::Play) {
        setMode(EditorMode::Paused);
    }
}

void Editor::stop() {
    if (mode_ != EditorMode::Edit) {
        // Restore scene state
        setMode(EditorMode::Edit);
    }
}

void Editor::openPanel(const std::string& name) {
    for (auto& panel : panels_) {
        if (panel->getName() == name) {
            panel->setVisible(true);
            return;
        }
    }
}

void Editor::closePanel(const std::string& name) {
    for (auto& panel : panels_) {
        if (panel->getName() == name) {
            panel->setVisible(false);
            return;
        }
    }
}

bool Editor::isPanelOpen(const std::string& name) const {
    for (const auto& panel : panels_) {
        if (panel->getName() == name) {
            return panel->isVisible();
        }
    }
    return false;
}

void Editor::showNotification(const std::string& message, float duration) {
    notifications_.push_back({message, duration, Notification::Type::Info});
}

void Editor::showError(const std::string& message) {
    notifications_.push_back({message, 5.0f, Notification::Type::Error});
}

void Editor::showWarning(const std::string& message) {
    notifications_.push_back({message, 4.0f, Notification::Type::Warning});
}

void Editor::newScene() {
    if (sceneDirty_) {
        // TODO: Ask to save
    }
    
    if (world_) {
        world_->clear();
    }
    selection_->clearSelection();
    undoSystem_->clear();
    
    currentScenePath_.clear();
    sceneDirty_ = false;
    
    showNotification("New scene created");
}

void Editor::openScene(const std::string& path) {
    std::string scenePath = path;
    
    if (scenePath.empty()) {
        // TODO: Show file dialog
        return;
    }
    
    // TODO: Load scene
    currentScenePath_ = scenePath;
    sceneDirty_ = false;
    
    showNotification("Scene loaded: " + scenePath);
}

void Editor::saveScene() {
    if (currentScenePath_.empty()) {
        saveSceneAs();
        return;
    }
    
    // TODO: Save scene
    sceneDirty_ = false;
    undoSystem_->markClean();
    
    showNotification("Scene saved");
}

void Editor::saveSceneAs() {
    // TODO: Show file dialog
}

void Editor::saveLayout() {
    ImGui::SaveIniSettingsToDisk(config_.layoutPath.c_str());
}

void Editor::loadLayout() {
    // ImGui handles this automatically if the file exists
}

void Editor::saveConfig() {
    // TODO: Save editor config to JSON
}

void Editor::loadConfig() {
    // TODO: Load editor config from JSON
}

} // namespace Sanic::Editor
