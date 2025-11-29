#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include "ClusterHierarchy.h"

class VulkanContext;

/**
 * ClusterCullingPipeline
 * ======================
 * GPU-driven cluster culling pipeline for Nanite-style rendering.
 * 
 * Features:
 * - Hierarchical BVH traversal with persistent threads
 * - Frustum culling (sphere + AABB)
 * - Backface culling (cone test)
 * - LOD selection based on screen-space error
 * - Two-pass occlusion culling (Main + Post) - integrated with HZB
 * 
 * Pipeline:
 * 1. Instance culling (per-object frustum test)
 * 2. Node culling (BVH traversal)
 * 3. Cluster culling (final visibility)
 * 4. Output: Visible cluster list for mesh shader dispatch
 */
class ClusterCullingPipeline {
public:
    /**
     * Culling configuration
     */
    struct CullingConfig {
        uint32_t maxInstances;          // Maximum instances to process
        uint32_t maxCandidateNodes;     // Max nodes in traversal queue
        uint32_t maxVisibleClusters;    // Max visible clusters output
        uint32_t maxHierarchyIterations; // Max BVH traversal iterations
        float errorThreshold;            // Screen-space error threshold (pixels)
        bool enableFrustumCulling;
        bool enableBackfaceCulling;
        bool enableOcclusionCulling;
        bool enableLODSelection;
        
        CullingConfig()
            : maxInstances(65536)
            , maxCandidateNodes(1024 * 1024)
            , maxVisibleClusters(512 * 1024)
            , maxHierarchyIterations(32)
            , errorThreshold(1.0f)
            , enableFrustumCulling(true)
            , enableBackfaceCulling(true)
            , enableOcclusionCulling(true)
            , enableLODSelection(true)
        {}
    };

    /**
     * Per-frame culling parameters
     */
    struct CullingParams {
        glm::mat4 viewMatrix;
        glm::mat4 projMatrix;
        glm::mat4 viewProjMatrix;
        glm::vec4 frustumPlanes[6];         // World-space frustum planes
        glm::vec3 cameraPosition;
        float nearPlane;
        glm::vec2 screenSize;
        float lodScale;                      // LOD bias (1.0 = normal)
        float errorThreshold;                // Screen-space error threshold
        uint32_t frameIndex;
        uint32_t flags;                      // Culling flags
    };

    /**
     * Instance data for culling
     */
    struct InstanceData {
        glm::mat4 worldMatrix;
        glm::vec4 boundingSphere;           // xyz = center, w = radius
        uint32_t hierarchyOffset;           // Offset into hierarchy node buffer
        uint32_t clusterOffset;             // Offset into cluster buffer
        uint32_t clusterCount;              // Number of clusters for this instance
        uint32_t flags;                     // Instance flags
    };

    /**
     * Culling statistics (read back for profiling)
     */
    struct CullingStats {
        uint32_t instancesProcessed;
        uint32_t instancesVisible;
        uint32_t nodesTraversed;
        uint32_t clustersTested;
        uint32_t clustersVisible;
        uint32_t clustersHWRaster;          // Hardware rasterizer
        uint32_t clustersSWRaster;          // Software rasterizer
        float gpuTimeMs;
    };

    ClusterCullingPipeline(VulkanContext& context, const CullingConfig& config = CullingConfig{});
    ~ClusterCullingPipeline();

    // Non-copyable
    ClusterCullingPipeline(const ClusterCullingPipeline&) = delete;
    ClusterCullingPipeline& operator=(const ClusterCullingPipeline&) = delete;

    /**
     * Register a cluster hierarchy for culling.
     * @return Instance index for tracking
     */
    uint32_t registerHierarchy(ClusterHierarchy* hierarchy, const glm::mat4& worldMatrix);

    /**
     * Update instance transform.
     */
    void updateInstanceTransform(uint32_t instanceIndex, const glm::mat4& worldMatrix);

    /**
     * Perform GPU culling.
     * Must be called after updating camera/frustum parameters.
     * @param cmd Command buffer to record culling commands
     * @param params Per-frame culling parameters
     */
    void performCulling(VkCommandBuffer cmd, const CullingParams& params);

    /**
     * Reset culling state for new frame.
     */
    void beginFrame(uint32_t frameIndex);

    // Accessors for mesh shader dispatch
    VkBuffer getVisibleClusterBuffer() const { return visibleClusterBuffer; }
    VkBuffer getDrawIndirectBuffer() const { return drawIndirectBuffer; }
    VkDeviceAddress getVisibleClusterBufferAddress() const { return visibleClusterBufferAddress; }
    
