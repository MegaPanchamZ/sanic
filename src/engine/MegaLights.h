/**
 * MegaLights.h
 * 
 * "Nanite for Lights" - Scalable dynamic lighting system.
 * Based on Unreal Engine 5's MegaLights implementation.
 * 
 * Features:
 * - Light clustering for efficient culling
 * - Stochastic light sampling with importance
 * - Virtual shadow map tiling per light
 * - Temporal denoising for stable shadows
 * - Integration with existing VSM system
 * 
 * Reference: Engine/Source/Runtime/Renderer/Private/MegaLights/
 */

#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <array>

class VulkanContext;
class VirtualShadowMap;
class RenderGraph;

namespace Sanic {

// ============================================================================
// LIGHT TYPES
// ============================================================================

enum class MegaLightType : uint32_t {
    Point = 0,
    Spot = 1,
    Rect = 2,       // Area light (rectangle)
    Disk = 3,       // Area light (disk)
    Directional = 4 // Sun/moon
};

// ============================================================================
// LIGHT STRUCTURES
// ============================================================================

/**
 * MegaLight - A light that participates in the MegaLights system
 */
struct MegaLight {
    // Transform
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 direction = glm::vec3(0.0f, -1.0f, 0.0f);
    glm::vec3 tangent = glm::vec3(1.0f, 0.0f, 0.0f);  // For area lights
    
    // Properties
    glm::vec3 color = glm::vec3(1.0f);
    float intensity = 1.0f;
    float range = 10.0f;
    float falloffExponent = 2.0f;
    
    // Spot light parameters
    float innerConeAngle = 0.0f;
    float outerConeAngle = 45.0f;
    
    // Area light dimensions
    glm::vec2 areaSize = glm::vec2(1.0f);
    
    // Shadow settings
    bool castsShadow = true;
    float shadowBias = 0.005f;
    float shadowNormalBias = 0.02f;
    uint32_t shadowResolution = 512;
    
    // Type
    MegaLightType type = MegaLightType::Point;
    
    // Runtime data
    uint32_t id = 0;
    float importance = 0.0f;        // Calculated per-frame
    uint32_t vsmPageStart = 0;      // VSM allocation
    uint32_t vsmPageCount = 0;
    bool enabled = true;
    
    // For stochastic sampling
    float samplingWeight = 1.0f;
};

/**
 * GPU-compatible light data (64 bytes)
 */
struct GPUMegaLight {
    glm::vec4 positionAndType;      // xyz = position, w = type
    glm::vec4 directionAndRange;    // xyz = direction, w = range
    glm::vec4 colorAndIntensity;    // xyz = color, w = intensity
    glm::vec4 spotParams;           // x = innerAngle, y = outerAngle, z = falloff, w = importance
};

/**
 * Light cluster - a 3D cell containing visible lights
 */
struct LightCluster {
    static constexpr uint32_t MAX_LIGHTS_PER_CLUSTER = 256;
    uint32_t lightIndices[MAX_LIGHTS_PER_CLUSTER];
    uint32_t lightCount;
    float totalImportance;
};

/**
 * GPU light cluster (header + indices)
 */
struct GPULightCluster {
    uint32_t offset;    // Offset into light index list
    uint32_t count;     // Number of lights in this cluster
};

/**
 * Light sample from stochastic sampling
 */
struct LightSample {
    uint32_t lightIndex;
    float pdf;          // Probability of selecting this light
    float weight;       // 1.0 / pdf for unbiased estimate
    float pad;
};

// ============================================================================
// CLUSTER GRID CONFIGURATION
// ============================================================================

struct MegaLightsConfig {
    // Cluster grid dimensions
    uint32_t clusterCountX = 16;
    uint32_t clusterCountY = 9;
    uint32_t clusterCountZ = 24;
    
    // Depth slicing
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    bool useExponentialDepth = true;
    float depthExponent = 2.0f;
    
    // Sampling
    uint32_t samplesPerPixel = 1;
    uint32_t maxLightsPerSample = 4;
    bool useImportanceSampling = true;
    bool useBlueNoise = true;
    
    // Denoising
    bool enableDenoising = true;
    uint32_t spatialFilterRadius = 3;
    float temporalBlend = 0.9f;
    float varianceClipGamma = 1.5f;
    
    // VSM integration
    bool enableVSM = true;
    uint32_t maxVSMPagesPerLight = 4;
    uint32_t totalVSMBudget = 4096;  // Total pages available
    
    // Quality
    float importanceThreshold = 0.001f;
    float shadowRayBias = 0.01f;
};

// ============================================================================
// MEGALIGHTS SYSTEM
// ============================================================================

class MegaLights {
public:
    MegaLights(VulkanContext& context);
    ~MegaLights();
    
    bool initialize(uint32_t width, uint32_t height, const MegaLightsConfig& config = {});
    void shutdown();
    void resize(uint32_t width, uint32_t height);
    
