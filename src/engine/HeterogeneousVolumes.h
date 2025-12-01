/**
 * HeterogeneousVolumes.h
 * 
 * Heterogeneous volume rendering system for smoke, fire, clouds.
 * Based on Unreal Engine 5's Heterogeneous Volumes implementation.
 * 
 * Features:
 * - Sparse VDB-like volume representation
 * - Adaptive ray marching
 * - Lumen GI integration
 * - Fire/blackbody emission
 * - Phase function support (HG, Rayleigh)
 * - Temporal reprojection for stability
 * 
 * Reference: Engine/Source/Runtime/Renderer/Private/HeterogeneousVolumes/
 */

#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>

class VulkanContext;
class RenderGraph;

namespace Sanic {

// ============================================================================
// CONSTANTS
// ============================================================================

constexpr uint32_t VOLUME_BRICK_SIZE = 8;           // Voxels per brick dimension
constexpr uint32_t MAX_HETEROGENEOUS_VOLUMES = 64;
constexpr uint32_t VOLUME_ATLAS_SIZE = 512;         // 3D atlas dimension

// ============================================================================
// VOLUME DATA TYPES
// ============================================================================

/**
 * Volume data channels
 */
enum class VolumeChannel : uint32_t {
    Density = 0,
    Temperature = 1,
    EmissionR = 2,
    EmissionG = 3,
    EmissionB = 4,
    VelocityX = 5,
    VelocityY = 6,
    VelocityZ = 7,
    
    Count = 8
};

/**
 * Volume rendering mode
 */
enum class VolumeRenderMode : uint32_t {
    PathTraced = 0,     // Full path tracing (highest quality)
    RayMarched = 1,     // Standard ray marching
    FastApprox = 2      // Fast approximation for many volumes
};

/**
 * Phase function types
 */
enum class PhaseFunction : uint32_t {
    Isotropic = 0,
    HenyeyGreenstein = 1,
    Rayleigh = 2,
    Mie = 3,
    Schlick = 4
};

// ============================================================================
// VOLUME STRUCTURES
// ============================================================================

/**
 * Sparse brick - a small 8x8x8 region of volume data
 */
struct VolumeBrick {
    uint32_t atlasOffset;       // Offset in 3D atlas
    float minDensity;           // For early ray termination
    float maxDensity;
    uint32_t flags;
};

/**
 * Volume bounds in world space
 */
struct VolumeBounds {
    glm::vec3 min;
    glm::vec3 max;
    
    glm::vec3 size() const { return max - min; }
    glm::vec3 center() const { return (min + max) * 0.5f; }
};

/**
 * HeterogeneousVolume - A single volumetric object
 */
struct HeterogeneousVolume {
    uint32_t id = 0;
    std::string name;
    
    // Transform
    glm::mat4 worldMatrix = glm::mat4(1.0f);
    glm::mat4 invWorldMatrix = glm::mat4(1.0f);
    VolumeBounds localBounds;
    
    // Resolution
    glm::uvec3 resolution = glm::uvec3(64);
    glm::uvec3 brickCount = glm::uvec3(8);  // resolution / BRICK_SIZE
    
    // Appearance
    glm::vec3 scattering = glm::vec3(1.0f);     // Scattering coefficient
    glm::vec3 absorption = glm::vec3(0.1f);     // Absorption coefficient
    glm::vec3 emission = glm::vec3(0.0f);       // Base emission color
    float densityScale = 1.0f;
    float temperatureScale = 1.0f;
    
    // Phase function
    PhaseFunction phaseFunction = PhaseFunction::HenyeyGreenstein;
    float phaseG = 0.0f;  // Anisotropy: -1=back, 0=iso, 1=forward
    
    // Shadow
    bool castsShadow = true;
    float shadowDensityScale = 1.0f;
    
    // Fire/blackbody settings
    bool useBlackbody = false;
    float blackbodyIntensity = 1.0f;
    float temperatureOffset = 0.0f;
    
    // Animation
    glm::vec3 velocity = glm::vec3(0.0f);
    float noiseScale = 0.0f;
    float noiseSpeed = 0.0f;
    
    // Sparse data
    std::vector<VolumeBrick> bricks;
    std::vector<float> densityData;     // Voxel density values
    std::vector<float> temperatureData; // Optional temperature field
    std::vector<glm::vec3> emissionData; // Optional emission field
    
    // Runtime state
    bool isDirty = true;
    uint32_t atlasSlot = UINT32_MAX;
    
    // Methods
    void setResolution(uint32_t x, uint32_t y, uint32_t z);
    void setDensity(uint32_t x, uint32_t y, uint32_t z, float value);
    float getDensity(uint32_t x, uint32_t y, uint32_t z) const;
    void setTemperature(uint32_t x, uint32_t y, uint32_t z, float value);
    void setEmission(uint32_t x, uint32_t y, uint32_t z, const glm::vec3& value);
    
