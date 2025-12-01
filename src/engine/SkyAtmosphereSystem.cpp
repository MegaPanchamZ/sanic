#include "SkyAtmosphereSystem.h"
#include "VulkanRenderer.h"
#include "Pipeline.h"
#include "Descriptor.h"
#include "Buffer.h"
#include "Image.h"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace Kinetic {

// Constants
static const float PI = 3.14159265359f;
static const float DEG_TO_RAD = PI / 180.0f;

//------------------------------------------------------------------------------
// SkyAtmosphereSystem
//------------------------------------------------------------------------------

SkyAtmosphereSystem::SkyAtmosphereSystem() {
    // Default atmosphere matches Earth
}

SkyAtmosphereSystem::~SkyAtmosphereSystem() {
    shutdown();
}

bool SkyAtmosphereSystem::initialize(VulkanRenderer* renderer) {
    m_renderer = renderer;
    
    if (!renderer) {
        return false;
    }
    
    // Create LUT textures
    createLUTTextures();
    
    // Create compute and graphics pipelines
    createPipelines();
    
    // Create descriptor sets
    createDescriptorSets();
    
    // Create uniform buffer
    VkDeviceSize uniformSize = sizeof(AtmosphereUniforms);
    m_uniformBuffer = std::make_unique<Buffer>();
    m_uniformBuffer->create(renderer, uniformSize, 
                            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                            VMA_MEMORY_USAGE_CPU_TO_GPU);
    
    m_lutsDirty = true;
    
    return true;
}

void SkyAtmosphereSystem::shutdown() {
    if (m_renderer) {
        vkDeviceWaitIdle(m_renderer->getDevice());
        
        if (m_lutSampler != VK_NULL_HANDLE) {
            vkDestroySampler(m_renderer->getDevice(), m_lutSampler, nullptr);
            m_lutSampler = VK_NULL_HANDLE;
        }
    }
    
    m_transmittanceLUT.reset();
    m_multiScatteringLUT.reset();
    m_scatteringLUT.reset();
    m_aerialPerspectiveLUT.reset();
    
    m_transmittancePipeline.reset();
    m_multiScatteringPipeline.reset();
    m_scatteringPipeline.reset();
    m_aerialPerspectivePipeline.reset();
    m_skyPipeline.reset();
    
    m_lutComputeDescSet.reset();
    m_skyRenderDescSet.reset();
    m_uniformBuffer.reset();
    
    m_renderer = nullptr;
}

void SkyAtmosphereSystem::createLUTTextures() {
    // Create LUT sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 1.0f;
    
    vkCreateSampler(m_renderer->getDevice(), &samplerInfo, nullptr, &m_lutSampler);
    
    // Transmittance LUT (2D, RGBA16F)
    m_transmittanceLUT = std::make_unique<Image>();
    m_transmittanceLUT->create2D(m_renderer,
                                  m_lutSizes.transmittance.x,
                                  m_lutSizes.transmittance.y,
                                  VK_FORMAT_R16G16B16A16_SFLOAT,
                                  VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    
    // Multi-scattering LUT (2D, RGBA16F)
    m_multiScatteringLUT = std::make_unique<Image>();
    m_multiScatteringLUT->create2D(m_renderer,
                                    m_lutSizes.multiScattering.x,
                                    m_lutSizes.multiScattering.y,
                                    VK_FORMAT_R16G16B16A16_SFLOAT,
                                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    
    // Scattering LUT (3D, RGBA16F)
    m_scatteringLUT = std::make_unique<Image>();
    m_scatteringLUT->create3D(m_renderer,
                               m_lutSizes.scattering.x,
                               m_lutSizes.scattering.y,
                               m_lutSizes.scattering.z,
                               VK_FORMAT_R16G16B16A16_SFLOAT,
                               VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    
    // Aerial perspective LUT (3D froxel, RGBA16F)
    m_aerialPerspectiveLUT = std::make_unique<Image>();
    m_aerialPerspectiveLUT->create3D(m_renderer,
                                      m_lutSizes.aerialPerspective.x,
                                      m_lutSizes.aerialPerspective.y,
                                      m_lutSizes.aerialPerspective.z,
                                      VK_FORMAT_R16G16B16A16_SFLOAT,
                                      VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
}

void SkyAtmosphereSystem::createPipelines() {
    // Transmittance LUT compute pipeline
    m_transmittancePipeline = std::make_unique<ComputePipeline>();
    m_transmittancePipeline->create(m_renderer, "shaders/atmosphere_lut.comp.spv");
    
    // Multi-scattering LUT compute pipeline (reuses same shader with different entry)
    m_multiScatteringPipeline = std::make_unique<ComputePipeline>();
    m_multiScatteringPipeline->create(m_renderer, "shaders/atmosphere_lut.comp.spv");
    
    // Scattering LUT compute pipeline
    m_scatteringPipeline = std::make_unique<ComputePipeline>();
    m_scatteringPipeline->create(m_renderer, "shaders/atmosphere_lut.comp.spv");
    
    // Aerial perspective compute pipeline
    m_aerialPerspectivePipeline = std::make_unique<ComputePipeline>();
    m_aerialPerspectivePipeline->create(m_renderer, "shaders/atmosphere_lut.comp.spv");
    
    // Sky rendering graphics pipeline
    m_skyPipeline = std::make_unique<GraphicsPipeline>();
    m_skyPipeline->createFromShaders(m_renderer, 
                                      "shaders/fullscreen.vert.spv",
                                      "shaders/sky_atmosphere.frag.spv");
}

void SkyAtmosphereSystem::createDescriptorSets() {
    // LUT compute descriptor set
    m_lutComputeDescSet = std::make_unique<DescriptorSet>();
    m_lutComputeDescSet->addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);
    m_lutComputeDescSet->addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT);  // Transmittance
    m_lutComputeDescSet->addBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT);  // Multi-scattering
    m_lutComputeDescSet->addBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT);  // Scattering
    m_lutComputeDescSet->addBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT);  // Aerial
    m_lutComputeDescSet->create(m_renderer);
    
    // Sky render descriptor set
    m_skyRenderDescSet = std::make_unique<DescriptorSet>();
    m_skyRenderDescSet->addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT);
    m_skyRenderDescSet->addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);  // Transmittance
    m_skyRenderDescSet->addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);  // Multi-scattering
    m_skyRenderDescSet->addBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);  // Scattering
    m_skyRenderDescSet->create(m_renderer);
}

