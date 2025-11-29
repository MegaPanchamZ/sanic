/**
 * FinalRenderer.h
 * 
 * Final integration of all rendering systems into a cohesive pipeline.
 * Coordinates Nanite-style GPU-driven rendering with Lumen-style GI.
 * 
 * Turn 40-42: Final Integration
 */

#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <string>

// Forward declarations
class VulkanContext;
class ClusterHierarchy;
class ClusterCullingPipeline;
class HZBPipeline;
class VisBufferRenderer;
class SoftwareRasterizerPipeline;
class MaterialSystem;
class TemporalSystem;
class SurfaceCache;
class ScreenSpaceTracing;
class SDFGenerator;
class ScreenProbes;
class RadianceCache;
class GlobalIllumination;
class VirtualShadowMaps;
class RayTracedShadows;
class PostProcess;

// Rendering statistics
struct RenderStats {
    // Geometry
    uint32_t totalClusters;
    uint32_t visibleClusters;
    uint32_t culledClusters;
    uint32_t softwareRasterizedClusters;
    uint32_t hardwareRasterizedClusters;
    
    // Triangles
    uint64_t totalTriangles;
    uint64_t visibleTriangles;
    uint64_t rasterizedTriangles;
    
    // Shadows
    uint32_t shadowPagesRendered;
    uint32_t shadowRaysTraced;
    
    // GI
    uint32_t screenProbesPlaced;
    uint32_t radianceCacheUpdates;
    uint32_t sdfVoxelsUpdated;
    
    // Performance
    float gpuTimeMs;
    float cullingTimeMs;
    float rasterTimeMs;
    float shadowTimeMs;
    float giTimeMs;
    float postProcessTimeMs;
};

// Camera data
struct CameraData {
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 viewProj;
    glm::mat4 invView;
    glm::mat4 invProj;
    glm::mat4 invViewProj;
    glm::mat4 prevViewProj;
    
    glm::vec3 position;
    float nearPlane;
    glm::vec3 forward;
    float farPlane;
    glm::vec3 right;
    float fov;
    glm::vec3 up;
    float aspectRatio;
    
    glm::vec4 frustumPlanes[6];
};

// Light data
struct LightData {
    glm::vec4 position;     // xyz = pos, w = type (0=dir, 1=point, 2=spot)
    glm::vec4 direction;    // xyz = dir, w = range
    glm::vec4 color;        // xyz = color, w = intensity
    glm::vec4 params;       // x = innerAngle, y = outerAngle, z = shadowIndex, w = enabled
};

// Scene data for rendering
struct SceneData {
    std::vector<LightData> lights;
    glm::vec3 sunDirection;
    glm::vec3 sunColor;
    float sunIntensity;
    
    glm::vec3 ambientColor;
    float ambientIntensity;
    
    float skyIntensity;
    float giIntensity;
    float aoIntensity;
    float reflectionIntensity;
};

// Frame data
struct FrameContext {
    uint32_t frameIndex;
    float deltaTime;
    float totalTime;
    
    CameraData camera;
    SceneData scene;
    
    VkCommandBuffer commandBuffer;
    VkSemaphore imageAvailable;
    VkSemaphore renderFinished;
    VkFence inFlight;
    
    uint32_t swapchainIndex;
};

// Render pass configuration
struct RenderConfig {
    // Resolution
    uint32_t width;
    uint32_t height;
    uint32_t internalWidth;     // For DLSS/FSR
    uint32_t internalHeight;
    
    // Quality settings
    bool enableNanite;
    bool enableSoftwareRasterizer;
    bool enableHZBCulling;
    bool enableTemporalAA;
    
    // Shadows
    bool enableVSM;
    bool enableRayTracedShadows;
    uint32_t shadowQuality;     // 0=low, 1=med, 2=high, 3=ultra
    
    // Global Illumination
    bool enableGI;
    bool enableScreenProbes;
    bool enableRadianceCache;
    bool enableSDF;
    uint32_t giQuality;
    
    // Post-processing
    bool enableBloom;
    bool enableDOF;
    bool enableMotionBlur;
    bool enableAutoExposure;
    
    // Debug
    bool enableWireframe;
    bool showClusters;
    bool showHZB;
    bool showGIDebug;
};

