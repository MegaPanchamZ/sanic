#pragma once

/**
 * WaterSystem.h
 * 
 * Single shading model water system with physics integration.
 * Based on UE5's water system architecture.
 * 
 * Features:
 * - Gerstner wave simulation
 * - Screen-space reflections/refractions
 * - Underwater rendering effects
 * - Caustics projection
 * - Foam generation
 * - Buoyancy physics (Jolt integration)
 * - Flow maps for rivers
 */

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <memory>
#include <vector>
#include <functional>

namespace Kinetic {

// Forward declarations
class VulkanRenderer;
class GraphicsPipeline;
class ComputePipeline;
class DescriptorSet;
class Buffer;
class Image;
class Mesh;
namespace Physics { class PhysicsWorld; class Body; }

/**
 * Single Gerstner wave definition
 */
struct GerstnerWave {
    glm::vec2 direction = glm::vec2(1.0f, 0.0f);
    float amplitude = 0.5f;        // meters
    float frequency = 1.0f;        // waves per meter
    float phase = 1.0f;            // phase speed multiplier
    float steepness = 0.5f;        // 0-1, affects horizontal displacement
};

/**
 * Water body types
 */
enum class WaterBodyType {
    Ocean,      // Infinite ocean plane with Gerstner waves
    Lake,       // Bounded body with gentle waves
    River,      // Linear body with flow direction
    Pond,       // Small body with minimal waves
    Custom      // User-defined mesh
};

/**
 * Water material properties
 */
struct WaterMaterialParams {
    // Colors
    glm::vec3 shallowColor = glm::vec3(0.0f, 0.6f, 0.5f);
    glm::vec3 deepColor = glm::vec3(0.0f, 0.1f, 0.2f);
    glm::vec3 scatterColor = glm::vec3(0.0f, 0.4f, 0.3f);
    
    // Depth thresholds
    float shallowDepth = 1.0f;     // meters
    float deepDepth = 50.0f;       // meters
    float maxVisibleDepth = 100.0f;
    
    // Absorption (per-channel, Beer-Lambert)
    glm::vec3 absorption = glm::vec3(0.5f, 0.2f, 0.1f);
    
    // Surface
    float refractionStrength = 0.3f;
    float normalStrength = 1.0f;
    float specularPower = 512.0f;
    float fresnelPower = 5.0f;
    
    // Foam
    float foamScale = 10.0f;
    float foamIntensity = 1.0f;
    float shoreFoamWidth = 2.0f;
    
    // Caustics
    float causticsStrength = 0.5f;
    float causticsScale = 0.1f;
};

/**
 * Water body instance
 */
struct WaterBody {
    uint32_t id = 0;
    WaterBodyType type = WaterBodyType::Ocean;
    
    // Transform
    glm::vec3 position = glm::vec3(0.0f);
    glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 scale = glm::vec3(1.0f);
    
    // Bounds (for non-ocean types)
    glm::vec3 boundsMin = glm::vec3(-100.0f, 0.0f, -100.0f);
    glm::vec3 boundsMax = glm::vec3(100.0f, 0.0f, 100.0f);
    
    // Water level
    float waterLevel = 0.0f;
    
    // Waves (up to 8)
    std::vector<GerstnerWave> waves;
    
    // Material
    WaterMaterialParams material;
    
    // Flow (for rivers)
    bool hasFlow = false;
    glm::vec2 flowDirection = glm::vec2(1.0f, 0.0f);
    float flowSpeed = 1.0f;
    
    // Physics
    bool enableBuoyancy = true;
    float density = 1000.0f;       // kg/m³ (water default)
    
    // Rendering
    bool visible = true;
    bool underwaterEffects = true;
    Mesh* customMesh = nullptr;
};

/**
 * Buoyancy query result
 */
struct BuoyancyResult {
    bool isSubmerged = false;
    float submersionDepth = 0.0f;
    float submersionRatio = 0.0f;   // 0-1
    glm::vec3 buoyancyForce = glm::vec3(0.0f);
    glm::vec3 buoyancyTorque = glm::vec3(0.0f);
    glm::vec3 waterVelocity = glm::vec3(0.0f);
};

/**
 * Underwater post-process parameters
 */
struct UnderwaterParams {
    bool enabled = true;
    glm::vec3 fogColor = glm::vec3(0.0f, 0.3f, 0.4f);
    float fogDensity = 0.1f;
    float distortionStrength = 0.02f;
    float causticsStrength = 0.3f;
};

/**
 * Water system managing all water bodies
 */
class WaterSystem {
public:
    WaterSystem();
    ~WaterSystem();
    
    // Initialization
    bool initialize(VulkanRenderer* renderer);
    void shutdown();
    
    // Physics integration
    void setPhysicsWorld(Physics::PhysicsWorld* world);
    
    // Water body management
    uint32_t createWaterBody(const WaterBody& body);
    void removeWaterBody(uint32_t id);
    WaterBody* getWaterBody(uint32_t id);
    const std::vector<WaterBody>& getWaterBodies() const { return m_waterBodies; }
    
    // Preset creation
    uint32_t createOcean(float waterLevel, const std::vector<GerstnerWave>& waves);
    uint32_t createLake(const glm::vec3& center, const glm::vec2& size, float waterLevel);
    uint32_t createRiver(const std::vector<glm::vec3>& splinePoints, float width, float flowSpeed);
    
    // Per-frame update
    void update(float deltaTime);
    
