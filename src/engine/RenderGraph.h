/**
 * RenderGraph.h
 * 
 * Unreal Engine RDG-style Render Dependency Graph system.
 * Implements:
 * - Automatic resource barrier management
 * - Pass dependency tracking and culling
 * - Transient resource allocation
 * - PSO caching
 * - Async compute support
 * 
 * Based on Unreal Engine's FRDGBuilder architecture.
 */

#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include <optional>
#include <bitset>
#include <queue>
#include <glm/glm.hpp>

class VulkanContext;

// Forward declarations
class RDGTexture;
class RDGBuffer;
class RDGPass;
class RenderGraph;
class RDGResourcePool;

// ============================================================================
// RESOURCE DESCRIPTORS
// ============================================================================

/**
 * Texture descriptor for RDG textures
 */
struct RDGTextureDesc {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 1;
    uint32_t mipLevels = 1;
    uint32_t arrayLayers = 1;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageUsageFlags usage = 0;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkImageType imageType = VK_IMAGE_TYPE_2D;
    VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
    bool isTransient = true;  // Can be aliased with other transient resources
    
    bool operator==(const RDGTextureDesc& other) const {
        return width == other.width && height == other.height &&
               depth == other.depth && mipLevels == other.mipLevels &&
               arrayLayers == other.arrayLayers && format == other.format &&
               usage == other.usage && samples == other.samples &&
               imageType == other.imageType && tiling == other.tiling;
    }
    
    static RDGTextureDesc Create2D(uint32_t w, uint32_t h, VkFormat fmt, VkImageUsageFlags use) {
        RDGTextureDesc desc;
        desc.width = w;
        desc.height = h;
        desc.format = fmt;
        desc.usage = use;
        return desc;
    }
    
    static RDGTextureDesc Create2DArray(uint32_t w, uint32_t h, uint32_t layers, VkFormat fmt, VkImageUsageFlags use) {
        RDGTextureDesc desc = Create2D(w, h, fmt, use);
        desc.arrayLayers = layers;
        return desc;
    }
    
    static RDGTextureDesc Create3D(uint32_t w, uint32_t h, uint32_t d, VkFormat fmt, VkImageUsageFlags use) {
        RDGTextureDesc desc;
        desc.width = w;
        desc.height = h;
        desc.depth = d;
        desc.format = fmt;
        desc.usage = use;
        desc.imageType = VK_IMAGE_TYPE_3D;
        return desc;
    }
};

/**
 * Buffer descriptor for RDG buffers
 */
struct RDGBufferDesc {
    VkDeviceSize size = 0;
    VkBufferUsageFlags usage = 0;
    bool isTransient = true;
    bool hostVisible = false;  // For readback
    
    bool operator==(const RDGBufferDesc& other) const {
        return size == other.size && usage == other.usage && hostVisible == other.hostVisible;
    }
    
    static RDGBufferDesc CreateStructured(VkDeviceSize sz, VkBufferUsageFlags use) {
        RDGBufferDesc desc;
        desc.size = sz;
        desc.usage = use | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        return desc;
    }
    
    static RDGBufferDesc CreateIndirect(VkDeviceSize sz) {
        RDGBufferDesc desc;
        desc.size = sz;
        desc.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        return desc;
    }
};

// ============================================================================
// RESOURCE ACCESS TRACKING
// ============================================================================

/**
 * Resource access types for barrier generation
 */
enum class RDGAccessType : uint32_t {
    None            = 0,
    Read            = 1 << 0,
    Write           = 1 << 1,
    ReadWrite       = Read | Write,
    
    // Specific access types for fine-grained barriers
    SRVCompute      = 1 << 2,
    SRVGraphics     = 1 << 3,
    UAVCompute      = 1 << 4,
    UAVGraphics     = 1 << 5,
    RTV             = 1 << 6,  // Render Target View
    DSV             = 1 << 7,  // Depth Stencil View
    CopySrc         = 1 << 8,
    CopyDst         = 1 << 9,
    Present         = 1 << 10,
    IndirectBuffer  = 1 << 11,
    VertexBuffer    = 1 << 12,
    IndexBuffer     = 1 << 13,
};

