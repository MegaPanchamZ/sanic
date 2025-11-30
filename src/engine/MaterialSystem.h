/**
 * MaterialSystem.h
 * 
 * Unified material management and deferred material evaluation.
 * Implements Nanite-style material binning and batched texture access.
 * 
 * Features:
 * - Material binning from visibility buffer
 * - Per-material tile lists for coherent shading
 * - Bindless texture management
 * - PBR material support
 * - Deferred lighting with IBL
 */

#pragma once
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>
#include <string>
#include <memory>
#include "VulkanContext.h"

// Maximum materials and textures
constexpr uint32_t MAX_MATERIALS = 256;
constexpr uint32_t MAX_TEXTURES = 4096;
constexpr uint32_t MAX_LIGHTS = 1024;

// Invalid texture index
constexpr uint32_t INVALID_TEXTURE = 0xFFFFFFFF;

/**
 * Blend mode enum - determines how material is rendered
 */
enum class EBlendMode : uint32_t {
    Opaque = 0,           // Standard opaque rendering
    Masked = 1,           // Alpha test (cutout)
    Translucent = 2,      // Alpha blended (sorted back-to-front)
    Additive = 3,         // Additive blending
    Modulate = 4          // Multiplicative blending
};

/**
 * Material flags bitfield
 */
namespace MaterialFlags {
    constexpr uint32_t TwoSided           = 1 << 0;   // Render both sides
    constexpr uint32_t UseVertexColor     = 1 << 1;   // Multiply by vertex color
    constexpr uint32_t Subsurface         = 1 << 2;   // Has subsurface scattering
    constexpr uint32_t Foliage            = 1 << 3;   // Use foliage shading model
    constexpr uint32_t Hair               = 1 << 4;   // Use hair shading model
    constexpr uint32_t Cloth              = 1 << 5;   // Use cloth shading model
    constexpr uint32_t ClearCoat          = 1 << 6;   // Has clear coat layer
    constexpr uint32_t Anisotropic        = 1 << 7;   // Anisotropic specular
    constexpr uint32_t Emissive           = 1 << 8;   // Has emissive contribution
    constexpr uint32_t WorldPositionOffset = 1 << 9; // Uses WPO
    constexpr uint32_t PixelDepthOffset   = 1 << 10;  // Uses PDO
    constexpr uint32_t Decal              = 1 << 11;  // Is a decal material
    constexpr uint32_t DitheredLOD        = 1 << 12;  // Use dithered LOD transitions
}

/**
 * GPU Material structure (must match shader)
 * 
 * Supports:
 * - PBR workflow with metallic/roughness
 * - Multiple blend modes (opaque, masked, translucent)
 * - Alpha masking with configurable threshold
 * - Various shading models via flags
 */
struct alignas(16) GPUMaterial {
    // Texture indices (bindless)
    uint32_t albedoTexture;
    uint32_t normalTexture;
    uint32_t roughnessMetallicTexture;
    uint32_t emissiveTexture;
    
    // Base color (RGBA - alpha used for masked materials)
    glm::vec4 baseColor;
    
    // PBR parameters
    float roughness;
    float metallic;
    float emissiveStrength;
    
    // Blend mode (packed with flags for alignment)
    uint32_t blendModeAndFlags;  // Low 4 bits: blend mode, High 28 bits: flags
    
    // Opacity mask parameters
    float opacityMaskClipValue;  // Alpha threshold for masked materials (default 0.333)
    float subsurfaceOpacity;     // For subsurface scattering
    float clearCoatRoughness;    // For clear coat materials
    float anisotropy;            // For anisotropic materials
    
    // Helper functions
    void setBlendMode(EBlendMode mode) {
        blendModeAndFlags = (blendModeAndFlags & 0xFFFFFFF0) | static_cast<uint32_t>(mode);
    }
    
    EBlendMode getBlendMode() const {
        return static_cast<EBlendMode>(blendModeAndFlags & 0xF);
    }
    
    void setFlags(uint32_t flags) {
        blendModeAndFlags = (blendModeAndFlags & 0xF) | (flags << 4);
    }
    
    uint32_t getFlags() const {
        return blendModeAndFlags >> 4;
    }
    
    bool hasFlag(uint32_t flag) const {
        return (getFlags() & flag) != 0;
    }
    
    void addFlag(uint32_t flag) {
        setFlags(getFlags() | flag);
    }
    
    // Check if material requires alpha testing
    bool isMasked() const {
        return getBlendMode() == EBlendMode::Masked;
    }
    
    // Check if material requires transparency sorting
    bool isTranslucent() const {
        EBlendMode mode = getBlendMode();
        return mode == EBlendMode::Translucent || 
               mode == EBlendMode::Additive || 
               mode == EBlendMode::Modulate;
    }
    
