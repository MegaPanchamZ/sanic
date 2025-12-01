/**
 * SubstrateMaterial.h
 * 
 * Layered material system based on Unreal Engine 5's Substrate.
 * Allows stacking of material "slabs" for complex multi-layer effects:
 * - Base metal + clear coat + dust + scratches
 * - Each layer has physical properties
 * - Energy-conserving multi-layer BSDF evaluation
 * 
 * Reference: Engine/Source/Runtime/Renderer/Private/Substrate/
 */

#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <string>
#include <array>

class VulkanContext;

namespace Sanic {

// ============================================================================
// CONSTANTS
// ============================================================================

constexpr uint32_t MAX_SUBSTRATE_SLABS = 8;
constexpr uint32_t SUBSTRATE_MATERIAL_STRIDE = 256;  // Bytes per material in buffer

// ============================================================================
// SLAB TYPES
// ============================================================================

/**
 * Type of material layer
 */
enum class SubstrateSlabType : uint32_t {
    Standard = 0,       // Standard PBR (metallic/roughness)
    ClearCoat = 1,      // Clear coat layer (automotive paint, lacquer)
    Transmission = 2,   // Transmissive material (glass, water)
    Subsurface = 3,     // Subsurface scattering (skin, wax)
    Cloth = 4,          // Cloth/fabric sheen
    Hair = 5,           // Anisotropic hair/fur
    Eye = 6,            // Eye shader (iris, cornea)
    ThinFilm = 7        // Thin-film interference (soap bubbles, oil)
};

/**
 * Blend mode for combining slabs
 */
enum class SubstrateBlendMode : uint32_t {
    Opaque = 0,         // Full replace
    Alpha = 1,          // Alpha blend
    Additive = 2,       // Add on top
    Multiply = 3,       // Multiply
    HeightBlend = 4     // Height-based blend
};

// ============================================================================
// SLAB STRUCTURE
// ============================================================================

/**
 * SubstrateSlab - A single layer of material
 * 
 * Each slab represents a distinct material layer with its own
 * optical and physical properties.
 */
struct SubstrateSlab {
    // ========================================================================
    // COMMON PROPERTIES
    // ========================================================================
    
    SubstrateSlabType type = SubstrateSlabType::Standard;
    SubstrateBlendMode blendMode = SubstrateBlendMode::Opaque;
    float blendWeight = 1.0f;       // How much this slab contributes
    float thickness = 0.0f;         // Physical thickness (for absorption)
    
    // ========================================================================
    // APPEARANCE
    // ========================================================================
    
    // Base color
    glm::vec3 baseColor = glm::vec3(0.5f);
    float opacity = 1.0f;
    
    // Surface properties
    float metallic = 0.0f;
    float roughness = 0.5f;
    float specular = 0.5f;          // F0 reflectance (non-metallic)
    float anisotropy = 0.0f;        // -1 to 1, 0 = isotropic
    
    // Normal modification
    float normalStrength = 1.0f;
    glm::vec3 tangentDirection = glm::vec3(1, 0, 0);
    
    // ========================================================================
    // CLEAR COAT (type == ClearCoat)
    // ========================================================================
    
    float clearCoatRoughness = 0.02f;
    float clearCoatIOR = 1.5f;      // Index of refraction
    glm::vec3 clearCoatNormal = glm::vec3(0, 0, 1);
    
    // ========================================================================
    // TRANSMISSION (type == Transmission)
    // ========================================================================
    
    glm::vec3 absorption = glm::vec3(0.0f);  // Per-unit absorption
    float transmissionIOR = 1.5f;
    float transmissionDispersion = 0.0f;     // Chromatic dispersion
    
    // ========================================================================
    // SUBSURFACE (type == Subsurface)
    // ========================================================================
    
    glm::vec3 subsurfaceColor = glm::vec3(1, 0.2f, 0.1f);
    float subsurfaceRadius = 1.0f;           // Scatter radius in world units
    float subsurfacePhase = 0.0f;            // -1 back, 0 iso, 1 forward
    
    // ========================================================================
    // CLOTH (type == Cloth)
    // ========================================================================
    
