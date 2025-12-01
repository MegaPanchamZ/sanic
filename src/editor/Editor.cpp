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
#include "panels/AssetBrowser.h"
#include "panels/ConsolePanel.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>

// Embedded font data (Inter Regular - a clean, modern UI font)
// This is a subset of Inter containing common ASCII + extended Latin
#include "fonts/InterRegular.h"
#include "fonts/InterBold.h"
#include "fonts/IconsMaterialDesign.h"

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
    
    // Initialize ImGui (only create context if it doesn't already exist)
    if (ImGui::GetCurrentContext() == nullptr) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    }
    
    ImGuiIO& io = ImGui::GetIO();
    
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
    initInfo.MinImageCount = imageCount;
    initInfo.ImageCount = imageCount;
    
    // New API: RenderPass, Subpass, MSAASamples moved to PipelineInfoMain
    initInfo.PipelineInfoMain.RenderPass = renderPass;
    initInfo.PipelineInfoMain.Subpass = 0;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    
    ImGui_ImplVulkan_Init(&initInfo);
    
    // Note: In new ImGui, fonts are uploaded automatically on first render
    
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
    ImGuiIO& io = ImGui::GetIO();
    ImGuiStyle& style = ImGui::GetStyle();
    
    // ============================================
    // FONT SETUP - Anti-aliased, crisp fonts
    // ============================================
    io.Fonts->Clear();
    
    // Font configuration for crisp rendering
    ImFontConfig fontConfig;
    fontConfig.OversampleH = 2;
    fontConfig.OversampleV = 2;
    fontConfig.PixelSnapH = true;
    fontConfig.RasterizerDensity = 1.0f;
    
    // Main UI font - Inter Regular at 15px (good balance of readability and density)
#ifdef SANIC_FONT_INTER_REGULAR
    io.Fonts->AddFontFromMemoryCompressedTTF(
        InterRegular_compressed_data, 
        InterRegular_compressed_size, 
        15.0f, 
        &fontConfig
    );
    
    // Icon font (Material Design Icons) - merge with Inter Regular
#ifdef SANIC_FONT_MDI
    ImFontConfig iconConfig;
    iconConfig.MergeMode = true;
    iconConfig.PixelSnapH = true;
    iconConfig.GlyphMinAdvanceX = 16.0f;
    iconConfig.GlyphOffset = ImVec2(0.0f, 2.0f);  // Slight vertical offset to align with text
    // MDI uses Private Use Area starting at U+F0000
    static const ImWchar iconRanges[] = { ICON_MIN_MDI, ICON_MAX_MDI, 0 };
    io.Fonts->AddFontFromMemoryCompressedTTF(
        MaterialDesignIcons_compressed_data,
        MaterialDesignIcons_compressed_size,
        15.0f,
        &iconConfig,
        iconRanges
    );
#endif

#else
    // Fallback: Load default font with better config
    fontConfig.SizePixels = 15.0f;
    io.Fonts->AddFontDefault(&fontConfig);
#endif

    // Bold font for headers (as a separate font, not merged)
#ifdef SANIC_FONT_INTER_BOLD
    ImFontConfig boldConfig;
    boldConfig.OversampleH = 2;
    boldConfig.OversampleV = 2;
    boldConfig.PixelSnapH = true;
    boldConfig.MergeMode = false;
    io.Fonts->AddFontFromMemoryCompressedTTF(
        InterBold_compressed_data,
        InterBold_compressed_size,
        15.0f,
        &boldConfig
    );