    void buildBricks();  // Build sparse brick structure
    void fillWithNoise(float baseFrequency, int octaves, float persistence);
};

/**
 * GPU volume data (padded to 256 bytes)
 */
struct GPUHeterogeneousVolume {
    glm::mat4 worldMatrix;
    glm::mat4 invWorldMatrix;
    
    glm::vec4 boundsMin;        // xyz = min, w = densityScale
    glm::vec4 boundsMax;        // xyz = max, w = temperatureScale
    
    glm::vec4 scatteringAbsorption; // rgb = scattering, a = absorption.r
    glm::vec4 absorptionEmission;   // rg = absorption.gb, ba = emission.rg
    glm::vec4 emissionPhase;        // r = emission.b, g = phaseG, b = phaseType, a = flags
    
    glm::uvec4 resolutionBrickCount; // xyz = resolution, w = brickCount
    glm::uvec4 atlasParams;          // x = atlasSlot, y = brickOffset, zw = reserved
};

// ============================================================================
// HETEROGENEOUS VOLUMES SYSTEM
// ============================================================================

/**
 * HeterogeneousVolumesConfig - System configuration
 */
struct HeterogeneousVolumesConfig {
    // Quality
    VolumeRenderMode renderMode = VolumeRenderMode::RayMarched;
    uint32_t maxRaymarchSteps = 128;
    float stepSize = 0.5f;              // World units
    float shadowStepSize = 1.0f;
    
    // Jittering for temporal stability
    bool useJitter = true;
    bool useBlueNoise = true;
    
    // Temporal reprojection
    bool enableTemporal = true;
    float temporalBlend = 0.9f;
    
    // Lighting
    bool enableShadows = true;
    bool enableMultiScatter = true;
    uint32_t multiScatterSteps = 4;
    
    // Lumen integration
    bool injectToLumen = true;
    bool receiveLumenGI = true;
    
    // Performance
    bool useOcclusionCulling = true;
    float lodBias = 0.0f;
};

/**
 * HeterogeneousVolumesSystem - Main system class
 */
class HeterogeneousVolumesSystem {
public:
    HeterogeneousVolumesSystem(VulkanContext& context);
    ~HeterogeneousVolumesSystem();
    
    bool initialize(uint32_t width, uint32_t height, 
                   const HeterogeneousVolumesConfig& config = {});
    void shutdown();
    void resize(uint32_t width, uint32_t height);
    
    // Configuration
    void setConfig(const HeterogeneousVolumesConfig& config);
    const HeterogeneousVolumesConfig& getConfig() const { return config_; }
    
    // Volume management
    uint32_t createVolume(const std::string& name);
    void updateVolume(uint32_t id, const HeterogeneousVolume& volume);
    void deleteVolume(uint32_t id);
    HeterogeneousVolume* getVolume(uint32_t id);
    const HeterogeneousVolume* getVolume(uint32_t id) const;
    
    // Import from VDB or other formats
    uint32_t importVDB(const std::string& path);
    
    // Per-frame update
    void beginFrame(const glm::mat4& view, const glm::mat4& proj,
                   const glm::vec3& cameraPos);
    
    /**
     * Phase 1: Update volume data in GPU atlas
     */
    void updateAtlas(VkCommandBuffer cmd);
    
    /**
     * Phase 2: Raymarch volumes
     */
    void raymarch(VkCommandBuffer cmd,
                 VkImageView depthBuffer,
                 VkImageView shadowMap);
    
    /**
     * Phase 3: Inject emission to Lumen
     */
    void injectToLumen(VkCommandBuffer cmd,
                      VkBuffer radianceCacheBuffer);
    
    /**
     * Phase 4: Composite with scene
     */
    void composite(VkCommandBuffer cmd,
                  VkImageView sceneColor,
                  VkImageView outputColor);
    
    // Results
    VkImageView getVolumeScatteringView() const { return scatteringView_; }
    VkImageView getVolumeTransmittanceView() const { return transmittanceView_; }
    
    // Debug
    void debugVisualize(VkCommandBuffer cmd, VkImageView output, int mode = 0);
    
private:
    void createResources();
    void createPipelines();
    void createAtlas();
    void updateVolumeBuffer();
    void uploadVolumeToAtlas(HeterogeneousVolume& volume);
    
    VulkanContext& context_;
    HeterogeneousVolumesConfig config_;
    
    uint32_t screenWidth_ = 1920;
    uint32_t screenHeight_ = 1080;
    uint32_t frameIndex_ = 0;
    
    // Volumes
    std::vector<std::unique_ptr<HeterogeneousVolume>> volumes_;
    std::unordered_map<uint32_t, uint32_t> idToIndex_;
    uint32_t nextVolumeId_ = 1;
    