void SkyAtmosphereSystem::setAtmosphereParams(const AtmosphereParams& params) {
    m_atmosphereParams = params;
    m_lutsDirty = true;
}

void SkyAtmosphereSystem::setSunParams(const SunParams& params) {
    m_sunParams = params;
}

void SkyAtmosphereSystem::setSunDirection(const glm::vec3& direction) {
    m_sunParams.direction = glm::normalize(direction);
}

void SkyAtmosphereSystem::setSunDirectionFromTimeOfDay(float hours, float latitude) {
    TimeOfDayController tod;
    tod.setTime(hours);
    tod.setLatitude(latitude);
    m_sunParams.direction = tod.getSunDirection();
}

void SkyAtmosphereSystem::updateUniformBuffer() {
    AtmosphereUniforms uniforms{};
    
    // Rayleigh
    uniforms.rayleighScattering = m_atmosphereParams.rayleighScattering;
    uniforms.rayleighDensityH = m_atmosphereParams.rayleighDensityH;
    
    // Mie
    uniforms.mieScattering = m_atmosphereParams.mieScattering;
    uniforms.mieDensityH = m_atmosphereParams.mieDensityH;
    uniforms.mieExtinction = m_atmosphereParams.mieExtinction;
    uniforms.miePhaseG = m_atmosphereParams.miePhaseG;
    
    // Ozone
    uniforms.ozoneAbsorption = m_atmosphereParams.ozoneAbsorption;
    uniforms.ozoneLayerCenter = m_atmosphereParams.ozoneLayerCenter;
    uniforms.ozoneLayerWidth = m_atmosphereParams.ozoneLayerWidth;
    
    // Ground
    uniforms.groundAlbedo = m_atmosphereParams.groundAlbedo;
    uniforms.earthRadius = m_atmosphereParams.earthRadius;
    uniforms.atmosphereRadius = m_atmosphereParams.atmosphereRadius;
    
    // Sun
    uniforms.sunDirection = m_sunParams.direction;
    uniforms.sunIntensity = m_sunParams.intensity;
    uniforms.sunColor = m_sunParams.color;
    uniforms.sunDiskRadius = m_sunParams.diskRadius;
    
    // LUT sizes
    uniforms.transmittanceSize = m_lutSizes.transmittance;
    uniforms.multiScatteringSize = m_lutSizes.multiScattering;
    uniforms.scatteringSize = m_lutSizes.scattering;
    uniforms.aerialPerspectiveSize = m_lutSizes.aerialPerspective;
    
    m_uniformBuffer->upload(&uniforms, sizeof(uniforms));
}

