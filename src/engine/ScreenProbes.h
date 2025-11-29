/**
 * ScreenProbes.h
 * 
 * Lumen-style screen-space probe system for global illumination.
 * Implements adaptive probe placement and hierarchical tracing.
 * 
 * Turn 25-27: Screen probe system
 */

#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

class VulkanContext;

// Screen probe - positioned in screen space, traces radiance
struct ScreenProbe {
    glm::vec2 screenPos;        // Screen-space position
    float depth;                // Depth at probe location
    uint32_t flags;             // Valid, needs update, etc.
    
    glm::vec3 worldPos;         // World-space position
    float radius;               // Influence radius
    
    glm::vec3 normal;           // Surface normal at probe
    uint32_t octahedralOffset;  // Offset into octahedral atlas
};

// GPU-compatible probe data
struct alignas(16) GPUScreenProbe {
    glm::vec4 positionDepth;    // xyz = world pos, w = depth
    glm::vec4 normalRadius;     // xyz = normal, w = radius
    glm::ivec4 atlasInfo;       // x = octahedralOffset, y = resolution, z = flags, w = pad
};

// Probe tile - 8x8 pixel tile with probe info
struct ProbeTile {
    uint32_t probeIndex;
    uint32_t probeCount;        // Multiple probes per tile for complex geometry
    glm::vec2 avgDirection;     // Average trace direction for tile
};

struct ScreenProbeConfig {
    uint32_t tileSize = 8;              // Pixels per probe tile
    uint32_t octahedralResolution = 8;  // Probe radiance resolution (8x8 per probe)
    uint32_t maxProbesPerTile = 4;      // Max probes in complex tiles
    uint32_t raysPerProbe = 64;         // Rays traced per probe per frame
    uint32_t temporalFrames = 8;        // Frames to accumulate
    float importanceSamplingBias = 0.5f;
    float maxTraceDistance = 200.0f;
    bool useHierarchicalTracing = true;
    bool useTemporalReuse = true;
};

struct ProbeAtlasConfig {
    uint32_t atlasWidth = 2048;
    uint32_t atlasHeight = 2048;
    uint32_t probeResolution = 8;       // Each probe is 8x8 in atlas
    uint32_t borderSize = 1;            // Border for filtering
    VkFormat radianceFormat = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
    VkFormat depthFormat = VK_FORMAT_R16_SFLOAT;
};

class ScreenProbes {
public:
    ScreenProbes() = default;
    ~ScreenProbes();
    
    bool initialize(VulkanContext* context, 
                    uint32_t screenWidth, 
                    uint32_t screenHeight,
                    const ScreenProbeConfig& config = {});
    void cleanup();
    
    // Per-frame update
    void placeProbes(VkCommandBuffer cmd,
                     VkImageView depthView,
                     VkImageView normalView,
                     const glm::mat4& viewProj,
                     const glm::mat4& invViewProj);
    
    // Trace radiance for probes
    void traceProbes(VkCommandBuffer cmd,
                     VkImageView gbufferAlbedo,
                     VkImageView gbufferNormal,
                     VkImageView gbufferDepth,
                     VkBuffer lightBuffer,
                     uint32_t lightCount);
    
    // Filter and denoise probe radiance
    void filterProbes(VkCommandBuffer cmd);
    
    // Interpolate probes to screen
    void interpolateToScreen(VkCommandBuffer cmd,
                             VkImageView outputRadiance,
                             VkImageView depthView,
                             VkImageView normalView);
    
    // Accessors
    VkImageView getProbeAtlasView() const { return probeAtlasView_; }
    VkBuffer getProbeBuffer() const { return probeBuffer_; }
    uint32_t getProbeCount() const { return probeCount_; }
    
    const ScreenProbeConfig& getConfig() const { return config_; }
    
private:
    bool createProbeAtlas();
    bool createProbeBuffers();
    bool createPipelines();
    bool loadShader(const std::string& path, VkShaderModule& outModule);
    
    VulkanContext* context_ = nullptr;
    bool initialized_ = false;
    
    ScreenProbeConfig config_;
    ProbeAtlasConfig atlasConfig_;
    uint32_t screenWidth_ = 0;
    uint32_t screenHeight_ = 0;
    uint32_t tileCountX_ = 0;
    uint32_t tileCountY_ = 0;
    uint32_t probeCount_ = 0;
    uint32_t frameIndex_ = 0;
    
    // Probe atlas (octahedral radiance storage)
    VkImage probeAtlas_ = VK_NULL_HANDLE;
    VkDeviceMemory probeAtlasMemory_ = VK_NULL_HANDLE;
    VkImageView probeAtlasView_ = VK_NULL_HANDLE;
    
    // Probe depth atlas
    VkImage probeDepthAtlas_ = VK_NULL_HANDLE;
    VkDeviceMemory probeDepthMemory_ = VK_NULL_HANDLE;
    VkImageView probeDepthView_ = VK_NULL_HANDLE;
    
    // History atlas for temporal accumulation
    VkImage historyAtlas_ = VK_NULL_HANDLE;
    VkDeviceMemory historyMemory_ = VK_NULL_HANDLE;
    VkImageView historyView_ = VK_NULL_HANDLE;
    
    // Probe data buffer
    VkBuffer probeBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory probeMemory_ = VK_NULL_HANDLE;
    
    // Tile buffer
    VkBuffer tileBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory tileMemory_ = VK_NULL_HANDLE;
    
    // Ray buffer for importance sampling
    VkBuffer rayBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory rayMemory_ = VK_NULL_HANDLE;
    
    // Pipelines
    VkPipeline probePlacePipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout probePlaceLayout_ = VK_NULL_HANDLE;
    
    VkPipeline probeTracePipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout probeTraceLayout_ = VK_NULL_HANDLE;
    
    VkPipeline probeFilterPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout probeFilterLayout_ = VK_NULL_HANDLE;
    
    VkPipeline probeInterpolatePipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout probeInterpolateLayout_ = VK_NULL_HANDLE;
    
    // Descriptors
    VkDescriptorPool descPool_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descLayout_ = VK_NULL_HANDLE;
    VkDescriptorSet descSet_ = VK_NULL_HANDLE;
    
    VkSampler probeSampler_ = VK_NULL_HANDLE;
};