    // Camera data
    glm::mat4 viewMatrix_;
    glm::mat4 projMatrix_;
    glm::mat4 viewProjMatrix_;
    glm::mat4 prevViewProjMatrix_;
    glm::vec3 cameraPosition_;
    
    // 3D Volume Atlas
    VkImage volumeAtlas_ = VK_NULL_HANDLE;
    VkDeviceMemory volumeAtlasMemory_ = VK_NULL_HANDLE;
    VkImageView volumeAtlasView_ = VK_NULL_HANDLE;
    std::vector<bool> atlasSlotUsed_;
    
    // Result textures
    VkImage scatteringImage_ = VK_NULL_HANDLE;
    VkDeviceMemory scatteringMemory_ = VK_NULL_HANDLE;
    VkImageView scatteringView_ = VK_NULL_HANDLE;
    
    VkImage transmittanceImage_ = VK_NULL_HANDLE;
    VkDeviceMemory transmittanceMemory_ = VK_NULL_HANDLE;
    VkImageView transmittanceView_ = VK_NULL_HANDLE;
    
    // History for temporal reprojection
    std::array<VkImage, 2> historyImages_;
    std::array<VkDeviceMemory, 2> historyMemory_;
    std::array<VkImageView, 2> historyViews_;
    uint32_t currentHistoryIndex_ = 0;
    
    // GPU buffers
    VkBuffer volumeBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory volumeBufferMemory_ = VK_NULL_HANDLE;
    
    VkBuffer uniformBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory uniformMemory_ = VK_NULL_HANDLE;
    void* uniformMapped_ = nullptr;
    
    // Uniform data
    struct VolumeUniforms {
        glm::mat4 viewMatrix;
        glm::mat4 projMatrix;
        glm::mat4 invViewMatrix;
        glm::mat4 invProjMatrix;
        glm::mat4 viewProjMatrix;
        glm::mat4 prevViewProjMatrix;
        
        glm::vec4 cameraPosition;
        glm::vec4 screenParams;
        
        glm::vec4 sunDirectionIntensity;
        glm::vec4 sunColor;
        
        uint32_t volumeCount;
        uint32_t maxSteps;
        float stepSize;
        float time;
        
        uint32_t flags;
        float temporalBlend;
        float jitterScale;
        uint32_t frameIndex;
    };
    
    // Samplers
    VkSampler linearSampler_ = VK_NULL_HANDLE;
    VkSampler volumeSampler_ = VK_NULL_HANDLE;
    
    // Pipelines
    VkPipeline raymarchPipeline_ = VK_NULL_HANDLE;
    VkPipeline compositePipeline_ = VK_NULL_HANDLE;
    VkPipeline lumenInjectPipeline_ = VK_NULL_HANDLE;
    
    VkPipelineLayout computeLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;
    
    // Helpers
    void createImage3D(VkImage& image, VkDeviceMemory& memory, VkImageView& view,
                      uint32_t width, uint32_t height, uint32_t depth,
                      VkFormat format, VkImageUsageFlags usage);
    void createImage2D(VkImage& image, VkDeviceMemory& memory, VkImageView& view,
                      uint32_t width, uint32_t height,
                      VkFormat format, VkImageUsageFlags usage);
    VkShaderModule loadShader(const std::string& path);
};

// ============================================================================
// BLACKBODY UTILITIES
// ============================================================================

namespace Blackbody {
    /**
     * Convert temperature (Kelvin) to RGB color
     * Based on Planck's law approximation
     */
    glm::vec3 temperatureToRGB(float kelvin);
    
    /**
     * Get emission intensity for a given temperature
     */
    float emissionIntensity(float kelvin, float baseIntensity = 1.0f);
}

// ============================================================================
// PHASE FUNCTION UTILITIES
// ============================================================================

namespace VolumePhase {
    /**
     * Isotropic phase function
     */
    inline float isotropic() { return 1.0f / (4.0f * 3.14159265f); }
    
    /**
     * Henyey-Greenstein phase function
     * g: anisotropy parameter (-1 to 1)
     */
    inline float henyeyGreenstein(float cosTheta, float g) {
        float g2 = g * g;
        float denom = 1.0f + g2 - 2.0f * g * cosTheta;
        return (1.0f - g2) / (4.0f * 3.14159265f * denom * std::sqrt(denom));
    }
    
    /**
     * Rayleigh phase function (for small particles)
     */
    inline float rayleigh(float cosTheta) {
        return (3.0f / (16.0f * 3.14159265f)) * (1.0f + cosTheta * cosTheta);
    }
    
    /**
     * Schlick approximation (faster than HG)
     */
    inline float schlick(float cosTheta, float k) {
        float denom = 1.0f + k * cosTheta;
        return (1.0f - k * k) / (4.0f * 3.14159265f * denom * denom);
    }
}

} // namespace Sanic