void SkyAtmosphereSystem::computeTransmittanceLUT(VkCommandBuffer cmd) {
    updateUniformBuffer();
    
    // Transition image to general layout for compute
    m_transmittanceLUT->transitionLayout(cmd, VK_IMAGE_LAYOUT_GENERAL);
    
    // Bind pipeline and descriptor set
    m_transmittancePipeline->bind(cmd);
    m_lutComputeDescSet->bind(cmd, m_transmittancePipeline->getLayout());
    
    // Dispatch
    uint32_t groupsX = (m_lutSizes.transmittance.x + 7) / 8;
    uint32_t groupsY = (m_lutSizes.transmittance.y + 7) / 8;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);
    
    // Transition back to shader read
    m_transmittanceLUT->transitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void SkyAtmosphereSystem::computeMultiScatteringLUT(VkCommandBuffer cmd) {
    // Transition image
    m_multiScatteringLUT->transitionLayout(cmd, VK_IMAGE_LAYOUT_GENERAL);
    
    // Bind pipeline
    m_multiScatteringPipeline->bind(cmd);
    m_lutComputeDescSet->bind(cmd, m_multiScatteringPipeline->getLayout());
    
    // Push constant to select multi-scattering mode
    uint32_t mode = 1;  // Multi-scattering
    vkCmdPushConstants(cmd, m_multiScatteringPipeline->getLayout(),
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &mode);
    
    // Dispatch
    uint32_t groupsX = (m_lutSizes.multiScattering.x + 7) / 8;
    uint32_t groupsY = (m_lutSizes.multiScattering.y + 7) / 8;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);
    
    // Transition back
    m_multiScatteringLUT->transitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void SkyAtmosphereSystem::computeScatteringLUT(VkCommandBuffer cmd) {
    // Transition image
    m_scatteringLUT->transitionLayout(cmd, VK_IMAGE_LAYOUT_GENERAL);
    
    // Bind pipeline
    m_scatteringPipeline->bind(cmd);
    m_lutComputeDescSet->bind(cmd, m_scatteringPipeline->getLayout());
    
    // Push constant to select scattering mode
    uint32_t mode = 2;  // Scattering
    vkCmdPushConstants(cmd, m_scatteringPipeline->getLayout(),
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &mode);
    
    // Dispatch 3D
    uint32_t groupsX = (m_lutSizes.scattering.x + 3) / 4;
    uint32_t groupsY = (m_lutSizes.scattering.y + 3) / 4;
    uint32_t groupsZ = (m_lutSizes.scattering.z + 3) / 4;
    vkCmdDispatch(cmd, groupsX, groupsY, groupsZ);
    
    // Transition back
    m_scatteringLUT->transitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void SkyAtmosphereSystem::computeAllLUTs(VkCommandBuffer cmd) {
    // Compute in dependency order
    computeTransmittanceLUT(cmd);
    
    // Barrier before multi-scattering (depends on transmittance)
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, 
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 1, &barrier, 0, nullptr, 0, nullptr);
    
    computeMultiScatteringLUT(cmd);
    
    // Barrier before scattering (depends on multi-scattering)
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 1, &barrier, 0, nullptr, 0, nullptr);
    
    computeScatteringLUT(cmd);
    
    m_lutsDirty = false;
}