#endif

    // Note: Don't call io.Fonts->Build() manually - new ImGui backends handle this automatically
    
    // ============================================
    // COLOR SCHEME - Modern Dark Theme
    // Inspired by modern IDEs (VS Code Dark+, JetBrains Darcula)
    // ============================================
    
    // Base colors
    const ImVec4 bg_dark       = ImVec4(0.086f, 0.086f, 0.094f, 1.00f);  // #16161a - Very dark background
    const ImVec4 bg_main       = ImVec4(0.110f, 0.114f, 0.129f, 1.00f);  // #1c1d21 - Main window bg
    const ImVec4 bg_light      = ImVec4(0.145f, 0.149f, 0.169f, 1.00f);  // #25262b - Lighter panels
    const ImVec4 bg_lighter    = ImVec4(0.180f, 0.184f, 0.208f, 1.00f);  // #2e2f35 - Hover states
    const ImVec4 border        = ImVec4(0.220f, 0.224f, 0.251f, 1.00f);  // #383940 - Subtle borders
    
    // Text colors  
    const ImVec4 text_primary  = ImVec4(0.925f, 0.937f, 0.957f, 1.00f);  // #eceff4 - Primary text
    const ImVec4 text_secondary= ImVec4(0.600f, 0.620f, 0.680f, 1.00f);  // #999ead - Secondary text
    const ImVec4 text_disabled = ImVec4(0.400f, 0.420f, 0.480f, 1.00f);  // #666b7a - Disabled text
    
    // Accent colors - Vibrant blue with purple tint
    const ImVec4 accent        = ImVec4(0.318f, 0.549f, 0.988f, 1.00f);  // #518cfc - Primary accent
    const ImVec4 accent_hover  = ImVec4(0.420f, 0.620f, 1.000f, 1.00f);  // #6b9eff - Lighter on hover
    const ImVec4 accent_active = ImVec4(0.220f, 0.450f, 0.900f, 1.00f);  // #3873e6 - Darker on click
    const ImVec4 accent_dim    = ImVec4(0.318f, 0.549f, 0.988f, 0.40f);  // Translucent accent
    
    // Success/Warning/Error
    const ImVec4 success       = ImVec4(0.306f, 0.788f, 0.490f, 1.00f);  // #4ec97d - Green
    const ImVec4 warning       = ImVec4(0.988f, 0.729f, 0.263f, 1.00f);  // #fcba43 - Orange/Yellow
    const ImVec4 error         = ImVec4(0.937f, 0.325f, 0.314f, 1.00f);  // #ef5350 - Red
    
    ImVec4* colors = style.Colors;
    
    // Background colors
    colors[ImGuiCol_WindowBg]              = bg_main;
    colors[ImGuiCol_ChildBg]               = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_PopupBg]               = ImVec4(bg_light.x, bg_light.y, bg_light.z, 0.98f);
    colors[ImGuiCol_MenuBarBg]             = bg_dark;
    colors[ImGuiCol_ScrollbarBg]           = ImVec4(0, 0, 0, 0);
    
    // Borders
    colors[ImGuiCol_Border]                = border;
    colors[ImGuiCol_BorderShadow]          = ImVec4(0, 0, 0, 0);
    
    // Frame backgrounds (input fields, checkboxes, etc.)
    colors[ImGuiCol_FrameBg]               = bg_light;
    colors[ImGuiCol_FrameBgHovered]        = bg_lighter;
    colors[ImGuiCol_FrameBgActive]         = ImVec4(bg_lighter.x + 0.05f, bg_lighter.y + 0.05f, bg_lighter.z + 0.05f, 1.0f);
    
    // Title bar
    colors[ImGuiCol_TitleBg]               = bg_dark;
    colors[ImGuiCol_TitleBgActive]         = bg_dark;
    colors[ImGuiCol_TitleBgCollapsed]      = bg_dark;
    
    // Scrollbar
    colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.3f, 0.3f, 0.35f, 1.0f);
    colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.4f, 0.4f, 0.45f, 1.0f);
    colors[ImGuiCol_ScrollbarGrabActive]   = accent;
    
    // Buttons
    colors[ImGuiCol_Button]                = bg_light;
    colors[ImGuiCol_ButtonHovered]         = bg_lighter;
    colors[ImGuiCol_ButtonActive]          = accent_active;
    
    // Headers (collapsing headers, tree nodes, selectable)
    colors[ImGuiCol_Header]                = ImVec4(accent.x, accent.y, accent.z, 0.25f);
    colors[ImGuiCol_HeaderHovered]         = ImVec4(accent.x, accent.y, accent.z, 0.40f);
    colors[ImGuiCol_HeaderActive]          = ImVec4(accent.x, accent.y, accent.z, 0.55f);
    
    // Separators
    colors[ImGuiCol_Separator]             = border;
    colors[ImGuiCol_SeparatorHovered]      = accent;
    colors[ImGuiCol_SeparatorActive]       = accent_active;
    
    // Resize grip
    colors[ImGuiCol_ResizeGrip]            = ImVec4(accent.x, accent.y, accent.z, 0.20f);
    colors[ImGuiCol_ResizeGripHovered]     = ImVec4(accent.x, accent.y, accent.z, 0.60f);
    colors[ImGuiCol_ResizeGripActive]      = accent;
    
    // Tabs
    colors[ImGuiCol_Tab]                   = bg_light;
    colors[ImGuiCol_TabHovered]            = ImVec4(accent.x, accent.y, accent.z, 0.50f);
    colors[ImGuiCol_TabActive]             = ImVec4(accent.x, accent.y, accent.z, 0.70f);
    colors[ImGuiCol_TabUnfocused]          = bg_light;
    colors[ImGuiCol_TabUnfocusedActive]    = bg_lighter;
    colors[ImGuiCol_TabSelectedOverline]   = accent;  // New in ImGui 1.90+
    
    // Docking
    colors[ImGuiCol_DockingPreview]        = ImVec4(accent.x, accent.y, accent.z, 0.70f);
    colors[ImGuiCol_DockingEmptyBg]        = bg_dark;
    
    // Plot
    colors[ImGuiCol_PlotLines]             = accent;
    colors[ImGuiCol_PlotLinesHovered]      = accent_hover;
    colors[ImGuiCol_PlotHistogram]         = accent;
    colors[ImGuiCol_PlotHistogramHovered]  = accent_hover;
    
    // Tables
    colors[ImGuiCol_TableHeaderBg]         = bg_light;
    colors[ImGuiCol_TableBorderStrong]     = border;
    colors[ImGuiCol_TableBorderLight]      = ImVec4(border.x, border.y, border.z, 0.5f);
    colors[ImGuiCol_TableRowBg]            = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_TableRowBgAlt]         = ImVec4(1.0f, 1.0f, 1.0f, 0.02f);
    
    // Text
    colors[ImGuiCol_Text]                  = text_primary;
    colors[ImGuiCol_TextDisabled]          = text_disabled;
    colors[ImGuiCol_TextSelectedBg]        = ImVec4(accent.x, accent.y, accent.z, 0.35f);
    
    // Widgets
    colors[ImGuiCol_CheckMark]             = accent;
    colors[ImGuiCol_SliderGrab]            = accent;
    colors[ImGuiCol_SliderGrabActive]      = accent_hover;
    
    // Nav highlight
    colors[ImGuiCol_NavHighlight]          = accent;
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 1.0f, 1.0f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.2f, 0.2f, 0.2f, 0.20f);
    
    // Modal dim
    colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.0f, 0.0f, 0.0f, 0.60f);
    
    // Drag and drop
    colors[ImGuiCol_DragDropTarget]        = ImVec4(accent.x, accent.y, accent.z, 0.90f);
    
    // ============================================
    // STYLE SETTINGS - Modern, polished look
    // ============================================
    
    // Window
    style.WindowPadding        = ImVec2(12.0f, 12.0f);
    style.WindowRounding       = 8.0f;
    style.WindowBorderSize     = 1.0f;
    style.WindowMinSize        = ImVec2(100.0f, 100.0f);
    style.WindowTitleAlign     = ImVec2(0.0f, 0.5f);
    
    // Frame (inputs, checkboxes, etc.)
    style.FramePadding         = ImVec2(8.0f, 5.0f);
    style.FrameRounding        = 6.0f;
    style.FrameBorderSize      = 0.0f;
    
    // Items
    style.ItemSpacing          = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing     = ImVec2(6.0f, 4.0f);
    style.IndentSpacing        = 20.0f;
    
    // Touch/click
    style.TouchExtraPadding    = ImVec2(0.0f, 0.0f);
    
    // Widgets
    style.CellPadding          = ImVec2(6.0f, 4.0f);
    style.GrabMinSize          = 12.0f;
    style.GrabRounding         = 4.0f;
    
    // Scrollbar
    style.ScrollbarSize        = 12.0f;
    style.ScrollbarRounding    = 6.0f;
    
    // Tabs
    style.TabRounding          = 6.0f;
    style.TabBorderSize        = 0.0f;
    style.TabBarBorderSize     = 1.0f;
    
    // Child/popup
    style.ChildRounding        = 6.0f;
    style.ChildBorderSize      = 0.0f;
    style.PopupRounding        = 8.0f;
    style.PopupBorderSize      = 1.0f;
    
    // Separator
    style.SeparatorTextBorderSize = 2.0f;
    
    // Anti-aliasing
    style.AntiAliasedLines     = true;
    style.AntiAliasedLinesUseTex = true;
    style.AntiAliasedFill      = true;
    
    // Curvature
    style.CircleTessellationMaxError = 0.30f;
    style.CurveTessellationTol = 1.25f;
    
    // Alignment
    style.WindowMenuButtonPosition = ImGuiDir_None;  // Hide the collapse button
    style.ColorButtonPosition  = ImGuiDir_Right;
    style.ButtonTextAlign      = ImVec2(0.5f, 0.5f);
    style.SelectableTextAlign  = ImVec2(0.0f, 0.0f);
    
    // Hover delays
    style.HoverStationaryDelay = 0.15f;
    style.HoverDelayShort      = 0.15f;
    style.HoverDelayNormal     = 0.40f;
    
    // Docking
    style.DockingSeparatorSize = 2.0f;
    
    // ============================================
    // VIEWPORT SPECIFIC ADJUSTMENTS
    // ============================================
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }
}

