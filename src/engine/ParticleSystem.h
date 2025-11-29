/**
 * ParticleSystem.h
 * 
 * GPU-accelerated particle system with physics integration.
 * 
 * Features:
 * - Compute shader particle simulation
 * - SDF collision detection
 * - Depth buffer soft particles
 * - Sprite sheet animation
 * - GPU indirect rendering
 * - Particle events (spawn on death, etc.)
 */

#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <random>

class VulkanContext;

namespace Sanic {

// ============================================================================
// PARTICLE DATA
// ============================================================================

// GPU particle data - matches shader struct
struct GPUParticle {
    glm::vec3 position;
    float lifetime;
    
    glm::vec3 velocity;
    float age;
    
    glm::vec4 color;
    
    glm::vec2 size;
    float rotation;
    float angularVelocity;
    
    uint32_t textureIndex;
    uint32_t flags;
    uint32_t padding[2];
};
static_assert(sizeof(GPUParticle) == 80, "GPUParticle must be 80 bytes");

// ============================================================================
// EMITTER SHAPES
// ============================================================================

enum class EmitterShape {
    Point,
    Sphere,
    Hemisphere,
    Cone,
    Box,
    Circle,
    Edge,
    Mesh
};

struct EmitterShapeConfig {
    EmitterShape shape = EmitterShape::Point;
    
    // Common
    glm::vec3 position = glm::vec3(0.0f);
    glm::quat rotation = glm::quat(1, 0, 0, 0);
    
    // Sphere/Hemisphere/Circle
    float radius = 1.0f;
    float radiusThickness = 0.0f;  // 0 = surface, 1 = volume
    
    // Cone
    float angle = 45.0f;           // Opening angle in degrees
    float length = 1.0f;
    
    // Box
    glm::vec3 boxSize = glm::vec3(1.0f);
    
    // Edge
    glm::vec3 edgeStart = glm::vec3(-1, 0, 0);
    glm::vec3 edgeEnd = glm::vec3(1, 0, 0);
    
    // Mesh emission (vertex/edge/triangle)
    uint32_t meshId = UINT32_MAX;
};

// ============================================================================
// VALUE OVER LIFETIME
// ============================================================================

template<typename T>
struct ValueOverLifetime {
    enum class Mode {
        Constant,
        Curve,
        RandomBetweenConstants,
        RandomBetweenCurves
    };
    
    Mode mode = Mode::Constant;
    T constantValue;
    T constantValueMin;
    T constantValueMax;
    
    // Curve points (time -> value)
    std::vector<std::pair<float, T>> curve;
    std::vector<std::pair<float, T>> curveMin;
    std::vector<std::pair<float, T>> curveMax;
    
    T evaluate(float t, float random = 0.5f) const;
};

using FloatOverLifetime = ValueOverLifetime<float>;
using Vec3OverLifetime = ValueOverLifetime<glm::vec3>;
using ColorOverLifetime = ValueOverLifetime<glm::vec4>;

// ============================================================================
// PARTICLE MODULES
// ============================================================================

struct EmissionModule {
    bool enabled = true;
    FloatOverLifetime rateOverTime;          // Particles per second
    FloatOverLifetime rateOverDistance;      // Particles per unit distance
    
    struct Burst {
        float time;
        int count;
        int cycles = 1;
        float interval = 0.0f;
        float probability = 1.0f;
    };
    std::vector<Burst> bursts;
};

struct VelocityModule {
    bool enabled = true;
    Vec3OverLifetime linearVelocity;
    Vec3OverLifetime orbitalVelocity;        // Around emitter center
    FloatOverLifetime radialVelocity;        // Away from emitter
    FloatOverLifetime speedModifier;
    
    enum class Space { Local, World };
    Space space = Space::Local;
};

struct ColorModule {
    bool enabled = true;
    ColorOverLifetime colorOverLifetime;
    
    // Gradient based on speed
    bool useSpeedGradient = false;
    float speedRange = 10.0f;
    std::vector<std::pair<float, glm::vec4>> speedGradient;
};

struct SizeModule {
    bool enabled = true;
    FloatOverLifetime sizeOverLifetime;
    
    // Separate X/Y for stretched particles
    bool separateAxes = false;
    FloatOverLifetime sizeXOverLifetime;
    FloatOverLifetime sizeYOverLifetime;
};

struct RotationModule {
    bool enabled = true;
    FloatOverLifetime rotationOverLifetime;  // Degrees per second
    FloatOverLifetime angularVelocity;
};

struct NoiseModule {
    bool enabled = false;
    float strength = 1.0f;
    float frequency = 0.5f;
    float scrollSpeed = 0.0f;
    bool damping = false;
    int octaves = 1;
};

struct ForceModule {
    bool enabled = true;
    glm::vec3 gravity = glm::vec3(0, -9.81f, 0);
    float drag = 0.0f;
    float multiplyBySize = 0.0f;
    
