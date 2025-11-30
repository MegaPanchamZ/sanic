/**
 * LandscapeSystem.h
 * 
 * Unreal Engine style landscape/terrain system with GPU-driven rendering.
 * 
 * Key features:
 * - Heightmap-based terrain with 16-bit precision
 * - 8-level continuous LOD with morphing
 * - Weightmap-based material layer painting
 * - Virtual texture streaming for terrain
 * - GPU-driven LOD selection via compute
 * - Clipmap-based terrain rendering
 * 
 * Architecture:
 * - Components subdivided into quads for LOD
 * - Each quad has 4 LOD levels for smooth transitions
 * - Material layers blended via weightmaps
 * - Collision generated from heightmap
 */

#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <unordered_map>
#include <memory>
#include <string>
#include <array>

namespace Sanic {

// Forward declarations
class VulkanContext;
class AsyncPhysics;

/**
 * Landscape layer for material painting
 */
struct LandscapeLayer {
    uint32_t id;
    std::string name;
    
    // Material properties
    uint32_t diffuseTextureId;
    uint32_t normalTextureId;
    uint32_t roughnessTextureId;
    
    // UV scaling
    float uvScale = 1.0f;
    float uvRotation = 0.0f;
    
    // Blending
    float heightBlendFactor = 0.5f;     // Height-based blending weight
    float noiseScale = 100.0f;          // Noise for blend variation
};

/**
 * Weightmap for a landscape component
 * Each channel represents blend weight for a layer
 */
struct LandscapeWeightmap {
    uint32_t width;
    uint32_t height;
    uint32_t channelCount;              // Up to 4 layers per weightmap
    std::vector<uint8_t> data;          // Interleaved RGBA weights
    
    // GPU resources
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    
    bool isDirty = true;
};

/**
 * LOD level configuration
 */
struct LandscapeLODLevel {
    uint32_t resolution;                // Vertices per edge at this LOD
    float lodDistance;                  // Distance at which this LOD activates
    float morphRange;                   // Distance over which to morph to next LOD
};

/**
 * Landscape component (subdivision of full landscape)
 */
struct LandscapeComponent {
    uint32_t id;
    glm::ivec2 sectionCoord;            // Position in landscape grid
    
    // Heightmap data
    uint32_t heightmapResolution;
    std::vector<uint16_t> heightmap;    // 16-bit heightmap values
    float minHeight;
    float maxHeight;
    
    // Bounds
    glm::vec3 boundsMin;
    glm::vec3 boundsMax;
    glm::vec3 center;
    
    // GPU resources
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;
    
    // Heightmap texture
    VkImage heightmapImage = VK_NULL_HANDLE;
    VkImageView heightmapView = VK_NULL_HANDLE;
    VkDeviceMemory heightmapMemory = VK_NULL_HANDLE;
    
    // Weightmaps (4 layers per weightmap)
    std::vector<LandscapeWeightmap> weightmaps;
    
    // LOD
    std::array<uint32_t, 4> neighborLODs;   // N, E, S, W neighbor LOD levels
    uint32_t currentLOD = 0;
    float morphFactor = 0.0f;
    
    // Physics collision
    void* physicsShape = nullptr;
    uint32_t physicsBodyId = 0;
    
    bool isLoaded = false;
    bool isVisible = false;
};

/**
 * Landscape draw data for GPU
 */
struct LandscapeDrawData {
    glm::mat4 localToWorld;
    glm::vec4 lodParams;                // x: lod, y: morphFactor, z: sectionScale, w: heightScale
    glm::ivec4 neighborLODs;            // LOD levels of neighbors for seam stitching
    uint32_t heightmapIndex;            // Bindless texture index
    uint32_t weightmapIndex;            // Bindless texture index
    uint32_t pad[2];
};

/**
 * Landscape configuration
 */
struct LandscapeConfig {
    // Size
    uint32_t componentsX = 16;          // Number of components in X
    uint32_t componentsY = 16;          // Number of components in Y
    float componentSize = 256.0f;       // World units per component
    float heightScale = 512.0f;         // Maximum height
    
    // Heightmap
    uint32_t heightmapResolution = 129; // Vertices per component edge (power of 2 + 1)
    uint32_t weightmapResolution = 512; // Weightmap resolution per component
    
    // LOD
    uint32_t lodLevels = 8;
    float lodBias = 1.0f;               // LOD distance multiplier
    float lodMorphRange = 0.2f;         // Morph range as fraction of LOD distance
    
    // Material
    uint32_t maxLayersPerComponent = 8;
    
    // Physics
    bool enableCollision = true;
    float collisionLOD = 2;             // LOD level for physics mesh
};

/**
 * Brush for painting on landscape
 */
struct LandscapeBrush {
    enum class Mode {
        Raise,
        Lower,
        Smooth,
        Flatten,
        Noise,
        Layer                           // Paint layer weights
    };
    
    Mode mode = Mode::Raise;
    float radius = 10.0f;
    float falloff = 0.5f;               // 0 = sharp, 1 = smooth
    float strength = 0.5f;
    
    // Layer painting
    uint32_t targetLayerId = 0;
};

/**
 * Landscape rendering and management system
 */
class LandscapeSystem {
public:
    LandscapeSystem();
    ~LandscapeSystem();
    
    /**
     * Initialize the landscape system
     */
    bool initialize(VulkanContext* context, AsyncPhysics* physics = nullptr);
    
    /**
     * Shutdown and cleanup
     */
    void shutdown();
    
    /**
     * Create a new landscape
     * @param config Landscape configuration
     * @return Landscape ID or 0 on failure
     */
    uint32_t createLandscape(const LandscapeConfig& config);
    
    /**
     * Destroy a landscape
     */
    void destroyLandscape(uint32_t landscapeId);
    