void Editor::createDefaultPanels() {
    panels_.push_back(std::make_unique<Viewport>());
    panels_.push_back(std::make_unique<HierarchyPanel>());
    panels_.push_back(std::make_unique<InspectorPanel>());
    panels_.push_back(std::make_unique<AssetBrowser>());
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
    
    // Initialize ImGuizmo for this frame
    ImGuizmo::BeginFrame();
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
    
    // Setup default layout on first run
    if (firstRun_) {
        setupDefaultDockLayout();
        firstRun_ = false;
    }
    
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
    
    float toolbarHeight = 44.0f;
    
    // Toolbar styling
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 4));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.086f, 0.086f, 0.094f, 1.0f));  // Match menu bar
    
    if (ImGui::BeginViewportSideBar("##Toolbar", ImGui::GetMainViewport(), ImGuiDir_Up, toolbarHeight, toolbarFlags)) {
        // Center alignment helper
        float buttonSize = 34.0f;
        float totalWidth = buttonSize * 2 + 8.0f;  // 2 buttons + spacing
        float centerX = (ImGui::GetWindowWidth() - totalWidth) * 0.5f;
        
        // Left side: Transform mode buttons could go here
        // (Leaving space for future gizmo mode toggles)
        
        // Center: Play/Pause/Stop controls
        ImGui::SetCursorPosX(centerX);
        ImGui::SetCursorPosY((toolbarHeight - buttonSize) * 0.5f);
        
        bool isPlaying = mode_ == EditorMode::Play || mode_ == EditorMode::Simulate;
        bool isPaused = mode_ == EditorMode::Paused;
        
        // Play/Pause button with accent color when active
        if (isPlaying) {
            // Green tint when playing
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.45f, 0.25f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.55f, 0.30f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.40f, 0.20f, 1.0f));
        } else if (isPaused) {
            // Yellow tint when paused
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.50f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.65f, 0.60f, 0.20f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.50f, 0.45f, 0.12f, 1.0f));
        }
        
        // Use unicode symbols for cleaner look
        const char* playLabel = isPlaying ? "\xE2\x8F\xB8" : "\xE2\x96\xB6";  // ⏸ or ▶
        if (ImGui::Button(playLabel, ImVec2(buttonSize, buttonSize))) {
            if (isPlaying) pause();
            else play();
        }
        
        if (isPlaying || isPaused) {
            ImGui::PopStyleColor(3);
        }
        
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(isPlaying ? "Pause (Ctrl+P)" : isPaused ? "Resume (Ctrl+P)" : "Play (Ctrl+P)");
        }
        
        ImGui::SameLine(0, 4);
        
        // Stop button
        bool canStop = isPlaying || isPaused;
        if (!canStop) {
            ImGui::BeginDisabled();
        }
        
        const char* stopLabel = "\xE2\x96\xA0";  // ■
        if (ImGui::Button(stopLabel, ImVec2(buttonSize, buttonSize))) {
            stop();
        }
        
        if (!canStop) {
            ImGui::EndDisabled();
        }
        if (ImGui::IsItemHovered() && canStop) {
            ImGui::SetTooltip("Stop");
        }
        
        // Right side: Could add scene selection, etc.
        
        ImGui::End();
    }
    
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