    glm::vec3 sheenColor = glm::vec3(1.0f);
    float sheenRoughness = 0.5f;
    
    // ========================================================================
    // THIN FILM (type == ThinFilm)
    // ========================================================================
    
    float thinFilmThickness = 500.0f;        // Nanometers
    float thinFilmIOR = 1.5f;
    
    // ========================================================================
    // HAIR (type == Hair)
    // ========================================================================
    
    float hairScatter = 1.0f;
    float hairShift = 0.0f;                  // Cuticle tilt
    glm::vec3 hairColor = glm::vec3(0.1f);
};

/**
 * GPU-compatible slab data (128 bytes per slab)
 */
struct GPUSubstrateSlab {
    // vec4 0: type, blend, weight, thickness
    glm::vec4 typeBlendWeightThickness;
    
    // vec4 1: baseColor, opacity
    glm::vec4 baseColorOpacity;
    
    // vec4 2: metallic, roughness, specular, anisotropy
    glm::vec4 metallicRoughnessSpecularAniso;
    
    // vec4 3: normalStrength, clearCoatRoughness, clearCoatIOR, transmissionIOR
    glm::vec4 normalClearCoatIOR;
    
    // vec4 4: absorption, subsurfaceRadius
    glm::vec4 absorptionSubsurface;
    
    // vec4 5: subsurfaceColor, phase
    glm::vec4 subsurfaceColorPhase;
    
    // vec4 6: sheenColor, sheenRoughness
    glm::vec4 sheenColorRoughness;
    
    // vec4 7: thinFilm params, hair params
    glm::vec4 thinFilmHair;
};

// ============================================================================
// SUBSTRATE MATERIAL
// ============================================================================

/**
 * SubstrateMaterial - A complete multi-layer material
 */
struct SubstrateMaterial {
    std::string name;
    uint32_t id = 0;
    
    // Slab stack (bottom to top)
    std::array<SubstrateSlab, MAX_SUBSTRATE_SLABS> slabs;
    uint32_t slabCount = 1;
    
    // Global properties
    bool twoSided = false;
    float displacementScale = 0.0f;
    bool useTriplanarMapping = false;
    
    // Texture bindings (indices into material texture array)
    int32_t baseColorTexture = -1;
    int32_t normalTexture = -1;
    int32_t metallicRoughnessTexture = -1;
    int32_t emissiveTexture = -1;
    int32_t clearCoatTexture = -1;
    int32_t subsurfaceTexture = -1;
    
    // Helper methods
    SubstrateSlab& addSlab(SubstrateSlabType type = SubstrateSlabType::Standard);
    void removeSlab(uint32_t index);
    void reorderSlab(uint32_t from, uint32_t to);
};

/**
 * GPU material data (256 bytes per material)
 */
struct GPUSubstrateMaterial {
    GPUSubstrateSlab slabs[MAX_SUBSTRATE_SLABS];
    
    // Material flags and counts
    glm::uvec4 flagsAndCounts;  // x = slabCount, y = flags, z = textureMask, w = reserved
    
    // Texture indices
    glm::ivec4 textureIndices0; // baseColor, normal, metallicRoughness, emissive
    glm::ivec4 textureIndices1; // clearCoat, subsurface, reserved, reserved
};

// ============================================================================
// SUBSTRATE SYSTEM
// ============================================================================

/**
 * SubstrateSystem - Manages substrate materials and shader resources
 */
class SubstrateSystem {
public:
    SubstrateSystem(VulkanContext& context);
    ~SubstrateSystem();
    
    bool initialize();
    void shutdown();
    
    // Material management
    uint32_t createMaterial(const std::string& name);
    void updateMaterial(uint32_t id, const SubstrateMaterial& material);
    void deleteMaterial(uint32_t id);
    SubstrateMaterial* getMaterial(uint32_t id);
    const SubstrateMaterial* getMaterial(uint32_t id) const;
    
    // Create common material presets
    uint32_t createDefaultLitMaterial();
    uint32_t createClearCoatMaterial();
    uint32_t createSubsurfaceMaterial();
    uint32_t createClothMaterial();
    uint32_t createGlassMaterial();
    
