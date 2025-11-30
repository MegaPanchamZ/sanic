/**
 * FoliageSystem.h
 * 
 * GPU-driven foliage instancing system with hierarchical culling.
 * 
 * Key features:
 * - Massively instanced foliage (millions of instances)
 * - GPU-driven culling via compute
 * - Hierarchical cluster culling
 * - Distance-based LOD with crossfade
 * - Procedural placement from density maps
 * - Wind animation with wave propagation
 * 
 * Architecture:
 * - Instances organized into clusters (64-256 instances)
 * - Clusters organized into sectors for broad-phase culling
 * - GPU writes visible instances to indirect draw buffer
 * - LOD selection per-instance based on screen size
 */

#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <unordered_map>
#include <memory>
#include <string>
#include <random>

namespace Sanic {

// Forward declarations
class VulkanContext;
class LandscapeSystem;

/**
 * Foliage mesh LOD
 */
struct FoliageLOD {
    uint32_t meshId;
    float screenSize;           // Screen size threshold (0-1)
    float ditheredCrossfade;    // Crossfade range for smooth transition
};

/**
 * Foliage type definition
 */
struct FoliageType {
    uint32_t id;
    std::string name;
    
    // LOD chain
    std::vector<FoliageLOD> lods;
    
    // Placement
    float density = 1.0f;               // Instances per square meter
    float minScale = 0.8f;
    float maxScale = 1.2f;
    float randomRotation = 360.0f;      // Random yaw rotation degrees
    bool alignToNormal = true;          // Align up to surface normal
    float normalAlignStrength = 0.5f;   // 0 = world up, 1 = surface normal
    
    // Culling
    float cullDistance = 500.0f;        // Max draw distance
    float fadeDistance = 50.0f;         // Distance over which to fade out
    float shadowCullDistance = 100.0f;  // Max distance for shadow casting
    
    // Wind
    float windStrength = 1.0f;
    float windSpeed = 1.0f;
    float windFrequency = 1.0f;
    
    // Collision
    bool hasCollision = false;
    float collisionRadius = 0.5f;
};

/**
 * Single foliage instance (GPU layout)
 */
struct alignas(16) FoliageInstance {
    glm::vec4 positionScale;    // xyz: position, w: uniform scale
    glm::vec4 rotationLOD;      // xyz: rotation (euler or quaternion xyz), w: LOD/fade
    uint32_t typeId;
    uint32_t clusterIndex;
    uint32_t flags;             // Bit flags for state
    uint32_t padding;
};

/**
 * Foliage cluster (64-256 instances)
 */
struct FoliageCluster {
    uint32_t id;
    uint32_t typeId;
    
    // Bounding volume
    glm::vec3 boundsMin;
    glm::vec3 boundsMax;
    glm::vec3 center;
    float radius;
    
    // Instance range
    uint32_t instanceOffset;
    uint32_t instanceCount;
    
    // LOD
    float lodBias = 0.0f;
    
    // Visibility state (updated by GPU)
    bool isVisible = false;
};

/**
 * Foliage sector (broad-phase culling unit)
 */
struct FoliageSector {
    uint32_t id;
    glm::ivec2 gridCoord;
    
    // Bounds
    glm::vec3 boundsMin;
    glm::vec3 boundsMax;
    
    // Clusters in this sector
    std::vector<uint32_t> clusterIds;
    
    // State
    bool isLoaded = false;
    bool isVisible = false;
};

/**
 * GPU culling data
 */
struct FoliageCullData {
    glm::mat4 viewProj;
    glm::vec4 frustumPlanes[6];
    glm::vec3 cameraPosition;
    float padding0;
    glm::vec3 cameraForward;
    float padding1;
    float time;
    float lodBias;
    uint32_t totalInstances;
    uint32_t padding2;
};

/**
 * Foliage system configuration
 */
struct FoliageConfig {
    // Sectors
    float sectorSize = 64.0f;           // World units per sector
    
    // Clusters
    uint32_t maxInstancesPerCluster = 256;
    
    // GPU limits
    uint32_t maxTotalInstances = 1000000;
    uint32_t maxVisibleInstances = 100000;
    uint32_t maxClusters = 10000;
    
    // Culling
    bool useOcclusionCulling = true;
    bool useHierarchicalCulling = true;
    float cullingMargin = 1.0f;         // Extra margin for frustum culling
    
    // LOD
    float lodBias = 1.0f;
    bool useDitheredTransitions = true;
    
    // Shadows
    bool castShadows = true;
    uint32_t shadowLOD = 1;             // Use this LOD for shadows
};

/**
 * GPU-driven foliage instancing system
 */
class FoliageSystem {
public:
    FoliageSystem();
    ~FoliageSystem();
    
    /**
     * Initialize the foliage system
     */
    bool initialize(VulkanContext* context, const FoliageConfig& config = {});
    
    /**
     * Shutdown and cleanup
     */
    void shutdown();
    
