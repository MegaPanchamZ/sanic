/**
 * UISystem.h
 * 
 * UI rendering system for both game HUD and editor tools.
 * 
 * Features:
 * - Immediate mode debug UI (Dear ImGui wrapper)
 * - Retained mode game UI (custom widget system)
 * - Text rendering with font atlases
 * - UI batching for efficient rendering
 * - Input focus management
 * - Scalable UI for different resolutions
 */

#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>

class VulkanContext;

namespace Sanic {

// ============================================================================
// UI STYLING
// ============================================================================

struct UIColor {
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
    
    UIColor() = default;
    UIColor(float r, float g, float b, float a = 1.0f) : r(r), g(g), b(b), a(a) {}
    
    static UIColor fromHex(uint32_t hex) {
        return UIColor(
            ((hex >> 16) & 0xFF) / 255.0f,
            ((hex >> 8) & 0xFF) / 255.0f,
            (hex & 0xFF) / 255.0f,
            1.0f
        );
    }
    
    glm::vec4 toVec4() const { return glm::vec4(r, g, b, a); }
};

struct UIStyle {
    // Colors
    UIColor primary = UIColor::fromHex(0x3498db);
    UIColor secondary = UIColor::fromHex(0x2ecc71);
    UIColor background = UIColor(0.1f, 0.1f, 0.12f, 0.95f);
    UIColor backgroundAlt = UIColor(0.15f, 0.15f, 0.18f, 0.95f);
    UIColor text = UIColor(0.95f, 0.95f, 0.95f, 1.0f);
    UIColor textDisabled = UIColor(0.5f, 0.5f, 0.5f, 1.0f);
    UIColor border = UIColor(0.3f, 0.3f, 0.35f, 1.0f);
    UIColor highlight = UIColor::fromHex(0xe74c3c);
    UIColor shadow = UIColor(0.0f, 0.0f, 0.0f, 0.5f);
    
    // Sizes
    float fontSize = 14.0f;
    float padding = 8.0f;
    float margin = 4.0f;
    float borderRadius = 4.0f;
    float borderWidth = 1.0f;
    float scrollbarWidth = 12.0f;
    
    // Animation
    float hoverTransitionTime = 0.1f;
    float clickTransitionTime = 0.05f;
};

// ============================================================================
// UI RECT & LAYOUT
// ============================================================================

struct UIRect {
    float x = 0, y = 0;
    float width = 100, height = 20;
    
    bool contains(float px, float py) const {
        return px >= x && px <= x + width && py >= y && py <= y + height;
    }
    
    UIRect shrink(float amount) const {
        return {x + amount, y + amount, width - amount * 2, height - amount * 2};
    }
    
    UIRect expand(float amount) const {
        return {x - amount, y - amount, width + amount * 2, height + amount * 2};
    }
};

enum class UILayoutDirection {
    Vertical,
    Horizontal
};

enum class UIAlign {
    Start,
    Center,
    End,
    Stretch
};

struct UILayoutState {
    UIRect bounds;
    UILayoutDirection direction = UILayoutDirection::Vertical;
    UIAlign align = UIAlign::Start;
    float cursor = 0.0f;
    float maxSecondary = 0.0f;  // For calculating container size
};

// ============================================================================
// FONT SYSTEM
// ============================================================================

struct UIGlyph {
    uint32_t codepoint;
    float x0, y0, x1, y1;       // Texture coordinates (normalized)
    float xOffset, yOffset;     // Glyph offset from baseline
    float advance;              // Horizontal advance
    float width, height;        // Glyph size in pixels
};

class UIFont {
public:
    UIFont() = default;
    ~UIFont();
    
    bool loadFromFile(const std::string& path, float size, VulkanContext& context);
    bool loadDefault(float size, VulkanContext& context);
    
    const UIGlyph* getGlyph(uint32_t codepoint) const;
    float getKerning(uint32_t left, uint32_t right) const;
    
    float getLineHeight() const { return lineHeight_; }
    float getAscent() const { return ascent_; }
    float getDescent() const { return descent_; }
    
    VkImageView getAtlasView() const { return atlasView_; }
    VkSampler getAtlasSampler() const { return atlasSampler_; }
    
