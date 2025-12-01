#include "WaterSystem.h"
#include "VulkanRenderer.h"
#include "Pipeline.h"
#include "Descriptor.h"
#include "Buffer.h"
#include "Image.h"
#include "Mesh.h"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <algorithm>

namespace Kinetic {

static const float PI = 3.14159265359f;
static const float GRAVITY = 9.81f;

//------------------------------------------------------------------------------
// WaterSystem
//------------------------------------------------------------------------------

WaterSystem::WaterSystem() {}

WaterSystem::~WaterSystem() {
    shutdown();
}

bool WaterSystem::initialize(VulkanRenderer* renderer) {
    m_renderer = renderer;
    
    if (!renderer) {
        return false;
    }
    
    // Create water mesh grid
    createWaterMesh(m_meshResolution);
    
    // Create pipelines
    createPipelines();
    
    // Create sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.maxAnisotropy = 16.0f;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxLod = 8.0f;
    vkCreateSampler(m_renderer->getDevice(), &samplerInfo, nullptr, &m_sampler);
    
    // Create normal map (will be loaded or generated)
    m_normalMap = std::make_unique<Image>();
    m_normalMap->create2D(m_renderer, 512, 512, VK_FORMAT_R8G8B8A8_UNORM,
                          VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    
    // Create foam texture
    m_foamTexture = std::make_unique<Image>();
    m_foamTexture->create2D(m_renderer, 256, 256, VK_FORMAT_R8_UNORM,
                            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    
    // Create caustics texture
    m_causticsTexture = std::make_unique<Image>();
    m_causticsTexture->create2D(m_renderer, 512, 512, VK_FORMAT_R8_UNORM,
                                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
    
    // Create default flow map
    m_flowMapDefault = std::make_unique<Image>();
    m_flowMapDefault->create2D(m_renderer, 64, 64, VK_FORMAT_R8G8_UNORM,
                               VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    
    // Create buffers
    m_waveBuffer = std::make_unique<Buffer>();
    m_waveBuffer->create(m_renderer, sizeof(GerstnerWave) * 8 + sizeof(uint32_t),
                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                         VMA_MEMORY_USAGE_CPU_TO_GPU);
    
    m_materialBuffer = std::make_unique<Buffer>();
    m_materialBuffer->create(m_renderer, sizeof(WaterMaterialParams),
                             VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                             VMA_MEMORY_USAGE_CPU_TO_GPU);
    
    m_uniformBuffer = std::make_unique<Buffer>();
    m_uniformBuffer->create(m_renderer, sizeof(WaterUniforms),
                            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                            VMA_MEMORY_USAGE_CPU_TO_GPU);
    
    // Create descriptor sets
    createDescriptorSets();
    
    return true;
}

void WaterSystem::shutdown() {
    if (m_renderer) {
        vkDeviceWaitIdle(m_renderer->getDevice());
        
        if (m_sampler != VK_NULL_HANDLE) {
            vkDestroySampler(m_renderer->getDevice(), m_sampler, nullptr);
            m_sampler = VK_NULL_HANDLE;
        }
    }
    
    m_waterMesh.reset();
    m_normalMap.reset();
    m_foamTexture.reset();
    m_causticsTexture.reset();
    m_flowMapDefault.reset();
    
    m_waterPipeline.reset();
    m_underwaterPipeline.reset();
    m_causticsPipeline.reset();
    
    m_waterDescSet.reset();
    m_underwaterDescSet.reset();
    
    m_waveBuffer.reset();
    m_materialBuffer.reset();
    m_uniformBuffer.reset();
    
    m_waterBodies.clear();
    m_renderer = nullptr;
}

void WaterSystem::createWaterMesh(uint32_t resolution) {
    // Create a tessellated grid mesh for water surface
    std::vector<float> vertices;
    std::vector<uint32_t> indices;
    
    float step = 1.0f / static_cast<float>(resolution - 1);
    
    // Generate vertices
    for (uint32_t z = 0; z < resolution; z++) {
        for (uint32_t x = 0; x < resolution; x++) {
            float u = static_cast<float>(x) * step;
            float v = static_cast<float>(z) * step;
            
            // Position (will be scaled by water body bounds)
            vertices.push_back(u - 0.5f);  // X
            vertices.push_back(0.0f);       // Y (displaced in shader)
            vertices.push_back(v - 0.5f);  // Z
            
            // Texture coordinates
            vertices.push_back(u);
            vertices.push_back(v);
        }
    }
    
    // Generate indices (triangle strip friendly)
    for (uint32_t z = 0; z < resolution - 1; z++) {
        for (uint32_t x = 0; x < resolution - 1; x++) {
            uint32_t topLeft = z * resolution + x;
            uint32_t topRight = topLeft + 1;
            uint32_t bottomLeft = (z + 1) * resolution + x;
            uint32_t bottomRight = bottomLeft + 1;
            
            // First triangle
            indices.push_back(topLeft);
            indices.push_back(bottomLeft);
            indices.push_back(topRight);
            
            // Second triangle
            indices.push_back(topRight);
            indices.push_back(bottomLeft);
            indices.push_back(bottomRight);
        }
    }
    
    m_waterMesh = std::make_unique<Mesh>();
    m_waterMesh->create(m_renderer, vertices.data(), vertices.size() * sizeof(float),
                        indices.data(), indices.size() * sizeof(uint32_t),
                        indices.size());
}

void WaterSystem::createPipelines() {
    // Water surface pipeline
    m_waterPipeline = std::make_unique<GraphicsPipeline>();
    m_waterPipeline->createFromShaders(m_renderer,
                                        "shaders/water_surface.vert.spv",
                                        "shaders/water_surface.frag.spv");
    
    // Underwater post-process pipeline
    m_underwaterPipeline = std::make_unique<GraphicsPipeline>();
    m_underwaterPipeline->createFromShaders(m_renderer,
                                             "shaders/fullscreen.vert.spv",
                                             "shaders/underwater.frag.spv");
    
    // Caustics compute pipeline
    m_causticsPipeline = std::make_unique<ComputePipeline>();
    m_causticsPipeline->create(m_renderer, "shaders/caustics_gen.comp.spv");
}

void WaterSystem::createDescriptorSets() {
    // Water rendering descriptor set
    m_waterDescSet = std::make_unique<DescriptorSet>();
    m_waterDescSet->addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);  // Scene color
    m_waterDescSet->addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);  // Scene depth
    m_waterDescSet->addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);  // Normal map
    m_waterDescSet->addBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);  // Environment cubemap
    m_waterDescSet->addBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);  // Foam texture
    m_waterDescSet->addBinding(5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);  // Caustics
    m_waterDescSet->addBinding(6, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT);            // Wave uniforms
    m_waterDescSet->addBinding(7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT);            // Wave data
    m_waterDescSet->addBinding(8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_VERTEX_BIT);    // Flow map
    m_waterDescSet->addBinding(9, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT);          // Material
    m_waterDescSet->create(m_renderer);
    