inline RDGAccessType operator|(RDGAccessType a, RDGAccessType b) {
    return static_cast<RDGAccessType>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline RDGAccessType operator&(RDGAccessType a, RDGAccessType b) {
    return static_cast<RDGAccessType>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline bool HasFlag(RDGAccessType flags, RDGAccessType flag) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag)) != 0;
}

/**
 * Resource state tracking per subresource
 */
struct RDGSubresourceState {
    RDGAccessType access = RDGAccessType::None;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 accessMask = VK_ACCESS_2_NONE;
    uint32_t producerPassIndex = UINT32_MAX;  // Last pass that wrote
    uint32_t lastReadPassIndex = UINT32_MAX;  // Last pass that read
    bool isCompute = false;  // For async compute tracking
};

// ============================================================================
// RDG RESOURCES
// ============================================================================

using RDGTextureHandle = uint32_t;
using RDGBufferHandle = uint32_t;
using RDGPassHandle = uint32_t;

constexpr RDGTextureHandle RDG_INVALID_TEXTURE = UINT32_MAX;
constexpr RDGBufferHandle RDG_INVALID_BUFFER = UINT32_MAX;
constexpr RDGPassHandle RDG_INVALID_PASS = UINT32_MAX;

/**
 * RDG Texture - Graph-tracked texture resource
 */
class RDGTexture {
public:
    RDGTextureHandle handle = RDG_INVALID_TEXTURE;
    std::string name;
    RDGTextureDesc desc;
    
    // Physical resources (allocated during execution)
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    std::vector<VkImageView> mipViews;  // Per-mip views for UAV access
    
    // State tracking per subresource (mip * arrayLayer)
    std::vector<RDGSubresourceState> subresourceStates;
    
    // External resource (not created by graph)
    bool isExternal = false;
    bool isImported = false;
    
    // Lifetime
    RDGPassHandle firstPass = RDG_INVALID_PASS;
    RDGPassHandle lastPass = RDG_INVALID_PASS;
    
    uint32_t getSubresourceCount() const {
        return desc.mipLevels * desc.arrayLayers;
    }
    
    uint32_t getSubresourceIndex(uint32_t mip, uint32_t layer) const {
        return layer * desc.mipLevels + mip;
    }
};

/**
 * RDG Buffer - Graph-tracked buffer resource
 */
class RDGBuffer {
public:
    RDGBufferHandle handle = RDG_INVALID_BUFFER;
    std::string name;
    RDGBufferDesc desc;
    
    // Physical resources
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceAddress deviceAddress = 0;
    
    // State tracking
    RDGSubresourceState state;
    
    // External resource
    bool isExternal = false;
    bool isImported = false;
    
    // Lifetime
    RDGPassHandle firstPass = RDG_INVALID_PASS;
    RDGPassHandle lastPass = RDG_INVALID_PASS;
};

// ============================================================================
// PASS FLAGS AND TYPES
// ============================================================================

/**
 * Pass execution flags
 */
enum class RDGPassFlags : uint32_t {
    None            = 0,
    Compute         = 1 << 0,  // Compute pass
    Raster          = 1 << 1,  // Rasterization pass
    Copy            = 1 << 2,  // Copy/transfer pass
    AsyncCompute    = 1 << 3,  // Can run on async compute queue
    NeverCull       = 1 << 4,  // Never cull this pass
    SkipRenderPass  = 1 << 5,  // Skip render pass begin/end (for merged passes)
};

inline RDGPassFlags operator|(RDGPassFlags a, RDGPassFlags b) {
    return static_cast<RDGPassFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline bool HasFlag(RDGPassFlags flags, RDGPassFlags flag) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag)) != 0;
}

// ============================================================================
// RDG PASS
// ============================================================================

/**
 * Resource access declaration for a pass
 */
