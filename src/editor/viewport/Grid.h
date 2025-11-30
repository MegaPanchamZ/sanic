/**
 * Grid.h
 * 
 * 3D grid rendering for the editor viewport.
 */

#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <vector>

namespace Sanic::Editor {

class Grid {
public:
    Grid();
    ~Grid();
    
    // Configuration
    void setSize(float size) { size_ = size; }
    void setSpacing(float spacing) { spacing_ = spacing; }
    void setMajorLineInterval(int interval) { majorLineInterval_ = interval; }
    
    void setColor(const glm::vec4& color) { color_ = color; }
    void setMajorColor(const glm::vec4& color) { majorColor_ = color; }
    void setAxisColors(const glm::vec4& x, const glm::vec4& z) { 
        xAxisColor_ = x; 
        zAxisColor_ = z; 
    }
    
    void setFadeDistance(float nearFade, float farFade) {
        nearFade_ = nearFade;
        farFade_ = farFade;
    }
    
    void setVisible(bool visible) { visible_ = visible; }
    bool isVisible() const { return visible_; }
    
    // Generate grid lines for immediate mode rendering
    // Returns pairs of (position, color) for line vertices
    struct GridVertex {
        glm::vec3 position;
        glm::vec4 color;
    };
    
    std::vector<GridVertex> generateLines(const glm::vec3& cameraPos) const;
    
    // Draw using ImGui draw list (for simple rendering)
    void drawImGui(const glm::mat4& viewProj, const glm::vec2& viewportSize, 
                   const glm::vec3& cameraPos);
    
private:
    float size_ = 100.0f;
    float spacing_ = 1.0f;
    int majorLineInterval_ = 10;
    
    glm::vec4 color_ = glm::vec4(0.3f, 0.3f, 0.3f, 1.0f);
    glm::vec4 majorColor_ = glm::vec4(0.4f, 0.4f, 0.4f, 1.0f);
    glm::vec4 xAxisColor_ = glm::vec4(0.8f, 0.2f, 0.2f, 1.0f);
    glm::vec4 zAxisColor_ = glm::vec4(0.2f, 0.2f, 0.8f, 1.0f);
    
    float nearFade_ = 50.0f;
    float farFade_ = 200.0f;
    
    bool visible_ = true;
};

} // namespace Sanic::Editor