    // Default constructor with sensible defaults
    static GPUMaterial createDefault() {
        GPUMaterial mat{};
        mat.albedoTexture = INVALID_TEXTURE;
        mat.normalTexture = INVALID_TEXTURE;
        mat.roughnessMetallicTexture = INVALID_TEXTURE;
        mat.emissiveTexture = INVALID_TEXTURE;
        mat.baseColor = glm::vec4(1.0f);
        mat.roughness = 0.5f;
        mat.metallic = 0.0f;
        mat.emissiveStrength = 0.0f;
        mat.blendModeAndFlags = 0;  // Opaque, no flags
        mat.opacityMaskClipValue = 0.333f;  // Nanite-style default
        mat.subsurfaceOpacity = 0.0f;
        mat.clearCoatRoughness = 0.0f;
        mat.anisotropy = 0.0f;
        return mat;
    }
    
    // Create a masked material for foliage, etc.
    static GPUMaterial createMasked(float clipValue = 0.5f) {
        GPUMaterial mat = createDefault();
        mat.setBlendMode(EBlendMode::Masked);
        mat.opacityMaskClipValue = clipValue;
        return mat;
    }
};
static_assert(sizeof(GPUMaterial) == 64, "GPUMaterial must be 64 bytes");

/**
 * Light types
 */
enum class LightType : uint32_t {
    Directional = 0,
    Point = 1,
    Spot = 2,
    Area = 3
};

/**
 * GPU Light structure (must match shader)
 */
struct alignas(16) GPULight {
    glm::vec3 position;
    uint32_t type;
    glm::vec3 direction;
    float range;
    glm::vec3 color;
    float intensity;
    float innerConeAngle;
    float outerConeAngle;
    float sourceRadius;
    float sourceLength;
};
static_assert(sizeof(GPULight) == 64, "GPULight must be 64 bytes");

/**
 * Material binning counters
 */
struct alignas(16) MaterialCounters {
    uint32_t tileCountPerMaterial[MAX_MATERIALS];
    uint32_t pixelCountPerMaterial[MAX_MATERIALS];
    uint32_t totalTiles;
    uint32_t totalPixels;
    uint32_t pad[2];
};

/**
 * Pixel work item for deferred shading
 */
struct alignas(16) PixelWorkItem {
    uint32_t packedCoord;       // x | (y << 16)
    uint32_t triangleId;
    uint32_t clusterId;
    uint32_t instanceId;
};
static_assert(sizeof(PixelWorkItem) == 16, "PixelWorkItem must be 16 bytes");

/**
 * Material tile descriptor
 */
struct alignas(16) MaterialTile {
    uint32_t tileX;
    uint32_t tileY;
    uint32_t pixelOffset;
    uint32_t pixelCount;
};
static_assert(sizeof(MaterialTile) == 16, "MaterialTile must be 16 bytes");

/**
 * Material configuration
 */
struct MaterialConfig {
    uint32_t maxPixelWorkItems = 4 * 1024 * 1024;  // 4M pixels max
    uint32_t maxMaterialTiles = 256 * 1024;         // 256K tiles
    bool enableBindlessTextures = true;
};

/**
 * Lighting configuration
 */
struct LightingConfig {
    float ambientIntensity = 0.3f;
    float exposure = 1.0f;
    bool enableIBL = true;
};

class MaterialSystem {
public:
    MaterialSystem() = default;
    ~MaterialSystem();
    
    // Disable copy
    MaterialSystem(const MaterialSystem&) = delete;
    MaterialSystem& operator=(const MaterialSystem&) = delete;
    
    /**
     * Initialize the material system
     */
    bool initialize(VulkanContext* context, const MaterialConfig& config = {});
    
    /**
     * Cleanup resources
     */
    void cleanup();
    
    /**
     * Register a material and get its ID
     */
    uint32_t registerMaterial(const GPUMaterial& material);
    
    /**
     * Update an existing material
     */
    void updateMaterial(uint32_t materialId, const GPUMaterial& material);
    
    /**
     * Register a texture and get its bindless index
     */
    uint32_t registerTexture(VkImageView imageView, VkSampler sampler);
    
    /**
     * Add a light to the scene
     */
    uint32_t addLight(const GPULight& light);
    
    /**
     * Update a light
     */
    void updateLight(uint32_t lightId, const GPULight& light);
    
    /**
     * Clear all lights
     */
    void clearLights();
    
    /**
     * Upload material and light data to GPU
     */
    void uploadData(VkCommandBuffer cmd);
    
    /**
     * Bin pixels by material from visibility buffer
     * 
     * @param cmd Command buffer
     * @param visibilityBuffer Visibility buffer
     * @param clusterBuffer Cluster buffer (for material IDs)
     * @param screenWidth Screen width
     * @param screenHeight Screen height
     */
    void binMaterials(VkCommandBuffer cmd,
                     VkBuffer visibilityBuffer,
                     VkBuffer clusterBuffer,
                     uint32_t screenWidth,
                     uint32_t screenHeight);
    
