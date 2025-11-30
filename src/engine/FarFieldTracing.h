/**
 * FarFieldTracing.h
 * 
 * Lumen-style far-field tracing using global distance fields.
 * Provides indirect lighting contribution from distant geometry.
 * 
 * Key features:
 * - Global SDF representation for far-field geometry
 * - Dithered transition between near and far field
 * - Hardware ray tracing fallback (optional)
 * - Distance-based quality scaling
 */

#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

class VulkanContext;

// Far-field tracing mode
enum class FarFieldMode {
    Disabled,           // No far-field tracing
    GlobalSDF,          // Use global signed distance field
    HardwareRT,         // Use hardware ray tracing
    Hybrid              // SDF for most, RT for high quality
};

// Global SDF brick
struct SDFBrick {
    glm::ivec3 position;    // Brick position in grid
    uint32_t mipLevel;
    glm::vec3 boundsMin;
    float pad0;
    glm::vec3 boundsMax;
    float pad1;
    uint32_t dataOffset;    // Offset into SDF data buffer
    uint32_t flags;
    uint32_t pad2, pad3;
};

// GPU brick data
struct alignas(16) GPUSDFBrick {
    glm::ivec4 positionMip;     // xyz = position, w = mip
    glm::vec4 boundsMin;
    glm::vec4 boundsMax;
    glm::ivec4 dataOffsetFlags; // x = offset, y = flags
};

// Far-field configuration
struct FarFieldConfig {
    FarFieldMode mode = FarFieldMode::GlobalSDF;
    
    // Distance thresholds
    float nearFieldRadius = 100.0f;     // Near-field tracing radius
    float farFieldMaxDistance = 500.0f; // Max far-field trace
    float transitionWidth = 20.0f;      // Dither transition width
    
    // Global SDF settings
    uint32_t globalSDFResolution = 256;
    float globalSDFVoxelSize = 2.0f;
    uint32_t brickResolution = 8;
    uint32_t maxBricks = 65536;
    
    // Quality
    uint32_t farFieldSamples = 8;
    float farFieldRoughnessBias = 0.3f;
    bool useTemporalAccumulation = true;
    
    // Hardware RT (if supported)
    bool useHardwareRTForFarField = false;
};

class FarFieldTracing {
public:
    FarFieldTracing() = default;
    ~FarFieldTracing();
    
    bool initialize(VulkanContext* context, const FarFieldConfig& config = {});
    void cleanup();
    
    /**
     * Check if hardware RT is available for far-field
     */
    bool supportsHardwareRT() const { return hasHardwareRT_; }
    
    /**
     * Build/update global SDF from mesh data
     * @param cmd Command buffer
     * @param meshSDFs Array of mesh SDF views
     * @param transforms World transforms for each mesh
     */
    void updateGlobalSDF(VkCommandBuffer cmd,
                         const std::vector<VkImageView>& meshSDFs,
                         const std::vector<glm::mat4>& transforms,
                         const glm::vec3& cameraPos);
    
    /**
     * Build far-field acceleration structure (for hardware RT)
     */
    void buildFarFieldTLAS(VkCommandBuffer cmd,
                           VkAccelerationStructureKHR* meshBLAS,
                           const std::vector<glm::mat4>& transforms,
                           uint32_t meshCount);
    
    /**
     * Trace far-field radiance
     * @param cmd Command buffer
     * @param rayOrigins Origin positions (from near-field hits)
     * @param rayDirections Trace directions
     * @param outputRadiance Far-field radiance output
     */
    void traceFarField(VkCommandBuffer cmd,
                       VkImageView rayOrigins,
                       VkImageView rayDirections,
                       VkImageView surfaceCache,
                       VkImageView outputRadiance);
    
    /**
     * Compute far-field contribution for screen probes
     */
    void traceFarFieldProbes(VkCommandBuffer cmd,
                             VkBuffer probeBuffer,
                             uint32_t probeCount,
                             VkImageView surfaceCache,
                             VkBuffer outputRadiance);
    
    // Accessors
    VkImageView getGlobalSDFView() const { return globalSDFView_; }
    VkBuffer getBrickBuffer() const { return brickBuffer_; }
    VkAccelerationStructureKHR getFarFieldTLAS() const { return farFieldTLAS_; }
    
    const FarFieldConfig& getConfig() const { return config_; }
    
    struct Stats {
        uint32_t activeBricks;
        uint32_t sdfMemoryBytes;
        float averageTraceDistance;
        uint32_t farFieldHits;
    };
    Stats getStats() const;
    
private:
    bool createGlobalSDF();
    bool createBrickBuffer();
    bool createPipelines();
    bool loadShader(const std::string& path, VkShaderModule& outModule);
    
    // SDF compositing
    void compositeMeshSDFs(VkCommandBuffer cmd,
                           const std::vector<VkImageView>& meshSDFs,
                           const std::vector<glm::mat4>& transforms);
    
    // Brick management
    void updateBrickAllocation(const glm::vec3& cameraPos);
    void streamBricks(VkCommandBuffer cmd);
    
    VulkanContext* context_ = nullptr;
    FarFieldConfig config_;
    bool initialized_ = false;
    bool hasHardwareRT_ = false;
    
    // Global SDF volume
    VkImage globalSDF_ = VK_NULL_HANDLE;
    VkDeviceMemory globalSDFMemory_ = VK_NULL_HANDLE;
    VkImageView globalSDFView_ = VK_NULL_HANDLE;
    
    // Brick data
    std::vector<SDFBrick> bricks_;
    VkBuffer brickBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory brickMemory_ = VK_NULL_HANDLE;
    
    // SDF data buffer (actual distance values)
    VkBuffer sdfDataBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory sdfDataMemory_ = VK_NULL_HANDLE;
    
    // Far-field TLAS (for hardware RT)
    VkAccelerationStructureKHR farFieldTLAS_ = VK_NULL_HANDLE;
    VkBuffer farFieldTLASBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory farFieldTLASMemory_ = VK_NULL_HANDLE;
    
    // Pipelines
    VkPipeline sdfCompositePipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout sdfCompositeLayout_ = VK_NULL_HANDLE;
    
    VkPipeline farFieldTracePipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout farFieldTraceLayout_ = VK_NULL_HANDLE;
    
    VkPipeline farFieldProbePipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout farFieldProbeLayout_ = VK_NULL_HANDLE;
    
    // Hardware RT pipeline (optional)
    VkPipeline farFieldRTPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout farFieldRTLayout_ = VK_NULL_HANDLE;
    
    // Descriptors
    VkDescriptorPool descPool_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descLayout_ = VK_NULL_HANDLE;
    VkDescriptorSet descSet_ = VK_NULL_HANDLE;
    
    VkSampler sdfSampler_ = VK_NULL_HANDLE;
    
    // Statistics
    glm::vec3 lastCameraPos_ = glm::vec3(0.0f);
    uint32_t frameIndex_ = 0;
};

