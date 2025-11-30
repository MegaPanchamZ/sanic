/**
 * LumenRadiosity.h
 * 
 * Lumen-style radiosity for multi-bounce indirect lighting.
 * Uses hemisphere probes on surface cache with SH encoding.
 * 
 * Key features:
 * - Probe grid on surface cache (configurable spacing)
 * - Hemisphere tracing per probe
 * - Spherical harmonics (L2) encoding
 * - Spatial filtering with plane weighting
 * - Temporal accumulation over multiple frames
 */

#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

class VulkanContext;

// Radiosity probe on surface cache
struct RadiosityProbe {
    glm::vec3 position;         // World position
    uint32_t cardIndex;         // Which surface cache card
    
    glm::vec3 normal;           // Surface normal
    float validity;             // How valid is this probe
    
    glm::vec2 atlasCoord;       // Position in surface cache atlas
    uint32_t age;               // Frames since last full update
    uint32_t flags;
};

// GPU probe data
struct alignas(16) GPURadiosityProbe {
    glm::vec4 positionValidity;     // xyz = position, w = validity
    glm::vec4 normalCardIndex;      // xyz = normal, w = cardIndex
    glm::vec4 atlasCoordAge;        // xy = atlasCoord, z = age, w = flags
};

// Spherical harmonics L2 coefficients (9 per channel = 27 total)
struct SHCoefficients {
    glm::vec4 r[3];     // Red channel (9 coefficients packed in 3 vec4)
    glm::vec4 g[3];     // Green channel
    glm::vec4 b[3];     // Blue channel
};

// GPU SH data
struct alignas(16) GPUSH {
    glm::vec4 coeffs[9];    // RGB packed: xyz = RGB for each SH basis
};

// Radiosity configuration
struct RadiosityConfig {
    // Probe placement
    uint32_t probeSpacing = 4;              // Texels between probes in surface cache
    uint32_t hemisphereResolution = 4;      // 4x4 = 16 rays per probe
    
    // Tracing
    uint32_t maxTraceDistance = 200;        // Max trace distance
    float traceBias = 0.1f;                 // Normal offset to avoid self-intersection
    bool useSoftwareTracing = true;         // vs hardware RT
    
    // Filtering
    bool usePlaneWeighting = true;          // Weight by plane alignment
    bool useProbeOcclusion = true;          // Check probe visibility
    float spatialFilterRadius = 2.0f;       // World-space filter radius
    
    // Temporal
    uint32_t temporalFrames = 4;            // Frames to accumulate
    float temporalWeight = 0.25f;           // Per-frame contribution
    
    // Quality
    uint32_t maxProbesPerFrame = 16384;     // Update budget
    bool useHardwareRT = false;             // Use RT for tracing
};

// Frame temporaries - per-frame allocated resources
struct RadiosityFrameData {
    // Probe atlases
    VkImage traceRadianceAtlas;             // Raw traced radiance
    VkImageView traceRadianceView;
    VkDeviceMemory traceRadianceMemory;
    
    // SH coefficient atlases (one per channel)
    VkImage probeSHRed;
    VkImageView probeSHRedView;
    VkDeviceMemory probeSHRedMemory;
    
    VkImage probeSHGreen;
    VkImageView probeSHGreenView;
    VkDeviceMemory probeSHGreenMemory;
    
    VkImage probeSHBlue;
    VkImageView probeSHBlueView;
    VkDeviceMemory probeSHBlueMemory;
    
    glm::ivec2 probeAtlasSize;
};

class LumenRadiosity {
public:
    LumenRadiosity() = default;
    ~LumenRadiosity();
    
    bool initialize(VulkanContext* context,
                    uint32_t surfaceCacheWidth,
                    uint32_t surfaceCacheHeight,
                    const RadiosityConfig& config = {});
    void cleanup();
    
    /**
     * Place probes on surface cache
     * @param cmd Command buffer
     * @param surfaceCacheDepth Surface cache depth atlas
     * @param surfaceCacheNormal Surface cache normal atlas
     */
    void placeProbes(VkCommandBuffer cmd,
                     VkImageView surfaceCacheDepth,
                     VkImageView surfaceCacheNormal);
    