    // Underwater descriptor set
    m_underwaterDescSet = std::make_unique<DescriptorSet>();
    m_underwaterDescSet->addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
    m_underwaterDescSet->addBinding(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT);
    m_underwaterDescSet->create(m_renderer);
}

void WaterSystem::setPhysicsWorld(Physics::PhysicsWorld* world) {
    m_physicsWorld = world;
}

uint32_t WaterSystem::createWaterBody(const WaterBody& body) {
    WaterBody newBody = body;
    newBody.id = m_nextBodyId++;
    m_waterBodies.push_back(newBody);
    return newBody.id;
}

void WaterSystem::removeWaterBody(uint32_t id) {
    auto it = std::find_if(m_waterBodies.begin(), m_waterBodies.end(),
                           [id](const WaterBody& b) { return b.id == id; });
    if (it != m_waterBodies.end()) {
        m_waterBodies.erase(it);
    }
}

WaterBody* WaterSystem::getWaterBody(uint32_t id) {
    for (auto& body : m_waterBodies) {
        if (body.id == id) {
            return &body;
        }
    }
    return nullptr;
}

uint32_t WaterSystem::createOcean(float waterLevel, const std::vector<GerstnerWave>& waves) {
    WaterBody ocean;
    ocean.type = WaterBodyType::Ocean;
    ocean.waterLevel = waterLevel;
    ocean.waves = waves;
    ocean.scale = glm::vec3(10000.0f, 1.0f, 10000.0f);  // Large scale
    
    // Default ocean waves if none provided
    if (ocean.waves.empty()) {
        ocean.waves.push_back({glm::vec2(1.0f, 0.0f), 1.0f, 0.1f, 1.0f, 0.5f});
        ocean.waves.push_back({glm::vec2(0.7f, 0.7f), 0.5f, 0.2f, 0.8f, 0.3f});
        ocean.waves.push_back({glm::vec2(-0.3f, 0.9f), 0.3f, 0.3f, 1.2f, 0.4f});
        ocean.waves.push_back({glm::vec2(0.5f, -0.8f), 0.2f, 0.5f, 0.9f, 0.2f});
    }
    
    return createWaterBody(ocean);
}

