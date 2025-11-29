/**
 * VolumetricLighting.h
 * 
 * Volumetric fog and lighting using froxel-based approach.
 * 
 * Features:
 * - Froxel grid for volumetric scattering
 * - Light shafts (god rays) from directional lights
 * - Local fog volumes (box, sphere, height)
 * - Integration with shadow maps and DDGI
 * - Temporal reprojection for stable results
 * - Phase function support (Mie, Rayleigh, HG)
 */

#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <memory>

class VulkanContext;

namespace Sanic {

// ============================================================================
// VOLUMETRIC SETTINGS
// ============================================================================

struct VolumetricSettings {
    // Froxel grid resolution
    uint32_t froxelWidth = 160;
    uint32_t froxelHeight = 90;
    uint32_t froxelDepth = 128;
    
    // Depth distribution (exponential for more near detail)
    float nearPlane = 0.1f;
    float farPlane = 150.0f;
    float depthDistributionPower = 2.0f;
    
    // Global density
    float globalDensity = 0.02f;
    float heightFogDensity = 0.1f;
    float heightFogFalloff = 0.1f;
    float heightFogBaseHeight = 0.0f;
    
    // Scattering
    float scatteringCoefficient = 0.5f;
    float extinctionCoefficient = 0.1f;
    glm::vec3 albedo = glm::vec3(1.0f);
    
    // Phase function (Henyey-Greenstein)
    float anisotropy = 0.3f;  // -1 = backscatter, 0 = isotropic, 1 = forward scatter
    
    // Quality
    int temporalSamples = 16;
    int raymarchSteps = 64;
    bool enableTemporalReprojection = true;
    float temporalBlendFactor = 0.95f;
    
    // Noise
    float noiseScale = 0.1f;
    float noiseIntensity = 0.5f;
    glm::vec3 noiseSpeed = glm::vec3(0.02f, 0.01f, 0.03f);
};

// ============================================================================
// FOG VOLUME
// ============================================================================

struct FogVolume {
    enum class Shape {
        Box,
        Sphere,
        Cylinder,
        HeightFog
    };
    
    Shape shape = Shape::Box;
    glm::vec3 position = glm::vec3(0.0f);
    glm::quat rotation = glm::quat(1, 0, 0, 0);
    glm::vec3 size = glm::vec3(10.0f);
    
    float density = 0.5f;
    glm::vec3 color = glm::vec3(1.0f);
    float falloffDistance = 2.0f;
    
    // For height fog
    float baseHeight = 0.0f;
    float heightFalloff = 0.1f;
    
    // Animation
    glm::vec3 windDirection = glm::vec3(1, 0, 0);
    float windSpeed = 0.5f;
    
    // Priority for blending
    int priority = 0;
    float blendWeight = 1.0f;
    
    // Unique ID
    uint32_t id = 0;
};

// GPU-compatible fog volume data
struct GPUFogVolume {
    glm::mat4 worldToLocal;
    glm::vec4 colorDensity;     // xyz = color, w = density
    glm::vec4 sizeAndShape;     // xyz = size, w = shape
    glm::vec4 falloffParams;    // x = falloff, y = baseHeight, z = heightFalloff, w = priority
};

// ============================================================================
// LIGHT SHAFT DATA
// ============================================================================

struct LightShaftSettings {
    bool enabled = true;
    float intensity = 1.0f;
    float density = 1.0f;
    float decay = 0.98f;
    float weight = 0.5f;
    int samples = 64;
    float exposure = 1.0f;
    
    // Radial blur source (for sun)
    bool useSunPosition = true;
    glm::vec2 customSourcePos = glm::vec2(0.5f);  // Screen space
};

// ============================================================================
// VOLUMETRIC LIGHTING SYSTEM
// ============================================================================

class VolumetricLighting {
public:
    VolumetricLighting(VulkanContext& context);
    ~VolumetricLighting();
    
