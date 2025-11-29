/**
 * RadianceCache.h
 * 
 * World-space radiance cache using clipmaps.
 * Provides stable GI independent of screen resolution.
 * 
 * Turn 28-30: Radiance cache clipmaps
 */

#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

class VulkanContext;

// Clipmap level - each level covers 2x the area of the previous
struct ClipMapLevel {
    glm::vec3 center;           // World-space center
    float voxelSize;            // Size of each voxel
    
    glm::ivec3 resolution;      // Typically 64³ or 128³
    glm::ivec3 offset;          // Toroidal offset for scrolling
    
    VkImage radianceVolume;
    VkDeviceMemory radianceMemory;
    VkImageView radianceView;
    
    VkImage irradianceVolume;   // Pre-integrated irradiance
    VkDeviceMemory irradianceMemory;
    VkImageView irradianceView;
    
    bool needsUpdate;
};

// GPU clipmap data
struct alignas(16) GPUClipMapData {
    glm::vec4 centerExtent;     // xyz = center, w = half-extent
    glm::ivec4 resolutionOffset; // xyz = resolution, w = reserved
    glm::vec4 toroidalOffset;   // xyz = offset (0-1), w = voxelSize
};

struct RadianceCacheConfig {
    uint32_t clipMapLevels = 4;         // Number of clipmap levels
    uint32_t baseResolution = 64;       // Resolution of each level
    float baseCellSize = 0.5f;          // Size of finest voxel (meters)
    float clipMapScale = 2.0f;          // Scale between levels
    uint32_t probesPerCell = 1;         // Probe density
    uint32_t updateBudget = 1024;       // Max cells to update per frame
    VkFormat radianceFormat = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
    VkFormat irradianceFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    bool useSphericalHarmonics = true;  // SH vs direct storage
    uint32_t shOrder = 2;               // L2 SH = 9 coefficients
};

// Radiance probe for cache cell
struct RadianceProbe {
    glm::vec3 position;
    uint32_t clipLevel;
    
    // Spherical harmonics coefficients (L2 = 9 per channel)
    glm::vec4 shR[3];   // Red channel SH
    glm::vec4 shG[3];   // Green channel SH
    glm::vec4 shB[3];   // Blue channel SH
    
    float validity;     // How reliable is this probe
    uint32_t age;       // Frames since last update
};

// GPU-compatible probe
struct alignas(16) GPURadianceProbe {
    glm::vec4 positionValidity; // xyz = position, w = validity
    glm::vec4 shCoeffs[9];      // Packed SH coefficients
};

class RadianceCache {
public:
    RadianceCache() = default;
    ~RadianceCache();
    
    bool initialize(VulkanContext* context, const RadianceCacheConfig& config = {});
    void cleanup();
    
    // Update cache around camera position
    void update(VkCommandBuffer cmd,
                const glm::vec3& cameraPos,
                VkImageView gbufferDepth,
                VkImageView gbufferNormal,
                VkImageView gbufferAlbedo,
                VkBuffer lightBuffer,
                uint32_t lightCount);
    
    // Scroll clipmaps when camera moves
    void scrollClipMaps(VkCommandBuffer cmd, const glm::vec3& cameraPos);
    
    // Inject screen probes into cache
    void injectProbes(VkCommandBuffer cmd,
                      VkBuffer probeBuffer,
                      uint32_t probeCount);
    
    // Sample radiance at world position
    void sampleRadiance(VkCommandBuffer cmd,
                        VkImageView outputRadiance,
                        VkImageView depthView,
                        VkImageView normalView,
                        const glm::mat4& invViewProj);
    
    // Compute irradiance from radiance
    void computeIrradiance(VkCommandBuffer cmd);
    
    // Accessors
    VkImageView getRadianceView(uint32_t level) const;
    VkImageView getIrradianceView(uint32_t level) const;
    VkBuffer getClipMapBuffer() const { return clipMapBuffer_; }
    
    const RadianceCacheConfig& getConfig() const { return config_; }
    
private:
    bool createClipMaps();
    bool createBuffers();
    bool createPipelines();
    bool loadShader(const std::string& path, VkShaderModule& outModule);
    
    // Toroidal addressing helpers
    glm::ivec3 worldToClipCoord(const glm::vec3& worldPos, uint32_t level) const;
    glm::ivec3 getToroidalOffset(uint32_t level) const;
    
    VulkanContext* context_ = nullptr;
    bool initialized_ = false;
    
    RadianceCacheConfig config_;
    std::vector<ClipMapLevel> clipMaps_;
    glm::vec3 lastCameraPos_ = glm::vec3(0.0f);
    
    // Clipmap uniform buffer
    VkBuffer clipMapBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory clipMapMemory_ = VK_NULL_HANDLE;
    
    // Probe buffer for cache cells
    VkBuffer probeBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory probeMemory_ = VK_NULL_HANDLE;
    
    // Update queue buffer
    VkBuffer updateQueueBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory updateQueueMemory_ = VK_NULL_HANDLE;
    
    // Pipelines
    VkPipeline scrollPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout scrollLayout_ = VK_NULL_HANDLE;
    
    VkPipeline injectPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout injectLayout_ = VK_NULL_HANDLE;
    
    VkPipeline samplePipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout sampleLayout_ = VK_NULL_HANDLE;
    
    VkPipeline irradiancePipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout irradianceLayout_ = VK_NULL_HANDLE;
    
    // Descriptors
    VkDescriptorPool descPool_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descLayout_ = VK_NULL_HANDLE;
    VkDescriptorSet descSet_ = VK_NULL_HANDLE;
    
    VkSampler volumeSampler_ = VK_NULL_HANDLE;
};