uint32_t WaterSystem::createLake(const glm::vec3& center, const glm::vec2& size, float waterLevel) {
    WaterBody lake;
    lake.type = WaterBodyType::Lake;
    lake.position = center;
    lake.waterLevel = waterLevel;
    lake.scale = glm::vec3(size.x, 1.0f, size.y);
    lake.boundsMin = center - glm::vec3(size.x * 0.5f, 10.0f, size.y * 0.5f);
    lake.boundsMax = center + glm::vec3(size.x * 0.5f, 10.0f, size.y * 0.5f);
    
    // Gentle waves for lake
    lake.waves.push_back({glm::vec2(1.0f, 0.0f), 0.1f, 0.5f, 0.5f, 0.2f});
    lake.waves.push_back({glm::vec2(0.0f, 1.0f), 0.08f, 0.7f, 0.4f, 0.15f});
    
    return createWaterBody(lake);
}

uint32_t WaterSystem::createRiver(const std::vector<glm::vec3>& splinePoints, float width, float flowSpeed) {
    if (splinePoints.size() < 2) {
        return 0;
    }
    
    WaterBody river;
    river.type = WaterBodyType::River;
    river.hasFlow = true;
    river.flowSpeed = flowSpeed;
    
    // Calculate bounds from spline
    glm::vec3 minPos = splinePoints[0];
    glm::vec3 maxPos = splinePoints[0];
    for (const auto& p : splinePoints) {
        minPos = glm::min(minPos, p);
        maxPos = glm::max(maxPos, p);
    }
    
    river.boundsMin = minPos - glm::vec3(width, 10.0f, width);
    river.boundsMax = maxPos + glm::vec3(width, 10.0f, width);
    river.position = (minPos + maxPos) * 0.5f;
    river.waterLevel = river.position.y;
    
    // Calculate flow direction from spline
    glm::vec3 flowDir = glm::normalize(splinePoints.back() - splinePoints.front());
    river.flowDirection = glm::vec2(flowDir.x, flowDir.z);
    
    // Small waves along flow
    river.waves.push_back({river.flowDirection, 0.05f, 1.0f, flowSpeed, 0.1f});
    
    return createWaterBody(river);
}

void WaterSystem::update(float deltaTime) {
    m_time += deltaTime;
}

void WaterSystem::updateWaveBuffer() {
    // This would be called before rendering each water body
}

void WaterSystem::updateMaterialBuffer(const WaterBody& body) {
    m_materialBuffer->upload(&body.material, sizeof(WaterMaterialParams));
}

