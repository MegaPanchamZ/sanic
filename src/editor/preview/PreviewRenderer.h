/**
 * PreviewRenderer.h
 * 
 * Renderer for asset preview thumbnails in inspector and asset browser.
 */

#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>

namespace Sanic::Editor {

class PreviewRenderer {
public:
    PreviewRenderer();
    virtual ~PreviewRenderer();
    
    struct InitInfo {
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkQueue graphicsQueue = VK_NULL_HANDLE;
        uint32_t width = 256;
        uint32_t height = 256;
    };
    
    bool initialize(const InitInfo& info);
    void shutdown();
    void resize(uint32_t width, uint32_t height);
    
    // Get preview texture for ImGui
    VkDescriptorSet getOutputDescriptor() const { return outputDescriptor_; }
    
    // Camera control
    void setCamera(const glm::vec3& position, const glm::vec3& target);
    void orbit(float deltaYaw, float deltaPitch);
    void zoom(float delta);
    void resetCamera();
    
    // Lighting
    void setLightDirection(const glm::vec3& dir) { lightDirection_ = glm::normalize(dir); }
    void setLightColor(const glm::vec3& color) { lightColor_ = color; }
    void setAmbientColor(const glm::vec3& color) { ambientColor_ = color; }
    
protected:
    virtual void createResources() {}
    virtual void destroyResources() {}
    virtual void renderContent(VkCommandBuffer cmd) {}
    
    void beginRender(VkCommandBuffer cmd);
    void endRender(VkCommandBuffer cmd);
    
    glm::mat4 getViewMatrix() const;
    glm::mat4 getProjectionMatrix() const;
    
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    
    InitInfo info_;
    bool initialized_ = false;
    
    // Render target
    VkImage colorImage_ = VK_NULL_HANDLE;
    VkDeviceMemory colorMemory_ = VK_NULL_HANDLE;
    VkImageView colorView_ = VK_NULL_HANDLE;
    
    VkImage depthImage_ = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory_ = VK_NULL_HANDLE;
    VkImageView depthView_ = VK_NULL_HANDLE;
    
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorSet outputDescriptor_ = VK_NULL_HANDLE;
    
    // Camera
    float cameraDistance_ = 3.0f;
    float cameraYaw_ = 45.0f;    // degrees
    float cameraPitch_ = 30.0f;  // degrees
    glm::vec3 cameraTarget_ = glm::vec3(0.0f);
    float fov_ = 45.0f;
    
    // Lighting
    glm::vec3 lightDirection_ = glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f));
    glm::vec3 lightColor_ = glm::vec3(1.0f);
    glm::vec3 ambientColor_ = glm::vec3(0.1f);
};

/**
 * Preview renderer for 3D meshes.
 */
class MeshPreview : public PreviewRenderer {
public:
    void setMesh(class Mesh* mesh) { mesh_ = mesh; needsUpdate_ = true; }
    void setMaterial(class Material* material) { material_ = material; needsUpdate_ = true; }
    
    void render();
    
protected:
    void createResources() override;
    void destroyResources() override;
    void renderContent(VkCommandBuffer cmd) override;
    
private:
    class Mesh* mesh_ = nullptr;
    class Material* material_ = nullptr;
    bool needsUpdate_ = false;
    
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
};

/**
 * Preview renderer for materials using preset shapes.
 */
class MaterialPreview : public PreviewRenderer {
public:
    enum class Shape { Sphere, Cube, Plane, Cylinder };
    
    void setMaterial(class Material* material) { material_ = material; needsUpdate_ = true; }
    void setShape(Shape shape) { shape_ = shape; needsUpdate_ = true; }
    
    void render();
    
protected:
    void createResources() override;
    void destroyResources() override;
    void renderContent(VkCommandBuffer cmd) override;
    
private:
    void createPreviewShapes();
    
    class Material* material_ = nullptr;
    Shape shape_ = Shape::Sphere;
    bool needsUpdate_ = false;
    
    // Preview meshes
    std::unique_ptr<class Mesh> sphereMesh_;
    std::unique_ptr<class Mesh> cubeMesh_;
    std::unique_ptr<class Mesh> planeMesh_;
    std::unique_ptr<class Mesh> cylinderMesh_;
};

/**
 * Preview for 2D textures.
 */
class TexturePreview {
public:
    enum class Channel { RGB, R, G, B, A };
    
    void setTexture(VkImageView imageView, VkSampler sampler);
    void draw(float width, float height);
    
    void setChannel(Channel channel) { channel_ = channel; }
    void setMipLevel(int level) { mipLevel_ = level; }
    void setExposure(float exposure) { exposure_ = exposure; }
    void setShowAlpha(bool show) { showAlpha_ = show; }
    
private:
    VkDescriptorSet descriptor_ = VK_NULL_HANDLE;
    Channel channel_ = Channel::RGB;
    int mipLevel_ = 0;
    float exposure_ = 1.0f;
    bool showAlpha_ = false;
};

} // namespace Sanic::Editor
