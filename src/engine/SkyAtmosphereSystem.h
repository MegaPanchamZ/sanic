#pragma once

/**
 * SkyAtmosphereSystem.h
 * 
 * Physically-based sky atmosphere rendering system.
 * Implements Bruneton's atmospheric scattering model.
 * 
 * Features:
 * - Precomputed transmittance, scattering, and multi-scattering LUTs
 * - Real-time aerial perspective
 * - Time-of-day support
 * - Sun disk rendering
 */

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <memory>
#include <string>

namespace Kinetic {

// Forward declarations
class VulkanRenderer;
class ComputePipeline;
class GraphicsPipeline;
class DescriptorSet;
class Buffer;
class Image;

/**
 * Atmosphere parameters (matches Bruneton model)
 */
struct AtmosphereParams {
    // Rayleigh scattering
    glm::vec3 rayleighScattering = glm::vec3(5.802f, 13.558f, 33.1f) * 1e-6f;
    float rayleighDensityH = 8.0f;  // Scale height in km
    
    // Mie scattering
    glm::vec3 mieScattering = glm::vec3(3.996f) * 1e-6f;
    glm::vec3 mieExtinction = glm::vec3(4.44f) * 1e-6f;
    float mieDensityH = 1.2f;       // Scale height in km
    float miePhaseG = 0.8f;         // Anisotropy factor
    
    // Ozone absorption
    glm::vec3 ozoneAbsorption = glm::vec3(0.65f, 1.881f, 0.085f) * 1e-6f;
    float ozoneLayerCenter = 25.0f;  // km
    float ozoneLayerWidth = 15.0f;   // km
    
    // Ground
    glm::vec3 groundAlbedo = glm::vec3(0.3f);
    
    // Geometry
    float earthRadius = 6360.0f;      // km
    float atmosphereRadius = 6460.0f; // km
};

/**
 * Sun parameters
 */
struct SunParams {
    glm::vec3 direction = glm::normalize(glm::vec3(0.5f, 0.5f, 0.0f));
    glm::vec3 color = glm::vec3(1.0f, 0.95f, 0.9f);
    float intensity = 20.0f;         // Illuminance in lux (scaled)
    float diskRadius = 0.00467f;     // Angular radius in radians (~0.267 degrees)
    float softEdge = 0.0f;           // Edge softness
};

/**
 * LUT texture sizes
 */
struct LUTSizes {
    glm::uvec2 transmittance = glm::uvec2(256, 64);      // Width, Height
    glm::uvec2 multiScattering = glm::uvec2(32, 32);     // Width, Height
    glm::uvec3 scattering = glm::uvec3(32, 32, 32);      // Width, Height, Depth (view, sun angle, height)
    glm::uvec3 aerialPerspective = glm::uvec3(32, 32, 32); // Screen-space aerial perspective
};

/**
 * Sky atmosphere system managing atmospheric rendering
 */
class SkyAtmosphereSystem {
public:
    SkyAtmosphereSystem();
    ~SkyAtmosphereSystem();
    
    // Initialization
    bool initialize(VulkanRenderer* renderer);
    void shutdown();
    
    // Configuration
    void setAtmosphereParams(const AtmosphereParams& params);
    const AtmosphereParams& getAtmosphereParams() const { return m_atmosphereParams; }
    
    void setSunParams(const SunParams& params);
    const SunParams& getSunParams() const { return m_sunParams; }
    
    void setSunDirection(const glm::vec3& direction);
    void setSunDirectionFromTimeOfDay(float hours, float latitude = 45.0f);
    
    // LUT computation (call when params change)
    void computeTransmittanceLUT(VkCommandBuffer cmd);
    void computeMultiScatteringLUT(VkCommandBuffer cmd);
    void computeScatteringLUT(VkCommandBuffer cmd);
    void computeAllLUTs(VkCommandBuffer cmd);
    
    // Per-frame updates
    void updateAerialPerspective(VkCommandBuffer cmd, 
                                  const glm::mat4& viewProjection,
                                  const glm::vec3& cameraPos);
    
    // Rendering
    void renderSky(VkCommandBuffer cmd,
                   VkRenderPass renderPass,
                   uint32_t subpass,
                   const glm::mat4& viewProjection,
                   const glm::vec3& cameraPos);
    