void Editor::drawStatusBar() {
    ImGuiWindowFlags statusFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;
    
    float statusHeight = 26.0f;
    
    // Status bar styling - subtle background
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 5));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.086f, 0.086f, 0.094f, 1.0f));
    
    if (ImGui::BeginViewportSideBar("##StatusBar", ImGui::GetMainViewport(), ImGuiDir_Down, statusHeight, statusFlags)) {
        // Mode indicator with colored badge
        const char* modeStr = "EDIT";
        ImVec4 modeColor = ImVec4(0.6f, 0.6f, 0.65f, 1.0f);
        ImVec4 modeBgColor = ImVec4(0.15f, 0.15f, 0.17f, 1.0f);
        
        switch (mode_) {
            case EditorMode::Edit:
                modeStr = "EDIT";
                break;
            case EditorMode::Play:
                modeStr = "PLAYING";
                modeColor = ImVec4(0.30f, 0.85f, 0.45f, 1.0f);
                modeBgColor = ImVec4(0.10f, 0.30f, 0.15f, 1.0f);
                break;
            case EditorMode::Paused:
                modeStr = "PAUSED";
                modeColor = ImVec4(0.95f, 0.80f, 0.30f, 1.0f);
                modeBgColor = ImVec4(0.30f, 0.25f, 0.10f, 1.0f);
                break;
            case EditorMode::Simulate:
                modeStr = "SIMULATE";
                modeColor = ImVec4(0.35f, 0.70f, 0.95f, 1.0f);
                modeBgColor = ImVec4(0.10f, 0.20f, 0.30f, 1.0f);
                break;
        }
        
        // Draw mode badge
        ImVec2 textSize = ImGui::CalcTextSize(modeStr);
        ImVec2 cursorPos = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        
        float badgePadX = 6.0f;
        float badgePadY = 2.0f;
        float badgeRounding = 3.0f;
        
        drawList->AddRectFilled(
            ImVec2(cursorPos.x - badgePadX, cursorPos.y - badgePadY),
            ImVec2(cursorPos.x + textSize.x + badgePadX, cursorPos.y + textSize.y + badgePadY),
            ImGui::ColorConvertFloat4ToU32(modeBgColor),
            badgeRounding
        );
        
        ImGui::TextColored(modeColor, "%s", modeStr);
        
        ImGui::SameLine(0, 16);
        
        // Separator
        ImGui::TextColored(ImVec4(0.3f, 0.3f, 0.35f, 1.0f), "|");
        ImGui::SameLine(0, 16);
        
        // Selection info
        size_t selCount = selection_->getSelectionCount();
        if (selCount > 0) {
            ImGui::TextColored(ImVec4(0.75f, 0.78f, 0.82f, 1.0f), "%zu selected", selCount);
        } else {
            ImGui::TextColored(ImVec4(0.45f, 0.47f, 0.52f, 1.0f), "No selection");
        }
        
        // Right-aligned stats
        float rightPadding = 16.0f;
        char fpsText[32];
        snprintf(fpsText, sizeof(fpsText), "%.0f FPS", ImGui::GetIO().Framerate);
        float fpsTextWidth = ImGui::CalcTextSize(fpsText).x;
        
        ImGui::SameLine(ImGui::GetWindowWidth() - fpsTextWidth - rightPadding);
        
        // Color code FPS
        float fps = ImGui::GetIO().Framerate;
        ImVec4 fpsColor;
        if (fps >= 55.0f) {
            fpsColor = ImVec4(0.30f, 0.85f, 0.45f, 1.0f);  // Green - good
        } else if (fps >= 30.0f) {
            fpsColor = ImVec4(0.95f, 0.80f, 0.30f, 1.0f);  // Yellow - okay
        } else {
            fpsColor = ImVec4(0.95f, 0.35f, 0.30f, 1.0f);  // Red - bad
        }
        ImGui::TextColored(fpsColor, "%s", fpsText);
        
        ImGui::End();
    }
    
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