struct RDGResourceAccess {
    enum class Type { Texture, Buffer };
    Type type;
    union {
        RDGTextureHandle texture;
        RDGBufferHandle buffer;
    };
    RDGAccessType access;
    uint32_t mipLevel = 0;      // For textures
    uint32_t mipCount = 1;      // Number of mips accessed
    uint32_t arrayLayer = 0;    // For texture arrays
    uint32_t layerCount = 1;    // Number of layers accessed
};

/**
 * RDG Pass - A single render/compute pass
 */
class RDGPass {
public:
    using ExecuteFunction = std::function<void(VkCommandBuffer cmd, RenderGraph& graph)>;
    
    RDGPassHandle handle = RDG_INVALID_PASS;
    std::string name;
    RDGPassFlags flags = RDGPassFlags::None;
    ExecuteFunction executeFunc;
    
    // Resource accesses
    std::vector<RDGResourceAccess> textureAccesses;
    std::vector<RDGResourceAccess> bufferAccesses;
    
    // Dependency tracking
    std::vector<RDGPassHandle> producers;  // Passes we depend on
    std::vector<RDGPassHandle> consumers;  // Passes that depend on us
    
    // Execution state
    bool isCulled = false;
    bool isExecuted = false;
    int32_t asyncComputeFence = -1;  // Fence index for async compute sync
    
    // Render pass info (for raster passes)
    std::vector<VkRenderingAttachmentInfo> colorAttachments;
    VkRenderingAttachmentInfo depthAttachment = {};
    VkRenderingAttachmentInfo stencilAttachment = {};
    bool hasDepth = false;
    bool hasStencil = false;
    VkExtent2D renderExtent = {0, 0};
    
    // For merged render passes
    RDGPassHandle mergedWithPass = RDG_INVALID_PASS;
    bool isMergeRoot = false;
};

// ============================================================================
// BARRIER BATCH
// ============================================================================

/**
 * Batched barriers for efficient submission
 */
struct RDGBarrierBatch {
    std::vector<VkImageMemoryBarrier2> imageBarriers;
    std::vector<VkBufferMemoryBarrier2> bufferBarriers;
    VkPipelineStageFlags2 srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    VkPipelineStageFlags2 dstStageMask = VK_PIPELINE_STAGE_2_NONE;
    
    void clear() {
        imageBarriers.clear();
        bufferBarriers.clear();
        srcStageMask = VK_PIPELINE_STAGE_2_NONE;
        dstStageMask = VK_PIPELINE_STAGE_2_NONE;
    }
    
    bool empty() const {
        return imageBarriers.empty() && bufferBarriers.empty();
    }
    
    void submit(VkCommandBuffer cmd) const {
        if (empty()) return;
        
        VkDependencyInfo depInfo = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        depInfo.imageMemoryBarrierCount = static_cast<uint32_t>(imageBarriers.size());
        depInfo.pImageMemoryBarriers = imageBarriers.data();
        depInfo.bufferMemoryBarrierCount = static_cast<uint32_t>(bufferBarriers.size());
        depInfo.pBufferMemoryBarriers = bufferBarriers.data();
        
        vkCmdPipelineBarrier2(cmd, &depInfo);
    }
};

// ============================================================================
// PSO CACHE
// ============================================================================

/**
 * Pipeline State Object cache key
 */
struct PSOCacheKey {
    VkPipelineBindPoint bindPoint;
    std::vector<VkFormat> colorFormats;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    VkFormat stencilFormat = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    
    // Shader hashes
    uint64_t vertexShaderHash = 0;
    uint64_t fragmentShaderHash = 0;
    uint64_t computeShaderHash = 0;
    uint64_t meshShaderHash = 0;
    uint64_t taskShaderHash = 0;
    
    // State hashes
    uint64_t vertexInputHash = 0;
    uint64_t rasterStateHash = 0;
    uint64_t depthStencilHash = 0;
    uint64_t blendStateHash = 0;
    
    bool operator==(const PSOCacheKey& other) const;
};

struct PSOCacheKeyHash {
    size_t operator()(const PSOCacheKey& key) const;
};

/**
 * Cached pipeline state
 */
