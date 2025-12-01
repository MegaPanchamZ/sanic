#include "VolumetricCloudsSystem.h"
#include "VulkanRenderer.h"
#include "Pipeline.h"
#include "Descriptor.h"
#include "Buffer.h"
#include "Image.h"
#include "SkyAtmosphereSystem.h"

#include <glm/gtc/matrix_transform.hpp>
#include <random>

namespace Kinetic {

//------------------------------------------------------------------------------
// VolumetricCloudsSystem
//------------------------------------------------------------------------------

VolumetricCloudsSystem::VolumetricCloudsSystem() {}

VolumetricCloudsSystem::~VolumetricCloudsSystem() {
    shutdown();
}

bool VolumetricCloudsSystem::initialize(VulkanRenderer* renderer) {
    m_renderer = renderer;
    
    if (!renderer) {
        return false;
    }
    
    // Create noise textures (will be generated later)
    createNoiseTextures();
    
    // Create pipelines
    createPipelines();
    
    // Create samplers
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.maxAnisotropy = 1.0f;
    vkCreateSampler(m_renderer->getDevice(), &samplerInfo, nullptr, &m_linearSampler);
    
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    vkCreateSampler(m_renderer->getDevice(), &samplerInfo, nullptr, &m_nearestSampler);
    
    // Create uniform buffer
    m_uniformBuffer = std::make_unique<Buffer>();
    m_uniformBuffer->create(renderer, sizeof(CloudUniforms),
                            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                            VMA_MEMORY_USAGE_CPU_TO_GPU);
    
    // Create blue noise texture
    m_blueNoise = std::make_unique<Image>();
    m_blueNoise->create2D(m_renderer, 64, 64, VK_FORMAT_R8_UNORM,
                          VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    
    // Generate blue noise data
    std::vector<uint8_t> blueNoiseData(64 * 64);
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, 255);
    for (auto& v : blueNoiseData) {
        v = static_cast<uint8_t>(dist(rng));
    }
    m_blueNoise->upload(blueNoiseData.data(), blueNoiseData.size());
    
    // Create weather map
    m_weatherMap = std::make_unique<Image>();
    m_weatherMap->create2D(m_renderer, 512, 512, VK_FORMAT_R8G8B8A8_UNORM,
                           VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    
    return true;
}

void VolumetricCloudsSystem::shutdown() {
    if (m_renderer) {
        vkDeviceWaitIdle(m_renderer->getDevice());
        
        if (m_linearSampler != VK_NULL_HANDLE) {
            vkDestroySampler(m_renderer->getDevice(), m_linearSampler, nullptr);
            m_linearSampler = VK_NULL_HANDLE;
        }
        if (m_nearestSampler != VK_NULL_HANDLE) {
            vkDestroySampler(m_renderer->getDevice(), m_nearestSampler, nullptr);
            m_nearestSampler = VK_NULL_HANDLE;
        }
    }
    
    m_shapeNoise.reset();
    m_detailNoise.reset();
    m_curlNoise.reset();
    m_weatherMap.reset();
    m_blueNoise.reset();
    m_cloudOutput.reset();
    m_cloudHistory.reset();
    m_cloudDepth.reset();
    
    m_cloudPipeline.reset();
    m_noiseGenPipeline.reset();
    m_weatherGenPipeline.reset();
    m_temporalPipeline.reset();
    
    m_cloudDescSet.reset();
    m_noiseDescSet.reset();
    m_uniformBuffer.reset();
    
    m_renderer = nullptr;
}

void VolumetricCloudsSystem::createNoiseTextures() {
    // Shape noise (128^3, 4-channel for FBM layers)
    m_shapeNoise = std::make_unique<Image>();
    m_shapeNoise->create3D(m_renderer, 128, 128, 128, VK_FORMAT_R8G8B8A8_UNORM,
                           VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    
    // Detail noise (32^3, single channel)
    m_detailNoise = std::make_unique<Image>();
    m_detailNoise->create3D(m_renderer, 32, 32, 32, VK_FORMAT_R8_UNORM,
                            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    
    // Curl noise (128^3, 3-channel for wind distortion)
    m_curlNoise = std::make_unique<Image>();
    m_curlNoise->create3D(m_renderer, 128, 128, 128, VK_FORMAT_R8G8B8A8_SNORM,
                          VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
}

void VolumetricCloudsSystem::createRenderTargets(glm::uvec2 resolution) {
    if (m_currentResolution == resolution) {
        return;  // Already created at this size
    }
    
    m_currentResolution = resolution;
    
    // Apply resolution scale
    glm::uvec2 scaledRes = glm::uvec2(
        static_cast<uint32_t>(resolution.x * m_qualitySettings.resolutionScale),
        static_cast<uint32_t>(resolution.y * m_qualitySettings.resolutionScale)
    );
    scaledRes = glm::max(scaledRes, glm::uvec2(1, 1));
    
    // Cloud color output
    m_cloudOutput = std::make_unique<Image>();
    m_cloudOutput->create2D(m_renderer, scaledRes.x, scaledRes.y, VK_FORMAT_R16G16B16A16_SFLOAT,
                            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    
    // History buffer for temporal reprojection
    m_cloudHistory = std::make_unique<Image>();
    m_cloudHistory->create2D(m_renderer, scaledRes.x, scaledRes.y, VK_FORMAT_R16G16B16A16_SFLOAT,
                             VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    
    // Cloud depth for compositing
    m_cloudDepth = std::make_unique<Image>();
    m_cloudDepth->create2D(m_renderer, scaledRes.x, scaledRes.y, VK_FORMAT_R32_SFLOAT,
                           VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    
    // Recreate descriptor sets
    createDescriptorSets();
}

void VolumetricCloudsSystem::createPipelines() {
    // Main cloud rendering pipeline
    m_cloudPipeline = std::make_unique<ComputePipeline>();
    m_cloudPipeline->create(m_renderer, "shaders/volumetric_clouds.comp.spv");
    
    // Noise generation pipeline (for shape, detail, curl noise)
    m_noiseGenPipeline = std::make_unique<ComputePipeline>();
    m_noiseGenPipeline->create(m_renderer, "shaders/cloud_noise_gen.comp.spv");
    
    // Weather map generation
    m_weatherGenPipeline = std::make_unique<ComputePipeline>();
    m_weatherGenPipeline->create(m_renderer, "shaders/weather_gen.comp.spv");
    
    // Temporal reprojection
    m_temporalPipeline = std::make_unique<ComputePipeline>();
    m_temporalPipeline->create(m_renderer, "shaders/cloud_temporal.comp.spv");
}

void VolumetricCloudsSystem::createDescriptorSets() {
    // Cloud rendering descriptor set
    m_cloudDescSet = std::make_unique<DescriptorSet>();
    m_cloudDescSet->addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT);      // Output
    m_cloudDescSet->addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT); // Shape noise
    m_cloudDescSet->addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT); // Detail noise
    m_cloudDescSet->addBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT); // Weather map
    m_cloudDescSet->addBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT); // Blue noise
    m_cloudDescSet->addBinding(5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT); // Depth buffer
    m_cloudDescSet->addBinding(6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT); // Transmittance LUT
    m_cloudDescSet->addBinding(7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT); // Previous frame
    m_cloudDescSet->addBinding(8, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);     // Uniforms
    m_cloudDescSet->create(m_renderer);
    
    // Update descriptor set bindings
    if (m_cloudOutput && m_shapeNoise && m_detailNoise) {
        m_cloudDescSet->updateImage(0, m_cloudOutput->getView(), VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL);
        m_cloudDescSet->updateImage(1, m_shapeNoise->getView(), m_linearSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_cloudDescSet->updateImage(2, m_detailNoise->getView(), m_linearSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_cloudDescSet->updateImage(3, m_weatherMap->getView(), m_linearSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_cloudDescSet->updateImage(4, m_blueNoise->getView(), m_nearestSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_cloudDescSet->updateImage(7, m_cloudHistory->getView(), m_linearSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_cloudDescSet->updateBuffer(8, m_uniformBuffer->getBuffer(), 0, sizeof(CloudUniforms));
    }
}

void VolumetricCloudsSystem::setCloudLayer(const CloudLayerParams& layer) {
    m_cloudLayer = layer;
}

void VolumetricCloudsSystem::setWindParams(const CloudWindParams& wind) {
    m_windParams = wind;
}

void VolumetricCloudsSystem::setLightingParams(const CloudLightingParams& lighting) {
    m_lightingParams = lighting;
}

void VolumetricCloudsSystem::setQualitySettings(const CloudQualitySettings& quality) {
    m_qualitySettings = quality;
    // May need to recreate render targets if resolution changed
}

void VolumetricCloudsSystem::setSkyAtmosphere(SkyAtmosphereSystem* atmosphere) {
    m_atmosphere = atmosphere;
}

void VolumetricCloudsSystem::generateNoiseTextures(VkCommandBuffer cmd) {
    if (m_noiseGenerated) return;
    
    // Transition textures to general for compute
    m_shapeNoise->transitionLayout(cmd, VK_IMAGE_LAYOUT_GENERAL);
    m_detailNoise->transitionLayout(cmd, VK_IMAGE_LAYOUT_GENERAL);
    m_curlNoise->transitionLayout(cmd, VK_IMAGE_LAYOUT_GENERAL);
    
    // Generate shape noise (Worley/Perlin hybrid)
    m_noiseGenPipeline->bind(cmd);
    
    // Push constant for noise type
    struct NoiseGenParams {
        uint32_t noiseType;  // 0=shape, 1=detail, 2=curl
        uint32_t resolution;
        float frequency;
        float padding;
    } params;
    
    // Shape noise
    params.noiseType = 0;
    params.resolution = 128;
    params.frequency = 4.0f;
    vkCmdPushConstants(cmd, m_noiseGenPipeline->getLayout(), 
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(params), &params);
    vkCmdDispatch(cmd, 128 / 4, 128 / 4, 128 / 4);
    
    // Barrier
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
    
    // Detail noise
    params.noiseType = 1;
    params.resolution = 32;
    params.frequency = 8.0f;
    vkCmdPushConstants(cmd, m_noiseGenPipeline->getLayout(),
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(params), &params);
    vkCmdDispatch(cmd, 32 / 4, 32 / 4, 32 / 4);
    
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
    
    // Curl noise
    params.noiseType = 2;
    params.resolution = 128;
    params.frequency = 2.0f;
    vkCmdPushConstants(cmd, m_noiseGenPipeline->getLayout(),
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(params), &params);
    vkCmdDispatch(cmd, 128 / 4, 128 / 4, 128 / 4);
    
    // Transition to shader read
    m_shapeNoise->transitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    m_detailNoise->transitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    m_curlNoise->transitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    
    m_noiseGenerated = true;
}

void VolumetricCloudsSystem::update(float deltaTime) {
    m_time += deltaTime;
    m_frameNumber++;
}

void VolumetricCloudsSystem::updateUniformBuffer(const glm::mat4& viewProjection,
                                                  const glm::mat4& prevViewProjection,
                                                  const glm::vec3& cameraPos,
                                                  glm::uvec2 resolution) {
    CloudUniforms uniforms{};
    
    uniforms.invViewProjection = glm::inverse(viewProjection);
    uniforms.prevViewProjection = prevViewProjection;
    uniforms.cameraPos = cameraPos;
    uniforms.time = m_time;
    
    // Get sun from atmosphere system if available
    if (m_atmosphere) {
        const auto& sunParams = m_atmosphere->getSunParams();
        uniforms.sunDirection = sunParams.direction;
        uniforms.sunIntensity = sunParams.intensity;
        uniforms.sunColor = sunParams.color;
    } else {
        uniforms.sunDirection = glm::normalize(glm::vec3(0.5f, 0.5f, 0.0f));
        uniforms.sunIntensity = 20.0f;
        uniforms.sunColor = glm::vec3(1.0f, 0.95f, 0.9f);
    }
    
    // Cloud layer
    uniforms.cloudLayerBottom = m_cloudLayer.bottomAltitude;
    uniforms.cloudLayerTop = m_cloudLayer.topAltitude;
    uniforms.cloudDensity = m_cloudLayer.density;
    uniforms.cloudCoverage = m_cloudLayer.coverage;
    uniforms.cloudType = static_cast<float>(m_cloudLayer.type);
    
    // Wind
    uniforms.windDirection = glm::normalize(m_windParams.direction);
    uniforms.windSpeed = m_windParams.speed;
    
    // Lighting
    uniforms.ambientColor = m_lightingParams.ambientColor;
    uniforms.ambientStrength = m_lightingParams.ambientStrength;
    uniforms.extinction = m_lightingParams.extinction;
    uniforms.scatterForward = m_lightingParams.scatterForward;
    uniforms.scatterBack = m_lightingParams.scatterBack;
    uniforms.silverIntensity = m_lightingParams.silverIntensity;
    
    // Resolution and rendering
    uniforms.resolution = glm::vec2(resolution);
    uniforms.earthRadius = 6360.0f;  // km
    uniforms.frameNumber = static_cast<float>(m_frameNumber);
    
    // Quality
    uniforms.temporalBlend = m_qualitySettings.enableTemporalReprojection 
                             ? m_qualitySettings.temporalBlend : 0.0f;
    uniforms.rayOffsetStrength = m_qualitySettings.rayOffsetStrength;
    uniforms.detailScale = m_qualitySettings.detailNoiseScale;
    uniforms.shapeScale = m_qualitySettings.shapeNoiseScale;
    
    m_uniformBuffer->upload(&uniforms, sizeof(uniforms));
}

void VolumetricCloudsSystem::render(VkCommandBuffer cmd,
                                     const glm::mat4& viewProjection,
                                     const glm::mat4& prevViewProjection,
                                     const glm::vec3& cameraPos,
                                     VkImageView depthBuffer,
                                     glm::uvec2 resolution) {
    // Ensure render targets exist
    createRenderTargets(resolution);
    
    // Generate noise if needed
    if (!m_noiseGenerated) {
        generateNoiseTextures(cmd);
    }
    
    // Update uniforms
    updateUniformBuffer(viewProjection, prevViewProjection, cameraPos, resolution);
    
    // Update depth buffer in descriptor set
    m_cloudDescSet->updateImage(5, depthBuffer, m_linearSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    
    // Update transmittance LUT if atmosphere available
    if (m_atmosphere) {
        m_cloudDescSet->updateImage(6, m_atmosphere->getTransmittanceLUTView(), 
                                    m_atmosphere->getLUTSampler(),
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    
    // Copy current output to history for next frame
    if (m_qualitySettings.enableTemporalReprojection && m_frameNumber > 0) {
        // Already handled via descriptor binding
    }
    
    // Transition output to general
    m_cloudOutput->transitionLayout(cmd, VK_IMAGE_LAYOUT_GENERAL);
    
    // Bind pipeline and dispatch
    m_cloudPipeline->bind(cmd);
    m_cloudDescSet->bind(cmd, m_cloudPipeline->getLayout());
    
    glm::uvec2 scaledRes = glm::uvec2(
        static_cast<uint32_t>(resolution.x * m_qualitySettings.resolutionScale),
        static_cast<uint32_t>(resolution.y * m_qualitySettings.resolutionScale)
    );
    
    uint32_t groupsX = (scaledRes.x + 7) / 8;
    uint32_t groupsY = (scaledRes.y + 7) / 8;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);
    
    // Transition output for reading
    m_cloudOutput->transitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    
    // Copy output to history for next frame
    if (m_qualitySettings.enableTemporalReprojection) {
        VkImageCopy copyRegion{};
        copyRegion.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        copyRegion.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        copyRegion.extent = { scaledRes.x, scaledRes.y, 1 };
        
        m_cloudOutput->transitionLayout(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        m_cloudHistory->transitionLayout(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        
        vkCmdCopyImage(cmd,
                       m_cloudOutput->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       m_cloudHistory->getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &copyRegion);
        
        m_cloudOutput->transitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_cloudHistory->transitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
}

VkImageView VolumetricCloudsSystem::getCloudOutput() const {
    return m_cloudOutput ? m_cloudOutput->getView() : VK_NULL_HANDLE;
}

VkImageView VolumetricCloudsSystem::getCloudDepth() const {
    return m_cloudDepth ? m_cloudDepth->getView() : VK_NULL_HANDLE;
}

void VolumetricCloudsSystem::setWeatherMap(VkImageView weatherMap) {
    m_externalWeatherMap = weatherMap;
}

void VolumetricCloudsSystem::generateProceduralWeather(VkCommandBuffer cmd) {
    // Transition weather map
    m_weatherMap->transitionLayout(cmd, VK_IMAGE_LAYOUT_GENERAL);
    
    // Bind weather generation pipeline
    m_weatherGenPipeline->bind(cmd);
    
    // Push parameters
    struct WeatherParams {
        float coverage;
        float time;
        glm::vec2 windOffset;
    } params;
    params.coverage = m_cloudLayer.coverage;
    params.time = m_time;
    params.windOffset = glm::vec2(m_windParams.direction.x, m_windParams.direction.z) * m_time * 0.01f;
    
    vkCmdPushConstants(cmd, m_weatherGenPipeline->getLayout(),
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(params), &params);
    
    vkCmdDispatch(cmd, 512 / 8, 512 / 8, 1);
    
    m_weatherMap->transitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void VolumetricCloudsSystem::drawDebugUI() {
    // ImGui debug interface would go here
}

//------------------------------------------------------------------------------
// WeatherMapGenerator
//------------------------------------------------------------------------------

WeatherMapGenerator::WeatherMapGenerator() {}

void WeatherMapGenerator::generate(VkCommandBuffer cmd,
                                    Image* output,
                                    float coverage,
                                    float precipitation,
                                    glm::vec2 windOffset) {
    // Implementation would generate procedural weather patterns
}

void WeatherMapGenerator::blendWeatherFronts(VkCommandBuffer cmd,
                                              Image* output,
                                              const std::vector<glm::vec4>& fronts) {
    // Implementation would blend multiple weather fronts
}

} // namespace Kinetic
