/**
 * ClusterHierarchy.h
 * 
 * Nanite-style hierarchical cluster system for GPU-driven rendering.
 * Implements:
 * - Hierarchical BVH nodes with LOD groups
 * - Screen-space error metric for LOD selection
 * - Cluster bounds (sphere + AABB) for culling
 * - Persistent thread traversal support
 * - Multi-LOD generation using meshoptimizer
 * 
 * Based on Unreal Engine Nanite architecture:
 * - NaniteDataDecode.ush (FCluster, FHierarchyNodeSlice)
 * - NaniteCulling.ush (FCandidateNode)
 */

#pragma once
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <memory>
#include "VulkanContext.h"

// Forward declarations
class Mesh;
struct Vertex;

// ============================================================================
// CLUSTER DATA STRUCTURES
// Matches GLSL shader structures for GPU upload
// ============================================================================

/**
 * Cluster bounding data for culling and LOD selection.
 * Combines sphere bounds (for LOD) and AABB (for frustum/occlusion culling).
 */
struct alignas(16) ClusterBounds {
    // Bounding sphere for LOD calculation (16 bytes)
    float sphereCenter[3];      // World-space center
    float sphereRadius;         // Bounding sphere radius
    
    // AABB for frustum/occlusion culling (16 bytes)
    float boxCenter[3];         // AABB center
    float boxExtentX;           // AABB half-extent X
    
    // More AABB + LOD (16 bytes)
    float boxExtentY;           // AABB half-extent Y
    float boxExtentZ;           // AABB half-extent Z
    float lodError;             // Screen-space error when this cluster is used
    float parentLodError;       // Parent's error (for LOD selection)
};
static_assert(sizeof(ClusterBounds) == 48, "ClusterBounds must be 48 bytes for GPU alignment");

/**
 * Cluster geometry data - references into meshlet buffers.
 */
struct ClusterGeometry {
    uint32_t meshletOffset;     // First meshlet index in the meshlet buffer
    uint32_t meshletCount;      // Number of meshlets in this cluster
    uint32_t vertexOffset;      // Offset into vertex buffer (for attribute fetch)
    uint32_t triangleOffset;    // Offset into triangle buffer
    
    uint32_t triangleCount;     // Number of triangles in this cluster
    uint32_t flags;             // Cluster flags (two-sided, masked, etc.)
    uint32_t materialId;        // Material index for shading
    uint32_t padding;           // Padding to 32 bytes
};
static_assert(sizeof(ClusterGeometry) == 32, "ClusterGeometry must be 32 bytes for GPU alignment");

/**
 * Cluster - complete cluster data combining bounds and geometry.
 * This is the atomic unit of rendering in the Nanite-style system.
 */
struct Cluster {
    ClusterBounds bounds;       // 48 bytes
    ClusterGeometry geometry;   // 32 bytes
};
static_assert(sizeof(Cluster) == 80, "Cluster must be 80 bytes for GPU alignment");

// ============================================================================
// HIERARCHY NODE STRUCTURES
// ============================================================================

/**
 * BVH hierarchy node for cluster DAG traversal.
 * Each node can have multiple children (clusters or other nodes).
 * Based on Nanite's FHierarchyNodeSlice.
 */
struct HierarchyNode {
    // Bounding box for this node (encompasses all children) - 16 bytes
    float boxCenter[3];
    float boxExtentX;
    
    // More bounds + LOD - 16 bytes
    float boxExtentY;
    float boxExtentZ;
    float lodError;             // Max LOD error of all children
    float minLodError;          // Min LOD error (for early-out)
    
    // Child references - 16 bytes
    uint32_t childOffset;       // Index of first child in node/cluster array
    uint32_t childCount;        // Number of children (nodes or clusters)
    uint32_t flags;             // NODE_FLAG_* bits
    uint32_t level;             // Hierarchy level (0 = leaf clusters)
};
static_assert(sizeof(HierarchyNode) == 48, "HierarchyNode must be 48 bytes for GPU alignment");

// Node flags
constexpr uint32_t NODE_FLAG_LEAF = 0x1;           // Children are clusters, not nodes
constexpr uint32_t NODE_FLAG_STREAMING = 0x2;      // Node needs streaming
constexpr uint32_t NODE_FLAG_HAS_IMPOSTOR = 0x4;   // Has impostor for distance rendering

/**
 * Candidate node for GPU culling queue.
 * Used in persistent thread traversal.
 * Based on Nanite's FCandidateNode.
 */
struct CandidateNode {
    uint32_t nodeIndex;         // Index into hierarchy node array
    uint32_t instanceId;        // Instance this node belongs to
    uint32_t flags;             // Culling pass flags
    uint32_t padding;
};
static_assert(sizeof(CandidateNode) == 16, "CandidateNode must be 16 bytes for GPU alignment");