    // GPU resource access
    VkBuffer getMaterialBuffer() const { return materialBuffer_; }
    VkDescriptorSet getDescriptorSet() const { return descriptorSet_; }
    
    // Shader integration
    VkShaderModule getEvaluationShader() const { return evaluationShader_; }
    
    // Update GPU buffers after material changes
    void updateGPUBuffers();
    
    // Get material index for rendering
    uint32_t getMaterialGPUIndex(uint32_t id) const;
    
private:
    void createResources();
    void createPipelines();
    void uploadMaterial(uint32_t gpuIndex, const SubstrateMaterial& material);
    GPUSubstrateSlab convertSlabToGPU(const SubstrateSlab& slab) const;
    
    VulkanContext& context_;
    
    // Materials
    std::vector<std::unique_ptr<SubstrateMaterial>> materials_;
    std::unordered_map<uint32_t, uint32_t> idToGPUIndex_;
    uint32_t nextMaterialId_ = 1;
    
    // GPU resources
    VkBuffer materialBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory materialMemory_ = VK_NULL_HANDLE;
    void* materialMapped_ = nullptr;
    uint32_t maxMaterials_ = 1024;
    
    // Descriptor
    VkDescriptorSetLayout descriptorLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;
    
    // Shader
    VkShaderModule evaluationShader_ = VK_NULL_HANDLE;
};

// ============================================================================
// BSDF EVALUATION UTILITIES
// ============================================================================

namespace SubstrateBSDF {
    /**
     * Fresnel-Schlick approximation
     */
    inline float fresnelSchlick(float cosTheta, float f0) {
        return f0 + (1.0f - f0) * std::pow(1.0f - cosTheta, 5.0f);
    }
    
    inline glm::vec3 fresnelSchlick(float cosTheta, const glm::vec3& f0) {
        float factor = std::pow(1.0f - cosTheta, 5.0f);
        return f0 + (glm::vec3(1.0f) - f0) * factor;
    }
    
    /**
     * Fresnel for dielectric (IOR-based)
     */
    inline float fresnelDielectric(float cosI, float ior) {
        float sinT2 = (1.0f - cosI * cosI) / (ior * ior);
        if (sinT2 > 1.0f) return 1.0f;  // Total internal reflection
        
        float cosT = std::sqrt(1.0f - sinT2);
        float rs = (cosI - ior * cosT) / (cosI + ior * cosT);
        float rp = (ior * cosI - cosT) / (ior * cosI + cosT);
        return 0.5f * (rs * rs + rp * rp);
    }
    
    /**
     * GGX microfacet distribution
     */
    inline float distributionGGX(float NdotH, float roughness) {
        float a = roughness * roughness;
        float a2 = a * a;
        float NdotH2 = NdotH * NdotH;
        
        float nom = a2;
        float denom = (NdotH2 * (a2 - 1.0f) + 1.0f);
        denom = 3.14159265f * denom * denom;
        
        return nom / std::max(denom, 0.0001f);
    }
    
    /**
     * Smith geometry term
     */
    inline float geometrySmith(float NdotV, float NdotL, float roughness) {
        float r = roughness + 1.0f;
        float k = (r * r) / 8.0f;
        
        float ggx1 = NdotV / (NdotV * (1.0f - k) + k);
        float ggx2 = NdotL / (NdotL * (1.0f - k) + k);
        
        return ggx1 * ggx2;
    }
    
    /**
     * Evaluate a single slab's BSDF
     */
    glm::vec3 evaluateSlab(const SubstrateSlab& slab,
                           const glm::vec3& V,
                           const glm::vec3& L,
                           const glm::vec3& N,
                           float& outThroughput);
    
    /**
     * Evaluate complete multi-layer substrate material
     */
    glm::vec3 evaluateMaterial(const SubstrateMaterial& material,
                               const glm::vec3& V,
                               const glm::vec3& L,
                               const glm::vec3& N);
}

// ============================================================================
// MATERIAL NODE INTEGRATION
// ============================================================================

// Forward declare for material graph integration
class SubstrateSlabNode;
class SubstrateBlendNode;
class SubstrateMaterialOutputNode;

} // namespace Sanic