    // Configuration
    void setConfig(const MegaLightsConfig& config);
    const MegaLightsConfig& getConfig() const { return config_; }
    
    // Light management
    uint32_t addLight(const MegaLight& light);
    void updateLight(uint32_t id, const MegaLight& light);
    void removeLight(uint32_t id);
    void clearLights();
    
    const std::vector<MegaLight>& getLights() const { return lights_; }
    MegaLight* getLight(uint32_t id);
    
    // Per-frame update
    void beginFrame(const glm::mat4& view, const glm::mat4& proj, 
                    const glm::vec3& cameraPos);
    
    /**
     * Phase 1: Build light clusters
     * Assigns lights to 3D frustum clusters for efficient lookup
     */
    void buildLightClusters(VkCommandBuffer cmd);
    
    /**
     * Phase 2: Stochastic light sampling
     * For each pixel, sample N lights based on importance
     */
    void sampleLights(VkCommandBuffer cmd, 
                     VkImageView depthBuffer,
                     VkImageView normalBuffer,
                     VkImageView blueNoiseTexture);
    
    /**
     * Phase 3: Shadow evaluation
     * Evaluate shadows for sampled lights (RT or VSM)
     */
    void evaluateShadows(VkCommandBuffer cmd,
                        VkImageView depthBuffer,
                        VirtualShadowMap* vsm = nullptr);
    
    /**
     * Phase 4: Temporal denoising
     * Apply temporal filtering to reduce noise
     */
    void denoise(VkCommandBuffer cmd,
                VkImageView velocityBuffer,
                VkImageView depthBuffer);
    
    /**
     * Phase 5: Resolve final lighting
     * Combine sampled lighting with denoised shadows
     */
    void resolve(VkCommandBuffer cmd,
                VkImageView albedoBuffer,
                VkImageView normalBuffer,
                VkImageView pbrBuffer,
                VkImageView outputBuffer);
    
    // Results
    VkImageView getShadowMask() const { return shadowMaskView_; }
    VkImageView getLightingBuffer() const { return lightingBufferView_; }
    VkImageView getDenoisedShadows() const { return denoisedShadowView_; }
    
    // Debug
    void debugVisualize(VkCommandBuffer cmd, VkImageView output, int mode = 0);
    
    // Statistics
    struct Stats {
        uint32_t totalLights;
        uint32_t visibleLights;
        uint32_t clustersUsed;
        uint32_t averageLightsPerCluster;
        uint32_t vsmPagesUsed;
        float frameTime;
    };
    Stats getStats() const { return stats_; }
    
private:
    void createResources();
    void createPipelines();
    void createDescriptorSets();
    void updateLightBuffer();
    void calculateLightImportance(const glm::vec3& cameraPos, const glm::mat4& viewProj);
    void allocateVSMPages();
    
    VulkanContext& context_;
    MegaLightsConfig config_;
    
    uint32_t screenWidth_ = 1920;
    uint32_t screenHeight_ = 1080;
    uint32_t frameIndex_ = 0;
    
    // Lights
    std::vector<MegaLight> lights_;
    uint32_t nextLightId_ = 1;
    
    // Camera data
    glm::mat4 viewMatrix_;
    glm::mat4 projMatrix_;
    glm::mat4 viewProjMatrix_;
    glm::mat4 prevViewProjMatrix_;
    glm::vec3 cameraPosition_;
    
    // GPU buffers
    VkBuffer lightBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory lightBufferMemory_ = VK_NULL_HANDLE;
    
    VkBuffer clusterBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory clusterBufferMemory_ = VK_NULL_HANDLE;
    
    VkBuffer lightIndexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory lightIndexBufferMemory_ = VK_NULL_HANDLE;
    
    VkBuffer sampleBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory sampleBufferMemory_ = VK_NULL_HANDLE;
    
    // Uniform buffer
    struct MegaLightsUniforms {
        glm::mat4 viewMatrix;
        glm::mat4 projMatrix;
        glm::mat4 invViewMatrix;
        glm::mat4 invProjMatrix;
        glm::mat4 viewProjMatrix;
        glm::mat4 prevViewProjMatrix;
        
        glm::vec4 cameraPosition;
        glm::vec4 screenParams;     // width, height, 1/width, 1/height
        
        glm::ivec4 clusterDims;     // x, y, z, total
        glm::vec4 depthParams;      // near, far, exponent, time
        
        uint32_t lightCount;
        uint32_t samplesPerPixel;
        uint32_t frameIndex;
        uint32_t flags;
    };
    
    VkBuffer uniformBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory uniformMemory_ = VK_NULL_HANDLE;
    void* uniformMapped_ = nullptr;
    
    // Shadow mask (raw stochastic shadows)
    VkImage shadowMask_ = VK_NULL_HANDLE;
    VkDeviceMemory shadowMaskMemory_ = VK_NULL_HANDLE;
    VkImageView shadowMaskView_ = VK_NULL_HANDLE;
    
