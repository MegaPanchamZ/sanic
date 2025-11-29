/**
 * ParticleSystem.cpp - GPU Particle System Implementation
 */

#include "ParticleSystem.h"
#include <algorithm>
#include <cmath>

namespace Sanic {

// ============================================================================
// VALUE OVER LIFETIME TEMPLATE IMPLEMENTATIONS
// ============================================================================

template<typename T>
T ValueOverLifetime<T>::evaluate(float t, float random) const {
    t = std::clamp(t, 0.0f, 1.0f);
    
    switch (mode) {
        case Mode::Constant:
            return constantValue;
            
        case Mode::RandomBetweenConstants:
            return constantValueMin + (constantValueMax - constantValueMin) * random;
            
        case Mode::Curve: {
            if (curve.empty()) return constantValue;
            if (curve.size() == 1) return curve[0].second;
            
            // Find segment
            for (size_t i = 0; i < curve.size() - 1; i++) {
                if (t >= curve[i].first && t <= curve[i + 1].first) {
                    float segT = (t - curve[i].first) / (curve[i + 1].first - curve[i].first);
                    return curve[i].second + (curve[i + 1].second - curve[i].second) * segT;
                }
            }
            return curve.back().second;
        }
            
        case Mode::RandomBetweenCurves: {
            // Evaluate both curves and lerp
            ValueOverLifetime<T> temp;
            temp.mode = Mode::Curve;
            temp.curve = curveMin;
            T minVal = temp.evaluate(t, random);
            temp.curve = curveMax;
            T maxVal = temp.evaluate(t, random);
            return minVal + (maxVal - minVal) * random;
        }
    }
    
    return constantValue;
}

// Explicit template instantiations
template float ValueOverLifetime<float>::evaluate(float t, float random) const;
template glm::vec3 ValueOverLifetime<glm::vec3>::evaluate(float t, float random) const;
template glm::vec4 ValueOverLifetime<glm::vec4>::evaluate(float t, float random) const;

// ============================================================================
// PARTICLE EMITTER IMPLEMENTATION
// ============================================================================

ParticleEmitter::ParticleEmitter(const ParticleEmitterConfig& config) 
    : config_(config), rng_(std::random_device{}()) {
    cpuParticles_.resize(config.maxParticles);
}

ParticleEmitter::~ParticleEmitter() {
    // Cleanup handled by ParticleSystem
}

void ParticleEmitter::setTransform(const glm::mat4& transform) {
    lastPosition_ = glm::vec3(transform_[3]);
    transform_ = transform;
}

void ParticleEmitter::setActive(bool active) {
    active_ = active;
}

void ParticleEmitter::play() {
    playing_ = true;
    time_ = 0.0f;
    
    if (config_.prewarm) {
        // Simulate one full duration
        update(config_.duration, glm::vec3(0));
    }
}

void ParticleEmitter::pause() {
    playing_ = false;
}

void ParticleEmitter::stop(bool clearParticles) {
    playing_ = false;
    time_ = 0.0f;
    emissionAccumulator_ = 0.0f;
    
    if (clearParticles) {
        aliveCount_ = 0;
    }
}

void ParticleEmitter::emit(int count) {
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    
    for (int i = 0; i < count && aliveCount_ < config_.maxParticles; i++) {
        GPUParticle& p = cpuParticles_[aliveCount_];
        
        glm::vec3 velocity;
        p.position = sampleEmitterShape(velocity);
        p.velocity = velocity * config_.startSpeed.evaluate(0.0f, dist(rng_));
        
        p.lifetime = config_.startLifetime.evaluate(0.0f, dist(rng_));
        p.age = 0.0f;
        
        glm::vec4 startColor = config_.startColor.evaluate(0.0f, dist(rng_));
        p.color = startColor;
        
        float size = config_.startSize.evaluate(0.0f, dist(rng_));
        p.size = glm::vec2(size);
        
        p.rotation = config_.startRotation.evaluate(0.0f, dist(rng_));
        p.angularVelocity = 0.0f;
        
        p.textureIndex = 0;
        p.flags = 0;
        
        aliveCount_++;
    }
}

void ParticleEmitter::update(float deltaTime, const glm::vec3& /*cameraPos*/) {
    if (!active_) return;
    
    time_ += deltaTime;
    
    // Handle looping
    if (!config_.loop && time_ >= config_.duration) {
        playing_ = false;
    }
    
    // Emission
    if (playing_ && config_.emission.enabled) {
        float rate = config_.emission.rateOverTime.evaluate(time_ / config_.duration, 0.5f);
        emissionAccumulator_ += rate * deltaTime;
        
        int toEmit = static_cast<int>(emissionAccumulator_);
        emissionAccumulator_ -= toEmit;
        emit(toEmit);
        
        // Handle bursts
        for (const auto& burst : config_.emission.bursts) {
            if (time_ >= burst.time && time_ - deltaTime < burst.time) {
                std::uniform_real_distribution<float> dist(0.0f, 1.0f);
                if (dist(rng_) < burst.probability) {
                    emit(burst.count);
                }
            }
        }
    }
    
    // Update particles
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    
    uint32_t writeIdx = 0;
    for (uint32_t i = 0; i < aliveCount_; i++) {
        GPUParticle& p = cpuParticles_[i];
        
        p.age += deltaTime;
        
        // Kill old particles
        if (p.age >= p.lifetime) {
            continue;
        }
        
        float normalizedAge = p.age / p.lifetime;
        
        // Apply gravity
        if (config_.force.enabled) {
            p.velocity += config_.force.gravity * deltaTime;
            p.velocity *= (1.0f - config_.force.drag * deltaTime);
        }
        
        // Apply velocity module
        if (config_.velocity.enabled) {
            float speedMod = config_.velocity.speedModifier.evaluate(normalizedAge, dist(rng_));
            p.velocity *= speedMod;
        }
        
        // Move particle
        p.position += p.velocity * deltaTime;
        
        // Update color
        if (config_.color.enabled) {
            p.color = config_.color.colorOverLifetime.evaluate(normalizedAge, dist(rng_));
        }
        
        // Update size
        if (config_.size.enabled) {
            float size = config_.size.sizeOverLifetime.evaluate(normalizedAge, dist(rng_));
            p.size = glm::vec2(size);
        }
        
        // Update rotation
        if (config_.rotation.enabled) {
            float angVel = config_.rotation.angularVelocity.evaluate(normalizedAge, dist(rng_));
            p.rotation += angVel * deltaTime;
        }
        
        // Copy to write position
        if (writeIdx != i) {
            cpuParticles_[writeIdx] = p;
        }
        writeIdx++;
    }
    
    aliveCount_ = writeIdx;
}

glm::vec3 ParticleEmitter::sampleEmitterShape(glm::vec3& outVelocity) {
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    std::uniform_real_distribution<float> distSigned(-1.0f, 1.0f);
    
    glm::vec3 pos = config_.shape.position;
    outVelocity = glm::vec3(0, 1, 0);  // Default upward
    
    switch (config_.shape.shape) {
        case EmitterShape::Point:
            break;
            
        case EmitterShape::Sphere: {
            float u = dist(rng_);
            float v = dist(rng_);
            float theta = 2.0f * 3.14159f * u;
            float phi = std::acos(2.0f * v - 1.0f);
            
            glm::vec3 dir = glm::vec3(
                std::sin(phi) * std::cos(theta),
                std::sin(phi) * std::sin(theta),
                std::cos(phi)
            );
            
            float radius = config_.shape.radius;
            if (config_.shape.radiusThickness > 0) {
                radius *= (1.0f - config_.shape.radiusThickness * dist(rng_));
            }
            
            pos += dir * radius;
            outVelocity = dir;
            break;
        }
            
        case EmitterShape::Hemisphere: {
            float u = dist(rng_);
            float v = dist(rng_) * 0.5f;  // Only upper hemisphere
            float theta = 2.0f * 3.14159f * u;
            float phi = std::acos(2.0f * v - 1.0f);
            
            glm::vec3 dir = glm::vec3(
                std::sin(phi) * std::cos(theta),
                std::cos(phi),
                std::sin(phi) * std::sin(theta)
            );
            
            pos += dir * config_.shape.radius;
            outVelocity = dir;
            break;
        }
            
        case EmitterShape::Cone: {
            float angle = glm::radians(config_.shape.angle);
            float r = dist(rng_);
            float theta = dist(rng_) * 2.0f * 3.14159f;
            
            float coneRadius = r * std::tan(angle);
            
            glm::vec3 offset = glm::vec3(
                std::cos(theta) * coneRadius,
                r * config_.shape.length,
                std::sin(theta) * coneRadius
            );
            
            pos += offset;
            outVelocity = glm::normalize(offset);
            break;
        }
            
        case EmitterShape::Box:
            pos += glm::vec3(
                distSigned(rng_) * config_.shape.boxSize.x * 0.5f,
                distSigned(rng_) * config_.shape.boxSize.y * 0.5f,
                distSigned(rng_) * config_.shape.boxSize.z * 0.5f
            );
            break;
            
        case EmitterShape::Circle: {
            float angle = dist(rng_) * 2.0f * 3.14159f;
            float r = config_.shape.radius * std::sqrt(dist(rng_));
            pos += glm::vec3(std::cos(angle) * r, 0, std::sin(angle) * r);
            break;
        }
            
        case EmitterShape::Edge: {
            float t = dist(rng_);
            pos += glm::mix(config_.shape.edgeStart, config_.shape.edgeEnd, t);
            break;
        }
            
        case EmitterShape::Mesh:
            // Would sample from mesh vertices/triangles
            break;
    }
    
    // Transform to world space
    glm::vec4 worldPos = transform_ * glm::vec4(pos, 1.0f);
    glm::vec4 worldVel = transform_ * glm::vec4(outVelocity, 0.0f);
    
    outVelocity = glm::vec3(worldVel);
    return glm::vec3(worldPos);
}

// ============================================================================
// PARTICLE SYSTEM IMPLEMENTATION
// ============================================================================

ParticleSystem::ParticleSystem(VulkanContext& context) : context_(context) {}

ParticleSystem::~ParticleSystem() {
    shutdown();
}

bool ParticleSystem::initialize() {
    createComputePipelines();
    return true;
}

void ParticleSystem::shutdown() {
    emitters_.clear();
    
    VkDevice device = VK_NULL_HANDLE;  // Get from context
    
    if (simulatePipeline_) vkDestroyPipeline(device, simulatePipeline_, nullptr);
    if (emitPipeline_) vkDestroyPipeline(device, emitPipeline_, nullptr);
    if (computeLayout_) vkDestroyPipelineLayout(device, computeLayout_, nullptr);
    if (computeDescLayout_) vkDestroyDescriptorSetLayout(device, computeDescLayout_, nullptr);
    
    if (renderPipeline_) vkDestroyPipeline(device, renderPipeline_, nullptr);
    if (renderAdditPipeline_) vkDestroyPipeline(device, renderAdditPipeline_, nullptr);
    if (renderLayout_) vkDestroyPipelineLayout(device, renderLayout_, nullptr);
    if (renderDescLayout_) vkDestroyDescriptorSetLayout(device, renderDescLayout_, nullptr);
    
    if (descriptorPool_) vkDestroyDescriptorPool(device, descriptorPool_, nullptr);
}

std::shared_ptr<ParticleEmitter> ParticleSystem::createEmitter(const ParticleEmitterConfig& config) {
    auto emitter = std::make_shared<ParticleEmitter>(config);
    emitters_.push_back(emitter);
    return emitter;
}

void ParticleSystem::destroyEmitter(std::shared_ptr<ParticleEmitter> emitter) {
    auto it = std::find(emitters_.begin(), emitters_.end(), emitter);
    if (it != emitters_.end()) {
        emitters_.erase(it);
    }
}

void ParticleSystem::setSDFVolume(VkImageView sdfView, const glm::vec3& boundsMin, const glm::vec3& boundsMax) {
    sdfView_ = sdfView;
    sdfBoundsMin_ = boundsMin;
    sdfBoundsMax_ = boundsMax;
}

void ParticleSystem::setDepthBuffer(VkImageView depthView, const glm::mat4& viewProj) {
    depthView_ = depthView;
    viewProjMatrix_ = viewProj;
}

void ParticleSystem::update(float deltaTime, const glm::vec3& cameraPos) {
    for (auto& emitter : emitters_) {
        emitter->update(deltaTime, cameraPos);
    }
    
    // Remove dead emitters that are not looping
    emitters_.erase(
        std::remove_if(emitters_.begin(), emitters_.end(),
            [](const std::shared_ptr<ParticleEmitter>& e) {
                return !e->isAlive() && !e->getConfig().loop;
            }),
        emitters_.end()
    );
}

void ParticleSystem::simulate(VkCommandBuffer cmd) {
    // In a full implementation, dispatch compute shaders here
    (void)cmd;
}

void ParticleSystem::render(VkCommandBuffer cmd, const glm::mat4& viewProj) {
    // Bind render pipeline
    // For each emitter, draw indirect
    (void)cmd;
    (void)viewProj;
}

ParticleSystem::Stats ParticleSystem::getStats() const {
    Stats stats = {};
    stats.activeEmitters = static_cast<uint32_t>(emitters_.size());
    
    for (const auto& emitter : emitters_) {
        stats.totalAliveParticles += emitter->getAliveCount();
        stats.totalMaxParticles += emitter->getConfig().maxParticles;
    }
    
    return stats;
}

void ParticleSystem::createComputePipelines() {
    // TODO: Create compute pipelines for particle simulation
}

void ParticleSystem::createRenderPipeline(VkRenderPass renderPass) {
    // TODO: Create render pipeline for particle rendering
    (void)renderPass;
}

// ============================================================================
// PARTICLE PRESETS
// ============================================================================

namespace ParticlePresets {

ParticleEmitterConfig fire() {
    ParticleEmitterConfig config;
    config.name = "Fire";
    config.maxParticles = 500;
    config.duration = 0;
    config.loop = true;
    
    config.startLifetime = {FloatOverLifetime::Mode::RandomBetweenConstants, 0, 0.5f, 1.5f};
    config.startSpeed = {FloatOverLifetime::Mode::RandomBetweenConstants, 0, 2.0f, 5.0f};
    config.startSize = {FloatOverLifetime::Mode::RandomBetweenConstants, 0, 0.3f, 0.8f};
    config.startColor = {ColorOverLifetime::Mode::Constant, glm::vec4(1.0f, 0.5f, 0.1f, 1.0f)};
    
    config.shape.shape = EmitterShape::Cone;
    config.shape.angle = 15.0f;
    config.shape.radius = 0.3f;
    
    config.emission.rateOverTime = {FloatOverLifetime::Mode::Constant, 50.0f};
    
    config.color.enabled = true;
    config.color.colorOverLifetime.curve = {
        {0.0f, glm::vec4(1.0f, 0.8f, 0.2f, 1.0f)},
        {0.3f, glm::vec4(1.0f, 0.3f, 0.1f, 0.8f)},
        {1.0f, glm::vec4(0.2f, 0.1f, 0.1f, 0.0f)}
    };
    
    config.size.enabled = true;
    config.size.sizeOverLifetime.curve = {
        {0.0f, 0.5f},
        {0.3f, 1.0f},
        {1.0f, 0.2f}
    };
    
    config.force.gravity = glm::vec3(0, 3.0f, 0);  // Upward
    config.force.drag = 0.5f;
    
    config.renderer.blendMode = ParticleRendererConfig::BlendMode::Additive;
    
    return config;
}

ParticleEmitterConfig smoke() {
    ParticleEmitterConfig config;
    config.name = "Smoke";
    config.maxParticles = 200;
    config.loop = true;
    
    config.startLifetime = {FloatOverLifetime::Mode::RandomBetweenConstants, 0, 3.0f, 5.0f};
    config.startSpeed = {FloatOverLifetime::Mode::RandomBetweenConstants, 0, 1.0f, 2.0f};
    config.startSize = {FloatOverLifetime::Mode::RandomBetweenConstants, 0, 0.5f, 1.0f};
    config.startColor = {ColorOverLifetime::Mode::Constant, glm::vec4(0.3f, 0.3f, 0.3f, 0.5f)};
    
    config.shape.shape = EmitterShape::Cone;
    config.shape.angle = 20.0f;
    
    config.emission.rateOverTime = {FloatOverLifetime::Mode::Constant, 20.0f};
    
    config.color.enabled = true;
    config.color.colorOverLifetime.curve = {
        {0.0f, glm::vec4(0.4f, 0.4f, 0.4f, 0.6f)},
        {1.0f, glm::vec4(0.2f, 0.2f, 0.2f, 0.0f)}
    };
    
    config.size.enabled = true;
    config.size.sizeOverLifetime.curve = {
        {0.0f, 1.0f},
        {1.0f, 3.0f}
    };
    
    config.force.gravity = glm::vec3(0, 1.0f, 0);
    config.force.drag = 0.2f;
    
    config.noise.enabled = true;
    config.noise.strength = 0.5f;
    config.noise.frequency = 0.5f;
    
    return config;
}

ParticleEmitterConfig sparks() {
    ParticleEmitterConfig config;
    config.name = "Sparks";
    config.maxParticles = 100;
    config.loop = false;
    config.duration = 0.5f;
    
    config.startLifetime = {FloatOverLifetime::Mode::RandomBetweenConstants, 0, 0.3f, 0.8f};
    config.startSpeed = {FloatOverLifetime::Mode::RandomBetweenConstants, 0, 5.0f, 15.0f};
    config.startSize = {FloatOverLifetime::Mode::Constant, 0.05f};
    config.startColor = {ColorOverLifetime::Mode::Constant, glm::vec4(1.0f, 0.8f, 0.3f, 1.0f)};
    
    config.shape.shape = EmitterShape::Sphere;
    config.shape.radius = 0.1f;
    
    config.emission.bursts = {{0.0f, 50}};
    
    config.force.gravity = glm::vec3(0, -9.81f, 0);
    config.force.drag = 0.1f;
    
    config.collision.enabled = true;
    config.collision.bounce = 0.3f;
    
    config.trails.enabled = true;
    config.trails.lifetime = 0.1f;
    
    config.renderer.blendMode = ParticleRendererConfig::BlendMode::Additive;
    
    return config;
}

ParticleEmitterConfig explosion() {
    ParticleEmitterConfig config;
    config.name = "Explosion";
    config.maxParticles = 200;
    config.loop = false;
    config.duration = 2.0f;
    
    config.startLifetime = {FloatOverLifetime::Mode::RandomBetweenConstants, 0, 0.5f, 1.5f};
    config.startSpeed = {FloatOverLifetime::Mode::RandomBetweenConstants, 0, 10.0f, 20.0f};
    config.startSize = {FloatOverLifetime::Mode::RandomBetweenConstants, 0, 1.0f, 2.0f};
    
    config.shape.shape = EmitterShape::Sphere;
    config.shape.radius = 0.5f;
    
    config.emission.bursts = {{0.0f, 100}};
    
    config.color.enabled = true;
    config.color.colorOverLifetime.curve = {
        {0.0f, glm::vec4(1.0f, 1.0f, 0.5f, 1.0f)},
        {0.2f, glm::vec4(1.0f, 0.5f, 0.1f, 0.8f)},
        {1.0f, glm::vec4(0.2f, 0.1f, 0.1f, 0.0f)}
    };
    
    config.size.enabled = true;
    config.size.sizeOverLifetime.curve = {
        {0.0f, 1.0f},
        {0.3f, 1.5f},
        {1.0f, 0.5f}
    };
    
    config.force.gravity = glm::vec3(0, -2.0f, 0);
    config.force.drag = 2.0f;
    
    return config;
}

ParticleEmitterConfig muzzleFlash() {
    ParticleEmitterConfig config;
    config.name = "MuzzleFlash";
    config.maxParticles = 20;
    config.loop = false;
    config.duration = 0.1f;
    
    config.startLifetime = {FloatOverLifetime::Mode::Constant, 0.05f};
    config.startSpeed = {FloatOverLifetime::Mode::Constant, 0.0f};
    config.startSize = {FloatOverLifetime::Mode::RandomBetweenConstants, 0, 0.3f, 0.5f};
    config.startColor = {ColorOverLifetime::Mode::Constant, glm::vec4(1.0f, 0.9f, 0.5f, 1.0f)};
    
    config.shape.shape = EmitterShape::Point;
    
    config.emission.bursts = {{0.0f, 5}};
    
    config.renderer.blendMode = ParticleRendererConfig::BlendMode::Additive;
    
    return config;
}

ParticleEmitterConfig dust() {
    ParticleEmitterConfig config;
    config.name = "Dust";
    config.maxParticles = 100;
    config.loop = true;
    
    config.startLifetime = {FloatOverLifetime::Mode::RandomBetweenConstants, 0, 2.0f, 4.0f};
    config.startSpeed = {FloatOverLifetime::Mode::RandomBetweenConstants, 0, 0.1f, 0.5f};
    config.startSize = {FloatOverLifetime::Mode::RandomBetweenConstants, 0, 0.1f, 0.3f};
    config.startColor = {ColorOverLifetime::Mode::Constant, glm::vec4(0.6f, 0.5f, 0.4f, 0.3f)};
    
    config.shape.shape = EmitterShape::Box;
    config.shape.boxSize = glm::vec3(5.0f, 0.1f, 5.0f);
    
    config.emission.rateOverTime = {FloatOverLifetime::Mode::Constant, 10.0f};
    
    config.force.gravity = glm::vec3(0, 0.2f, 0);
    
    config.noise.enabled = true;
    config.noise.strength = 0.3f;
    
    return config;
}

ParticleEmitterConfig rain() {
    ParticleEmitterConfig config;
    config.name = "Rain";
    config.maxParticles = 2000;
    config.loop = true;
    
    config.startLifetime = {FloatOverLifetime::Mode::Constant, 1.5f};
    config.startSpeed = {FloatOverLifetime::Mode::RandomBetweenConstants, 0, 15.0f, 20.0f};
    config.startSize = {FloatOverLifetime::Mode::Constant, 0.02f};
    config.startColor = {ColorOverLifetime::Mode::Constant, glm::vec4(0.7f, 0.8f, 0.9f, 0.5f)};
    
    config.shape.shape = EmitterShape::Box;
    config.shape.boxSize = glm::vec3(20.0f, 0.1f, 20.0f);
    config.shape.position = glm::vec3(0, 10.0f, 0);
    
    config.emission.rateOverTime = {FloatOverLifetime::Mode::Constant, 500.0f};
    
    config.force.gravity = glm::vec3(0, -20.0f, 0);
    
    config.renderer.renderMode = ParticleRendererConfig::RenderMode::StretchedBillboard;
    config.renderer.velocityScale = 0.1f;
    
    return config;
}

ParticleEmitterConfig snow() {
    ParticleEmitterConfig config;
    config.name = "Snow";
    config.maxParticles = 1000;
    config.loop = true;
    
    config.startLifetime = {FloatOverLifetime::Mode::RandomBetweenConstants, 0, 5.0f, 10.0f};
    config.startSpeed = {FloatOverLifetime::Mode::RandomBetweenConstants, 0, 0.5f, 1.5f};
    config.startSize = {FloatOverLifetime::Mode::RandomBetweenConstants, 0, 0.02f, 0.1f};
    config.startColor = {ColorOverLifetime::Mode::Constant, glm::vec4(1.0f, 1.0f, 1.0f, 0.8f)};
    
    config.shape.shape = EmitterShape::Box;
    config.shape.boxSize = glm::vec3(20.0f, 0.1f, 20.0f);
    config.shape.position = glm::vec3(0, 10.0f, 0);
    
    config.emission.rateOverTime = {FloatOverLifetime::Mode::Constant, 100.0f};
    
    config.force.gravity = glm::vec3(0, -1.0f, 0);
    
    config.noise.enabled = true;
    config.noise.strength = 1.0f;
    config.noise.frequency = 0.3f;
    
    return config;
}

ParticleEmitterConfig leaves() {
    ParticleEmitterConfig config;
    config.name = "Leaves";
    config.maxParticles = 50;
    config.loop = true;
    
    config.startLifetime = {FloatOverLifetime::Mode::RandomBetweenConstants, 0, 5.0f, 10.0f};
    config.startSpeed = {FloatOverLifetime::Mode::RandomBetweenConstants, 0, 0.5f, 2.0f};
    config.startSize = {FloatOverLifetime::Mode::RandomBetweenConstants, 0, 0.1f, 0.2f};
    config.startRotation = {FloatOverLifetime::Mode::RandomBetweenConstants, 0, 0.0f, 360.0f};
    config.startColor = {ColorOverLifetime::Mode::Constant, glm::vec4(0.8f, 0.6f, 0.2f, 1.0f)};
    
    config.shape.shape = EmitterShape::Box;
    config.shape.boxSize = glm::vec3(10.0f, 0.1f, 10.0f);
    config.shape.position = glm::vec3(0, 5.0f, 0);
    
    config.emission.rateOverTime = {FloatOverLifetime::Mode::Constant, 5.0f};
    
    config.force.gravity = glm::vec3(0, -0.5f, 0);
    
    config.rotation.enabled = true;
    config.rotation.angularVelocity = {FloatOverLifetime::Mode::RandomBetweenConstants, 0, -180.0f, 180.0f};
    
    config.noise.enabled = true;
    config.noise.strength = 2.0f;
    config.noise.frequency = 0.5f;
    
    return config;
}

ParticleEmitterConfig blood() {
    ParticleEmitterConfig config;
    config.name = "Blood";
    config.maxParticles = 100;
    config.loop = false;
    config.duration = 0.5f;
    
    config.startLifetime = {FloatOverLifetime::Mode::RandomBetweenConstants, 0, 0.5f, 1.5f};
    config.startSpeed = {FloatOverLifetime::Mode::RandomBetweenConstants, 0, 3.0f, 8.0f};
    config.startSize = {FloatOverLifetime::Mode::RandomBetweenConstants, 0, 0.05f, 0.15f};
    config.startColor = {ColorOverLifetime::Mode::Constant, glm::vec4(0.5f, 0.0f, 0.0f, 1.0f)};
    
    config.shape.shape = EmitterShape::Hemisphere;
    config.shape.radius = 0.1f;
    
    config.emission.bursts = {{0.0f, 30}};
    
    config.force.gravity = glm::vec3(0, -9.81f, 0);
    
    config.collision.enabled = true;
    config.collision.killOnCollision = true;
    
    return config;
}

ParticleEmitterConfig magic() {
    ParticleEmitterConfig config;
    config.name = "Magic";
    config.maxParticles = 200;
    config.loop = true;
    
    config.startLifetime = {FloatOverLifetime::Mode::RandomBetweenConstants, 0, 1.0f, 2.0f};
    config.startSpeed = {FloatOverLifetime::Mode::RandomBetweenConstants, 0, 1.0f, 3.0f};
    config.startSize = {FloatOverLifetime::Mode::RandomBetweenConstants, 0, 0.1f, 0.3f};
    config.startColor = {ColorOverLifetime::Mode::Constant, glm::vec4(0.5f, 0.3f, 1.0f, 1.0f)};
    
    config.shape.shape = EmitterShape::Sphere;
    config.shape.radius = 0.5f;
    
    config.emission.rateOverTime = {FloatOverLifetime::Mode::Constant, 30.0f};
    
    config.color.enabled = true;
    config.color.colorOverLifetime.curve = {
        {0.0f, glm::vec4(0.3f, 0.5f, 1.0f, 1.0f)},
        {0.5f, glm::vec4(1.0f, 0.3f, 0.8f, 0.8f)},
        {1.0f, glm::vec4(0.5f, 0.2f, 1.0f, 0.0f)}
    };
    
    config.velocity.enabled = true;
    config.velocity.orbitalVelocity = {Vec3OverLifetime::Mode::Constant, glm::vec3(0, 2.0f, 0)};
    
    config.noise.enabled = true;
    config.noise.strength = 1.0f;
    
    config.renderer.blendMode = ParticleRendererConfig::BlendMode::Additive;
    
    return config;
}

ParticleEmitterConfig trail() {
    ParticleEmitterConfig config;
    config.name = "Trail";
    config.maxParticles = 100;
    config.loop = true;
    
    config.startLifetime = {FloatOverLifetime::Mode::Constant, 0.5f};
    config.startSpeed = {FloatOverLifetime::Mode::Constant, 0.0f};
    config.startSize = {FloatOverLifetime::Mode::Constant, 0.2f};
    config.startColor = {ColorOverLifetime::Mode::Constant, glm::vec4(1.0f, 0.8f, 0.3f, 1.0f)};
    
    config.shape.shape = EmitterShape::Point;
    
    config.emission.rateOverDistance = {FloatOverLifetime::Mode::Constant, 10.0f};
    
    config.color.enabled = true;
    config.color.colorOverLifetime.curve = {
        {0.0f, glm::vec4(1.0f, 0.8f, 0.3f, 1.0f)},
        {1.0f, glm::vec4(1.0f, 0.3f, 0.1f, 0.0f)}
    };
    
    config.size.enabled = true;
    config.size.sizeOverLifetime.curve = {
        {0.0f, 1.0f},
        {1.0f, 0.0f}
    };
    
    config.renderer.blendMode = ParticleRendererConfig::BlendMode::Additive;
    
    return config;
}

} // namespace ParticlePresets

} // namespace Sanic