struct CachedPSO {
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    uint64_t lastUsedFrame = 0;
    uint32_t useCount = 0;
};

/**
 * PSO Cache - Caches compiled pipeline states
 */
class PSOCache {
public:
    PSOCache(VulkanContext& context);
    ~PSOCache();
    
    VkPipeline getOrCreateGraphicsPipeline(
        const PSOCacheKey& key,
        const VkGraphicsPipelineCreateInfo& createInfo
    );
    
    VkPipeline getOrCreateComputePipeline(
        const PSOCacheKey& key,
        const VkComputePipelineCreateInfo& createInfo
    );
    
    void evictUnused(uint64_t currentFrame, uint64_t frameThreshold = 120);
    void saveToFile(const std::string& path);
    void loadFromFile(const std::string& path);
    
    size_t getCacheSize() const { return psoCache.size(); }
    uint64_t getCacheHits() const { return cacheHits; }
    uint64_t getCacheMisses() const { return cacheMisses; }
    
private:
    VulkanContext& context;
    std::unordered_map<PSOCacheKey, CachedPSO, PSOCacheKeyHash> psoCache;
    VkPipelineCache vulkanPipelineCache = VK_NULL_HANDLE;
    
    uint64_t cacheHits = 0;
    uint64_t cacheMisses = 0;
};

// ============================================================================
// RESOURCE POOL
// ============================================================================

/**
 * Pooled texture for reuse
 */
struct PooledTexture {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    RDGTextureDesc desc;
    uint64_t lastUsedFrame = 0;
};

/**
 * Pooled buffer for reuse
 */
struct PooledBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceAddress deviceAddress = 0;
    RDGBufferDesc desc;
    uint64_t lastUsedFrame = 0;
};

/**
 * RDG Resource Pool - Manages pooled transient resources
 */
class RDGResourcePool {
public:
    RDGResourcePool(VulkanContext& context);
    ~RDGResourcePool();
    
    PooledTexture* acquireTexture(const RDGTextureDesc& desc);
    void releaseTexture(PooledTexture* texture, uint64_t frame);
    
    PooledBuffer* acquireBuffer(const RDGBufferDesc& desc);
    void releaseBuffer(PooledBuffer* buffer, uint64_t frame);
    
    void evictUnused(uint64_t currentFrame, uint64_t frameThreshold = 30);
    void trimPool(size_t maxTextures, size_t maxBuffers);
    
    size_t getTexturePoolSize() const { return texturePool.size(); }
    size_t getBufferPoolSize() const { return bufferPool.size(); }
    size_t getTotalMemoryUsed() const { return totalMemoryUsed; }
    
private:
    VulkanContext& context;
    
    std::vector<std::unique_ptr<PooledTexture>> texturePool;
    std::vector<std::unique_ptr<PooledBuffer>> bufferPool;
    
    // Free lists for quick allocation
    std::vector<PooledTexture*> freeTextures;
    std::vector<PooledBuffer*> freeBuffers;
    
    size_t totalMemoryUsed = 0;
    
    PooledTexture* createTexture(const RDGTextureDesc& desc);
    PooledBuffer* createBuffer(const RDGBufferDesc& desc);
    void destroyTexture(PooledTexture* texture);
    void destroyBuffer(PooledBuffer* buffer);
    
    bool isCompatible(const RDGTextureDesc& a, const RDGTextureDesc& b) const;
    bool isCompatible(const RDGBufferDesc& a, const RDGBufferDesc& b) const;
};

// ============================================================================
// RENDER GRAPH BUILDER
// ============================================================================