    // Physics queries
    float getWaterHeight(const glm::vec3& worldPos, uint32_t bodyId = 0) const;
    glm::vec3 getWaterNormal(const glm::vec3& worldPos, uint32_t bodyId = 0) const;
    glm::vec3 getWaterVelocity(const glm::vec3& worldPos, uint32_t bodyId = 0) const;
    
    BuoyancyResult calculateBuoyancy(Physics::Body* body, uint32_t waterBodyId = 0) const;
    bool isPointUnderwater(const glm::vec3& worldPos) const;
    
    // Rendering
    void render(VkCommandBuffer cmd,
                const glm::mat4& viewProjection,
                const glm::mat4& prevViewProjection,
                const glm::vec3& cameraPos,
                VkImageView sceneColor,
                VkImageView sceneDepth);
    
    void renderUnderwaterEffects(VkCommandBuffer cmd,
                                  const glm::vec3& cameraPos,
                                  VkImageView sceneColor);
    
    // Caustics for deferred lighting
    VkImageView getCausticsTexture() const;
    void renderCaustics(VkCommandBuffer cmd,
                        const glm::mat4& lightViewProj,
                        float lightIntensity);
    
    // Flow maps
    void setFlowMap(uint32_t bodyId, VkImageView flowMap);
    void generateFlowMap(uint32_t bodyId, const std::vector<glm::vec3>& splinePoints);
    
    // Underwater post-process
    void setUnderwaterParams(const UnderwaterParams& params);
    const UnderwaterParams& getUnderwaterParams() const { return m_underwaterParams; }
    
    // Debug
    void drawDebugUI();
    
private:
    void createWaterMesh(uint32_t resolution);
    void createPipelines();
    void createDescriptorSets();
    void updateWaveBuffer();
    void updateMaterialBuffer(const WaterBody& body);
    
    // Wave calculation helpers
    glm::vec3 calculateGerstnerDisplacement(const glm::vec2& xz, 
                                             const std::vector<GerstnerWave>& waves,
                                             float time) const;
    glm::vec3 calculateGerstnerNormal(const glm::vec2& xz,
                                       const std::vector<GerstnerWave>& waves,
                                       float time) const;
    
    VulkanRenderer* m_renderer = nullptr;
    Physics::PhysicsWorld* m_physicsWorld = nullptr;
    
    // Water bodies
    std::vector<WaterBody> m_waterBodies;
    uint32_t m_nextBodyId = 1;
    
    // Time
    float m_time = 0.0f;
    
    // Underwater settings
    UnderwaterParams m_underwaterParams;
    
    // Water mesh (tessellated grid)
    std::unique_ptr<Mesh> m_waterMesh;
    uint32_t m_meshResolution = 256;
    
    // Textures
    std::unique_ptr<Image> m_normalMap;
    std::unique_ptr<Image> m_foamTexture;
    std::unique_ptr<Image> m_causticsTexture;
    std::unique_ptr<Image> m_flowMapDefault;
    
    VkSampler m_sampler = VK_NULL_HANDLE;
    
    // Pipelines
    std::unique_ptr<GraphicsPipeline> m_waterPipeline;
    std::unique_ptr<GraphicsPipeline> m_underwaterPipeline;
    std::unique_ptr<ComputePipeline> m_causticsPipeline;
    
    // Descriptor sets
    std::unique_ptr<DescriptorSet> m_waterDescSet;
    std::unique_ptr<DescriptorSet> m_underwaterDescSet;
    
    // Uniform buffers
    std::unique_ptr<Buffer> m_waveBuffer;      // GerstnerWave array
    std::unique_ptr<Buffer> m_materialBuffer;  // WaterMaterialParams
    std::unique_ptr<Buffer> m_uniformBuffer;   // Per-frame uniforms
    
    // Water uniform data
    struct WaterUniforms {
        glm::mat4 viewProjection;
        glm::mat4 prevViewProjection;
        glm::mat4 model;
        glm::vec3 cameraPos;
        float time;
        float waterLevel;
        float waveAmplitude;
        float waveFrequency;
        float waveSteepness;
    };
};

/**
 * Buoyancy component for physics bodies
 */
class BuoyancyComponent {
public:
    BuoyancyComponent();
    
    void setBody(Physics::Body* body);
    void setWaterSystem(WaterSystem* water);
    void setWaterBodyId(uint32_t id);
    
    // Sample points for buoyancy calculation
    void addSamplePoint(const glm::vec3& localPos, float radius = 0.5f);
    void clearSamplePoints();
    
    // Per-frame update (applies forces)
    void update(float deltaTime);
    
    // Configuration
    void setDragCoefficient(float linear, float angular);
    void setVolumeOverride(float volume);  // If not set, uses sample points
    
    // Results
    const BuoyancyResult& getLastResult() const { return m_lastResult; }
    bool isFullySubmerged() const { return m_lastResult.submersionRatio >= 0.99f; }
    bool isFloating() const { return m_lastResult.submersionRatio > 0.0f && m_lastResult.submersionRatio < 0.99f; }
    
private:
    Physics::Body* m_body = nullptr;
    WaterSystem* m_waterSystem = nullptr;
    uint32_t m_waterBodyId = 0;
    
    struct SamplePoint {
        glm::vec3 localPos;
        float radius;
    };
    std::vector<SamplePoint> m_samplePoints;
    
    float m_linearDrag = 0.5f;
    float m_angularDrag = 0.5f;
    float m_volumeOverride = -1.0f;
    
    BuoyancyResult m_lastResult;
};

} // namespace Kinetic