void SkyAtmosphereSystem::updateAerialPerspective(VkCommandBuffer cmd,
                                                   const glm::mat4& viewProjection,
                                                   const glm::vec3& cameraPos) {
    // Update uniforms with camera data
    AtmosphereUniforms* uniforms = static_cast<AtmosphereUniforms*>(m_uniformBuffer->map());
    uniforms->invViewProjection = glm::inverse(viewProjection);
    uniforms->cameraPosition = cameraPos;
    m_uniformBuffer->unmap();
    
    // Transition image
    m_aerialPerspectiveLUT->transitionLayout(cmd, VK_IMAGE_LAYOUT_GENERAL);
    
    // Bind pipeline
    m_aerialPerspectivePipeline->bind(cmd);
    m_lutComputeDescSet->bind(cmd, m_aerialPerspectivePipeline->getLayout());
    
    // Push constant for aerial perspective mode
    uint32_t mode = 3;  // Aerial perspective
    vkCmdPushConstants(cmd, m_aerialPerspectivePipeline->getLayout(),
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &mode);
    
    // Dispatch
    uint32_t groupsX = (m_lutSizes.aerialPerspective.x + 3) / 4;
    uint32_t groupsY = (m_lutSizes.aerialPerspective.y + 3) / 4;
    uint32_t groupsZ = (m_lutSizes.aerialPerspective.z + 3) / 4;
    vkCmdDispatch(cmd, groupsX, groupsY, groupsZ);
    
    // Transition back
    m_aerialPerspectiveLUT->transitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void SkyAtmosphereSystem::renderSky(VkCommandBuffer cmd,
                                     VkRenderPass renderPass,
                                     uint32_t subpass,
                                     const glm::mat4& viewProjection,
                                     const glm::vec3& cameraPos) {
    // Update uniforms
    AtmosphereUniforms* uniforms = static_cast<AtmosphereUniforms*>(m_uniformBuffer->map());
    uniforms->invViewProjection = glm::inverse(viewProjection);
    uniforms->cameraPosition = cameraPos;
    m_uniformBuffer->unmap();
    
    // Bind graphics pipeline
    m_skyPipeline->bind(cmd);
    
    // Bind descriptor set with LUTs
    m_skyRenderDescSet->bind(cmd, m_skyPipeline->getLayout());
    
    // Draw fullscreen triangle
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

VkImageView SkyAtmosphereSystem::getTransmittanceLUTView() const {
    return m_transmittanceLUT ? m_transmittanceLUT->getView() : VK_NULL_HANDLE;
}

VkImageView SkyAtmosphereSystem::getMultiScatteringLUTView() const {
    return m_multiScatteringLUT ? m_multiScatteringLUT->getView() : VK_NULL_HANDLE;
}

VkImageView SkyAtmosphereSystem::getScatteringLUTView() const {
    return m_scatteringLUT ? m_scatteringLUT->getView() : VK_NULL_HANDLE;
}

VkImageView SkyAtmosphereSystem::getAerialPerspectiveView() const {
    return m_aerialPerspectiveLUT ? m_aerialPerspectiveLUT->getView() : VK_NULL_HANDLE;
}

void SkyAtmosphereSystem::drawDebugUI() {
    // ImGui debug interface would go here
    // Show LUT textures, parameters, etc.
}

//------------------------------------------------------------------------------
// TimeOfDayController
//------------------------------------------------------------------------------

TimeOfDayController::TimeOfDayController() {}

void TimeOfDayController::setTime(float hours) {
    m_currentHour = fmod(hours, 24.0f);
    if (m_currentHour < 0.0f) m_currentHour += 24.0f;
}

void TimeOfDayController::setLatitude(float lat) {
    m_latitude = glm::clamp(lat, -90.0f, 90.0f);
}

void TimeOfDayController::setDayOfYear(int day) {
    m_dayOfYear = glm::clamp(day, 1, 365);
}

void TimeOfDayController::update(float deltaTime) {
    m_currentHour += deltaTime * m_timeScale / 3600.0f;  // Convert seconds to hours
    if (m_currentHour >= 24.0f) m_currentHour -= 24.0f;
}

void TimeOfDayController::setTimeScale(float scale) {
    m_timeScale = scale;
}

glm::vec3 TimeOfDayController::calculateSunPosition() const {
    // Simplified solar position calculation
    // Based on NOAA solar position algorithm
    
    float latRad = m_latitude * DEG_TO_RAD;
    
    // Fractional year (radians)
    float gamma = 2.0f * PI * (m_dayOfYear - 1 + (m_currentHour - 12.0f) / 24.0f) / 365.0f;
    
    // Equation of time (minutes)
    float eqtime = 229.18f * (0.000075f + 0.001868f * cos(gamma) - 0.032077f * sin(gamma)
                              - 0.014615f * cos(2.0f * gamma) - 0.040849f * sin(2.0f * gamma));
    
    // Solar declination angle
    float decl = 0.006918f - 0.399912f * cos(gamma) + 0.070257f * sin(gamma)
                 - 0.006758f * cos(2.0f * gamma) + 0.000907f * sin(2.0f * gamma)
                 - 0.002697f * cos(3.0f * gamma) + 0.00148f * sin(3.0f * gamma);
    
    // Solar time
    float timeOffset = eqtime;  // Simplified, ignoring longitude
    float trueSolarTime = m_currentHour * 60.0f + timeOffset;
    float hourAngle = (trueSolarTime / 4.0f - 180.0f) * DEG_TO_RAD;
    
    // Solar elevation and azimuth
    float sinElev = sin(latRad) * sin(decl) + cos(latRad) * cos(decl) * cos(hourAngle);
    float elevation = asin(sinElev);
    
    float cosAz = (sin(decl) - sin(latRad) * sinElev) / (cos(latRad) * cos(elevation));
    cosAz = glm::clamp(cosAz, -1.0f, 1.0f);
    float azimuth = acos(cosAz);
    
    if (hourAngle > 0.0f) {
        azimuth = 2.0f * PI - azimuth;
    }
    
    // Convert to direction vector
    // Y is up, azimuth is from north, elevation from horizon
    float cosElev = cos(elevation);
    glm::vec3 sunDir;
    sunDir.x = cosElev * sin(azimuth);
    sunDir.y = sin(elevation);
    sunDir.z = cosElev * cos(azimuth);
    
    return glm::normalize(sunDir);
}

glm::vec3 TimeOfDayController::getSunDirection() const {
    return calculateSunPosition();
}

void TimeOfDayController::setSunrise() {
    m_currentHour = 6.0f;
}

void TimeOfDayController::setNoon() {
    m_currentHour = 12.0f;
}

void TimeOfDayController::setSunset() {
    m_currentHour = 18.0f;
}

void TimeOfDayController::setMidnight() {
    m_currentHour = 0.0f;
}

} // namespace Kinetic