    /**
     * Trace hemisphere for each probe
     * @param cmd Command buffer
     * @param surfaceCache Surface cache radiance
     * @param globalSDF Global SDF for far-field
     */
    void traceProbes(VkCommandBuffer cmd,
                     VkImageView surfaceCache,
                     VkImageView globalSDF,
                     VkBuffer lightBuffer,
                     uint32_t lightCount);
    
    /**
     * Spatial filter traced radiance
     */
    void spatialFilter(VkCommandBuffer cmd);
    
    /**
     * Convert traced radiance to SH
     */
    void convertToSH(VkCommandBuffer cmd);
    
    /**
     * Integrate SH probes to surface cache pixels
     * @param cmd Command buffer
     * @param indirectLightingAtlas Output indirect lighting
     */
    void integrateSH(VkCommandBuffer cmd,
                     VkImageView indirectLightingAtlas);
    
    /**
     * Temporal accumulation
     */
    void temporalAccumulate(VkCommandBuffer cmd);
    
    /**
     * Full radiosity update (calls all passes)
     */
    void update(VkCommandBuffer cmd,
                VkImageView surfaceCache,
                VkImageView surfaceCacheDepth,
                VkImageView surfaceCacheNormal,
                VkImageView globalSDF,
                VkBuffer lightBuffer,
                uint32_t lightCount,
                VkImageView indirectLightingAtlas);
    
    // Accessors
    VkBuffer getProbeBuffer() const { return probeBuffer_; }
    VkBuffer getSHBuffer() const { return shBuffer_; }
    VkImageView getTraceRadianceView() const;
    
    uint32_t getProbeCount() const { return probeCount_; }
    const RadiosityConfig& getConfig() const { return config_; }
    
    struct Stats {
        uint32_t totalProbes;
        uint32_t validProbes;
        uint32_t updatedThisFrame;
        float averageValidity;
    };
    Stats getStats() const;
    
private:
    bool createProbeBuffers();
    bool createFrameData();
    bool createPipelines();
    bool loadShader(const std::string& path, VkShaderModule& outModule);
    
    VulkanContext* context_ = nullptr;
    RadiosityConfig config_;
    bool initialized_ = false;
    
    uint32_t surfaceCacheWidth_ = 0;
    uint32_t surfaceCacheHeight_ = 0;
    uint32_t probeCount_ = 0;
    uint32_t frameIndex_ = 0;
    
    // Probe data
    std::vector<RadiosityProbe> probes_;
    VkBuffer probeBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory probeMemory_ = VK_NULL_HANDLE;
    
    // SH coefficients buffer
    VkBuffer shBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory shMemory_ = VK_NULL_HANDLE;
    
    // Frame temporaries (double-buffered)
    RadiosityFrameData frameData_[2];
    
    // History for temporal
    VkImage historyAtlas_ = VK_NULL_HANDLE;
    VkImageView historyView_ = VK_NULL_HANDLE;
    VkDeviceMemory historyMemory_ = VK_NULL_HANDLE;
    
    // Pipelines
    VkPipeline probePlacePipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout probePlaceLayout_ = VK_NULL_HANDLE;
    
    VkPipeline probeTracePipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout probeTraceLayout_ = VK_NULL_HANDLE;
    
    VkPipeline spatialFilterPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout spatialFilterLayout_ = VK_NULL_HANDLE;
    
    VkPipeline convertSHPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout convertSHLayout_ = VK_NULL_HANDLE;
    
    VkPipeline integratePipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout integrateLayout_ = VK_NULL_HANDLE;
    
    VkPipeline temporalPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout temporalLayout_ = VK_NULL_HANDLE;
    
    // Hardware RT tracing pipeline (optional)
    VkPipeline rtTracePipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout rtTraceLayout_ = VK_NULL_HANDLE;
    
    // Descriptors
    VkDescriptorPool descPool_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descLayout_ = VK_NULL_HANDLE;
    VkDescriptorSet descSet_[2] = {};  // Double-buffered
    
    VkSampler probeSampler_ = VK_NULL_HANDLE;
};

