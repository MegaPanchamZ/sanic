#pragma once

/**
 * VolumetricCloudsSystem.h
 * 
 * Real-time volumetric cloud rendering system.
 * Based on UE5's volumetric cloud implementation.
 * 
 * Features:
 * - Ray-marched volumetric clouds
 * - Weather map for coverage control
 * - Multiple cloud layers/types (stratus, cumulus, cumulonimbus)
 * - Temporal reprojection for performance
 * - Light scattering and silver lining effects
 */

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <memory>
#include <array>

namespace Kinetic {

// Forward declarations
class VulkanRenderer;
class ComputePipeline;
class DescriptorSet;
class Buffer;
class Image;
class SkyAtmosphereSystem;

/**
 * Cloud layer parameters
 */
struct CloudLayerParams {
    float bottomAltitude = 1.5f;     // km
    float topAltitude = 5.0f;        // km
    float density = 1.0f;
    float coverage = 0.5f;
    
    // Cloud type affects shape gradient
    enum class Type {
        Stratus,      // Flat, layered
        Cumulus,      // Puffy
        Cumulonimbus  // Towering storm clouds
    } type = Type::Cumulus;
};

/**
 * Wind parameters for cloud animation
 */
struct CloudWindParams {
    glm::vec3 direction = glm::vec3(1.0f, 0.0f, 0.0f);
    float speed = 10.0f;              // m/s
    float turbulence = 0.1f;
};

/**
 * Cloud lighting parameters
 */
struct CloudLightingParams {
    glm::vec3 ambientColor = glm::vec3(0.6f, 0.7f, 0.9f);
    float ambientStrength = 0.3f;
    
    float extinction = 0.05f;         // Light absorption coefficient
    float scatterForward = 0.8f;      // Forward scattering (Henyey-Greenstein g)
    float scatterBack = 0.3f;         // Back scattering
    float silverIntensity = 0.5f;     // Silver lining effect
    
    float multiScatterStrength = 0.5f;
};

/**
 * Cloud rendering quality settings
 */
struct CloudQualitySettings {
    int primaryRaySteps = 64;         // Main ray march steps
    int lightRaySteps = 6;            // Light march steps toward sun
    float stepSize = 100.0f;          // Base step size in meters
    
    bool enableTemporalReprojection = true;
    float temporalBlend = 0.95f;      // Blend with previous frame
    float rayOffsetStrength = 1.0f;   // Blue noise ray offset
    
    float detailNoiseScale = 0.01f;
    float shapeNoiseScale = 0.001f;
    
    // Resolution scale (1.0 = full res, 0.5 = half res)
    float resolutionScale = 0.5f;
};

/**
 * Volumetric clouds system
 */
class VolumetricCloudsSystem {
public:
    VolumetricCloudsSystem();
    ~VolumetricCloudsSystem();
    
    // Initialization
    bool initialize(VulkanRenderer* renderer);
    void shutdown();
    
    // Configuration
    void setCloudLayer(const CloudLayerParams& layer);
    const CloudLayerParams& getCloudLayer() const { return m_cloudLayer; }
    
    void setWindParams(const CloudWindParams& wind);
    const CloudWindParams& getWindParams() const { return m_windParams; }
    
    void setLightingParams(const CloudLightingParams& lighting);
    const CloudLightingParams& getLightingParams() const { return m_lightingParams; }
    
    void setQualitySettings(const CloudQualitySettings& quality);
    const CloudQualitySettings& getQualitySettings() const { return m_qualitySettings; }
    
    // Link to atmosphere system for sun direction and colors
    void setSkyAtmosphere(SkyAtmosphereSystem* atmosphere);
    
    // Noise texture generation
    void generateNoiseTextures(VkCommandBuffer cmd);
    bool hasNoiseTextures() const { return m_noiseGenerated; }
    
    // Per-frame update and rendering
    void update(float deltaTime);
    
    void render(VkCommandBuffer cmd,
                const glm::mat4& viewProjection,
                const glm::mat4& prevViewProjection,
                const glm::vec3& cameraPos,
                VkImageView depthBuffer,
                glm::uvec2 resolution);
    
    // Output access
    VkImageView getCloudOutput() const;
    VkImageView getCloudDepth() const;  // For compositing
    