    bool initialize(uint32_t width, uint32_t height);
    void shutdown();
    void resize(uint32_t width, uint32_t height);
    
    // Settings
    void setSettings(const VolumetricSettings& settings);
    const VolumetricSettings& getSettings() const { return settings_; }
    
    void setLightShaftSettings(const LightShaftSettings& settings);
    
    // Fog volumes
    uint32_t addFogVolume(const FogVolume& volume);
    void updateFogVolume(uint32_t id, const FogVolume& volume);
    void removeFogVolume(uint32_t id);
    void clearFogVolumes();
    
    // Per-frame update
    void update(const glm::mat4& view, const glm::mat4& proj, 
               const glm::vec3& sunDirection, const glm::vec3& sunColor);
    
    // Render passes
    void injectLighting(VkCommandBuffer cmd, 
                       VkImageView shadowMap,
                       VkImageView ddgiIrradiance = VK_NULL_HANDLE);
    
    void raymarch(VkCommandBuffer cmd);
    
    void computeLightShafts(VkCommandBuffer cmd, 
                           VkImageView colorBuffer,
                           VkImageView depthBuffer);
    
    void apply(VkCommandBuffer cmd, 
              VkImageView sceneColor,
              VkImageView outputColor);
    
    // Results
    VkImageView getVolumetricTexture() const { return froxelScatteringView_; }
    VkImageView getLightShaftTexture() const { return lightShaftView_; }
    
    // Debug
    void debugVisualize(VkCommandBuffer cmd, VkImageView output, int mode = 0);
    
private:
    void createFroxelResources();
    void createLightShaftResources();
    void createPipelines();
    void updateFogVolumeBuffer();
    
    VulkanContext& context_;
    VolumetricSettings settings_;
    LightShaftSettings lightShaftSettings_;
    
    uint32_t screenWidth_ = 1920;
    uint32_t screenHeight_ = 1080;
    
    // Froxel grid (3D texture)
    VkImage froxelScattering_ = VK_NULL_HANDLE;
    VkDeviceMemory froxelScatteringMemory_ = VK_NULL_HANDLE;
    VkImageView froxelScatteringView_ = VK_NULL_HANDLE;
    
    // History buffer for temporal reprojection
    VkImage froxelHistory_ = VK_NULL_HANDLE;
    VkDeviceMemory froxelHistoryMemory_ = VK_NULL_HANDLE;
    VkImageView froxelHistoryView_ = VK_NULL_HANDLE;
    
    // Integrated scattering (2D, after raymarch)
    VkImage integratedScattering_ = VK_NULL_HANDLE;
    VkDeviceMemory integratedScatteringMemory_ = VK_NULL_HANDLE;
    VkImageView integratedScatteringView_ = VK_NULL_HANDLE;
    
    // Light shafts
    VkImage lightShaft_ = VK_NULL_HANDLE;
    VkDeviceMemory lightShaftMemory_ = VK_NULL_HANDLE;
    VkImageView lightShaftView_ = VK_NULL_HANDLE;
    
    // 3D noise texture
    VkImage noiseTexture_ = VK_NULL_HANDLE;
    VkDeviceMemory noiseMemory_ = VK_NULL_HANDLE;
    VkImageView noiseView_ = VK_NULL_HANDLE;
    VkSampler noiseSampler_ = VK_NULL_HANDLE;
    
    // Fog volume buffer
    VkBuffer fogVolumeBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory fogVolumeMemory_ = VK_NULL_HANDLE;
    std::vector<FogVolume> fogVolumes_;
    uint32_t nextFogVolumeId_ = 1;
    
    // Uniform buffer
    struct VolumetricUniforms {
        glm::mat4 viewMatrix;
        glm::mat4 projMatrix;
        glm::mat4 invViewMatrix;
        glm::mat4 invProjMatrix;
        glm::mat4 prevViewProjMatrix;
        