    /**
     * Register a foliage type
     */
    uint32_t registerType(const FoliageType& type);
    
    /**
     * Unregister a foliage type
     */
    void unregisterType(uint32_t typeId);
    
    /**
     * Add instances manually
     */
    void addInstances(uint32_t typeId, const std::vector<FoliageInstance>& instances);
    
    /**
     * Remove instances in a region
     */
    void removeInstances(const glm::vec3& center, float radius, uint32_t typeId = 0);
    
    /**
     * Procedurally scatter foliage on landscape
     * @param landscape Landscape to scatter on
     * @param typeId Foliage type
     * @param region Region to scatter in (min/max)
     * @param densityScale Multiplier for type density
     * @param seed Random seed
     */
    void scatterOnLandscape(LandscapeSystem* landscape, uint32_t landscapeId,
                            uint32_t typeId, 
                            const glm::vec3& regionMin, const glm::vec3& regionMax,
                            float densityScale = 1.0f, uint32_t seed = 12345);
    
    /**
     * Scatter using density map texture
     */
    void scatterWithDensityMap(LandscapeSystem* landscape, uint32_t landscapeId,
                               uint32_t typeId,
                               const std::vector<float>& densityMap,
                               uint32_t mapWidth, uint32_t mapHeight,
                               const glm::vec3& regionMin, const glm::vec3& regionMax);
    
    /**
     * Update for frame (animate wind, etc.)
     */
    void update(float deltaTime);
    
    /**
     * GPU culling pass
     */
    void cullInstances(VkCommandBuffer cmd, const glm::mat4& viewProj,
                       const glm::vec3& cameraPos);
    
    /**
     * Get indirect draw buffer for rendering
     */
    VkBuffer getIndirectBuffer() const { return indirectBuffer_; }
    uint32_t getDrawCount() const { return visibleCount_; }
    
    /**
     * Get instance buffer for vertex shader
     */
    VkBuffer getInstanceBuffer() const { return instanceBuffer_; }
    VkDeviceAddress getInstanceBufferAddress() const { return instanceBufferAddress_; }
    
    /**
     * Get visible instance buffer (compacted)
     */
    VkBuffer getVisibleBuffer() const { return visibleInstanceBuffer_; }
    
    // Statistics
    struct Statistics {
        uint32_t totalInstances;
        uint32_t visibleInstances;
        uint32_t totalClusters;
        uint32_t visibleClusters;
        uint32_t sectorsLoaded;
        float cullTimeMs;
    };
    Statistics getStatistics() const;
    
private:
    // Internal methods
    void rebuildClusters();
    void rebuildSectors();
    void uploadInstances();
    
    bool createBuffers();
    bool createCullPipeline();
    
    void frustumCullClusters(const glm::mat4& viewProj);
    
    VulkanContext* context_ = nullptr;
    FoliageConfig config_;
    
    // Foliage types
    std::unordered_map<uint32_t, FoliageType> types_;
    uint32_t nextTypeId_ = 1;
    
    // Instances (CPU copy for editing)
    std::vector<FoliageInstance> instances_;
    
    // Spatial organization
    std::vector<FoliageCluster> clusters_;
    std::vector<FoliageSector> sectors_;
    std::unordered_map<uint64_t, uint32_t> sectorGrid_;  // coord hash -> sector id
    
    // GPU buffers
    VkBuffer instanceBuffer_ = VK_NULL_HANDLE;           // All instances
    VkBuffer visibleInstanceBuffer_ = VK_NULL_HANDLE;    // Visible after culling
    VkBuffer clusterBuffer_ = VK_NULL_HANDLE;            // Cluster data
    VkBuffer indirectBuffer_ = VK_NULL_HANDLE;           // Draw commands
    VkBuffer cullDataBuffer_ = VK_NULL_HANDLE;           // Culling params
    VkBuffer counterBuffer_ = VK_NULL_HANDLE;            // Atomic counter
    
    VkDeviceMemory instanceMemory_ = VK_NULL_HANDLE;
    VkDeviceMemory visibleMemory_ = VK_NULL_HANDLE;
    VkDeviceMemory clusterMemory_ = VK_NULL_HANDLE;
    VkDeviceMemory indirectMemory_ = VK_NULL_HANDLE;
    VkDeviceMemory cullDataMemory_ = VK_NULL_HANDLE;
    VkDeviceMemory counterMemory_ = VK_NULL_HANDLE;
    
    VkDeviceAddress instanceBufferAddress_ = 0;
    
    // Culling compute pipeline
    VkPipeline clusterCullPipeline_ = VK_NULL_HANDLE;
    VkPipeline instanceCullPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout cullPipelineLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout cullDescSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSet cullDescSet_ = VK_NULL_HANDLE;
    VkDescriptorPool cullDescPool_ = VK_NULL_HANDLE;
    
    // State
    uint32_t visibleCount_ = 0;
    float currentTime_ = 0.0f;
    bool buffersDirty_ = true;
    
    bool initialized_ = false;
};

} // namespace Sanic

