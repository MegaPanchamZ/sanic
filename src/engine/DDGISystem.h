#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include "VulkanContext.h"
#include "AccelerationStructureBuilder.h"

// DDGI Configuration
struct DDGIConfig {
    // Probe grid dimensions
    glm::ivec3 probeCount = glm::ivec3(8, 4, 8);  // 8x4x8 = 256 probes
    
    // World-space spacing between probes
    glm::vec3 probeSpacing = glm::vec3(4.0f, 3.0f, 4.0f);
    
    // Origin of the probe grid (bottom-left-back corner)
    glm::vec3 gridOrigin = glm::vec3(-16.0f, 0.0f, -16.0f);
    
    // Number of rays per probe per frame
    uint32_t raysPerProbe = 256;
    
    // Irradiance probe resolution (octahedral, with 1-pixel border)
    uint32_t irradianceProbeSize = 8;   // 8x8 per probe (6x6 + border)
    
    // Depth probe resolution (octahedral, with 1-pixel border)  
    uint32_t depthProbeSize = 16;       // 16x16 per probe (14x14 + border)
    
    // Hysteresis for temporal blending (0 = no history, 1 = no update)
    float hysteresis = 0.97f;
    
    // Max ray distance
    float maxRayDistance = 100.0f;
    
    // Normal bias to prevent self-intersection
    float normalBias = 0.25f;
    
    // View bias along view direction
    float viewBias = 0.25f;
};

// Uniform buffer for DDGI shaders
struct DDGIUniforms {
    // Grid configuration
    glm::ivec4 probeCount;          // xyz = count, w = total probes
    glm::vec4 probeSpacing;         // xyz = spacing, w = 1/maxDistance
    glm::vec4 gridOrigin;           // xyz = origin, w = hysteresis
    
    // Texture sizes
    glm::ivec4 irradianceTextureSize;  // xy = texture size, zw = probe size with border
    glm::ivec4 depthTextureSize;       // xy = texture size, zw = probe size with border
    
    // Ray tracing params
    glm::vec4 rayParams;            // x = raysPerProbe, y = maxDistance, z = normalBias, w = viewBias
    
    // Random rotation for ray directions (changes each frame)
    glm::mat4 randomRotation;
};

class DDGISystem {
public:
    DDGISystem(VulkanContext& context, VkDescriptorPool descriptorPool);
    ~DDGISystem();
    
    // Initialize the DDGI system with given config
    void initialize(const DDGIConfig& config = DDGIConfig());
    
    // Update TLAS reference for ray tracing
    void setAccelerationStructure(VkAccelerationStructureKHR tlas);
    
    // Called each frame to update probes
    // Phase 1: Trace rays from probes
    // Phase 2: Update irradiance and depth textures
    void update(VkCommandBuffer commandBuffer, uint32_t frameIndex);
    
    // Get resources for composition shader
    VkImageView getIrradianceImageView() const { return irradianceImageView; }
    VkImageView getDepthImageView() const { return depthImageView; }
    VkSampler getProbeSampler() const { return probeSampler; }
    
    // Get the uniform buffer for shader binding
    VkBuffer getUniformBuffer() const { return uniformBuffer; }
    VkDeviceSize getUniformBufferSize() const { return sizeof(DDGIUniforms); }
    
    // Get descriptor set layout for shaders that need DDGI
    VkDescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }
    VkDescriptorSet getDescriptorSet() const { return descriptorSet; }
    
    // Get probe grid info for debug visualization
    const DDGIConfig& getConfig() const { return config; }
    glm::vec3 getProbeWorldPosition(int probeIndex) const;
    
private:
    void createProbeTextures();
    void createRadianceBuffer();
    void createUniformBuffer();
    void createDescriptorSetLayout();
    void createDescriptorSet();
    void createRayTracePipeline();
    void createProbeUpdatePipeline();
    void createSampler();
    void updateUniforms(uint32_t frameIndex);
    
    // Helper to create storage image
    void createStorageImage(uint32_t width, uint32_t height, VkFormat format,
                           VkImage& image, VkDeviceMemory& memory, VkImageView& view);
    
    // Vulkan resources
    VulkanContext& context;
    VkDevice device;
    VkPhysicalDevice physicalDevice;
    VkDescriptorPool descriptorPool;
    
    DDGIConfig config;
    
    // Irradiance texture atlas (stores all probe irradiance in 2D texture)
    VkImage irradianceImage = VK_NULL_HANDLE;
    VkDeviceMemory irradianceMemory = VK_NULL_HANDLE;
    VkImageView irradianceImageView = VK_NULL_HANDLE;
    
    // Depth texture atlas (stores visibility/distance for each probe)
    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory = VK_NULL_HANDLE;
    VkImageView depthImageView = VK_NULL_HANDLE;
    
    // Intermediate radiance buffer (ray trace results before filtering into probes)
    VkBuffer radianceBuffer = VK_NULL_HANDLE;
    VkDeviceMemory radianceMemory = VK_NULL_HANDLE;
    
    // Uniform buffer
    VkBuffer uniformBuffer = VK_NULL_HANDLE;
    VkDeviceMemory uniformBufferMemory = VK_NULL_HANDLE;
    void* uniformBufferMapped = nullptr;
    
    // Sampler for probe textures
    VkSampler probeSampler = VK_NULL_HANDLE;
    
    // Descriptor set layout and set
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    
    // Ray trace pipeline (compute shader that dispatches RT)
    VkPipeline rayTracePipeline = VK_NULL_HANDLE;
    VkPipelineLayout rayTracePipelineLayout = VK_NULL_HANDLE;
    
    // Probe update pipeline (compute shader to blend into textures)
    VkPipeline probeUpdatePipeline = VK_NULL_HANDLE;
    VkPipelineLayout probeUpdatePipelineLayout = VK_NULL_HANDLE;
    
    // TLAS for ray tracing
    VkAccelerationStructureKHR tlas = VK_NULL_HANDLE;
    
    // Frame counter for temporal accumulation
    uint32_t frameCounter = 0;
};