    // Lighting buffer (accumulated lighting before denoising)
    VkImage lightingBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory lightingBufferMemory_ = VK_NULL_HANDLE;
    VkImageView lightingBufferView_ = VK_NULL_HANDLE;
    
    // Denoised shadow buffer
    VkImage denoisedShadow_ = VK_NULL_HANDLE;
    VkDeviceMemory denoisedShadowMemory_ = VK_NULL_HANDLE;
    VkImageView denoisedShadowView_ = VK_NULL_HANDLE;
    
    // History buffers for temporal denoising
    std::array<VkImage, 2> historyBuffers_;
    std::array<VkDeviceMemory, 2> historyMemory_;
    std::array<VkImageView, 2> historyViews_;
    uint32_t currentHistoryIndex_ = 0;
    
    // Variance buffer for denoising
    VkImage varianceBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory varianceMemory_ = VK_NULL_HANDLE;
    VkImageView varianceView_ = VK_NULL_HANDLE;
    
    // Samplers
    VkSampler linearSampler_ = VK_NULL_HANDLE;
    VkSampler pointSampler_ = VK_NULL_HANDLE;
    
    // Pipelines
    VkPipeline clusterBuildPipeline_ = VK_NULL_HANDLE;
    VkPipeline lightSamplePipeline_ = VK_NULL_HANDLE;
    VkPipeline shadowEvalPipeline_ = VK_NULL_HANDLE;
    VkPipeline spatialDenoisePipeline_ = VK_NULL_HANDLE;
    VkPipeline temporalDenoisePipeline_ = VK_NULL_HANDLE;
    VkPipeline resolvePipeline_ = VK_NULL_HANDLE;
    
    VkPipelineLayout computeLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;
    
    // Statistics
    Stats stats_ = {};
    
    // Helpers
    void createImage2D(VkImage& image, VkDeviceMemory& memory, VkImageView& view,
                       uint32_t width, uint32_t height, VkFormat format,
                       VkImageUsageFlags usage);
    void createBuffer(VkBuffer& buffer, VkDeviceMemory& memory,
                     VkDeviceSize size, VkBufferUsageFlags usage,
                     VkMemoryPropertyFlags properties);
    VkShaderModule loadShader(const std::string& path);
};

// ============================================================================
// IMPORTANCE SAMPLING UTILITIES
// ============================================================================

namespace MegaLightsSampling {
    /**
     * Calculate light importance for a given surface point
     * Based on: distance, solid angle, light power
     */
    inline float calculateImportance(const MegaLight& light, 
                                     const glm::vec3& surfacePos,
                                     const glm::vec3& surfaceNormal) {
        glm::vec3 toLight = light.position - surfacePos;
        float distSq = glm::dot(toLight, toLight);
        float dist = std::sqrt(distSq);
        
        if (dist > light.range) return 0.0f;
        
        glm::vec3 L = toLight / dist;
        float NdotL = std::max(0.0f, glm::dot(surfaceNormal, L));
        
        if (NdotL <= 0.0f) return 0.0f;
        
        // Distance attenuation
        float attenuation = 1.0f / (distSq + 0.01f);
        float rangeFalloff = 1.0f - std::pow(dist / light.range, 4.0f);
        rangeFalloff = std::max(0.0f, rangeFalloff);
        
        // Light power
        float luminance = glm::dot(light.color, glm::vec3(0.299f, 0.587f, 0.114f));
        float power = luminance * light.intensity;
        
        // Spotlight falloff
        float spotFalloff = 1.0f;
        if (light.type == MegaLightType::Spot) {
            float cosTheta = glm::dot(-L, light.direction);
            float innerCos = std::cos(glm::radians(light.innerConeAngle));
            float outerCos = std::cos(glm::radians(light.outerConeAngle));
            spotFalloff = std::clamp((cosTheta - outerCos) / (innerCos - outerCos + 0.0001f), 0.0f, 1.0f);
        }
        
        return power * attenuation * rangeFalloff * NdotL * spotFalloff;
    }
    
    /**
     * Build CDF for importance sampling
     */
    inline void buildCDF(const std::vector<float>& importance, std::vector<float>& cdf) {
        cdf.resize(importance.size());
        float total = 0.0f;
        for (size_t i = 0; i < importance.size(); ++i) {
            total += importance[i];
            cdf[i] = total;
        }
        // Normalize
        if (total > 0.0f) {
            for (float& v : cdf) {
                v /= total;
            }
        }
    }
    
    /**
     * Sample from CDF using uniform random value
     */
    inline uint32_t sampleCDF(const std::vector<float>& cdf, float u) {
        // Binary search
        auto it = std::lower_bound(cdf.begin(), cdf.end(), u);
        return static_cast<uint32_t>(std::distance(cdf.begin(), it));
    }
}

} // namespace Sanic
