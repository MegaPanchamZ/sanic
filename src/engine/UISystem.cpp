/**
 * UISystem.cpp - User Interface System Implementation
 */

#include "UISystem.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

namespace Sanic {

// ============================================================================
// HELPER: Pack color to uint32
// ============================================================================
static uint32_t packColor(UIColor c) {
    uint8_t r = static_cast<uint8_t>(std::clamp(c.r, 0.0f, 1.0f) * 255.0f);
    uint8_t g = static_cast<uint8_t>(std::clamp(c.g, 0.0f, 1.0f) * 255.0f);
    uint8_t b = static_cast<uint8_t>(std::clamp(c.b, 0.0f, 1.0f) * 255.0f);
    uint8_t a = static_cast<uint8_t>(std::clamp(c.a, 0.0f, 1.0f) * 255.0f);
    return (a << 24) | (b << 16) | (g << 8) | r;
}

// ============================================================================
// UIFont Implementation
// ============================================================================

UIFont::~UIFont() {
    if (context_) {
        VkDevice device = VK_NULL_HANDLE;  // Get from context
        if (atlasSampler_) vkDestroySampler(device, atlasSampler_, nullptr);
        if (atlasView_) vkDestroyImageView(device, atlasView_, nullptr);
        if (atlasImage_) vkDestroyImage(device, atlasImage_, nullptr);
        if (atlasMemory_) vkFreeMemory(device, atlasMemory_, nullptr);
    }
}

bool UIFont::loadFromFile(const std::string& /*path*/, float size, VulkanContext& context) {
    context_ = &context;
    size_ = size;
    lineHeight_ = size * 1.2f;
    ascent_ = size * 0.8f;
    descent_ = size * 0.2f;
    
    // TODO: Load actual font file (stb_truetype or FreeType)
    // For now, create placeholder glyphs for ASCII
    for (uint32_t c = 32; c < 127; c++) {
        UIGlyph glyph;
        glyph.codepoint = c;
        glyph.advance = size * 0.6f;
        glyph.width = size * 0.5f;
        glyph.height = size;
        glyph.xOffset = 0;
        glyph.yOffset = -ascent_;
        glyphs_[c] = glyph;
    }
    
    return true;
}

bool UIFont::loadDefault(float size, VulkanContext& context) {
    return loadFromFile("", size, context);
}

const UIGlyph* UIFont::getGlyph(uint32_t codepoint) const {
    auto it = glyphs_.find(codepoint);
    return it != glyphs_.end() ? &it->second : nullptr;
}

float UIFont::getKerning(uint32_t left, uint32_t right) const {
    uint64_t key = (static_cast<uint64_t>(left) << 32) | right;
    auto it = kerning_.find(key);
    return it != kerning_.end() ? it->second : 0.0f;
}

glm::vec2 UIFont::measureText(const std::string& text) const {
    float width = 0.0f;
    uint32_t prev = 0;
    
    for (char c : text) {
        uint32_t cp = static_cast<uint32_t>(c);
        const UIGlyph* glyph = getGlyph(cp);
        if (glyph) {
            if (prev) width += getKerning(prev, cp);
            width += glyph->advance;
            prev = cp;
        }
    }
    
    return glm::vec2(width, lineHeight_);
}

// ============================================================================
// UIDrawList Implementation
// ============================================================================

void UIDrawList::pushQuad(const glm::vec2& p0, const glm::vec2& p1, const glm::vec2& p2, const glm::vec2& p3,
                          const glm::vec2& uv0, const glm::vec2& uv1, const glm::vec2& uv2, const glm::vec2& uv3,
                          uint32_t color) {
    uint32_t baseIdx = static_cast<uint32_t>(vertices.size());
    
    vertices.push_back({p0, uv0, color});
    vertices.push_back({p1, uv1, color});
    vertices.push_back({p2, uv2, color});
    vertices.push_back({p3, uv3, color});
    
    indices.push_back(baseIdx + 0);
    indices.push_back(baseIdx + 1);
    indices.push_back(baseIdx + 2);
    indices.push_back(baseIdx + 0);
    indices.push_back(baseIdx + 2);
    indices.push_back(baseIdx + 3);
}

void UIDrawList::addRect(float x, float y, float w, float h, UIColor color, float cornerRadius) {
    uint32_t col = packColor(color);
    
    if (cornerRadius <= 0.0f) {
        pushQuad({x, y}, {x + w, y}, {x + w, y + h}, {x, y + h},
                 {0, 0}, {1, 0}, {1, 1}, {0, 1}, col);
    } else {
        // Rounded rectangle - simplified
        float r = std::min(cornerRadius, std::min(w, h) * 0.5f);
        // Draw center rect
        pushQuad({x + r, y}, {x + w - r, y}, {x + w - r, y + h}, {x + r, y + h},
                 {0, 0}, {1, 0}, {1, 1}, {0, 1}, col);
        // Draw side rects
        pushQuad({x, y + r}, {x + r, y + r}, {x + r, y + h - r}, {x, y + h - r},
                 {0, 0}, {1, 0}, {1, 1}, {0, 1}, col);
        pushQuad({x + w - r, y + r}, {x + w, y + r}, {x + w, y + h - r}, {x + w - r, y + h - r},
                 {0, 0}, {1, 0}, {1, 1}, {0, 1}, col);
        // Corners would need triangulated circles
    }
}

void UIDrawList::addRect(const UIRect& rect, UIColor color, float cornerRadius) {
    addRect(rect.x, rect.y, rect.width, rect.height, color, cornerRadius);
}

void UIDrawList::addRectOutline(float x, float y, float w, float h, UIColor color, float thickness, float /*cornerRadius*/) {
    addRect(x, y, w, thickness, color, 0);          // Top
    addRect(x, y + h - thickness, w, thickness, color, 0);  // Bottom
    addRect(x, y + thickness, thickness, h - 2 * thickness, color, 0);  // Left
    addRect(x + w - thickness, y + thickness, thickness, h - 2 * thickness, color, 0);  // Right
}

void UIDrawList::addRectOutline(const UIRect& rect, UIColor color, float thickness, float cornerRadius) {
    addRectOutline(rect.x, rect.y, rect.width, rect.height, color, thickness, cornerRadius);
}

void UIDrawList::addText(const UIFont& font, const std::string& text, float x, float y, UIColor color) {
    uint32_t col = packColor(color);
    float cursorX = x;
    uint32_t prev = 0;
    
    for (char c : text) {
        uint32_t cp = static_cast<uint32_t>(c);
        const UIGlyph* glyph = font.getGlyph(cp);
        if (glyph) {
            if (prev) cursorX += font.getKerning(prev, cp);
            
            float gx = cursorX + glyph->xOffset;
            float gy = y + glyph->yOffset;
            
            pushQuad({gx, gy}, {gx + glyph->width, gy},
                     {gx + glyph->width, gy + glyph->height}, {gx, gy + glyph->height},
                     {glyph->x0, glyph->y0}, {glyph->x1, glyph->y0},
                     {glyph->x1, glyph->y1}, {glyph->x0, glyph->y1}, col);
            
            cursorX += glyph->advance;
            prev = cp;
        }
    }
}

void UIDrawList::addText(const std::string& text, float x, float y, UIColor color, float /*fontSize*/) {
    // Simplified text without font - just record the draw command for later
    // In a real implementation, this would use a default font
    uint32_t col = packColor(color);
    float cursorX = x;
    float charWidth = 8.0f;  // Placeholder fixed-width
    float charHeight = 14.0f;
    
    for (size_t i = 0; i < text.length(); i++) {
        pushQuad({cursorX, y}, {cursorX + charWidth, y},
                 {cursorX + charWidth, y + charHeight}, {cursorX, y + charHeight},
                 {0, 0}, {1, 0}, {1, 1}, {0, 1}, col);
        cursorX += charWidth;
    }
}

void UIDrawList::addImage(const UIRect& rect, VkImageView texture, UIColor tint) {
    uint32_t col = packColor(tint);
    
    // Record start of this image batch
    UIDrawCommand cmd;
    cmd.vertexOffset = static_cast<uint32_t>(vertices.size());
    cmd.indexOffset = static_cast<uint32_t>(indices.size());
    
    pushQuad({rect.x, rect.y}, {rect.x + rect.width, rect.y},
             {rect.x + rect.width, rect.y + rect.height}, {rect.x, rect.y + rect.height},
             {0, 0}, {1, 0}, {1, 1}, {0, 1}, col);
    
    cmd.indexCount = 6;
    cmd.texture = texture;
    cmd.clipRect = rect;
    commands.push_back(cmd);
}

void UIDrawList::addLine(glm::vec2 a, glm::vec2 b, UIColor color, float thickness) {
    glm::vec2 dir = b - a;
    float len = glm::length(dir);
    if (len < 0.001f) return;
    
    dir /= len;
    glm::vec2 perp = glm::vec2(-dir.y, dir.x) * (thickness * 0.5f);
    
    uint32_t col = packColor(color);
    pushQuad(a + perp, b + perp, b - perp, a - perp,
             {0, 0}, {1, 0}, {1, 1}, {0, 1}, col);
}

void UIDrawList::addCircle(float cx, float cy, float radius, UIColor color, int segments) {
    uint32_t col = packColor(color);
    uint32_t centerIdx = static_cast<uint32_t>(vertices.size());
    
    vertices.push_back({{cx, cy}, {0.5f, 0.5f}, col});
    
    for (int i = 0; i <= segments; i++) {
        float angle = (static_cast<float>(i) / segments) * 2.0f * 3.14159265f;
        float px = cx + std::cos(angle) * radius;
        float py = cy + std::sin(angle) * radius;
        float u = 0.5f + std::cos(angle) * 0.5f;
        float v = 0.5f + std::sin(angle) * 0.5f;
        vertices.push_back({{px, py}, {u, v}, col});
    }
    
    for (int i = 0; i < segments; i++) {
        indices.push_back(centerIdx);
        indices.push_back(centerIdx + 1 + i);
        indices.push_back(centerIdx + 2 + i);
    }
}

void UIDrawList::addCircle(glm::vec2 center, float radius, UIColor color, int segments) {
    addCircle(center.x, center.y, radius, color, segments);
}

// ============================================================================
// UIContext Implementation
// ============================================================================

void UIContext::beginFrame(const UIInputState& input, float deltaTime) {
    input_ = input;
    deltaTime_ = deltaTime;
    drawList_.clear();
    layoutStack_.clear();
    hotWidget_ = 0;
    lastWidget_ = 0;
}

void UIContext::endFrame() {
    // Reset transient state
}

void UIContext::beginLayout(const UIRect& bounds, UILayoutDirection direction) {
    UILayoutState state;
    state.bounds = bounds;
    state.direction = direction;
    state.cursor = 0;
    state.maxSecondary = 0;
    layoutStack_.push_back(state);
}

void UIContext::endLayout() {
    if (!layoutStack_.empty()) {
        layoutStack_.pop_back();
    }
}

UIRect UIContext::nextRect(float width, float height) {
    if (layoutStack_.empty()) {
        return {0, 0, width, height};
    }
    
    auto& layout = layoutStack_.back();
    UIRect rect;
    
    if (layout.direction == UILayoutDirection::Vertical) {
        rect.x = layout.bounds.x;
        rect.y = layout.bounds.y + layout.cursor;
        rect.width = width > 0 ? width : layout.bounds.width;
        rect.height = height;
        layout.cursor += height + currentStyle_.margin;
    } else {
        rect.x = layout.bounds.x + layout.cursor;
        rect.y = layout.bounds.y;
        rect.width = width;
        rect.height = height > 0 ? height : layout.bounds.height;
        layout.cursor += width + currentStyle_.margin;
    }
    
    return rect;
}

void UIContext::space(float size) {
    if (!layoutStack_.empty()) {
        layoutStack_.back().cursor += size;
    }
}

WidgetId UIContext::generateId(const std::string& label) {
    std::hash<std::string> hasher;
    return hasher(label);
}

UIWidgetState& UIContext::getWidgetState(WidgetId id) {
    return widgetStates_[id];
}

bool UIContext::isMouseInRect(const UIRect& rect) const {
    return rect.contains(input_.mousePos.x, input_.mousePos.y);
}

bool UIContext::button(const std::string& label, float width, float height) {
    float w = width > 0 ? width : 100.0f;
    float h = height > 0 ? height : 30.0f;
    UIRect rect = nextRect(w, h);
    
    WidgetId id = generateId(label);
    auto& state = getWidgetState(id);
    
    bool hovered = isMouseInRect(rect);
    bool clicked = hovered && input_.mouseClicked[0];
    
    state.hovered = hovered;
    state.active = hovered && input_.mouseDown[0];
    lastWidget_ = id;
    
    // Draw button
    UIColor bgColor = state.active ? currentStyle_.primary : 
                     (hovered ? currentStyle_.backgroundAlt : currentStyle_.background);
    drawList_.addRect(rect, bgColor, currentStyle_.borderRadius);
    drawList_.addRectOutline(rect, currentStyle_.border, currentStyle_.borderWidth, currentStyle_.borderRadius);
    
    // Draw label centered
    float textX = rect.x + rect.width * 0.5f - label.length() * 4.0f;  // Approximate centering
    float textY = rect.y + rect.height * 0.5f - 7.0f;
    drawList_.addText(label, textX, textY, currentStyle_.text, currentStyle_.fontSize);
    
    return clicked;
}

bool UIContext::checkbox(const std::string& label, bool& value) {
    float size = 20.0f;
    UIRect rect = nextRect(size + currentStyle_.padding + label.length() * 8.0f, size);
    
    WidgetId id = generateId(label);
    auto& state = getWidgetState(id);
    
    UIRect boxRect = {rect.x, rect.y, size, size};
    bool hovered = isMouseInRect(boxRect);
    bool clicked = hovered && input_.mouseClicked[0];
    
    if (clicked) value = !value;
    
    state.hovered = hovered;
    lastWidget_ = id;
    
    // Draw checkbox
    drawList_.addRect(boxRect, hovered ? currentStyle_.backgroundAlt : currentStyle_.background, 2.0f);
    drawList_.addRectOutline(boxRect, currentStyle_.border, 1.0f, 2.0f);
    
    if (value) {
        UIRect checkRect = boxRect.shrink(4.0f);
        drawList_.addRect(checkRect, currentStyle_.primary, 2.0f);
    }
    
    // Draw label
    drawList_.addText(label, rect.x + size + currentStyle_.padding, rect.y + 3.0f, 
                     currentStyle_.text, currentStyle_.fontSize);
    
    return clicked;
}

bool UIContext::radioButton(const std::string& label, int& currentValue, int thisValue) {
    float size = 20.0f;
    UIRect rect = nextRect(size + currentStyle_.padding + label.length() * 8.0f, size);
    
    WidgetId id = generateId(label + std::to_string(thisValue));
    auto& state = getWidgetState(id);
    
    float cx = rect.x + size * 0.5f;
    float cy = rect.y + size * 0.5f;
    float radius = size * 0.5f;
    
    bool hovered = glm::distance(input_.mousePos, glm::vec2(cx, cy)) < radius;
    bool clicked = hovered && input_.mouseClicked[0];
    
    if (clicked) currentValue = thisValue;
    
    state.hovered = hovered;
    lastWidget_ = id;
    
    // Draw radio button
    drawList_.addCircle(cx, cy, radius, currentStyle_.background, 16);
    drawList_.addCircle(cx, cy, radius - 1.0f, currentStyle_.border, 16);
    
    if (currentValue == thisValue) {
        drawList_.addCircle(cx, cy, radius - 5.0f, currentStyle_.primary, 16);
    }
    
    // Draw label
    drawList_.addText(label, rect.x + size + currentStyle_.padding, rect.y + 3.0f,
                     currentStyle_.text, currentStyle_.fontSize);
    
    return clicked;
}

void UIContext::label(const std::string& text) {
    UIRect rect = nextRect(text.length() * 8.0f, 20.0f);
    drawList_.addText(text, rect.x, rect.y + 3.0f, currentStyle_.text, currentStyle_.fontSize);
}

void UIContext::text(const std::string& text, UIColor color) {
    UIRect rect = nextRect(text.length() * 8.0f, 20.0f);
    UIColor c = (color.a > 0) ? color : currentStyle_.text;
    drawList_.addText(text, rect.x, rect.y + 3.0f, c, currentStyle_.fontSize);
}

bool UIContext::slider(const std::string& label, float& value, float min, float max) {
    UIRect rect = nextRect(200.0f, 30.0f);
    
    WidgetId id = generateId(label);
    auto& state = getWidgetState(id);
    
    // Track
    float trackY = rect.y + rect.height * 0.5f - 3.0f;
    UIRect trackRect = {rect.x, trackY, rect.width, 6.0f};
    
    bool hovered = isMouseInRect(trackRect.expand(5.0f));
    bool active = hovered && input_.mouseDown[0];
    
    if (active) {
        float t = (input_.mousePos.x - rect.x) / rect.width;
        value = min + std::clamp(t, 0.0f, 1.0f) * (max - min);
    }
    
    state.hovered = hovered;
    state.active = active;
    lastWidget_ = id;
    
    // Draw track
    drawList_.addRect(trackRect, currentStyle_.background, 3.0f);
    
    // Draw filled portion
    float fillWidth = rect.width * ((value - min) / (max - min));
    UIRect fillRect = {rect.x, trackY, fillWidth, 6.0f};
    drawList_.addRect(fillRect, currentStyle_.primary, 3.0f);
    
    // Draw handle
    float handleX = rect.x + fillWidth;
    drawList_.addCircle(handleX, rect.y + rect.height * 0.5f, 8.0f, currentStyle_.primary, 16);
    
    return active;
}

bool UIContext::sliderInt(const std::string& label, int& value, int min, int max) {
    float fval = static_cast<float>(value);
    bool changed = slider(label, fval, static_cast<float>(min), static_cast<float>(max));
    value = static_cast<int>(std::round(fval));
    return changed;
}

bool UIContext::inputText(const std::string& /*label*/, std::string& text, size_t /*maxLength*/) {
    UIRect rect = nextRect(200.0f, 30.0f);
    
    // Draw input background
    drawList_.addRect(rect, currentStyle_.background, currentStyle_.borderRadius);
    drawList_.addRectOutline(rect, currentStyle_.border, 1.0f, currentStyle_.borderRadius);
    
    // Draw text
    drawList_.addText(text, rect.x + currentStyle_.padding, rect.y + 8.0f,
                     currentStyle_.text, currentStyle_.fontSize);
    
    return false;  // TODO: Handle text input
}

bool UIContext::inputFloat(const std::string& label, float& value, float step) {
    UIRect rect = nextRect(200.0f, 30.0f);
    
    WidgetId id = generateId(label);
    
    // Draw input field
    UIRect inputRect = {rect.x + 40.0f, rect.y, rect.width - 80.0f, rect.height};
    drawList_.addRect(inputRect, currentStyle_.background, currentStyle_.borderRadius);
    drawList_.addRectOutline(inputRect, currentStyle_.border, 1.0f, currentStyle_.borderRadius);
    
    // Draw value
    char buf[32];
    snprintf(buf, sizeof(buf), "%.3f", value);
    drawList_.addText(buf, inputRect.x + 4.0f, inputRect.y + 8.0f, currentStyle_.text, currentStyle_.fontSize);
    
    // Draw +/- buttons
    UIRect minusRect = {rect.x, rect.y, 35.0f, rect.height};
    UIRect plusRect = {rect.x + rect.width - 35.0f, rect.y, 35.0f, rect.height};
    
    if (isMouseInRect(minusRect) && input_.mouseClicked[0]) {
        value -= step;
        return true;
    }
    if (isMouseInRect(plusRect) && input_.mouseClicked[0]) {
        value += step;
        return true;
    }
    
    drawList_.addRect(minusRect, currentStyle_.backgroundAlt, currentStyle_.borderRadius);
    drawList_.addRect(plusRect, currentStyle_.backgroundAlt, currentStyle_.borderRadius);
    drawList_.addText("-", minusRect.x + 14.0f, minusRect.y + 6.0f, currentStyle_.text, currentStyle_.fontSize);
    drawList_.addText("+", plusRect.x + 12.0f, plusRect.y + 6.0f, currentStyle_.text, currentStyle_.fontSize);
    
    lastWidget_ = id;
    return false;
}

bool UIContext::inputInt(const std::string& label, int& value, int step) {
    float fval = static_cast<float>(value);
    bool changed = inputFloat(label, fval, static_cast<float>(step));
    value = static_cast<int>(fval);
    return changed;
}

bool UIContext::colorPicker(const std::string& /*label*/, UIColor& color) {
    UIRect rect = nextRect(200.0f, 150.0f);
    
    // Draw color preview
    UIRect previewRect = {rect.x, rect.y, 50.0f, 50.0f};
    drawList_.addRect(previewRect, color, 4.0f);
    
    // RGB sliders would go here
    // For now just return false
    return false;
}

void UIContext::progressBar(float progress, const std::string& overlay) {
    UIRect rect = nextRect(200.0f, 20.0f);
    
    // Background
    drawList_.addRect(rect, currentStyle_.background, currentStyle_.borderRadius);
    
    // Fill
    float fillWidth = rect.width * std::clamp(progress, 0.0f, 1.0f);
    UIRect fillRect = {rect.x, rect.y, fillWidth, rect.height};
    drawList_.addRect(fillRect, currentStyle_.primary, currentStyle_.borderRadius);
    
    // Border
    drawList_.addRectOutline(rect, currentStyle_.border, 1.0f, currentStyle_.borderRadius);
    
    // Overlay text
    if (!overlay.empty()) {
        float textX = rect.x + rect.width * 0.5f - overlay.length() * 4.0f;
        drawList_.addText(overlay, textX, rect.y + 3.0f, currentStyle_.text, currentStyle_.fontSize);
    }
}

void UIContext::image(VkImageView texture, float width, float height, UIColor tint) {
    UIRect rect = nextRect(width, height);
    drawList_.addImage(rect, texture, tint);
}

bool UIContext::beginWindow(const std::string& title, UIRect& bounds, bool* open) {
    WidgetId id = generateId(title);
    auto& winState = windowStates_[id];
    
    if (winState.bounds.width == 0) {
        winState.bounds = bounds;
    }
    
    // Title bar
    float titleHeight = 25.0f;
    UIRect titleRect = {winState.bounds.x, winState.bounds.y, winState.bounds.width, titleHeight};
    
    // Handle dragging
    if (isMouseInRect(titleRect) && input_.mouseClicked[0]) {
        winState.dragging = true;
        winState.dragOffset = input_.mousePos - glm::vec2(winState.bounds.x, winState.bounds.y);
    }
    if (!input_.mouseDown[0]) {
        winState.dragging = false;
    }
    if (winState.dragging) {
        winState.bounds.x = input_.mousePos.x - winState.dragOffset.x;
        winState.bounds.y = input_.mousePos.y - winState.dragOffset.y;
    }
    
    // Draw window
    drawList_.addRect({winState.bounds.x, winState.bounds.y, winState.bounds.width, winState.bounds.height},
                     currentStyle_.background, currentStyle_.borderRadius);
    drawList_.addRect(titleRect, currentStyle_.backgroundAlt, currentStyle_.borderRadius);
    drawList_.addText(title, winState.bounds.x + 8.0f, winState.bounds.y + 5.0f,
                     currentStyle_.text, currentStyle_.fontSize);
    
    // Close button
    if (open) {
        UIRect closeRect = {winState.bounds.x + winState.bounds.width - 25.0f, winState.bounds.y, 25.0f, 25.0f};
        if (isMouseInRect(closeRect) && input_.mouseClicked[0]) {
            *open = false;
        }
        drawList_.addText("X", closeRect.x + 8.0f, closeRect.y + 5.0f, currentStyle_.text, currentStyle_.fontSize);
    }
    
    // Set up layout for window content
    UIRect contentBounds = {winState.bounds.x + currentStyle_.padding,
                           winState.bounds.y + titleHeight + currentStyle_.padding,
                           winState.bounds.width - currentStyle_.padding * 2,
                           winState.bounds.height - titleHeight - currentStyle_.padding * 2};
    beginLayout(contentBounds, UILayoutDirection::Vertical);
    
    bounds = winState.bounds;
    return true;
}

void UIContext::endWindow() {
    endLayout();
}

bool UIContext::beginPanel(const std::string& id, const UIRect& bounds) {
    WidgetId wid = generateId(id);
    (void)wid;
    
    drawList_.addRect(bounds, currentStyle_.background, currentStyle_.borderRadius);
    drawList_.addRectOutline(bounds, currentStyle_.border, 1.0f, currentStyle_.borderRadius);
    
    UIRect contentBounds = bounds.shrink(currentStyle_.padding);
    beginLayout(contentBounds, UILayoutDirection::Vertical);
    
    return true;
}

void UIContext::endPanel() {
    endLayout();
}

bool UIContext::beginScrollArea(const std::string& id, const UIRect& bounds, float contentHeight) {
    WidgetId wid = generateId(id);
    float& scrollY = scrollPositions_[wid];
    
    // Handle scrolling
    if (isMouseInRect(bounds)) {
        scrollY -= input_.scrollDelta * 20.0f;
        scrollY = std::clamp(scrollY, 0.0f, std::max(0.0f, contentHeight - bounds.height));
    }
    
    drawList_.addRect(bounds, currentStyle_.background, currentStyle_.borderRadius);
    
    // TODO: Set up clip rect
    UIRect contentBounds = {bounds.x, bounds.y - scrollY, bounds.width - currentStyle_.scrollbarWidth, contentHeight};
    beginLayout(contentBounds, UILayoutDirection::Vertical);
    
    // Draw scrollbar
    if (contentHeight > bounds.height) {
        float scrollbarHeight = (bounds.height / contentHeight) * bounds.height;
        float scrollbarY = bounds.y + (scrollY / contentHeight) * bounds.height;
        UIRect scrollbarRect = {bounds.x + bounds.width - currentStyle_.scrollbarWidth, scrollbarY,
                               currentStyle_.scrollbarWidth, scrollbarHeight};
        drawList_.addRect(scrollbarRect, currentStyle_.primary, currentStyle_.scrollbarWidth * 0.5f);
    }
    
    return true;
}

void UIContext::endScrollArea() {
    endLayout();
}

bool UIContext::beginTreeNode(const std::string& label, bool defaultOpen) {
    WidgetId id = generateId(label);
    
    if (treeNodeStates_.find(id) == treeNodeStates_.end()) {
        treeNodeStates_[id] = defaultOpen;
    }
    bool& open = treeNodeStates_[id];
    
    UIRect rect = nextRect(200.0f, 20.0f);
    
    if (isMouseInRect(rect) && input_.mouseClicked[0]) {
        open = !open;
    }
    
    // Draw arrow
    const char* arrow = open ? "v" : ">";
    drawList_.addText(arrow, rect.x, rect.y + 3.0f, currentStyle_.text, currentStyle_.fontSize);
    
    // Draw label
    drawList_.addText(label, rect.x + 16.0f, rect.y + 3.0f, currentStyle_.text, currentStyle_.fontSize);
    
    if (open) {
        // Indent children
        if (!layoutStack_.empty()) {
            layoutStack_.back().bounds.x += 16.0f;
            layoutStack_.back().bounds.width -= 16.0f;
        }
    }
    
    return open;
}

void UIContext::endTreeNode() {
    // Restore indent
    if (!layoutStack_.empty()) {
        layoutStack_.back().bounds.x -= 16.0f;
        layoutStack_.back().bounds.width += 16.0f;
    }
}

bool UIContext::beginTabBar(const std::string& id) {
    currentTabBar_ = generateId(id);
    currentTabIndex_ = 0;
    
    if (tabBarStates_.find(currentTabBar_) == tabBarStates_.end()) {
        tabBarStates_[currentTabBar_] = 0;
    }
    
    return true;
}

bool UIContext::tabItem(const std::string& label) {
    int selectedTab = tabBarStates_[currentTabBar_];
    bool isSelected = (currentTabIndex_ == selectedTab);
    
    UIRect rect = nextRect(80.0f, 25.0f);
    
    if (isMouseInRect(rect) && input_.mouseClicked[0]) {
        tabBarStates_[currentTabBar_] = currentTabIndex_;
        isSelected = true;
    }
    
    UIColor bgColor = isSelected ? currentStyle_.primary : currentStyle_.background;
    drawList_.addRect(rect, bgColor, currentStyle_.borderRadius);
    
    float textX = rect.x + rect.width * 0.5f - label.length() * 4.0f;
    drawList_.addText(label, textX, rect.y + 5.0f, currentStyle_.text, currentStyle_.fontSize);
    
    currentTabIndex_++;
    return isSelected;
}

void UIContext::endTabBar() {
    currentTabBar_ = 0;
}

void UIContext::openPopup(const std::string& id) {
    pendingPopup_ = generateId(id);
}

void UIContext::closePopup() {
    if (!popupStack_.empty()) {
        popupStack_.pop_back();
    }
}

bool UIContext::beginPopup(const std::string& id) {
    WidgetId wid = generateId(id);
    
    if (pendingPopup_ == wid) {
        popupStack_.push_back(wid);
        pendingPopup_ = 0;
    }
    
    if (std::find(popupStack_.begin(), popupStack_.end(), wid) == popupStack_.end()) {
        return false;
    }
    
    // Draw popup at mouse position
    UIRect bounds = {input_.mousePos.x, input_.mousePos.y, 150.0f, 200.0f};
    drawList_.addRect(bounds, currentStyle_.background, currentStyle_.borderRadius);
    drawList_.addRectOutline(bounds, currentStyle_.border, 1.0f, currentStyle_.borderRadius);
    
    beginLayout(bounds.shrink(currentStyle_.padding), UILayoutDirection::Vertical);
    
    return true;
}

void UIContext::endPopup() {
    endLayout();
}

bool UIContext::beginMenu(const std::string& label) {
    return button(label);
}

bool UIContext::menuItem(const std::string& label, const std::string& shortcut, bool* selected) {
    UIRect rect = nextRect(150.0f, 22.0f);
    
    bool hovered = isMouseInRect(rect);
    bool clicked = hovered && input_.mouseClicked[0];
    
    if (clicked && selected) {
        *selected = !*selected;
    }
    
    if (hovered) {
        drawList_.addRect(rect, currentStyle_.backgroundAlt, 2.0f);
    }
    
    drawList_.addText(label, rect.x + 8.0f, rect.y + 3.0f, currentStyle_.text, currentStyle_.fontSize);
    
    if (!shortcut.empty()) {
        float shortcutX = rect.x + rect.width - shortcut.length() * 8.0f - 8.0f;
        drawList_.addText(shortcut, shortcutX, rect.y + 3.0f, currentStyle_.textDisabled, currentStyle_.fontSize);
    }
    
    if (selected && *selected) {
        drawList_.addText("*", rect.x + rect.width - 20.0f, rect.y + 3.0f, currentStyle_.primary, currentStyle_.fontSize);
    }
    
    return clicked;
}

void UIContext::endMenu() {
    // Nothing special needed
}

void UIContext::tooltip(const std::string& text) {
    if (!isItemHovered()) return;
    
    UIRect rect = {input_.mousePos.x + 10.0f, input_.mousePos.y + 10.0f,
                   text.length() * 8.0f + 16.0f, 24.0f};
    
    drawList_.addRect(rect, currentStyle_.background, 4.0f);
    drawList_.addRectOutline(rect, currentStyle_.border, 1.0f, 4.0f);
    drawList_.addText(text, rect.x + 8.0f, rect.y + 5.0f, currentStyle_.text, currentStyle_.fontSize);
}

bool UIContext::isItemHovered() const {
    return widgetStates_.count(lastWidget_) && widgetStates_.at(lastWidget_).hovered;
}

void UIContext::setFocus(WidgetId id) {
    focusedWidget_ = id;
}

bool UIContext::hasFocus(WidgetId id) const {
    return focusedWidget_ == id;
}

void UIContext::pushStyle(const UIStyle& style) {
    styleStack_.push_back(currentStyle_);
    currentStyle_ = style;
}

void UIContext::popStyle() {
    if (!styleStack_.empty()) {
        currentStyle_ = styleStack_.back();
        styleStack_.pop_back();
    }
}

const UIStyle& UIContext::getStyle() const {
    return currentStyle_;
}

void UIContext::setFont(std::shared_ptr<UIFont> font) {
    font_ = font;
}

UIFont& UIContext::getFont() {
    static UIFont defaultFont;
    return font_ ? *font_ : defaultFont;
}

// ============================================================================
// UIRenderer Implementation
// ============================================================================

UIRenderer::UIRenderer(VulkanContext& context) : context_(context) {}

UIRenderer::~UIRenderer() {
    shutdown();
}

bool UIRenderer::initialize(VkRenderPass /*renderPass*/, uint32_t /*subpass*/) {
    // TODO: Create Vulkan resources
    return true;
}

void UIRenderer::shutdown() {
    // TODO: Cleanup Vulkan resources
}

void UIRenderer::updateBuffers(const UIDrawList& drawList) {
    currentDrawList_ = &drawList;
    // TODO: Upload vertex/index data to GPU
}

void UIRenderer::render(VkCommandBuffer /*cmd*/, uint32_t /*width*/, uint32_t /*height*/) {
    // TODO: Render UI
}

void UIRenderer::createPipeline(VkRenderPass /*renderPass*/, uint32_t /*subpass*/) {
    // TODO: Create graphics pipeline
}

void UIRenderer::createBuffers() {
    // TODO: Create vertex/index buffers
}

// ============================================================================
// Game Widget Implementations
// ============================================================================

void UIHealthBar::setHealth(float current, float max) {
    current_ = current;
    max_ = max;
}

void UIHealthBar::setColor(UIColor fillColor, UIColor bgColor) {
    fillColor_ = fillColor;
    backgroundColor_ = bgColor;
}

void UIHealthBar::setSize(float width, float height) {
    width_ = width;
    height_ = height;
}

void UIHealthBar::render(UIContext& ctx, float x, float y) {
    float targetValue = max_ > 0 ? (current_ / max_) : 0.0f;
    displayValue_ += (targetValue - displayValue_) * 0.1f;
    
    // Background
    ctx.getDrawList().addRect(x, y, width_, height_, backgroundColor_, height_ * 0.5f);
    
    // Fill
    float fillWidth = width_ * std::clamp(displayValue_, 0.0f, 1.0f);
    ctx.getDrawList().addRect(x, y, fillWidth, height_, fillColor_, height_ * 0.5f);
    
    // Border
    ctx.getDrawList().addRectOutline(x, y, width_, height_, UIColor(0.3f, 0.3f, 0.3f), 1.0f, height_ * 0.5f);
}

void UIMinimap::setSize(float size) {
    size_ = size;
}

void UIMinimap::setPlayerPosition(glm::vec2 pos) {
    playerPos_ = pos;
}

void UIMinimap::setPlayerRotation(float angle) {
    playerAngle_ = angle;
}

void UIMinimap::addMarker(const std::string& id, glm::vec2 pos, UIColor color) {
    markers_[id] = {pos, color};
}

void UIMinimap::removeMarker(const std::string& id) {
    markers_.erase(id);
}

void UIMinimap::setMapTexture(VkImageView texture) {
    mapTexture_ = texture;
}

void UIMinimap::setMapBounds(glm::vec2 min, glm::vec2 max) {
    mapMin_ = min;
    mapMax_ = max;
}

void UIMinimap::render(UIContext& ctx, float x, float y) {
    // Background circle
    ctx.getDrawList().addCircle(x + size_ * 0.5f, y + size_ * 0.5f, size_ * 0.5f,
                                UIColor(0.1f, 0.1f, 0.1f, 0.8f), 32);
    
    // Render markers
    for (const auto& [id, marker] : markers_) {
        glm::vec2 relPos = marker.position - playerPos_;
        float mx = x + size_ * 0.5f + relPos.x * 0.1f;
        float my = y + size_ * 0.5f + relPos.y * 0.1f;
        
        if (mx > x && mx < x + size_ && my > y && my < y + size_) {
            ctx.getDrawList().addCircle(mx, my, 3.0f, marker.color, 8);
        }
    }
    
    // Player indicator
    ctx.getDrawList().addCircle(x + size_ * 0.5f, y + size_ * 0.5f, 4.0f, UIColor(0, 1, 0), 8);
}

void UIDialog::showText(const std::string& text, float duration) {
    currentText_ = text;
    displayDuration_ = duration;
    elapsedTime_ = 0.0f;
    visibleChars_ = 0;
    showingChoice_ = false;
}

void UIDialog::showChoice(const std::string& prompt, const std::vector<std::string>& options,
                          std::function<void(int)> callback) {
    choicePrompt_ = prompt;
    options_ = options;
    choiceCallback_ = callback;
    showingChoice_ = true;
    selectedOption_ = 0;
}

void UIDialog::hide() {
    currentText_.clear();
    showingChoice_ = false;
}

void UIDialog::update(float deltaTime) {
    if (!currentText_.empty()) {
        elapsedTime_ += deltaTime;
        visibleChars_ = static_cast<size_t>(elapsedTime_ * charRevealRate_);
        
        if (displayDuration_ > 0 && elapsedTime_ > displayDuration_) {
            hide();
        }
    }
}

void UIDialog::render(UIContext& ctx, float screenWidth, float screenHeight) {
    if (currentText_.empty() && !showingChoice_) return;
    
    float boxWidth = screenWidth * 0.8f;
    float boxHeight = 100.0f;
    float boxX = (screenWidth - boxWidth) * 0.5f;
    float boxY = screenHeight - boxHeight - 20.0f;
    
    // Background
    ctx.getDrawList().addRect(boxX, boxY, boxWidth, boxHeight, UIColor(0.1f, 0.1f, 0.15f, 0.95f), 8.0f);
    ctx.getDrawList().addRectOutline(boxX, boxY, boxWidth, boxHeight, UIColor(0.4f, 0.4f, 0.5f), 2.0f, 8.0f);
    
    // Text with typewriter effect
    std::string visibleText = currentText_.substr(0, std::min(visibleChars_, currentText_.length()));
    ctx.getDrawList().addText(visibleText, boxX + 16.0f, boxY + 16.0f, UIColor(1, 1, 1), 16.0f);
    
    // Choice options
    if (showingChoice_) {
        float choiceY = boxY + 50.0f;
        for (size_t i = 0; i < options_.size(); i++) {
            UIColor color = (static_cast<int>(i) == selectedOption_) ?
                           UIColor(1, 1, 0) : UIColor(0.7f, 0.7f, 0.7f);
            ctx.getDrawList().addText(options_[i], boxX + 32.0f, choiceY, color, 14.0f);
            choiceY += 20.0f;
        }
    }
}

void UIInventory::setGridSize(int columns, int rows) {
    columns_ = columns;
    rows_ = rows;
}

void UIInventory::setCellSize(float size) {
    cellSize_ = size;
}

void UIInventory::setItems(const std::vector<Item>& items) {
    items_ = items;
}

void UIInventory::render(UIContext& ctx, float x, float y) {
    for (int row = 0; row < rows_; row++) {
        for (int col = 0; col < columns_; col++) {
            float cellX = x + col * (cellSize_ + 4.0f);
            float cellY = y + row * (cellSize_ + 4.0f);
            
            int slotIndex = row * columns_ + col;
            bool isHovered = (slotIndex == hoveredSlot_);
            bool isSelected = (slotIndex == selectedSlot_);
            
            UIColor bgColor = isSelected ? UIColor(0.3f, 0.5f, 0.7f, 0.8f) :
                             (isHovered ? UIColor(0.25f, 0.25f, 0.3f, 0.8f) :
                                         UIColor(0.15f, 0.15f, 0.2f, 0.8f));
            
            // Cell background
            ctx.getDrawList().addRect(cellX, cellY, cellSize_, cellSize_, bgColor, 4.0f);
            ctx.getDrawList().addRectOutline(cellX, cellY, cellSize_, cellSize_,
                                             UIColor(0.4f, 0.4f, 0.5f), 1.0f, 4.0f);
            
            // Item in slot
            if (slotIndex < static_cast<int>(items_.size()) && !items_[slotIndex].id.empty()) {
                const auto& item = items_[slotIndex];
                
                // Item icon would go here using addImage
                
                // Quantity
                if (item.quantity > 1) {
                    std::string qtyStr = std::to_string(item.quantity);
                    ctx.getDrawList().addText(qtyStr, cellX + cellSize_ - 12.0f, cellY + cellSize_ - 14.0f,
                                             UIColor(1, 1, 1), 12.0f);
                }
            }
        }
    }
}

} // namespace Sanic
