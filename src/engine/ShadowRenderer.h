#pragma once
#include "VulkanContext.h"
#include "GameObject.h"
#include "Camera.h"
#include <glm/glm.hpp>
#include <vector>
#include <array>

struct ShadowUBOData {
    alignas(16) glm::mat4 cascadeViewProj[4];
    alignas(16) glm::vec4 cascadeSplits;
    alignas(16) glm::vec4 shadowParams;
    alignas(16) glm::mat4 lightSpaceMatrix; 
};

class ShadowRenderer {
public:
    ShadowRenderer(VulkanContext& context, VkDescriptorSetLayout descriptorSetLayout);
    ~ShadowRenderer();

    void render(VkCommandBuffer cmd, const std::vector<GameObject>& gameObjects);
    
    VkImageView getShadowImageView() const { return shadowArrayImageView; }
    VkSampler getShadowSampler() const { return shadowSampler; }
    
    ShadowUBOData computeShadowData(const Camera& camera, const glm::vec3& lightDir, uint32_t screenWidth, uint32_t screenHeight);

private:
    void createRenderPass();
    void createResources();
    void createPipeline(VkDescriptorSetLayout descriptorSetLayout);
    void calculateCascadeSplits(float nearClip, float farClip, float lambda = 0.5f);
    
    void createImage(uint32_t width, uint32_t height, uint32_t layers, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory);
    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t baseLayer, uint32_t layerCount, VkImageViewType viewType);
    
    VkShaderModule createShaderModule(const std::vector<char>& code);
    std::vector<char> readFile(const std::string& filename);

    VulkanContext& context;
    
    VkRenderPass renderPass;
    VkPipelineLayout pipelineLayout;
    VkPipeline pipeline;
    
    static const uint32_t CASCADE_COUNT = 4;
    static const uint32_t SHADOW_MAP_SIZE = 2048;
    
    VkImage shadowArrayImage;
    VkDeviceMemory shadowArrayImageMemory;
    VkImageView shadowArrayImageView;
    VkSampler shadowSampler;
    
    std::array<VkImageView, CASCADE_COUNT> cascadeViews;
    std::array<VkFramebuffer, CASCADE_COUNT> cascadeFramebuffers;
    
    std::array<float, 4> cascadeSplitDistances;
};