    /**
     * Get visible cluster count (requires GPU readback - use for debugging only)
     */
    uint32_t getVisibleClusterCount() const;

    /**
     * Get culling statistics (requires GPU readback)
     */
    CullingStats getStats() const;

    // Descriptor set for mesh shader access
    VkDescriptorSet getCullingOutputDescriptorSet() const { return outputDescriptorSet; }
    VkDescriptorSetLayout getCullingOutputDescriptorSetLayout() const { return outputDescriptorSetLayout; }

private:
    VulkanContext& context;
    CullingConfig config;

    // Compute pipelines
    VkPipeline instanceCullPipeline = VK_NULL_HANDLE;
    VkPipeline nodeCullPipeline = VK_NULL_HANDLE;
    VkPipeline clusterCullPipeline = VK_NULL_HANDLE;
    VkPipeline hierarchicalCullPipeline = VK_NULL_HANDLE;  // Combined traversal

    VkPipelineLayout cullPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout cullDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet cullDescriptorSet = VK_NULL_HANDLE;

    // Output descriptor set (for mesh shader consumption)
    VkDescriptorSetLayout outputDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet outputDescriptorSet = VK_NULL_HANDLE;

    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;

    // GPU Buffers
    // Input buffers
    VkBuffer instanceBuffer = VK_NULL_HANDLE;
    VkDeviceMemory instanceBufferMemory = VK_NULL_HANDLE;
    VkDeviceAddress instanceBufferAddress = 0;

    VkBuffer clusterBuffer = VK_NULL_HANDLE;           // Combined clusters from all hierarchies
    VkDeviceMemory clusterBufferMemory = VK_NULL_HANDLE;
    VkDeviceAddress clusterBufferAddress = 0;

    VkBuffer hierarchyNodeBuffer = VK_NULL_HANDLE;     // Combined hierarchy nodes
    VkDeviceMemory hierarchyNodeBufferMemory = VK_NULL_HANDLE;
    VkDeviceAddress hierarchyNodeBufferAddress = 0;

    // Work queues (ping-pong for persistent threads)
    VkBuffer candidateBufferA = VK_NULL_HANDLE;
    VkDeviceMemory candidateBufferAMemory = VK_NULL_HANDLE;
    VkBuffer candidateBufferB = VK_NULL_HANDLE;
    VkDeviceMemory candidateBufferBMemory = VK_NULL_HANDLE;

    // Queue state
    VkBuffer queueStateBuffer = VK_NULL_HANDLE;
    VkDeviceMemory queueStateBufferMemory = VK_NULL_HANDLE;

    // Output buffers
    VkBuffer visibleClusterBuffer = VK_NULL_HANDLE;
    VkDeviceMemory visibleClusterBufferMemory = VK_NULL_HANDLE;
    VkDeviceAddress visibleClusterBufferAddress = 0;

    // Indirect draw buffer for mesh shader dispatch
    VkBuffer drawIndirectBuffer = VK_NULL_HANDLE;
    VkDeviceMemory drawIndirectBufferMemory = VK_NULL_HANDLE;

    // Readback buffer for stats
    VkBuffer statsReadbackBuffer = VK_NULL_HANDLE;
    VkDeviceMemory statsReadbackBufferMemory = VK_NULL_HANDLE;

    // CPU-side tracking
    std::vector<InstanceData> instances;
    std::vector<ClusterHierarchy*> hierarchies;
    uint32_t totalClusterCount = 0;
    uint32_t totalNodeCount = 0;
    bool buffersDirty = true;

    // Frame state
    uint32_t currentFrameIndex = 0;

    // Initialization
    void createDescriptorSetLayout();
    void createPipelineLayout();
    void createComputePipelines();
    void createBuffers();
    void createDescriptorSets();
    void updateDescriptorSets();

    // Buffer management
    void uploadInstanceData();
    void uploadClusterData();
    void rebuildBuffers();

    // Utility
    VkShaderModule createShaderModule(const std::vector<char>& code);
    std::vector<char> readShaderFile(const std::string& filename);
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, 
                      VkMemoryPropertyFlags properties, VkBuffer& buffer, 
                      VkDeviceMemory& memory);
    VkDeviceAddress getBufferAddress(VkBuffer buffer);

    // Push constant structure
    struct CullPushConstants {
        glm::mat4 viewProj;
        glm::vec4 frustumPlanes[6];
        glm::vec4 cameraPosition;       // xyz = pos, w = near
        glm::vec4 screenParams;         // x = width, y = height, z = lodScale, w = errorThreshold
        uint32_t clusterCount;
        uint32_t nodeCount;
        uint32_t frameIndex;
        uint32_t flags;
    };
};