glm::vec3 WaterSystem::calculateGerstnerDisplacement(const glm::vec2& xz,
                                                      const std::vector<GerstnerWave>& waves,
                                                      float time) const {
    glm::vec3 displacement(0.0f);
    
    for (const auto& wave : waves) {
        float k = 2.0f * PI * wave.frequency;
        float c = sqrtf(GRAVITY / k);  // Phase velocity (deep water)
        float a = wave.amplitude;
        float q = wave.steepness;
        
        glm::vec2 d = glm::normalize(wave.direction);
        float dotDP = glm::dot(d, xz);
        float theta = k * dotDP - c * wave.phase * time;
        
        float sinTheta = sinf(theta);
        float cosTheta = cosf(theta);
        
        displacement.x += q * a * d.x * cosTheta;
        displacement.z += q * a * d.y * cosTheta;
        displacement.y += a * sinTheta;
    }
    
    return displacement;
}

glm::vec3 WaterSystem::calculateGerstnerNormal(const glm::vec2& xz,
                                                const std::vector<GerstnerWave>& waves,
                                                float time) const {
    glm::vec3 tangent(1.0f, 0.0f, 0.0f);
    glm::vec3 bitangent(0.0f, 0.0f, 1.0f);
    
    for (const auto& wave : waves) {
        float k = 2.0f * PI * wave.frequency;
        float c = sqrtf(GRAVITY / k);
        float a = wave.amplitude;
        float q = wave.steepness;
        
        glm::vec2 d = glm::normalize(wave.direction);
        float dotDP = glm::dot(d, xz);
        float theta = k * dotDP - c * wave.phase * time;
        
        float sinTheta = sinf(theta);
        float cosTheta = cosf(theta);
        
        tangent.x += -q * k * d.x * d.x * sinTheta;
        tangent.y += q * k * d.x * a * cosTheta;
        tangent.z += -q * k * d.x * d.y * sinTheta;
        
        bitangent.x += -q * k * d.x * d.y * sinTheta;
        bitangent.y += q * k * d.y * a * cosTheta;
        bitangent.z += -q * k * d.y * d.y * sinTheta;
    }
    
    return glm::normalize(glm::cross(bitangent, tangent));
}

float WaterSystem::getWaterHeight(const glm::vec3& worldPos, uint32_t bodyId) const {
    const WaterBody* body = nullptr;
    
    if (bodyId == 0) {
        // Find containing water body
        for (const auto& b : m_waterBodies) {
            if (worldPos.x >= b.boundsMin.x && worldPos.x <= b.boundsMax.x &&
                worldPos.z >= b.boundsMin.z && worldPos.z <= b.boundsMax.z) {
                body = &b;
                break;
            }
        }
    } else {
        for (const auto& b : m_waterBodies) {
            if (b.id == bodyId) {
                body = &b;
                break;
            }
        }
    }
    
    if (!body) {
        return -1e10f;  // No water
    }
    
    glm::vec3 disp = calculateGerstnerDisplacement(glm::vec2(worldPos.x, worldPos.z), body->waves, m_time);
    return body->waterLevel + disp.y;
}

glm::vec3 WaterSystem::getWaterNormal(const glm::vec3& worldPos, uint32_t bodyId) const {
    const WaterBody* body = nullptr;
    for (const auto& b : m_waterBodies) {
        if (b.id == bodyId || bodyId == 0) {
            body = &b;
            break;
        }
    }
    
    if (!body) {
        return glm::vec3(0.0f, 1.0f, 0.0f);
    }
    
    return calculateGerstnerNormal(glm::vec2(worldPos.x, worldPos.z), body->waves, m_time);
}