        glm::vec4 sunDirectionAndIntensity;
        glm::vec4 sunColor;
        
        glm::vec4 fogParams;      // density, scattering, extinction, anisotropy
        glm::vec4 heightFogParams; // density, falloff, baseHeight, padding
        glm::vec4 noiseParams;    // scale, intensity, speed.x, speed.y
        
        glm::ivec4 froxelDims;    // width, height, depth, volumeCount
        glm::vec4 depthParams;    // near, far, power, time
    };
    
    VkBuffer uniformBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory uniformMemory_ = VK_NULL_HANDLE;
    void* uniformMapped_ = nullptr;
    
    // Samplers
    VkSampler linearSampler_ = VK_NULL_HANDLE;
    VkSampler shadowSampler_ = VK_NULL_HANDLE;
    
    // Pipelines
    VkPipeline injectPipeline_ = VK_NULL_HANDLE;
    VkPipeline raymarchPipeline_ = VK_NULL_HANDLE;
    VkPipeline lightShaftPipeline_ = VK_NULL_HANDLE;
    VkPipeline applyPipeline_ = VK_NULL_HANDLE;
    
    VkPipelineLayout computeLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;
    
    // Temporal data
    glm::mat4 prevViewProjMatrix_ = glm::mat4(1.0f);
    float time_ = 0.0f;
    uint32_t frameIndex_ = 0;
};

// ============================================================================
// PHASE FUNCTIONS
// ============================================================================

namespace PhaseFunction {
    // Isotropic scattering
    inline float isotropic() { return 1.0f / (4.0f * 3.14159265f); }
    
    // Rayleigh scattering (small particles, sky)
    inline float rayleigh(float cosTheta) {
        return (3.0f / (16.0f * 3.14159265f)) * (1.0f + cosTheta * cosTheta);
    }
    
    // Mie scattering (larger particles, fog/clouds)
    inline float mie(float cosTheta, float g = 0.76f) {
        float g2 = g * g;
        float denom = 1.0f + g2 - 2.0f * g * cosTheta;
        return (1.0f - g2) / (4.0f * 3.14159265f * denom * std::sqrt(denom));
    }
    
    // Henyey-Greenstein (common approximation)
    inline float henyeyGreenstein(float cosTheta, float g) {
        float g2 = g * g;
        return (1.0f - g2) / (4.0f * 3.14159265f * std::pow(1.0f + g2 - 2.0f * g * cosTheta, 1.5f));
    }
    
    // Schlick approximation (faster than HG)
    inline float schlick(float cosTheta, float k) {
        float denom = 1.0f + k * cosTheta;
        return (1.0f - k * k) / (4.0f * 3.14159265f * denom * denom);
    }
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

// Convert depth to froxel Z index
inline float depthToFroxelZ(float linearDepth, float nearPlane, float farPlane, 
                           float power, uint32_t sliceCount) {
    float normalizedDepth = (linearDepth - nearPlane) / (farPlane - nearPlane);
    return std::pow(normalizedDepth, 1.0f / power) * float(sliceCount);
}

// Convert froxel Z to linear depth
inline float froxelZToDepth(float z, float nearPlane, float farPlane,
                           float power, uint32_t sliceCount) {
    float normalizedZ = z / float(sliceCount);
    return nearPlane + std::pow(normalizedZ, power) * (farPlane - nearPlane);
}

// Beer-Lambert law for transmittance
inline float beerLambert(float opticalDepth) {
    return std::exp(-opticalDepth);
}

// In-scattering calculation
inline glm::vec3 inScattering(const glm::vec3& lightColor, float lightIntensity,
                              float scattering, float extinction,
                              float phaseValue, float opticalDepth) {
    float transmittance = beerLambert(extinction * opticalDepth);
    return lightColor * lightIntensity * scattering * phaseValue * (1.0f - transmittance) / extinction;
}

} // namespace Sanic