// GPU-side frame uniforms
struct alignas(16) FrameUniforms {
    glm::mat4 viewMatrix;
    glm::mat4 projMatrix;
    glm::mat4 viewProjMatrix;
    glm::mat4 invViewMatrix;
    glm::mat4 invProjMatrix;
    glm::mat4 invViewProjMatrix;
    glm::mat4 prevViewProjMatrix;
    
    glm::vec4 cameraPosition;       // xyz = pos, w = time
    glm::vec4 cameraParams;         // x = near, y = far, z = fov, w = aspect
    glm::vec4 screenSize;           // xy = size, zw = 1/size
    glm::vec4 jitterOffset;         // xy = current jitter, zw = prev jitter
    
    glm::vec4 sunDirection;         // xyz = dir, w = intensity
    glm::vec4 sunColor;
    glm::vec4 ambientColor;         // xyz = color, w = intensity
    
    uint32_t frameIndex;
    float deltaTime;
    float totalTime;
    uint32_t flags;                 // Bitfield for features
    
    uint32_t lightCount;
    uint32_t clusterCount;
    uint32_t materialCount;
    uint32_t pad;
};

class FinalRenderer {
public:
    FinalRenderer();
    ~FinalRenderer();
    
    bool initialize(VulkanContext* context, const RenderConfig& config);
    void cleanup();
    
    void resize(uint32_t width, uint32_t height);
    void setConfig(const RenderConfig& config);
    
    // Main render function
    void render(const FrameContext& frame);
    
    // Get results
    VkImageView getFinalOutput() const { return finalOutputView_; }
    const RenderStats& getStats() const { return stats_; }
    
    // Subsystem setters - allows external construction and injection
    void setClusterHierarchy(ClusterHierarchy* hierarchy) { clusterHierarchy_ = hierarchy; }
    void setClusterCulling(ClusterCullingPipeline* culling) { clusterCulling_ = culling; }
    void setHZBPipeline(HZBPipeline* hzb) { hzbPipeline_ = hzb; }
    void setVisBufferRenderer(VisBufferRenderer* renderer) { visBufferRenderer_ = renderer; }
    void setSoftwareRasterizer(SoftwareRasterizerPipeline* rasterizer) { softwareRasterizer_ = rasterizer; }
    void setMaterialSystem(MaterialSystem* materials) { materialSystem_ = materials; }
    void setTemporalSystem(TemporalSystem* temporal) { temporalSystem_ = temporal; }
    void setSurfaceCache(SurfaceCache* cache) { surfaceCache_ = cache; }
    void setScreenSpaceTracing(ScreenSpaceTracing* sst) { screenSpaceTracing_ = sst; }
    void setSDFGenerator(SDFGenerator* sdf) { sdfGenerator_ = sdf; }
    void setScreenProbes(ScreenProbes* probes) { screenProbes_ = probes; }
    void setRadianceCache(RadianceCache* cache) { radianceCache_ = cache; }
    void setGlobalIllumination(GlobalIllumination* gi) { globalIllumination_ = gi; }
    void setVirtualShadowMaps(VirtualShadowMaps* vsm) { virtualShadowMaps_ = vsm; }
    void setRayTracedShadows(RayTracedShadows* rts) { rayTracedShadows_ = rts; }
    void setPostProcess(PostProcess* pp) { postProcess_ = pp; }
    
    // System access for configuration (non-owning pointers)
    ClusterHierarchy* getClusterHierarchy() { return clusterHierarchy_; }
    MaterialSystem* getMaterialSystem() { return materialSystem_; }
    GlobalIllumination* getGI() { return globalIllumination_; }
    PostProcess* getPostProcess() { return postProcess_; }
    
private:
    // Render passes
    void updateFrameUniforms(const FrameContext& frame);
    void uploadLights(const FrameContext& frame);
    
    void executeGeometryPass(VkCommandBuffer cmd, const FrameContext& frame);
    void executeShadowPass(VkCommandBuffer cmd, const FrameContext& frame);
    void executeGIPass(VkCommandBuffer cmd, const FrameContext& frame);
    void executeLightingPass(VkCommandBuffer cmd, const FrameContext& frame);
    void executePostProcessPass(VkCommandBuffer cmd, const FrameContext& frame);
    
    // Sub-passes
    void buildHZB(VkCommandBuffer cmd);
    void cullClusters(VkCommandBuffer cmd, const CameraData& camera);
    void renderVisBuffer(VkCommandBuffer cmd);
    void resolveMaterials(VkCommandBuffer cmd);
    
