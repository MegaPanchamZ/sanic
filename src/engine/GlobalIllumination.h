/**
 * GlobalIllumination.h
 * 
 * Main GI system integrating screen probes, radiance cache, and final gather.
 * Coordinates all GI components for Lumen-style indirect lighting.
 * 
 * Turn 31-36: Complete GI integration
 */

#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>

class VulkanContext;
class ScreenProbes;
class RadianceCache;
class SurfaceCache;
class SDFGenerator;
class ScreenSpaceTracing;

// GI quality preset
enum class GIQuality {
    Low,        // Screen probes only, no world cache
    Medium,     // Screen probes + 2-level clipmap
    High,       // Full pipeline with SDF tracing
    Ultra       // Ray tracing where available
};

// GI method for different surfaces
enum class GIMethod {
    ScreenProbes,       // Primary for dynamic
    SurfaceCache,       // For static geometry
    RadianceCache,      // World-space fallback
    SDFTracing,         // For off-screen
    RayTracing          // Hardware RT
};

struct GIConfig {
    GIQuality quality = GIQuality::High;
    
    // Screen probes
    uint32_t probesPerTile = 1;
    uint32_t raysPerProbe = 64;
    
    // Radiance cache
    uint32_t clipMapLevels = 4;
    float baseCellSize = 0.5f;
    
    // Final gather
    uint32_t gatherSamples = 16;
    float gatherRadius = 2.0f;
    
    // Quality
    float maxTraceDistance = 200.0f;
    float skyIntensity = 1.0f;
    float emissiveMultiplier = 1.0f;
    float aoStrength = 1.0f;
    
    // Temporal
    uint32_t historyFrames = 8;
    float temporalWeight = 0.95f;
    
    // Debug
    bool showProbes = false;
    bool showRadianceCache = false;
    int debugMode = 0;  // 0=off, 1=diffuse, 2=specular, 3=ao
};

// Final GI output
struct GIOutput {
    VkImageView diffuseGI;      // Diffuse indirect lighting
    VkImageView specularGI;     // Specular indirect (reflections)
    VkImageView ao;             // Ambient occlusion
    VkImageView bentNormals;    // Bent normals for sky occlusion
};

class GlobalIllumination {
public:
    GlobalIllumination() = default;
    ~GlobalIllumination();
    
    bool initialize(VulkanContext* context,
                    uint32_t screenWidth,
                    uint32_t screenHeight,
                    const GIConfig& config = {});
    void cleanup();
    
    // Resize when window changes
    void resize(uint32_t width, uint32_t height);
    
    // Main GI update - call once per frame
    void update(VkCommandBuffer cmd,
                const glm::mat4& view,
                const glm::mat4& proj,
                const glm::vec3& cameraPos,
                float deltaTime);
    
    // Compute GI for the frame
    void computeGI(VkCommandBuffer cmd,
                   VkImageView gbufferAlbedo,
                   VkImageView gbufferNormal,
                   VkImageView gbufferDepth,
                   VkImageView gbufferRoughness,
                   VkBuffer lightBuffer,
                   uint32_t lightCount);
    
    // Apply GI to final image
    void applyGI(VkCommandBuffer cmd,
                 VkImageView directLighting,
                 VkImageView outputHDR);
    
    // Inject emissive surfaces
    void injectEmissives(VkCommandBuffer cmd,
                         VkImageView emissiveBuffer);
    
    // Update sky lighting
    void updateSky(VkCommandBuffer cmd,
                   VkImageView skybox,
                   const glm::vec3& sunDirection,
                   const glm::vec3& sunColor);
    
    // Get final GI output
    const GIOutput& getOutput() const { return output_; }
    
    // Access subsystems
    ScreenProbes* getScreenProbes() const { return screenProbes_.get(); }
    RadianceCache* getRadianceCache() const { return radianceCache_.get(); }
    
    // Configuration
    void setConfig(const GIConfig& config);
    const GIConfig& getConfig() const { return config_; }
    
private:
    bool createOutputTextures();
    bool createPipelines();
    bool loadShader(const std::string& path, VkShaderModule& outModule);
    
    // Internal passes
    void traceScreenProbes(VkCommandBuffer cmd);
    void updateRadianceCache(VkCommandBuffer cmd);
    void finalGather(VkCommandBuffer cmd);
    void temporalFilter(VkCommandBuffer cmd);
    void compositeGI(VkCommandBuffer cmd);
    
    VulkanContext* context_ = nullptr;
    bool initialized_ = false;
    
    GIConfig config_;
    uint32_t screenWidth_ = 0;
    uint32_t screenHeight_ = 0;
    uint32_t frameIndex_ = 0;
    
    // Subsystems
    std::unique_ptr<ScreenProbes> screenProbes_;
    std::unique_ptr<RadianceCache> radianceCache_;
    
    // External systems (not owned)
    SurfaceCache* surfaceCache_ = nullptr;
    SDFGenerator* sdfGenerator_ = nullptr;
    ScreenSpaceTracing* ssTracing_ = nullptr;
    
    // Output textures
    VkImage diffuseGIImage_ = VK_NULL_HANDLE;
    VkDeviceMemory diffuseGIMemory_ = VK_NULL_HANDLE;
    VkImageView diffuseGIView_ = VK_NULL_HANDLE;
    
    VkImage specularGIImage_ = VK_NULL_HANDLE;
    VkDeviceMemory specularGIMemory_ = VK_NULL_HANDLE;
    VkImageView specularGIView_ = VK_NULL_HANDLE;
    
    VkImage aoImage_ = VK_NULL_HANDLE;
    VkDeviceMemory aoMemory_ = VK_NULL_HANDLE;
    VkImageView aoView_ = VK_NULL_HANDLE;
    
    VkImage bentNormalsImage_ = VK_NULL_HANDLE;
    VkDeviceMemory bentNormalsMemory_ = VK_NULL_HANDLE;
    VkImageView bentNormalsView_ = VK_NULL_HANDLE;
    
    // History for temporal filtering
    VkImage historyDiffuse_[2] = {};
    VkDeviceMemory historyDiffuseMemory_[2] = {};
    VkImageView historyDiffuseView_[2] = {};
    
    VkImage historySpecular_[2] = {};
    VkDeviceMemory historySpecularMemory_[2] = {};
    VkImageView historySpecularView_[2] = {};
    
    // Pipelines
    VkPipeline finalGatherPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout finalGatherLayout_ = VK_NULL_HANDLE;
    
    VkPipeline temporalFilterPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout temporalFilterLayout_ = VK_NULL_HANDLE;
    
    VkPipeline compositePipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout compositeLayout_ = VK_NULL_HANDLE;
    
    VkPipeline skyInjectionPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout skyInjectionLayout_ = VK_NULL_HANDLE;
    
    // Descriptors
    VkDescriptorPool descPool_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descLayout_ = VK_NULL_HANDLE;
    VkDescriptorSet descSet_ = VK_NULL_HANDLE;
    
    VkSampler giSampler_ = VK_NULL_HANDLE;
    
    // Output bundle
    GIOutput output_;
    
    // View matrices for temporal reprojection
    glm::mat4 prevView_;
    glm::mat4 prevProj_;
    glm::mat4 prevViewProj_;
};
