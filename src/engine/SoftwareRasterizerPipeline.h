/**
 * SoftwareRasterizerPipeline.h
 * 
 * GPU-driven software rasterizer pipeline for small triangles.
 * Implements Nanite-style hybrid SW/HW rasterization:
 * 
 * 1. Triangle Binning: Classify triangles by screen size
 *    - Small triangles (< threshold) → SW rasterizer
 *    - Large triangles → HW mesh shader pipeline
 * 
 * 2. SW Rasterization: Compute shader rasterizes small triangles
 *    - Edge function evaluation
 *    - Atomic depth testing
 *    - Visibility buffer writes
 * 
 * 3. Visibility Buffer Resolve: Reconstruct G-Buffer attributes
 *    - Read triangle/cluster/instance IDs
 *    - Compute barycentrics
 *    - Interpolate attributes
 * 
 * Benefits:
 * - Avoids 2x2 quad overshading for small triangles
 * - Better utilization for dense meshes
 * - Unified visibility buffer output
 */

#pragma once
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <cstdint>
#include "VulkanContext.h"
#include "ClusterHierarchy.h"

/**
 * SW rasterized triangle - binned for compute rasterization
 */
struct alignas(16) SWTriangle {
    uint32_t clusterIndex;      // Index into visible clusters
    uint32_t triangleIndex;     // Triangle within cluster
    uint32_t instanceId;        // Instance ID
    uint32_t pad;
};
static_assert(sizeof(SWTriangle) == 16, "SWTriangle must be 16 bytes");

/**
 * HW rasterized batch - for mesh shader pipeline
 */
struct alignas(16) HWBatch {
    uint32_t visibleClusterIdx; // Index into visible clusters
    uint32_t triangleMask;      // Bitmask of triangles to render
    uint32_t instanceId;        // Instance ID
    uint32_t pad;
};
static_assert(sizeof(HWBatch) == 16, "HWBatch must be 16 bytes");

/**
 * Atomic counters for binning
 */
struct alignas(16) BinningCounters {
    uint32_t swTriangleCount;   // SW triangles written
    uint32_t hwBatchCount;      // HW batches written
    uint32_t totalSWPixels;     // Total SW pixels (for stats)
    uint32_t totalHWPixels;     // Total HW pixels (for stats)
};
static_assert(sizeof(BinningCounters) == 16, "BinningCounters must be 16 bytes");

/**
 * Rasterization statistics
 */
struct RasterStats {
    uint32_t swTriangles;       // Triangles sent to SW rasterizer
    uint32_t hwBatches;         // Batches sent to HW rasterizer
    uint64_t swPixels;          // Pixels rasterized by SW
    uint64_t hwPixels;          // Pixels rasterized by HW
    float swHwRatio;            // SW/HW pixel ratio
};

/**
 * Configuration for SW/HW threshold
 */
struct RasterConfig {
    float swThreshold = 32.0f;  // Pixels² threshold for SW rasterization
    uint32_t maxSWTriangles = 1024 * 1024; // Max SW triangles
    uint32_t maxHWBatches = 256 * 1024;    // Max HW batches
    bool enableStats = true;    // Track rasterization statistics
};

class SoftwareRasterizerPipeline {
public:
    SoftwareRasterizerPipeline() = default;
    ~SoftwareRasterizerPipeline();
    
    // Disable copy
    SoftwareRasterizerPipeline(const SoftwareRasterizerPipeline&) = delete;
    SoftwareRasterizerPipeline& operator=(const SoftwareRasterizerPipeline&) = delete;
    
    /**
     * Initialize the pipeline
     * @param context Vulkan context
     * @param config Rasterization configuration
     * @return true on success
     */
    bool initialize(VulkanContext* context, const RasterConfig& config = {});
    
    /**
     * Cleanup resources
     */
    void cleanup();
    
    /**
     * Bin triangles for SW vs HW rasterization
     * 
     * @param cmd Command buffer to record into
     * @param visibleClusterBuffer Buffer of visible clusters
     * @param visibleCount Number of visible clusters
     * @param viewProj View-projection matrix
     * @param screenWidth Screen width in pixels
     * @param screenHeight Screen height in pixels
     */
    void binTriangles(VkCommandBuffer cmd,
                     VkBuffer visibleClusterBuffer,
                     uint32_t visibleCount,
                     VkBuffer clusterBuffer,
                     VkBuffer instanceBuffer,
                     VkBuffer vertexBuffer,
                     VkBuffer indexBuffer,
                     const glm::mat4& viewProj,
                     uint32_t screenWidth,
                     uint32_t screenHeight);
    