    // Resource creation
    bool createGBuffers();
    bool createUniformBuffers();
    bool createLightBuffer();
    bool createPipelines();
    bool createDescriptorSets();
    
    VulkanContext* context_ = nullptr;
    RenderConfig config_;
    RenderStats stats_;
    bool initialized_ = false;
    
    // Subsystems (non-owning pointers - owned externally)
    ClusterHierarchy* clusterHierarchy_ = nullptr;
    ClusterCullingPipeline* clusterCulling_ = nullptr;
    HZBPipeline* hzbPipeline_ = nullptr;
    VisBufferRenderer* visBufferRenderer_ = nullptr;
    SoftwareRasterizerPipeline* softwareRasterizer_ = nullptr;
    MaterialSystem* materialSystem_ = nullptr;
    TemporalSystem* temporalSystem_ = nullptr;
    SurfaceCache* surfaceCache_ = nullptr;
    ScreenSpaceTracing* screenSpaceTracing_ = nullptr;
    SDFGenerator* sdfGenerator_ = nullptr;
    ScreenProbes* screenProbes_ = nullptr;
    RadianceCache* radianceCache_ = nullptr;
    GlobalIllumination* globalIllumination_ = nullptr;
    VirtualShadowMaps* virtualShadowMaps_ = nullptr;
    RayTracedShadows* rayTracedShadows_ = nullptr;
    PostProcess* postProcess_ = nullptr;
    
    // G-Buffer
    VkImage depthImage_ = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory_ = VK_NULL_HANDLE;
    VkImageView depthView_ = VK_NULL_HANDLE;
    
    VkImage visBufferImage_ = VK_NULL_HANDLE;
    VkDeviceMemory visBufferMemory_ = VK_NULL_HANDLE;
    VkImageView visBufferView_ = VK_NULL_HANDLE;
    
    VkImage normalImage_ = VK_NULL_HANDLE;
    VkDeviceMemory normalMemory_ = VK_NULL_HANDLE;
    VkImageView normalView_ = VK_NULL_HANDLE;
    
    VkImage albedoImage_ = VK_NULL_HANDLE;
    VkDeviceMemory albedoMemory_ = VK_NULL_HANDLE;
    VkImageView albedoView_ = VK_NULL_HANDLE;
    
    VkImage materialImage_ = VK_NULL_HANDLE;
    VkDeviceMemory materialMemory_ = VK_NULL_HANDLE;
    VkImageView materialView_ = VK_NULL_HANDLE;
    
    VkImage velocityImage_ = VK_NULL_HANDLE;
    VkDeviceMemory velocityMemory_ = VK_NULL_HANDLE;
    VkImageView velocityView_ = VK_NULL_HANDLE;
    
    // HDR lighting buffer
    VkImage hdrImage_ = VK_NULL_HANDLE;
    VkDeviceMemory hdrMemory_ = VK_NULL_HANDLE;
    VkImageView hdrView_ = VK_NULL_HANDLE;
    
    // Final output
    VkImage finalOutputImage_ = VK_NULL_HANDLE;
    VkDeviceMemory finalOutputMemory_ = VK_NULL_HANDLE;
    VkImageView finalOutputView_ = VK_NULL_HANDLE;
    
    // Uniform buffers
    VkBuffer frameUniformBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory frameUniformMemory_ = VK_NULL_HANDLE;
    void* frameUniformMapped_ = nullptr;
    
    // Light buffer
    VkBuffer lightBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory lightMemory_ = VK_NULL_HANDLE;
    void* lightBufferMapped_ = nullptr;
    
    // Lighting pipeline
    VkPipeline lightingPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout lightingLayout_ = VK_NULL_HANDLE;
    
    // Descriptors
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout frameDescLayout_ = VK_NULL_HANDLE;
    VkDescriptorSet frameDescSet_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout gbufferDescLayout_ = VK_NULL_HANDLE;
    VkDescriptorSet gbufferDescSet_ = VK_NULL_HANDLE;
    
    VkSampler linearSampler_ = VK_NULL_HANDLE;
    VkSampler nearestSampler_ = VK_NULL_HANDLE;
    
    // Query pools for timing
    VkQueryPool timestampPool_ = VK_NULL_HANDLE;
    float timestampPeriod_ = 0.0f;
};