glm::vec3 WaterSystem::getWaterVelocity(const glm::vec3& worldPos, uint32_t bodyId) const {
    const WaterBody* body = nullptr;
    for (const auto& b : m_waterBodies) {
        if (b.id == bodyId || bodyId == 0) {
            body = &b;
            break;
        }
    }
    
    if (!body) {
        return glm::vec3(0.0f);
    }
    
    glm::vec3 velocity(0.0f);
    
    // Wave-induced velocity
    for (const auto& wave : body->waves) {
        float k = 2.0f * PI * wave.frequency;
        float c = sqrtf(GRAVITY / k);
        
        glm::vec2 d = glm::normalize(wave.direction);
        float dotDP = glm::dot(d, glm::vec2(worldPos.x, worldPos.z));
        float theta = k * dotDP - c * wave.phase * m_time;
        
        float sinTheta = sinf(theta);
        
        // Orbital velocity (simplified)
        velocity.x += wave.amplitude * c * d.x * sinTheta;
        velocity.z += wave.amplitude * c * d.y * sinTheta;
    }
    
    // Add flow velocity for rivers
    if (body->hasFlow) {
        velocity.x += body->flowDirection.x * body->flowSpeed;
        velocity.z += body->flowDirection.y * body->flowSpeed;
    }
    
    return velocity;
}

BuoyancyResult WaterSystem::calculateBuoyancy(Physics::Body* body, uint32_t waterBodyId) const {
    BuoyancyResult result;
    
    // Would need actual physics body interface
    // This is a placeholder implementation
    
    return result;
}

bool WaterSystem::isPointUnderwater(const glm::vec3& worldPos) const {
    for (const auto& body : m_waterBodies) {
        float waterHeight = getWaterHeight(worldPos, body.id);
        if (worldPos.y < waterHeight) {
            return true;
        }
    }
    return false;
}