    // Descriptor access for other shaders
    VkImageView getTransmittanceLUTView() const;
    VkImageView getMultiScatteringLUTView() const;
    VkImageView getScatteringLUTView() const;
    VkImageView getAerialPerspectiveView() const;
    VkSampler getLUTSampler() const { return m_lutSampler; }
    
    // Debug
    void drawDebugUI();
    bool needsLUTRecompute() const { return m_lutsDirty; }
    
private:
    void createLUTTextures();
    void createPipelines();
    void createDescriptorSets();
    void updateUniformBuffer();
    
    VulkanRenderer* m_renderer = nullptr;
    
    // Parameters
    AtmosphereParams m_atmosphereParams;
    SunParams m_sunParams;
    LUTSizes m_lutSizes;
    bool m_lutsDirty = true;
    
    // LUT textures
    std::unique_ptr<Image> m_transmittanceLUT;    // 2D
    std::unique_ptr<Image> m_multiScatteringLUT;  // 2D
    std::unique_ptr<Image> m_scatteringLUT;       // 3D
    std::unique_ptr<Image> m_aerialPerspectiveLUT;// 3D froxel
    
    VkSampler m_lutSampler = VK_NULL_HANDLE;
    
    // Compute pipelines for LUT generation
    std::unique_ptr<ComputePipeline> m_transmittancePipeline;
    std::unique_ptr<ComputePipeline> m_multiScatteringPipeline;
    std::unique_ptr<ComputePipeline> m_scatteringPipeline;
    std::unique_ptr<ComputePipeline> m_aerialPerspectivePipeline;
    
    // Graphics pipeline for sky rendering
    std::unique_ptr<GraphicsPipeline> m_skyPipeline;
    
    // Descriptor sets
    std::unique_ptr<DescriptorSet> m_lutComputeDescSet;
    std::unique_ptr<DescriptorSet> m_skyRenderDescSet;
    
    // Uniform buffer
    std::unique_ptr<Buffer> m_uniformBuffer;
    
    // Atmosphere uniform data (matches shader)
    struct AtmosphereUniforms {
        // Rayleigh
        glm::vec3 rayleighScattering;
        float rayleighDensityH;
        
        // Mie
        glm::vec3 mieScattering;
        float mieDensityH;
        glm::vec3 mieExtinction;
        float miePhaseG;
        
        // Ozone
        glm::vec3 ozoneAbsorption;
        float ozoneLayerCenter;
        float ozoneLayerWidth;
        float padding1;
        
        // Ground
        glm::vec3 groundAlbedo;
        float earthRadius;
        float atmosphereRadius;
        float padding2;
        
        // Sun
        glm::vec3 sunDirection;
        float sunIntensity;
        glm::vec3 sunColor;
        float sunDiskRadius;
        
        // Camera (for aerial perspective)
        glm::mat4 invViewProjection;
        glm::vec3 cameraPosition;
        float padding3;
        
        // LUT sizes
        glm::uvec2 transmittanceSize;
        glm::uvec2 multiScatteringSize;
        glm::uvec3 scatteringSize;
        uint32_t padding4;
        glm::uvec3 aerialPerspectiveSize;
        uint32_t padding5;
    };
};

/**
 * Time of day controller for sun position
 */
class TimeOfDayController {
public:
    TimeOfDayController();
    
    void setTime(float hours);      // 0-24
    void setLatitude(float lat);    // -90 to 90
    void setDayOfYear(int day);     // 1-365
    
    void update(float deltaTime);   // Advance time
    void setTimeScale(float scale); // Speed multiplier
    
    glm::vec3 getSunDirection() const;
    float getCurrentHour() const { return m_currentHour; }
    
    // Presets
    void setSunrise();
    void setNoon();
    void setSunset();
    void setMidnight();
    
private:
    float m_currentHour = 12.0f;
    float m_latitude = 45.0f;
    int m_dayOfYear = 172;  // Summer solstice
    float m_timeScale = 1.0f;
    
    glm::vec3 calculateSunPosition() const;
};

} // namespace Kinetic