    // Wind zone influence
    float windInfluence = 1.0f;
};

struct CollisionModule {
    bool enabled = false;
    
    enum class Type { World, Planes, SDF };
    Type type = Type::World;
    
    float bounce = 0.5f;
    float friction = 0.0f;
    float lifetimeLoss = 0.0f;
    float radiusScale = 1.0f;
    
    bool killOnCollision = false;
    bool enableInteriorCollisions = false;
    
    // For SDF collision
    VkImageView sdfVolume = VK_NULL_HANDLE;
    glm::vec3 sdfBoundsMin;
    glm::vec3 sdfBoundsMax;
};

struct SubEmitterModule {
    bool enabled = false;
    
    struct SubEmitter {
        enum class Trigger { Birth, Death, Collision, Manual };
        Trigger trigger = Trigger::Death;
        std::string emitterName;
        float probability = 1.0f;
        bool inheritVelocity = true;
        float velocityScale = 1.0f;
    };
    std::vector<SubEmitter> subEmitters;
};

struct TextureSheetModule {
    bool enabled = false;
    int tilesX = 1;
    int tilesY = 1;
    
    enum class Animation { WholeSheet, SingleRow };
    Animation animation = Animation::WholeSheet;
    
    FloatOverLifetime frameOverTime;
    int startFrame = 0;
    int cycles = 1;
};

struct TrailModule {
    bool enabled = false;
    float ratio = 1.0f;                      // Fraction of particles with trails
    float lifetime = 1.0f;
    float minimumVertexDistance = 0.1f;
    
    bool worldSpace = true;
    bool dieWithParticle = true;
    
    ColorOverLifetime colorOverTrail;
    FloatOverLifetime widthOverTrail;
};

// ============================================================================
// RENDERER MODULE
// ============================================================================

struct ParticleRendererConfig {
    enum class RenderMode {
        Billboard,
        StretchedBillboard,
        HorizontalBillboard,
        VerticalBillboard,
        Mesh
    };
    RenderMode renderMode = RenderMode::Billboard;
    
    enum class SortMode {
        None,
        ByDistance,
        OldestFirst,
        YoungestFirst
    };
    SortMode sortMode = SortMode::ByDistance;
    
    // Material
    VkImageView texture = VK_NULL_HANDLE;
    
    enum class BlendMode {
        Alpha,
        Additive,
        Multiply,
        Premultiplied
    };
    BlendMode blendMode = BlendMode::Alpha;
    
    // Stretched billboard
    float cameraVelocityScale = 0.0f;
    float velocityScale = 0.0f;
    float lengthScale = 1.0f;
    
    // Mesh rendering
    uint32_t meshId = UINT32_MAX;
    
    // Soft particles
    bool softParticles = true;
    float softParticleDistance = 0.5f;
    
    // Shadows
    bool castShadows = false;
    bool receiveShadows = false;
};

// ============================================================================
// PARTICLE EMITTER
// ============================================================================

struct ParticleEmitterConfig {
    std::string name;
    
    // Timing
    float duration = 5.0f;
    bool loop = true;
    bool prewarm = false;
    float startDelay = 0.0f;
    
    // Capacity
    uint32_t maxParticles = 1000;
    
    // Start values
    FloatOverLifetime startLifetime{FloatOverLifetime::Mode::Constant, 5.0f};
    FloatOverLifetime startSpeed{FloatOverLifetime::Mode::Constant, 5.0f};
    FloatOverLifetime startSize{FloatOverLifetime::Mode::Constant, 1.0f};
    FloatOverLifetime startRotation{FloatOverLifetime::Mode::Constant, 0.0f};
    ColorOverLifetime startColor{ColorOverLifetime::Mode::Constant, glm::vec4(1.0f)};
    
    // Shape
    EmitterShapeConfig shape;
    
    // Modules
    EmissionModule emission;
    VelocityModule velocity;
    ColorModule color;
    SizeModule size;
    RotationModule rotation;
    NoiseModule noise;
    ForceModule force;
    CollisionModule collision;
    SubEmitterModule subEmitters;
    TextureSheetModule textureSheet;
    TrailModule trails;
    
    // Rendering
    ParticleRendererConfig renderer;
};

class ParticleEmitter {
public:
    ParticleEmitter(const ParticleEmitterConfig& config);
    ~ParticleEmitter();
    
    void setTransform(const glm::mat4& transform);
    void setActive(bool active);
    
    void play();
    void pause();
    void stop(bool clearParticles = true);
    void emit(int count);
    