/**
 * RenderGraph - Main render graph builder and executor
 * 
 * Usage:
 *   RenderGraph graph(context);
 *   
 *   auto gbufferAlbedo = graph.createTexture("GBuffer.Albedo", RDGTextureDesc::Create2D(...));
 *   auto gbufferNormal = graph.createTexture("GBuffer.Normal", RDGTextureDesc::Create2D(...));
 *   
 *   graph.addPass("GBuffer", RDGPassFlags::Raster, [=](VkCommandBuffer cmd, RenderGraph& g) {
 *       // Render to gbuffer
 *   })
 *   .writeTexture(gbufferAlbedo, RDGAccessType::RTV)
 *   .writeTexture(gbufferNormal, RDGAccessType::RTV);
 *   
 *   graph.addPass("Lighting", RDGPassFlags::Compute, [=](VkCommandBuffer cmd, RenderGraph& g) {
 *       // Compute lighting
 *   })
 *   .readTexture(gbufferAlbedo, RDGAccessType::SRVCompute)
 *   .readTexture(gbufferNormal, RDGAccessType::SRVCompute);
 *   
 *   graph.compile();
 *   graph.execute(cmd);
 */
class RenderGraph {
public:
    RenderGraph(VulkanContext& context);
    ~RenderGraph();
    
    // ========================================================================
    // RESOURCE CREATION
    // ========================================================================
    
    /**
     * Create a new transient texture
     */
    RDGTextureHandle createTexture(const std::string& name, const RDGTextureDesc& desc);
    
    /**
     * Create a new transient buffer
     */
    RDGBufferHandle createBuffer(const std::string& name, const RDGBufferDesc& desc);
    
    /**
     * Register an external texture (not managed by graph)
     */
    RDGTextureHandle registerExternalTexture(
        const std::string& name,
        VkImage image,
        VkImageView view,
        const RDGTextureDesc& desc,
        VkImageLayout currentLayout = VK_IMAGE_LAYOUT_UNDEFINED
    );
    
    /**
     * Register an external buffer
     */
    RDGBufferHandle registerExternalBuffer(
        const std::string& name,
        VkBuffer buffer,
        const RDGBufferDesc& desc
    );
    
    // ========================================================================
    // PASS CREATION
    // ========================================================================
    
    /**
     * Pass builder for fluent API
     */
    class PassBuilder {
    public:
        PassBuilder(RenderGraph& graph, RDGPassHandle passHandle);
        
        // Texture access
        PassBuilder& readTexture(RDGTextureHandle texture, RDGAccessType access, 
                                 uint32_t mip = 0, uint32_t mipCount = 1,
                                 uint32_t layer = 0, uint32_t layerCount = 1);
        PassBuilder& writeTexture(RDGTextureHandle texture, RDGAccessType access,
                                  uint32_t mip = 0, uint32_t mipCount = 1,
                                  uint32_t layer = 0, uint32_t layerCount = 1);
        
        // Buffer access
        PassBuilder& readBuffer(RDGBufferHandle buffer, RDGAccessType access);
        PassBuilder& writeBuffer(RDGBufferHandle buffer, RDGAccessType access);
        
        // Render target setup (for raster passes)
        PassBuilder& setRenderTarget(RDGTextureHandle texture, VkAttachmentLoadOp loadOp,
                                     VkAttachmentStoreOp storeOp, VkClearColorValue clearValue = {});
        PassBuilder& setDepthStencil(RDGTextureHandle texture, VkAttachmentLoadOp loadOp,
                                     VkAttachmentStoreOp storeOp, VkClearDepthStencilValue clearValue = {1.0f, 0});
        PassBuilder& setRenderExtent(uint32_t width, uint32_t height);
        
        // Get the pass handle
        RDGPassHandle getHandle() const { return passHandle; }
        
    private:
        RenderGraph& graph;
        RDGPassHandle passHandle;
    };
    
    /**
     * Add a new pass to the graph
     */
    PassBuilder addPass(const std::string& name, RDGPassFlags flags,
                        RDGPass::ExecuteFunction executeFunc);
    
    // ========================================================================
    // GRAPH EXECUTION
    // ========================================================================
    
    /**
     * Compile the graph - builds dependencies, allocates resources, plans barriers
     */
    void compile();
    
    /**
     * Execute the compiled graph
     */
    void execute(VkCommandBuffer cmd);
    
    /**
     * Reset the graph for next frame
     */
    void reset();
    
    // ========================================================================
    // RESOURCE ACCESS
    // ========================================================================
    