/**
 * Visible cluster output from culling.
 * Written by culling shaders, read by rasterizer.
 */
struct VisibleCluster {
    uint32_t clusterIndex;      // Index into cluster array
    uint32_t instanceId;        // Instance ID for transforms
    uint32_t flags;             // Rasterization flags (SW/HW, etc.)
    uint32_t pageIndex;         // For streaming/caching
};
static_assert(sizeof(VisibleCluster) == 16, "VisibleCluster must be 16 bytes for GPU alignment");

// ============================================================================
// GPU QUEUE STATE
// For persistent thread job queue (Nanite-style)
// ============================================================================

/**
 * Queue state for persistent thread hierarchy traversal.
 * Based on Nanite's FQueueState.
 */
struct QueueState {
    // Global counters - 16 bytes
    uint32_t totalVisibleClusters;
    uint32_t totalNodesProcessed;
    uint32_t hwRasterClusters;      // Clusters for hardware rasterizer
    uint32_t swRasterClusters;      // Clusters for software rasterizer
    
    // Per-pass state (Main + Post occlusion passes) - 32 bytes
    struct PassState {
        int32_t nodeReadOffset;     // Atomic read pointer
        int32_t nodeWriteOffset;    // Atomic write pointer
        int32_t nodeCount;          // Current node count (can be negative during sync)
        uint32_t padding;
    } passState[2];
    
    // Cluster output state - 16 bytes
    uint32_t clusterWriteOffset;
    uint32_t padding[3];
};
static_assert(sizeof(QueueState) == 64, "QueueState must be 64 bytes for GPU alignment");

// ============================================================================
// CLUSTER HIERARCHY CLASS
// ============================================================================

/**
 * ClusterHierarchy - Manages hierarchical cluster data for a mesh.
 * 
 * Responsibilities:
 * - Build cluster hierarchy from meshlet data
 * - Compute LOD error metrics
 * - Upload cluster/node data to GPU buffers
 * - Provide buffer addresses for GPU culling
 */
class ClusterHierarchy {
public:
    ClusterHierarchy(VulkanContext& context);
    ~ClusterHierarchy();
    
    /**
     * Build hierarchy from raw mesh data.
     * @param vertices Vertex positions for bounds calculation
     * @param indices Triangle indices
     * @param meshlets Existing meshlet data
     * @param meshletCount Number of meshlets
     */
    void buildFromMeshlets(
        const std::vector<glm::vec3>& vertices,
        const std::vector<uint32_t>& indices,
        const void* meshlets,
        uint32_t meshletCount
    );
    
    /**
     * Build hierarchy with LOD chain.
     * Creates multiple LOD levels using meshoptimizer.
     * @param maxLodLevels Maximum LOD levels to generate
     * @param lodErrorThreshold Error threshold for LOD transitions
     */
    void buildWithLOD(
        const std::vector<glm::vec3>& vertices,
        const std::vector<uint32_t>& indices,
        uint32_t maxLodLevels = 8,
        float lodErrorThreshold = 1.0f
    );
    
    // GPU buffer accessors
    VkBuffer getClusterBuffer() const { return clusterBuffer; }
    VkBuffer getHierarchyNodeBuffer() const { return hierarchyNodeBuffer; }
    VkBuffer getQueueStateBuffer() const { return queueStateBuffer; }
    VkBuffer getCandidateNodeBuffer() const { return candidateNodeBuffer; }
    VkBuffer getVisibleClusterBuffer() const { return visibleClusterBuffer; }
    
    VkDeviceAddress getClusterBufferAddress() const { return clusterBufferAddress; }
    VkDeviceAddress getHierarchyNodeBufferAddress() const { return hierarchyNodeBufferAddress; }
    
    uint32_t getClusterCount() const { return static_cast<uint32_t>(clusters.size()); }
    uint32_t getNodeCount() const { return static_cast<uint32_t>(hierarchyNodes.size()); }
    uint32_t getRootNodeIndex() const { return rootNodeIndex; }
    
    // LOD query
    float getMaxLodError() const { return maxLodError; }
    uint32_t getLodLevelCount() const { return lodLevelCount; }
    
    /**
     * LOD Level info for debugging/profiling
     */
    struct LODLevelInfo {
        uint32_t clusterOffset;     // First cluster index for this LOD
        uint32_t clusterCount;      // Number of clusters in this LOD
        uint32_t triangleCount;     // Total triangles in this LOD
        float lodError;             // Error threshold for this LOD
        float reductionRatio;       // Ratio compared to LOD 0
    };
    