    glm::vec2 measureText(const std::string& text) const;
    
private:
    std::unordered_map<uint32_t, UIGlyph> glyphs_;
    std::unordered_map<uint64_t, float> kerning_;  // (left << 32 | right) -> kerning
    
    float size_ = 14.0f;
    float lineHeight_ = 16.0f;
    float ascent_ = 12.0f;
    float descent_ = 4.0f;
    
    VkImage atlasImage_ = VK_NULL_HANDLE;
    VkDeviceMemory atlasMemory_ = VK_NULL_HANDLE;
    VkImageView atlasView_ = VK_NULL_HANDLE;
    VkSampler atlasSampler_ = VK_NULL_HANDLE;
    
    VulkanContext* context_ = nullptr;
};

// ============================================================================
// UI VERTEX & DRAW DATA
// ============================================================================

struct UIVertex {
    glm::vec2 position;
    glm::vec2 texCoord;
    uint32_t color;  // Packed RGBA
};

struct UIDrawCommand {
    uint32_t vertexOffset;
    uint32_t indexOffset;
    uint32_t indexCount;
    VkImageView texture;  // Font atlas or custom texture
    UIRect clipRect;
};

struct UIDrawList {
    std::vector<UIVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<UIDrawCommand> commands;
    
    void clear() {
        vertices.clear();
        indices.clear();
        commands.clear();
    }
    
    void addRect(const UIRect& rect, UIColor color, float cornerRadius = 0.0f);
    void addRect(float x, float y, float w, float h, UIColor color, float cornerRadius = 0.0f);
    void addRectOutline(const UIRect& rect, UIColor color, float thickness = 1.0f, float cornerRadius = 0.0f);
    void addRectOutline(float x, float y, float w, float h, UIColor color, float thickness = 1.0f, float cornerRadius = 0.0f);
    void addText(const UIFont& font, const std::string& text, float x, float y, UIColor color);
    void addText(const std::string& text, float x, float y, UIColor color, float fontSize = 14.0f);
    void addImage(const UIRect& rect, VkImageView texture, UIColor tint = UIColor());
    void addLine(glm::vec2 a, glm::vec2 b, UIColor color, float thickness = 1.0f);
    void addCircle(float cx, float cy, float radius, UIColor color, int segments = 32);
    void addCircle(glm::vec2 center, float radius, UIColor color, int segments = 32);
    
private:
    void pushQuad(const glm::vec2& p0, const glm::vec2& p1, const glm::vec2& p2, const glm::vec2& p3,
                  const glm::vec2& uv0, const glm::vec2& uv1, const glm::vec2& uv2, const glm::vec2& uv3,
                  uint32_t color);
};

// ============================================================================
// UI INPUT STATE
// ============================================================================

struct UIInputState {
    glm::vec2 mousePos = glm::vec2(0);
    glm::vec2 mouseDelta = glm::vec2(0);
    float scrollDelta = 0.0f;
    
    bool mouseDown[3] = {false, false, false};      // Left, Right, Middle
    bool mouseClicked[3] = {false, false, false};
    bool mouseReleased[3] = {false, false, false};
    bool mouseDoubleClicked[3] = {false, false, false};
    
    bool keyDown[512] = {false};
    bool keyPressed[512] = {false};
    bool keyReleased[512] = {false};
    
    std::string textInput;
    
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
};

// ============================================================================
// UI WIDGET STATE
// ============================================================================

using WidgetId = uint64_t;

struct UIWidgetState {
    bool hovered = false;
    bool active = false;
    bool focused = false;
    float hoverTime = 0.0f;
    float activeTime = 0.0f;
    
    // For animations
    float animProgress = 0.0f;
    glm::vec2 dragStart = glm::vec2(0);
};

// ============================================================================
// UI CONTEXT
// ============================================================================

class UIContext {
public:
    UIContext() = default;
    
    void beginFrame(const UIInputState& input, float deltaTime);
    void endFrame();
    
    // Layout
    void beginLayout(const UIRect& bounds, UILayoutDirection direction = UILayoutDirection::Vertical);
    void endLayout();
    UIRect nextRect(float width, float height);
    void space(float size);
    