void WaterSystem::render(VkCommandBuffer cmd,
                          const glm::mat4& viewProjection,
                          const glm::mat4& prevViewProjection,
                          const glm::vec3& cameraPos,
                          VkImageView sceneColor,
                          VkImageView sceneDepth) {
    if (m_waterBodies.empty()) return;
    
    // Update descriptor set with scene textures
    m_waterDescSet->updateImage(0, sceneColor, m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    m_waterDescSet->updateImage(1, sceneDepth, m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    m_waterDescSet->updateImage(2, m_normalMap->getView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    m_waterDescSet->updateImage(4, m_foamTexture->getView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    m_waterDescSet->updateImage(5, m_causticsTexture->getView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    
    // Bind pipeline
    m_waterPipeline->bind(cmd);
    
    // Render each visible water body
    for (const auto& body : m_waterBodies) {
        if (!body.visible) continue;
        
        // Update uniforms for this water body
        WaterUniforms uniforms{};
        uniforms.viewProjection = viewProjection;
        uniforms.prevViewProjection = prevViewProjection;
        uniforms.model = glm::translate(glm::mat4(1.0f), body.position) *
                         glm::mat4_cast(body.rotation) *
                         glm::scale(glm::mat4(1.0f), body.scale);
        uniforms.cameraPos = cameraPos;
        uniforms.time = m_time;
        uniforms.waterLevel = body.waterLevel;
        
        // Calculate aggregate wave params
        float maxAmplitude = 0.0f;
        float avgFrequency = 0.0f;
        float avgSteepness = 0.0f;
        for (const auto& wave : body.waves) {
            maxAmplitude = std::max(maxAmplitude, wave.amplitude);
            avgFrequency += wave.frequency;
            avgSteepness += wave.steepness;
        }
        if (!body.waves.empty()) {
            avgFrequency /= body.waves.size();
            avgSteepness /= body.waves.size();
        }
        
        uniforms.waveAmplitude = maxAmplitude;
        uniforms.waveFrequency = avgFrequency;
        uniforms.waveSteepness = avgSteepness;
        
        m_uniformBuffer->upload(&uniforms, sizeof(uniforms));
        
        // Update wave buffer
        struct WaveBufferData {
            GerstnerWave waves[8];
            uint32_t waveCount;
        } waveData{};
        
        waveData.waveCount = std::min(static_cast<uint32_t>(body.waves.size()), 8u);
        for (uint32_t i = 0; i < waveData.waveCount; i++) {
            waveData.waves[i] = body.waves[i];
        }
        m_waveBuffer->upload(&waveData, sizeof(waveData));
        
        // Update material
        updateMaterialBuffer(body);
        
        // Bind descriptor set
        m_waterDescSet->bind(cmd, m_waterPipeline->getLayout());
        
        // Draw water mesh
        m_waterMesh->bind(cmd);
        m_waterMesh->draw(cmd);
    }
}

void WaterSystem::renderUnderwaterEffects(VkCommandBuffer cmd,
                                           const glm::vec3& cameraPos,
                                           VkImageView sceneColor) {
    if (!m_underwaterParams.enabled) return;
    
    // Check if camera is underwater
    if (!isPointUnderwater(cameraPos)) return;
    
    // Bind underwater post-process pipeline
    m_underwaterPipeline->bind(cmd);
    
    // Update descriptor set
    m_underwaterDescSet->updateImage(0, sceneColor, m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    m_underwaterDescSet->bind(cmd, m_underwaterPipeline->getLayout());
    
    // Fullscreen triangle
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

VkImageView WaterSystem::getCausticsTexture() const {
    return m_causticsTexture ? m_causticsTexture->getView() : VK_NULL_HANDLE;
}

void WaterSystem::renderCaustics(VkCommandBuffer cmd,
                                  const glm::mat4& lightViewProj,
                                  float lightIntensity) {
    // Transition caustics texture
    m_causticsTexture->transitionLayout(cmd, VK_IMAGE_LAYOUT_GENERAL);
    
    // Bind compute pipeline
    m_causticsPipeline->bind(cmd);
    
    // Dispatch
    vkCmdDispatch(cmd, 512 / 8, 512 / 8, 1);
    
    // Transition back
    m_causticsTexture->transitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void WaterSystem::setFlowMap(uint32_t bodyId, VkImageView flowMap) {
    // Store external flow map for the water body
}

void WaterSystem::generateFlowMap(uint32_t bodyId, const std::vector<glm::vec3>& splinePoints) {
    // Generate flow map from spline
}

void WaterSystem::setUnderwaterParams(const UnderwaterParams& params) {
    m_underwaterParams = params;
}

void WaterSystem::drawDebugUI() {
    // ImGui debug interface
}

//------------------------------------------------------------------------------
// BuoyancyComponent
//------------------------------------------------------------------------------

BuoyancyComponent::BuoyancyComponent() {}

void BuoyancyComponent::setBody(Physics::Body* body) {
    m_body = body;
}

void BuoyancyComponent::setWaterSystem(WaterSystem* water) {
    m_waterSystem = water;
}

void BuoyancyComponent::setWaterBodyId(uint32_t id) {
    m_waterBodyId = id;
}

void BuoyancyComponent::addSamplePoint(const glm::vec3& localPos, float radius) {
    m_samplePoints.push_back({localPos, radius});
}

void BuoyancyComponent::clearSamplePoints() {
    m_samplePoints.clear();
}

void BuoyancyComponent::update(float deltaTime) {
    if (!m_body || !m_waterSystem) return;
    
    m_lastResult = BuoyancyResult{};
    
    // Calculate buoyancy at each sample point
    // This would integrate with the physics engine
    
    // For each sample point:
    // 1. Transform to world space
    // 2. Get water height at that position
    // 3. Calculate submersion depth
    // 4. Apply buoyancy force (Archimedes principle)
    // 5. Apply drag based on water velocity difference
}

void BuoyancyComponent::setDragCoefficient(float linear, float angular) {
    m_linearDrag = linear;
    m_angularDrag = angular;
}

void BuoyancyComponent::setVolumeOverride(float volume) {
    m_volumeOverride = volume;
}

} // namespace Kinetic