    /**
     * Evaluate materials for all binned pixels
     * 
     * @param cmd Command buffer
     * @param clusterBuffer Cluster buffer
     * @param instanceBuffer Instance transforms
     * @param vertexBuffer Vertex buffer
     * @param indexBuffer Index buffer
     * @param viewProj View-projection matrix
     * @param invViewProj Inverse view-projection
     * @param gbufferPosition Position G-Buffer image
     * @param gbufferNormal Normal G-Buffer image
     * @param gbufferAlbedo Albedo G-Buffer image
     * @param gbufferMaterial Material G-Buffer image
     * @param screenWidth Screen width
     * @param screenHeight Screen height
     */
    void evaluateMaterials(VkCommandBuffer cmd,
                          VkBuffer clusterBuffer,
                          VkBuffer instanceBuffer,
                          VkBuffer vertexBuffer,
                          VkBuffer indexBuffer,
                          const glm::mat4& viewProj,
                          const glm::mat4& invViewProj,
                          VkImageView gbufferPosition,
                          VkImageView gbufferNormal,
                          VkImageView gbufferAlbedo,
                          VkImageView gbufferMaterial,
                          uint32_t screenWidth,
                          uint32_t screenHeight);
    
    /**
     * Perform deferred lighting pass
     * 
     * @param cmd Command buffer
     * @param gbufferPosition Position G-Buffer image
     * @param gbufferNormal Normal G-Buffer image
     * @param gbufferAlbedo Albedo G-Buffer image
     * @param gbufferMaterial Material G-Buffer image
     * @param outputImage Output color image
     * @param cameraPos Camera world position
     * @param screenWidth Screen width
     * @param screenHeight Screen height
     * @param config Lighting configuration
     */
    void performLighting(VkCommandBuffer cmd,
                        VkImageView gbufferPosition,
                        VkImageView gbufferNormal,
                        VkImageView gbufferAlbedo,
                        VkImageView gbufferMaterial,
                        VkImageView outputImage,
                        const glm::vec3& cameraPos,
                        uint32_t screenWidth,
                        uint32_t screenHeight,
                        const LightingConfig& config = {});
    
    /**
     * Set environment maps for IBL
     */
    void setEnvironmentMaps(VkImageView irradianceMap,
                           VkImageView prefilteredMap,
                           VkImageView brdfLUT,
                           VkSampler envSampler);
    
    /**
     * Reset counters at frame start
     */
    void resetCounters(VkCommandBuffer cmd);
    
    /**
     * Get material count
     */
    uint32_t getMaterialCount() const { return static_cast<uint32_t>(materials_.size()); }
    
    /**
     * Get light count
     */
    uint32_t getLightCount() const { return static_cast<uint32_t>(lights_.size()); }
    
    /**
     * Get material buffer
     */
    VkBuffer getMaterialBuffer() const { return materialBuffer_; }
    
private:
    bool createBuffers();
    bool createPipelines();
    bool createDescriptorSets();
    bool loadShader(const std::string& path, VkShaderModule& outModule);
    
    VulkanContext* context_ = nullptr;
    MaterialConfig config_;
    
    // Materials
    std::vector<GPUMaterial> materials_;
    VkBuffer materialBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory materialMemory_ = VK_NULL_HANDLE;
    
    // Lights
    std::vector<GPULight> lights_;
    VkBuffer lightBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory lightMemory_ = VK_NULL_HANDLE;
    
    // Pixel work items
    VkBuffer pixelWorkBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory pixelWorkMemory_ = VK_NULL_HANDLE;
    
    // Material tiles
    VkBuffer materialTileBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory materialTileMemory_ = VK_NULL_HANDLE;
    
    // Counters
    VkBuffer counterBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory counterMemory_ = VK_NULL_HANDLE;
    
    // Buffer device addresses
    VkDeviceAddress pixelWorkAddr_ = 0;
    VkDeviceAddress materialTileAddr_ = 0;
    VkDeviceAddress counterAddr_ = 0;
    VkDeviceAddress materialAddr_ = 0;
    VkDeviceAddress lightAddr_ = 0;
    
    // Pipelines
    VkPipeline materialBinPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout materialBinLayout_ = VK_NULL_HANDLE;
    
    VkPipeline materialEvalPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout materialEvalLayout_ = VK_NULL_HANDLE;
    
    VkPipeline deferredLightingPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout deferredLightingLayout_ = VK_NULL_HANDLE;
    
    // Descriptor sets
    VkDescriptorSetLayout bindlessTextureLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool bindlessDescriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet bindlessDescriptorSet_ = VK_NULL_HANDLE;
    
    VkDescriptorSetLayout gbufferLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool gbufferDescriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet materialEvalDescriptorSet_ = VK_NULL_HANDLE;
    VkDescriptorSet lightingDescriptorSet_ = VK_NULL_HANDLE;
    
    // Bindless textures
    std::vector<VkImageView> registeredTextures_;
    VkSampler defaultSampler_ = VK_NULL_HANDLE;
    
    // Environment maps
    VkImageView irradianceMap_ = VK_NULL_HANDLE;
    VkImageView prefilteredMap_ = VK_NULL_HANDLE;
    VkImageView brdfLUT_ = VK_NULL_HANDLE;
    VkSampler envSampler_ = VK_NULL_HANDLE;
    
    bool initialized_ = false;
    bool dataDirty_ = true;
};