    // Widgets
    bool button(const std::string& label, float width = 0, float height = 0);
    bool checkbox(const std::string& label, bool& value);
    bool radioButton(const std::string& label, int& currentValue, int thisValue);
    void label(const std::string& text);
    void text(const std::string& text, UIColor color = UIColor());
    bool slider(const std::string& label, float& value, float min, float max);
    bool sliderInt(const std::string& label, int& value, int min, int max);
    bool inputText(const std::string& label, std::string& text, size_t maxLength = 256);
    bool inputFloat(const std::string& label, float& value, float step = 0.1f);
    bool inputInt(const std::string& label, int& value, int step = 1);
    bool colorPicker(const std::string& label, UIColor& color);
    void progressBar(float progress, const std::string& overlay = "");
    void image(VkImageView texture, float width, float height, UIColor tint = UIColor());
    
    // Containers
    bool beginWindow(const std::string& title, UIRect& bounds, bool* open = nullptr);
    void endWindow();
    bool beginPanel(const std::string& id, const UIRect& bounds);
    void endPanel();
    bool beginScrollArea(const std::string& id, const UIRect& bounds, float contentHeight);
    void endScrollArea();
    bool beginTreeNode(const std::string& label, bool defaultOpen = false);
    void endTreeNode();
    bool beginTabBar(const std::string& id);
    bool tabItem(const std::string& label);
    void endTabBar();
    
    // Popups
    void openPopup(const std::string& id);
    void closePopup();
    bool beginPopup(const std::string& id);
    void endPopup();
    bool beginMenu(const std::string& label);
    bool menuItem(const std::string& label, const std::string& shortcut = "", bool* selected = nullptr);
    void endMenu();
    
    // Tooltips
    void tooltip(const std::string& text);
    bool isItemHovered() const;
    
    // Focus
    void setFocus(WidgetId id);
    bool hasFocus(WidgetId id) const;
    
    // Styling
    void pushStyle(const UIStyle& style);
    void popStyle();
    const UIStyle& getStyle() const;
    
    // Font
    void setFont(std::shared_ptr<UIFont> font);
    UIFont& getFont();
    
    // Get draw data
    const UIDrawList& getDrawList() const { return drawList_; }
    UIDrawList& getDrawList() { return drawList_; }
    
private:
    WidgetId generateId(const std::string& label);
    UIWidgetState& getWidgetState(WidgetId id);
    bool isMouseInRect(const UIRect& rect) const;
    
    UIInputState input_;
    float deltaTime_ = 0.0f;
    
    std::vector<UILayoutState> layoutStack_;
    std::vector<UIStyle> styleStack_;
    UIStyle currentStyle_;
    
    std::shared_ptr<UIFont> font_;
    UIDrawList drawList_;
    
    std::unordered_map<WidgetId, UIWidgetState> widgetStates_;
    WidgetId hotWidget_ = 0;      // Widget under mouse
    WidgetId activeWidget_ = 0;   // Widget being interacted with
    WidgetId focusedWidget_ = 0;  // Widget with keyboard focus
    
    WidgetId lastWidget_ = 0;     // Last widget that was processed
    
    // Scroll state per scroll area
    std::unordered_map<WidgetId, float> scrollPositions_;
    
    // Popup state
    std::vector<WidgetId> popupStack_;
    WidgetId pendingPopup_ = 0;
    
    // Window state
    struct WindowState {
        UIRect bounds;
        bool collapsed = false;
        bool dragging = false;
        bool resizing = false;
        glm::vec2 dragOffset;
    };
    std::unordered_map<WidgetId, WindowState> windowStates_;
    
    // Tree node state
    std::unordered_map<WidgetId, bool> treeNodeStates_;
    
    // Tab bar state
    std::unordered_map<WidgetId, int> tabBarStates_;
    WidgetId currentTabBar_ = 0;
    int currentTabIndex_ = 0;
};

// ============================================================================
// UI RENDERER
// ============================================================================

class UIRenderer {
public:
    UIRenderer(VulkanContext& context);
    ~UIRenderer();
    
    bool initialize(VkRenderPass renderPass, uint32_t subpass = 0);
    void shutdown();
    
