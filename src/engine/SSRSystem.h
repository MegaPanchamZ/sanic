#pragma once
#include "VulkanContext.h"
#include <glm/glm.hpp>

struct SSRConfig {
    float maxDistance = 50.0f;        // Maximum ray march distance
    float thickness = 0.5f;           // Depth comparison thickness
    float maxSteps = 64.0f;           // Max ray march steps
    float roughnessThreshold = 0.3f;  // Above this, prefer RT
    bool rtFallbackEnabled = true;    // Use RT for misses
};

struct SSRUniforms {
    glm::mat4 view;
    glm::mat4 projection;
    glm::mat4 invView;
    glm::mat4 invProjection;
    glm::vec4 cameraPos;
    glm::vec2 screenSize;
    float maxDistance;
    float thickness;
    float maxSteps;
    float roughnessThreshold;
    float rtFallbackEnabled;
    float _padding;
};

class SSRSystem {
public:
    SSRSystem(VulkanContext& context, 
              uint32_t width, uint32_t height,
              VkDescriptorPool descriptorPool);
    ~SSRSystem();
    
    // Set the TLAS for ray-traced fallback
    void setTLAS(VkAccelerationStructureKHR tlas) { this->tlas = tlas; }
    
    void update(VkCommandBuffer cmd,
                const glm::mat4& view,
                const glm::mat4& projection,
                const glm::vec3& cameraPos,
                VkImageView positionView,
                VkImageView normalView,
                VkImageView albedoView,
                VkImageView pbrView,
                VkImageView depthView,
                VkImageView sceneColorView,
                VkSampler sampler);
    
    void resize(uint32_t width, uint32_t height);
    
    VkImageView getReflectionImageView() const { return reflectionImageView; }
    VkImage getReflectionImage() const { return reflectionImage; }
    
    void setConfig(const SSRConfig& config) { this->config = config; }
    SSRConfig& getConfig() { return config; }
    
private:
    VulkanContext& context;
    VkAccelerationStructureKHR tlas = VK_NULL_HANDLE;
    uint32_t width, height;
    VkDescriptorPool descriptorPool;
    SSRConfig config;
    bool needsImageTransition = true;
    
    // Reflection output image
    VkImage reflectionImage = VK_NULL_HANDLE;
    VkDeviceMemory reflectionMemory = VK_NULL_HANDLE;
    VkImageView reflectionImageView = VK_NULL_HANDLE;
    
    // Uniform buffer
    VkBuffer uniformBuffer = VK_NULL_HANDLE;
    VkDeviceMemory uniformMemory = VK_NULL_HANDLE;
    void* uniformMapped = nullptr;
    
    // Compute pipeline
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline computePipeline = VK_NULL_HANDLE;
    
    void createReflectionImage();
    void createUniformBuffer();
    void createDescriptorSetLayout();
    void createComputePipeline();
    void createDescriptorSet();
    
    void destroyResources();
    
    VkShaderModule createShaderModule(const std::vector<char>& code);
    std::vector<char> readFile(const std::string& filename);
};