    bool isPlaying() const { return playing_; }
    bool isAlive() const { return aliveCount_ > 0 || playing_; }
    
    uint32_t getAliveCount() const { return aliveCount_; }
    const ParticleEmitterConfig& getConfig() const { return config_; }
    
    // Called by ParticleSystem
    void update(float deltaTime, const glm::vec3& cameraPos);
    VkBuffer getParticleBuffer() const { return particleBuffer_; }
    VkBuffer getIndirectBuffer() const { return indirectBuffer_; }
    
private:
    friend class ParticleSystem;
    
    ParticleEmitterConfig config_;
    glm::mat4 transform_ = glm::mat4(1.0f);
    glm::vec3 lastPosition_ = glm::vec3(0);
    
    bool active_ = true;
    bool playing_ = false;
    float time_ = 0.0f;
    float emissionAccumulator_ = 0.0f;
    
    VkBuffer particleBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory particleMemory_ = VK_NULL_HANDLE;
    
    VkBuffer indirectBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory indirectMemory_ = VK_NULL_HANDLE;
    
    uint32_t aliveCount_ = 0;
    std::vector<GPUParticle> cpuParticles_;  // For CPU simulation fallback
    
    std::mt19937 rng_;
    
    glm::vec3 sampleEmitterShape(glm::vec3& outVelocity);
};

// ============================================================================
// PARTICLE SYSTEM
// ============================================================================

class ParticleSystem {
public:
    ParticleSystem(VulkanContext& context);
    ~ParticleSystem();
    
    bool initialize();
    void shutdown();
    
    // Emitter management
    std::shared_ptr<ParticleEmitter> createEmitter(const ParticleEmitterConfig& config);
    void destroyEmitter(std::shared_ptr<ParticleEmitter> emitter);
    
    // Global settings
    void setGlobalWind(const glm::vec3& wind) { globalWind_ = wind; }
    void setGlobalGravity(const glm::vec3& gravity) { globalGravity_ = gravity; }
    void setMaxGlobalParticles(uint32_t max) { maxGlobalParticles_ = max; }
    
    // SDF for collision
    void setSDFVolume(VkImageView sdfView, const glm::vec3& boundsMin, const glm::vec3& boundsMax);
    
    // Depth buffer for soft particles
    void setDepthBuffer(VkImageView depthView, const glm::mat4& viewProj);
    
    // Update all emitters
    void update(float deltaTime, const glm::vec3& cameraPos);
    
    // Dispatch compute simulation
    void simulate(VkCommandBuffer cmd);
    
    // Render all particles
    void render(VkCommandBuffer cmd, const glm::mat4& viewProj);
    
    // Statistics
    struct Stats {
        uint32_t activeEmitters;
        uint32_t totalAliveParticles;
        uint32_t totalMaxParticles;
        float gpuSimulationTime;
        float gpuRenderTime;
    };
    Stats getStats() const;
    
private:
    void createComputePipelines();
    void createRenderPipeline(VkRenderPass renderPass);
    
    VulkanContext& context_;
    
    // Compute simulation
    VkPipeline simulatePipeline_ = VK_NULL_HANDLE;
    VkPipeline emitPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout computeLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout computeDescLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    
    // Render
    VkPipeline renderPipeline_ = VK_NULL_HANDLE;
    VkPipeline renderAdditPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout renderLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout renderDescLayout_ = VK_NULL_HANDLE;
    
    std::vector<std::shared_ptr<ParticleEmitter>> emitters_;
    
    glm::vec3 globalWind_ = glm::vec3(0);
    glm::vec3 globalGravity_ = glm::vec3(0, -9.81f, 0);
    uint32_t maxGlobalParticles_ = 100000;
    
    VkImageView sdfView_ = VK_NULL_HANDLE;
    glm::vec3 sdfBoundsMin_ = glm::vec3(-100);
    glm::vec3 sdfBoundsMax_ = glm::vec3(100);
    
    VkImageView depthView_ = VK_NULL_HANDLE;
    glm::mat4 viewProjMatrix_ = glm::mat4(1.0f);
};

// ============================================================================
// COMMON PARTICLE EFFECTS (Presets)
// ============================================================================

namespace ParticlePresets {
    ParticleEmitterConfig fire();
    ParticleEmitterConfig smoke();
    ParticleEmitterConfig sparks();
    ParticleEmitterConfig explosion();
    ParticleEmitterConfig muzzleFlash();
    ParticleEmitterConfig dust();
    ParticleEmitterConfig rain();
    ParticleEmitterConfig snow();
    ParticleEmitterConfig leaves();
    ParticleEmitterConfig blood();
    ParticleEmitterConfig magic();
    ParticleEmitterConfig trail();
}

} // namespace Sanic