void Editor::drawNotifications() {
    if (notifications_.empty()) return;
    
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    float padding = 20.0f;
    float yOffset = 56.0f;  // Below toolbar
    
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - padding, 
                                    viewport->WorkPos.y + yOffset), 
                            ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
    
    // Toast-style notifications with subtle shadow effect
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 10));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.12f, 0.14f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.25f, 0.25f, 0.28f, 0.8f));
    
    if (ImGui::Begin("##Notifications", nullptr, flags)) {
        for (size_t i = 0; i < notifications_.size(); ++i) {
            const auto& notif = notifications_[i];
            
            // Icon and colors based on notification type
            const char* icon = "";
            ImVec4 iconColor;
            ImVec4 textColor;
            
            switch (notif.type) {
                case Notification::Type::Info:
                    icon = "\xE2\x84\xB9";  // ℹ
                    iconColor = ImVec4(0.35f, 0.60f, 0.95f, 1.0f);
                    textColor = ImVec4(0.85f, 0.87f, 0.90f, 1.0f);
                    break;
                case Notification::Type::Warning:
                    icon = "\xE2\x9A\xA0";  // ⚠
                    iconColor = ImVec4(0.95f, 0.75f, 0.25f, 1.0f);
                    textColor = ImVec4(0.95f, 0.85f, 0.65f, 1.0f);
                    break;
                case Notification::Type::Error:
                    icon = "\xE2\x9C\x96";  // ✖
                    iconColor = ImVec4(0.95f, 0.35f, 0.35f, 1.0f);
                    textColor = ImVec4(0.95f, 0.70f, 0.70f, 1.0f);
                    break;
            }
            
            // Fade out effect (starts fading at 1 second remaining)
            float alpha = std::min(notif.timeRemaining, 1.0f);
            iconColor.w = alpha;
            textColor.w = alpha;
            
            // Draw icon
            ImGui::TextColored(iconColor, "%s", icon);
            ImGui::SameLine(0, 8);
            
            // Draw message
            ImGui::TextColored(textColor, "%s", notif.message.c_str());
            
            if (i < notifications_.size() - 1) {
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.3f, 0.3f, 0.35f, 0.5f * alpha));
                ImGui::Separator();
                ImGui::PopStyleColor();
                ImGui::Spacing();
            }
        }
    }
    ImGui::End();
    
    ImGui::PopStyleColor(2);
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
    // Check if layout file exists
    std::ifstream file(config_.layoutPath);
    if (!file.good()) {
        // No saved layout, we'll create a default one in setupDocking on first run
        firstRun_ = true;
    }
    // ImGui handles loading automatically if the file exists
}