    /**
     * Set landscape transform
     */
    void setTransform(uint32_t landscapeId, const glm::vec3& position,
                      const glm::quat& rotation = glm::quat(1, 0, 0, 0));
    
    /**
     * Import heightmap from file or data
     */
    bool importHeightmap(uint32_t landscapeId, const std::string& path);
    bool importHeightmap(uint32_t landscapeId, const uint16_t* data, 
                         uint32_t width, uint32_t height);
    
    /**
     * Export heightmap
     */
    bool exportHeightmap(uint32_t landscapeId, const std::string& path);
    
    /**
     * Add a material layer
     */
    uint32_t addLayer(uint32_t landscapeId, const LandscapeLayer& layer);
    
    /**
     * Remove a layer
     */
    void removeLayer(uint32_t landscapeId, uint32_t layerId);
    
    /**
     * Apply brush stroke
     */
    void applyBrush(uint32_t landscapeId, const glm::vec3& worldPos,
                    const LandscapeBrush& brush);
    
    /**
     * Update LOD based on camera
     */
    void updateLOD(uint32_t landscapeId, const glm::vec3& cameraPos,
                   const glm::mat4& viewProj);
    
    /**
     * Cull and prepare draw data
     */
    void cullAndPrepare(uint32_t landscapeId, const glm::mat4& viewProj,
                        VkCommandBuffer cmd);
    
    /**
     * Get draw commands for rendering
     */
    uint32_t getDrawCount(uint32_t landscapeId) const;
    VkBuffer getDrawBuffer(uint32_t landscapeId) const;
    VkBuffer getDrawDataBuffer(uint32_t landscapeId) const;
    
    /**
     * Get landscape height at world position
     */
    float getHeightAt(uint32_t landscapeId, float worldX, float worldZ);
    
    /**
     * Get normal at world position
     */
    glm::vec3 getNormalAt(uint32_t landscapeId, float worldX, float worldZ);
    
    /**
     * Ray intersection test
     */
    bool raycast(uint32_t landscapeId, const glm::vec3& origin,
                 const glm::vec3& direction, float maxDistance,
                 glm::vec3& outHitPoint, glm::vec3& outNormal);
    
    // Statistics
    struct Statistics {
        uint32_t totalComponents;
        uint32_t visibleComponents;
        uint32_t trianglesRendered;
        uint32_t lodDistributions[8];
    };
    Statistics getStatistics(uint32_t landscapeId) const;
    
private:
    // Internal structures
    struct Landscape {
        uint32_t id;
        LandscapeConfig config;
        glm::mat4 transform;
        glm::mat4 invTransform;
        
        std::vector<LandscapeLayer> layers;
        std::vector<LandscapeComponent> components;
        std::vector<LandscapeLODLevel> lodLevels;
        
        // GPU buffers
        VkBuffer indirectBuffer = VK_NULL_HANDLE;       // Indirect draw commands
        VkBuffer drawDataBuffer = VK_NULL_HANDLE;       // Per-draw data
        VkDeviceMemory indirectMemory = VK_NULL_HANDLE;
        VkDeviceMemory drawDataMemory = VK_NULL_HANDLE;
        
        // Index buffers for each LOD (shared)
        std::array<VkBuffer, 8> lodIndexBuffers;
        std::array<uint32_t, 8> lodIndexCounts;
        VkDeviceMemory lodIndexMemory = VK_NULL_HANDLE;
        
        // Compute pipeline for LOD selection
        VkPipeline lodComputePipeline = VK_NULL_HANDLE;
        VkPipelineLayout lodPipelineLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout lodDescSetLayout = VK_NULL_HANDLE;
        VkDescriptorSet lodDescSet = VK_NULL_HANDLE;
        
        uint32_t visibleCount = 0;
    };
    
    // Helper methods
    bool createComponent(Landscape& landscape, uint32_t x, uint32_t y);
    void destroyComponent(LandscapeComponent& component);
    
    bool createHeightmapTexture(LandscapeComponent& component);
    bool updateHeightmapTexture(LandscapeComponent& component);
    
    bool createWeightmapTexture(LandscapeWeightmap& weightmap);
    bool updateWeightmapTexture(LandscapeWeightmap& weightmap);
    
    void generateLODIndices(Landscape& landscape);
    void createLODPipeline(Landscape& landscape);
    
    void updateComponentBounds(LandscapeComponent& component, const LandscapeConfig& config);
    void generatePhysicsCollision(LandscapeComponent& component, const LandscapeConfig& config);
    
    float sampleHeightmap(const LandscapeComponent& component, float localX, float localZ);
    glm::vec3 sampleNormal(const LandscapeComponent& component, float localX, float localZ);
    
    // Brush operations
    void brushRaise(LandscapeComponent& component, const glm::vec2& localPos,
                    const LandscapeBrush& brush, float dt);
    void brushSmooth(LandscapeComponent& component, const glm::vec2& localPos,
                     const LandscapeBrush& brush);
    void brushFlatten(LandscapeComponent& component, const glm::vec2& localPos,
                      const LandscapeBrush& brush, float targetHeight);
    void brushPaintLayer(LandscapeComponent& component, const glm::vec2& localPos,
                         const LandscapeBrush& brush, uint32_t layerId);
    
    VulkanContext* context_ = nullptr;
    AsyncPhysics* physics_ = nullptr;
    
    std::unordered_map<uint32_t, Landscape> landscapes_;
    uint32_t nextLandscapeId_ = 1;
    
    // Shared sampler
    VkSampler heightmapSampler_ = VK_NULL_HANDLE;
    VkSampler weightmapSampler_ = VK_NULL_HANDLE;
    
    bool initialized_ = false;
};

} // namespace Sanic