    /**
     * Rasterize SW-binned triangles
     * 
     * @param cmd Command buffer
     * @param visibilityBuffer 64-bit visibility buffer
     * @param viewProj View-projection matrix
     * @param screenWidth Screen width
     * @param screenHeight Screen height
     */
    void rasterizeSW(VkCommandBuffer cmd,
                    VkBuffer visibilityBuffer,
                    VkBuffer visibleClusterBuffer,
                    VkBuffer clusterBuffer,
                    VkBuffer instanceBuffer,
                    VkBuffer vertexBuffer,
                    VkBuffer indexBuffer,
                    const glm::mat4& viewProj,
                    uint32_t screenWidth,
                    uint32_t screenHeight);
    
    /**
     * Resolve visibility buffer to G-Buffer
     * 
     * @param cmd Command buffer
     * @param visibilityBuffer Input visibility buffer
     * @param gbufferPosition Output position image
     * @param gbufferNormal Output normal image
     * @param gbufferAlbedo Output albedo image
     * @param gbufferMaterial Output material image
     * @param viewProj View-projection matrix
     * @param invViewProj Inverse view-projection matrix
     * @param screenWidth Screen width
     * @param screenHeight Screen height
     */
    void resolveVisibilityBuffer(VkCommandBuffer cmd,
                                VkBuffer visibilityBuffer,
                                VkBuffer clusterBuffer,
                                VkBuffer instanceBuffer,
                                VkBuffer vertexBuffer,
                                VkBuffer indexBuffer,
                                VkImageView gbufferPosition,
                                VkImageView gbufferNormal,
                                VkImageView gbufferAlbedo,
                                VkImageView gbufferMaterial,
                                const glm::mat4& viewProj,
                                const glm::mat4& invViewProj,
                                uint32_t screenWidth,
                                uint32_t screenHeight);
    
    /**
     * Reset counters at frame start
     */
    void resetCounters(VkCommandBuffer cmd);
    
    /**
     * Get HW batch buffer for mesh shader dispatch
     */
    VkBuffer getHWBatchBuffer() const { return hwBatchBuffer_; }
    
    /**
     * Get HW batch count buffer (for indirect dispatch)
     */
    VkBuffer getCounterBuffer() const { return counterBuffer_; }
    
    /**
     * Read back rasterization statistics (synchronous)
     */
    RasterStats readbackStats();
    
    /**
     * Get configuration
     */
    const RasterConfig& getConfig() const { return config_; }
    
private:
    bool createBuffers();
    bool createPipelines();
    bool loadShader(const std::string& path, VkShaderModule& outModule);
    
    VulkanContext* context_ = nullptr;
    RasterConfig config_;
    
    // Compute pipelines
    VkPipeline triangleBinPipeline_ = VK_NULL_HANDLE;
    VkPipeline swRasterPipeline_ = VK_NULL_HANDLE;
    VkPipeline resolveVisbufferPipeline_ = VK_NULL_HANDLE;
    
    VkPipelineLayout triangleBinLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout swRasterLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout resolveLayout_ = VK_NULL_HANDLE;
    
    // Descriptor sets for image outputs
    VkDescriptorSetLayout resolveDescriptorLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool resolveDescriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet resolveDescriptorSet_ = VK_NULL_HANDLE;
    
    // Buffers
    VkBuffer swTriangleBuffer_ = VK_NULL_HANDLE;    // SW triangle list
    VkDeviceMemory swTriangleMemory_ = VK_NULL_HANDLE;
    
    VkBuffer hwBatchBuffer_ = VK_NULL_HANDLE;       // HW batch list
    VkDeviceMemory hwBatchMemory_ = VK_NULL_HANDLE;
    
    VkBuffer counterBuffer_ = VK_NULL_HANDLE;       // Atomic counters
    VkDeviceMemory counterMemory_ = VK_NULL_HANDLE;
    
    VkBuffer readbackBuffer_ = VK_NULL_HANDLE;      // Stats readback
    VkDeviceMemory readbackMemory_ = VK_NULL_HANDLE;
    
    // Buffer device addresses
    VkDeviceAddress swTriangleAddr_ = 0;
    VkDeviceAddress hwBatchAddr_ = 0;
    VkDeviceAddress counterAddr_ = 0;
    
    bool initialized_ = false;
};