    void updateBuffers(const UIDrawList& drawList);
    void render(VkCommandBuffer cmd, uint32_t width, uint32_t height);
    
    // Default font
    std::shared_ptr<UIFont> getDefaultFont() { return defaultFont_; }
    
private:
    void createPipeline(VkRenderPass renderPass, uint32_t subpass);
    void createBuffers();
    
    VulkanContext& context_;
    
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;
    
    VkBuffer vertexBuffer_ = VK_NULL_HANDLE;
    VkBuffer indexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory_ = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory_ = VK_NULL_HANDLE;
    
    size_t vertexBufferSize_ = 0;
    size_t indexBufferSize_ = 0;
    
    std::shared_ptr<UIFont> defaultFont_;
    
    const UIDrawList* currentDrawList_ = nullptr;
};

// ============================================================================
// GAME UI WIDGETS (Higher level)
// ============================================================================

// Health bar component
class UIHealthBar {
public:
    void setHealth(float current, float max);
    void setColor(UIColor fillColor, UIColor bgColor = UIColor(0.2f, 0.2f, 0.2f));
    void setSize(float width, float height);
    
    void render(UIContext& ctx, float x, float y);
    
private:
    float current_ = 100.0f;
    float max_ = 100.0f;
    UIColor fillColor_ = UIColor::fromHex(0x2ecc71);
    UIColor backgroundColor_ = UIColor(0.2f, 0.2f, 0.2f);
    float width_ = 200.0f;
    float height_ = 20.0f;
    float displayValue_ = 100.0f;  // Smoothed
};

// Minimap component
class UIMinimap {
public:
    void setSize(float size);
    void setPlayerPosition(glm::vec2 pos);
    void setPlayerRotation(float angle);
    void addMarker(const std::string& id, glm::vec2 pos, UIColor color);
    void removeMarker(const std::string& id);
    void setMapTexture(VkImageView texture);
    void setMapBounds(glm::vec2 min, glm::vec2 max);
    
    void render(UIContext& ctx, float x, float y);
    
private:
    float size_ = 200.0f;
    glm::vec2 playerPos_ = glm::vec2(0);
    float playerAngle_ = 0.0f;
    glm::vec2 mapMin_ = glm::vec2(-100);
    glm::vec2 mapMax_ = glm::vec2(100);
    
    VkImageView mapTexture_ = VK_NULL_HANDLE;
    
    struct Marker {
        glm::vec2 position;
        UIColor color;
    };
    std::unordered_map<std::string, Marker> markers_;
};

// Dialog/Subtitle display
class UIDialog {
public:
    void showText(const std::string& text, float duration = 3.0f);
    void showChoice(const std::string& prompt, const std::vector<std::string>& options,
                   std::function<void(int)> callback);
    void hide();
    
    void update(float deltaTime);
    void render(UIContext& ctx, float screenWidth, float screenHeight);
    
private:
    std::string currentText_;
    float displayDuration_ = 0.0f;
    float elapsedTime_ = 0.0f;
    size_t visibleChars_ = 0;
    float charRevealRate_ = 30.0f;  // Characters per second
    
    bool showingChoice_ = false;
    std::string choicePrompt_;
    std::vector<std::string> options_;
    std::function<void(int)> choiceCallback_;
    int selectedOption_ = 0;
};

// Inventory grid
class UIInventory {
public:
    struct Item {
        std::string id;
        std::string name;
        VkImageView icon;
        int quantity;
    };
    
    void setGridSize(int columns, int rows);
    void setCellSize(float size);
    void setItems(const std::vector<Item>& items);
    
    void render(UIContext& ctx, float x, float y);
    
    int getHoveredSlot() const { return hoveredSlot_; }
    int getSelectedSlot() const { return selectedSlot_; }
    
    std::function<void(int)> onSlotClicked;
    std::function<void(int, int)> onItemDropped;  // From slot, to slot
    
private:
    int columns_ = 8;
    int rows_ = 4;
    float cellSize_ = 50.0f;
    std::vector<Item> items_;
    
    int hoveredSlot_ = -1;
    int selectedSlot_ = -1;
    int draggedSlot_ = -1;
};

} // namespace Sanic
