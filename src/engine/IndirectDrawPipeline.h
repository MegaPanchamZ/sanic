#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <memory>

class VulkanContext;
class ClusterHierarchy;
class ClusterCullingPipeline;

/**
 * IndirectDrawPipeline
 * ====================
 * Manages GPU-driven indirect draw command generation.
 * 
 * Workflow:
 * 1. Cluster culling produces visible cluster list
 * 2. This pipeline generates indirect draw commands
 * 3. Commands are consumed by mesh shader dispatch
 * 
 * Also handles:
 * - Material binning for deferred shading
 * - HW/SW rasterizer classification
 * - Draw count tracking for statistics
 */
class IndirectDrawPipeline {
public:
    struct Config {
        uint32_t maxVisibleClusters;
        uint32_t maxDrawCommands;
        uint32_t maxMaterials;
        uint32_t maxClustersPerMaterial;
        
        Config()
            : maxVisibleClusters(512 * 1024)
            , maxDrawCommands(512 * 1024)
            , maxMaterials(256)
            , maxClustersPerMaterial(16 * 1024)
        {}
    };
    
    struct DrawStats {
        uint32_t totalDraws;
        uint32_t hwRasterDraws;
        uint32_t swRasterDraws;
        uint32_t materialsUsed;
    };
    
    IndirectDrawPipeline(VulkanContext& context, const Config& config = Config{});
    ~IndirectDrawPipeline();
    
    // Non-copyable
    IndirectDrawPipeline(const IndirectDrawPipeline&) = delete;
    IndirectDrawPipeline& operator=(const IndirectDrawPipeline&) = delete;
    
    /**
     * Build indirect draw commands from visible cluster list.
     * @param cmd Command buffer
     * @param visibleClusterBuffer Buffer containing visible clusters
     * @param clusterBuffer Buffer containing cluster data
     * @param visibleClusterCount Number of visible clusters
     */
    void buildDrawCommands(VkCommandBuffer cmd,
                           VkBuffer visibleClusterBuffer,
                           VkBuffer clusterBuffer,
                           uint32_t visibleClusterCount);
    
    /**
     * Reset counters for new frame.
     */
    void resetCounters(VkCommandBuffer cmd);
    
    /**
     * Get indirect command buffer for mesh shader dispatch.
     */
    VkBuffer getIndirectBuffer() const { return indirectCommandBuffer; }
    
    /**
     * Get draw count buffer (for vkCmdDrawMeshTasksIndirectCountEXT).
     */
    VkBuffer getDrawCountBuffer() const { return drawCountBuffer; }
    
    /**
     * Get material bin data for deferred shading.
     */
    VkBuffer getMaterialBinCounters() const { return materialBinCounterBuffer; }
    VkBuffer getMaterialBinData() const { return materialBinDataBuffer; }
    
    /**
     * Read back draw statistics (may stall).
     */
    DrawStats getDrawStats();
    
    /**
     * Get buffer device address for indirect buffer.
     */
    VkDeviceAddress getIndirectBufferAddress() const;
    VkDeviceAddress getDrawCountBufferAddress() const;

private:
    void createBuffers();
    void createPipeline();
    void destroyResources();
    
    VulkanContext& context;
    Config config;
    
    // GPU buffers
    VkBuffer indirectCommandBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indirectCommandMemory = VK_NULL_HANDLE;
    
    VkBuffer drawCountBuffer = VK_NULL_HANDLE;
    VkDeviceMemory drawCountMemory = VK_NULL_HANDLE;
    
    VkBuffer materialBinCounterBuffer = VK_NULL_HANDLE;
    VkDeviceMemory materialBinCounterMemory = VK_NULL_HANDLE;
    
    VkBuffer materialBinDataBuffer = VK_NULL_HANDLE;
    VkDeviceMemory materialBinDataMemory = VK_NULL_HANDLE;
    
    // Readback buffer for stats
    VkBuffer statsReadbackBuffer = VK_NULL_HANDLE;
    VkDeviceMemory statsReadbackMemory = VK_NULL_HANDLE;
    
    // Compute pipeline
    VkPipeline buildIndirectPipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    
    // Push constants
    struct BuildIndirectPushConstants {
        VkDeviceAddress visibleClusterBuffer;
        VkDeviceAddress clusterBuffer;
        VkDeviceAddress indirectBuffer;
        VkDeviceAddress drawCountBuffer;
        VkDeviceAddress materialBinCounters;
        VkDeviceAddress materialBinData;
        
        uint32_t visibleClusterCount;
        uint32_t maxClustersPerBin;
        uint32_t materialCount;
        uint32_t meshletsPerCluster;
    };
};