    const std::vector<LODLevelInfo>& getLODLevels() const { return lodLevels; }
    
private:
    VulkanContext& context;
    
    // CPU-side data
    std::vector<Cluster> clusters;
    std::vector<HierarchyNode> hierarchyNodes;
    std::vector<LODLevelInfo> lodLevels;
    uint32_t rootNodeIndex = 0;
    float maxLodError = 0.0f;
    uint32_t lodLevelCount = 1;
    
    // GPU buffers
    VkBuffer clusterBuffer = VK_NULL_HANDLE;
    VkDeviceMemory clusterBufferMemory = VK_NULL_HANDLE;
    VkDeviceAddress clusterBufferAddress = 0;
    
    VkBuffer hierarchyNodeBuffer = VK_NULL_HANDLE;
    VkDeviceMemory hierarchyNodeBufferMemory = VK_NULL_HANDLE;
    VkDeviceAddress hierarchyNodeBufferAddress = 0;
    
    VkBuffer queueStateBuffer = VK_NULL_HANDLE;
    VkDeviceMemory queueStateBufferMemory = VK_NULL_HANDLE;
    
    VkBuffer candidateNodeBuffer = VK_NULL_HANDLE;
    VkDeviceMemory candidateNodeBufferMemory = VK_NULL_HANDLE;
    
    VkBuffer visibleClusterBuffer = VK_NULL_HANDLE;
    VkDeviceMemory visibleClusterBufferMemory = VK_NULL_HANDLE;
    
    // Maximum sizes for GPU buffers
    static constexpr uint32_t MAX_CANDIDATE_NODES = 1024 * 1024;   // 1M candidates
    static constexpr uint32_t MAX_VISIBLE_CLUSTERS = 512 * 1024;   // 512K visible
    
    // Internal methods
    void computeClusterBounds(
        const std::vector<glm::vec3>& vertices,
        const std::vector<uint32_t>& indices,
        uint32_t startIndex,
        uint32_t triangleCount,
        ClusterBounds& outBounds
    );
    
    void buildBVH();
    void computeLodErrors();
    void uploadToGPU();
    void createGPUBuffers();
    void cleanupGPUBuffers();
    
    // Buffer helpers
    void createBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties,
        VkBuffer& buffer,
        VkDeviceMemory& bufferMemory
    );
    
    VkDeviceAddress getBufferAddress(VkBuffer buffer);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
};

// ============================================================================
// CLUSTER CULLING SYSTEM
// GPU-driven culling pipeline
// ============================================================================

/**
 * Culling pass types matching Nanite.
 */
enum class CullingPass : uint32_t {
    NO_OCCLUSION = 0,       // No occlusion test
    OCCLUSION_MAIN = 1,     // Test against previous frame HZB
    OCCLUSION_POST = 2      // Re-test with updated HZB
};

/**
 * ClusterCullingSystem - GPU culling pipeline for cluster hierarchy.
 */
class ClusterCullingSystem {
public:
    ClusterCullingSystem(VulkanContext& context);
    ~ClusterCullingSystem();
    
    /**
     * Initialize culling pipeline.
     * @param hzbImageView HZB image for occlusion culling
     */
    void initialize(VkImageView hzbImageView);
    
    /**
     * Reset queue state for new frame.
     */
    void beginFrame(VkCommandBuffer cmd);
    
    /**
     * Run instance culling pass.
     */
    void cullInstances(
        VkCommandBuffer cmd,
        const ClusterHierarchy& hierarchy,
        const glm::mat4& viewProj,
        const glm::vec3& cameraPos,
        uint32_t instanceCount
    );
    
    /**
     * Run cluster culling with hierarchy traversal.
     */
    void cullClusters(
        VkCommandBuffer cmd,
        CullingPass pass
    );
    
    /**
     * Get visible cluster buffer for rasterization.
     */
    VkBuffer getVisibleClusterBuffer() const { return visibleClusterBuffer; }
    uint32_t getVisibleClusterCount() const;
    
private:
    VulkanContext& context;
    
    // Pipelines
    VkPipeline instanceCullingPipeline = VK_NULL_HANDLE;
    VkPipeline clusterCullingPipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    
    // Descriptor sets
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    
    // Buffers
    VkBuffer visibleClusterBuffer = VK_NULL_HANDLE;
    VkDeviceMemory visibleClusterBufferMemory = VK_NULL_HANDLE;
    
    VkBuffer queueStateBuffer = VK_NULL_HANDLE;
    VkDeviceMemory queueStateBufferMemory = VK_NULL_HANDLE;
    
    void createPipelines();
    void createDescriptorSets();
};