    RDGTexture* getTexture(RDGTextureHandle handle);
    const RDGTexture* getTexture(RDGTextureHandle handle) const;
    RDGBuffer* getBuffer(RDGBufferHandle handle);
    const RDGBuffer* getBuffer(RDGBufferHandle handle) const;
    RDGPass* getPass(RDGPassHandle handle);
    
    VkImage getTextureImage(RDGTextureHandle handle) const;
    VkImageView getTextureView(RDGTextureHandle handle) const;
    VkBuffer getBufferVk(RDGBufferHandle handle) const;
    VkDeviceAddress getBufferAddress(RDGBufferHandle handle) const;
    
    // ========================================================================
    // UTILITIES
    // ========================================================================
    
    VulkanContext& getContext() { return context; }
    PSOCache& getPSOCache() { return *psoCache; }
    RDGResourcePool& getResourcePool() { return *resourcePool; }
    
    uint64_t getCurrentFrame() const { return currentFrame; }
    
    // Debug/profiling
    void enableDebugOutput(bool enable) { debugOutput = enable; }
    void dumpGraph(const std::string& filename);
    
private:
    VulkanContext& context;
    
    // Resources
    std::vector<std::unique_ptr<RDGTexture>> textures;
    std::vector<std::unique_ptr<RDGBuffer>> buffers;
    std::vector<std::unique_ptr<RDGPass>> passes;
    
    // Resource lookup by name
    std::unordered_map<std::string, RDGTextureHandle> textureNameMap;
    std::unordered_map<std::string, RDGBufferHandle> bufferNameMap;
    
    // Execution state
    std::vector<RDGPassHandle> executionOrder;
    std::vector<RDGBarrierBatch> passBarriers;  // Barriers before each pass
    std::vector<RDGBarrierBatch> passEpilogueBarriers;  // Barriers after pass (for split barriers)
    
    // Subsystems
    std::unique_ptr<PSOCache> psoCache;
    std::unique_ptr<RDGResourcePool> resourcePool;
    
    // Frame tracking
    uint64_t currentFrame = 0;
    bool isCompiled = false;
    bool debugOutput = false;
    
    // ========================================================================
    // INTERNAL METHODS
    // ========================================================================
    
    void buildDependencies();
    void topologicalSort();
    void cullUnusedPasses();
    void allocateResources();
    void planBarriers();
    void mergeRenderPasses();
    
    RDGBarrierBatch computeBarriers(RDGPassHandle passHandle);
    VkImageMemoryBarrier2 createImageBarrier(
        RDGTexture* texture,
        uint32_t subresource,
        const RDGSubresourceState& oldState,
        const RDGSubresourceState& newState
    );
    VkBufferMemoryBarrier2 createBufferBarrier(
        RDGBuffer* buffer,
        const RDGSubresourceState& oldState,
        const RDGSubresourceState& newState
    );
    
    VkImageLayout getOptimalLayout(RDGAccessType access, VkFormat format);
    VkPipelineStageFlags2 getStageFlags(RDGAccessType access, RDGPassFlags passFlags);
    VkAccessFlags2 getAccessFlags(RDGAccessType access);
    
    void executePass(VkCommandBuffer cmd, RDGPass* pass);
    void beginRenderPass(VkCommandBuffer cmd, RDGPass* pass);
    void endRenderPass(VkCommandBuffer cmd, RDGPass* pass);
    
    // Resource creation helpers
    void createPhysicalTexture(RDGTexture* texture);
    void createPhysicalBuffer(RDGBuffer* buffer);
    
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
};

// ============================================================================
// UTILITY MACROS
// ============================================================================

#define RDG_EVENT_NAME(name) name

// Scope for automatic GPU profiling
class RDGEventScope {
public:
    RDGEventScope(VkCommandBuffer cmd, const char* name);
    ~RDGEventScope();
private:
    VkCommandBuffer cmd;
};

#define RDG_GPU_SCOPE(cmd, name) RDGEventScope _rdgScope##__LINE__(cmd, name)