    // Weather map control
    void setWeatherMap(VkImageView weatherMap);
    void generateProceduralWeather(VkCommandBuffer cmd);
    
    // Debug
    void drawDebugUI();
    
private:
    void createNoiseTextures();
    void createRenderTargets(glm::uvec2 resolution);
    void createPipelines();
    void createDescriptorSets();
    void updateUniformBuffer(const glm::mat4& viewProjection,
                              const glm::mat4& prevViewProjection,
                              const glm::vec3& cameraPos,
                              glm::uvec2 resolution);
    
    VulkanRenderer* m_renderer = nullptr;
    SkyAtmosphereSystem* m_atmosphere = nullptr;
    
    // Parameters
    CloudLayerParams m_cloudLayer;
    CloudWindParams m_windParams;
    CloudLightingParams m_lightingParams;
    CloudQualitySettings m_qualitySettings;
    
    float m_time = 0.0f;
    uint32_t m_frameNumber = 0;
    glm::uvec2 m_currentResolution{0, 0};
    
    // Noise textures (3D)
    std::unique_ptr<Image> m_shapeNoise;       // Low frequency cloud shape
    std::unique_ptr<Image> m_detailNoise;      // High frequency erosion
    std::unique_ptr<Image> m_curlNoise;        // For wind distortion
    bool m_noiseGenerated = false;
    
    // Weather map (2D)
    std::unique_ptr<Image> m_weatherMap;       // R=coverage, G=precipitation, B=type
    VkImageView m_externalWeatherMap = VK_NULL_HANDLE;
    
    // Blue noise for temporal stability
    std::unique_ptr<Image> m_blueNoise;
    
    // Render targets
    std::unique_ptr<Image> m_cloudOutput;      // Current frame clouds
    std::unique_ptr<Image> m_cloudHistory;     // Previous frame for temporal
    std::unique_ptr<Image> m_cloudDepth;       // Cloud depth for compositing
    
    VkSampler m_linearSampler = VK_NULL_HANDLE;
    VkSampler m_nearestSampler = VK_NULL_HANDLE;
    
    // Pipelines
    std::unique_ptr<ComputePipeline> m_cloudPipeline;
    std::unique_ptr<ComputePipeline> m_noiseGenPipeline;
    std::unique_ptr<ComputePipeline> m_weatherGenPipeline;
    std::unique_ptr<ComputePipeline> m_temporalPipeline;
    
    // Descriptor sets
    std::unique_ptr<DescriptorSet> m_cloudDescSet;
    std::unique_ptr<DescriptorSet> m_noiseDescSet;
    
    // Uniform buffer
    std::unique_ptr<Buffer> m_uniformBuffer;
    
    // Cloud uniform data (matches shader)
    struct CloudUniforms {
        glm::mat4 invViewProjection;
        glm::mat4 prevViewProjection;
        glm::vec3 cameraPos;
        float time;
        
        glm::vec3 sunDirection;
        float sunIntensity;
        
        glm::vec3 sunColor;
        float cloudLayerBottom;
        
        float cloudLayerTop;
        float cloudDensity;
        float cloudCoverage;
        float cloudType;
        
        glm::vec3 windDirection;
        float windSpeed;
        
        glm::vec3 ambientColor;
        float ambientStrength;
        
        float extinction;
        float scatterForward;
        float scatterBack;
        float silverIntensity;
        
        glm::vec2 resolution;
        float earthRadius;
        float frameNumber;
        
        float temporalBlend;
        float rayOffsetStrength;
        float detailScale;
        float shapeScale;
    };
};

/**
 * Weather map generator for procedural clouds
 */
class WeatherMapGenerator {
public:
    WeatherMapGenerator();
    
    // Generate based on parameters
    void generate(VkCommandBuffer cmd, 
                  Image* output,
                  float coverage,
                  float precipitation,
                  glm::vec2 windOffset);
    
    // Blend multiple weather fronts
    void blendWeatherFronts(VkCommandBuffer cmd,
                            Image* output,
                            const std::vector<glm::vec4>& fronts);  // xy=pos, z=radius, w=intensity
    
private:
    std::unique_ptr<ComputePipeline> m_genPipeline;
};

} // namespace Kinetic