void Editor::setupDefaultDockLayout() {
    // Create default dock layout for first run
    ImGuiID dockspaceId = ImGui::GetID("MainDockSpace");
    
    ImGui::DockBuilderRemoveNode(dockspaceId); // Clear existing layout
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);
    
    // Split the dockspace
    ImGuiID dockMainId = dockspaceId;
    ImGuiID dockLeftId = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Left, 0.2f, nullptr, &dockMainId);
    ImGuiID dockRightId = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Right, 0.25f, nullptr, &dockMainId);
    ImGuiID dockBottomId = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Down, 0.25f, nullptr, &dockMainId);
    
    // Dock windows
    ImGui::DockBuilderDockWindow("Hierarchy", dockLeftId);
    ImGui::DockBuilderDockWindow("Inspector", dockRightId);
    ImGui::DockBuilderDockWindow("Console", dockBottomId);
    ImGui::DockBuilderDockWindow("Asset Browser", dockBottomId);
    ImGui::DockBuilderDockWindow("Viewport", dockMainId);
    
    ImGui::DockBuilderFinish(dockspaceId);
}

void Editor::saveConfig() {
    // TODO: Save editor config to JSON
}

void Editor::loadConfig() {
    // TODO: Load editor config from JSON
}

} // namespace Sanic::Editor
