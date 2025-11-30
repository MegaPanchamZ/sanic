#pragma once
#include "RHIResources.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Sanic {

//=============================================================================
// Viewport and Scissor
//=============================================================================

struct RHIViewport {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float minDepth = 0.0f;
    float maxDepth = 1.0f;
    
    RHIViewport() = default;
    RHIViewport(float w, float h) : width(w), height(h) {}
    RHIViewport(float x_, float y_, float w, float h, float minD = 0.0f, float maxD = 1.0f)
        : x(x_), y(y_), width(w), height(h), minDepth(minD), maxDepth(maxD) {}
};

struct RHIScissor {
    int32_t x = 0;
    int32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    
    RHIScissor() = default;
    RHIScissor(uint32_t w, uint32_t h) : width(w), height(h) {}
    RHIScissor(int32_t x_, int32_t y_, uint32_t w, uint32_t h)
        : x(x_), y(y_), width(w), height(h) {}
};

//=============================================================================
// Copy Operations
//=============================================================================

struct RHIBufferCopy {
    uint64_t srcOffset = 0;
    uint64_t dstOffset = 0;
    uint64_t size = 0;
};

struct RHITextureCopy {
    uint32_t srcMipLevel = 0;
    uint32_t srcArrayLayer = 0;
    int32_t srcOffsetX = 0;
    int32_t srcOffsetY = 0;
    int32_t srcOffsetZ = 0;
    
    uint32_t dstMipLevel = 0;
    uint32_t dstArrayLayer = 0;
    int32_t dstOffsetX = 0;
    int32_t dstOffsetY = 0;
    int32_t dstOffsetZ = 0;
    
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 1;
};

struct RHIBufferTextureCopy {
    uint64_t bufferOffset = 0;
    uint32_t bufferRowLength = 0;    // 0 = tightly packed
    uint32_t bufferImageHeight = 0;  // 0 = tightly packed
    
    uint32_t textureMipLevel = 0;
    uint32_t textureArrayLayer = 0;
    uint32_t textureArrayLayerCount = 1;
    int32_t textureOffsetX = 0;
    int32_t textureOffsetY = 0;
    int32_t textureOffsetZ = 0;
    
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 1;
};

//=============================================================================
// Resource Barriers
//=============================================================================

struct RHIBufferBarrier {
    IRHIBuffer* buffer = nullptr;
    RHIResourceState stateBefore = RHIResourceState::Undefined;
    RHIResourceState stateAfter = RHIResourceState::Undefined;
    uint64_t offset = 0;
    uint64_t size = ~0ull;  // Entire buffer
};

struct RHITextureBarrier {
    IRHITexture* texture = nullptr;
    RHIResourceState stateBefore = RHIResourceState::Undefined;
    RHIResourceState stateAfter = RHIResourceState::Undefined;
    uint32_t baseMipLevel = 0;
    uint32_t mipLevelCount = ~0u;  // All mips
    uint32_t baseArrayLayer = 0;
    uint32_t arrayLayerCount = ~0u;  // All layers
};

struct RHIGlobalBarrier {
    RHIResourceState stateBefore = RHIResourceState::Undefined;
    RHIResourceState stateAfter = RHIResourceState::Undefined;
};

// Unified barrier structure
struct RHIBarrier {
    enum class Type {
        Buffer,
        Texture,
        Global,
    };
    
    Type type = Type::Global;
    
    union {
        RHIBufferBarrier buffer;
        RHITextureBarrier texture;
        RHIGlobalBarrier global;
    };
    
    RHIBarrier() : type(Type::Global), global{} {}
    
    static RHIBarrier Buffer(IRHIBuffer* buf, RHIResourceState before, RHIResourceState after,
                             uint64_t offset = 0, uint64_t size = ~0ull) {
        RHIBarrier b;
        b.type = Type::Buffer;
        b.buffer.buffer = buf;
        b.buffer.stateBefore = before;
        b.buffer.stateAfter = after;
        b.buffer.offset = offset;
        b.buffer.size = size;
        return b;
    }
    
    static RHIBarrier Texture(IRHITexture* tex, RHIResourceState before, RHIResourceState after,
                              uint32_t baseMip = 0, uint32_t mipCount = ~0u,
                              uint32_t baseLayer = 0, uint32_t layerCount = ~0u) {
        RHIBarrier b;
        b.type = Type::Texture;
        b.texture.texture = tex;
        b.texture.stateBefore = before;
        b.texture.stateAfter = after;
        b.texture.baseMipLevel = baseMip;
        b.texture.mipLevelCount = mipCount;
        b.texture.baseArrayLayer = baseLayer;
        b.texture.arrayLayerCount = layerCount;
        return b;
    }
    
    static RHIBarrier Global(RHIResourceState before, RHIResourceState after) {
        RHIBarrier b;
        b.type = Type::Global;
        b.global.stateBefore = before;
        b.global.stateAfter = after;
        return b;
    }
    
    // Common transitions
    static RHIBarrier TextureToRenderTarget(IRHITexture* tex, RHIResourceState from = RHIResourceState::Undefined) {
        return Texture(tex, from, RHIResourceState::RenderTarget);
    }
    
    static RHIBarrier TextureToShaderResource(IRHITexture* tex, RHIResourceState from = RHIResourceState::RenderTarget) {
        return Texture(tex, from, RHIResourceState::ShaderResource);
    }
    
    static RHIBarrier TextureToUnorderedAccess(IRHITexture* tex, RHIResourceState from = RHIResourceState::ShaderResource) {
        return Texture(tex, from, RHIResourceState::UnorderedAccess);
    }
    
    static RHIBarrier TextureToPresent(IRHITexture* tex, RHIResourceState from = RHIResourceState::RenderTarget) {
        return Texture(tex, from, RHIResourceState::Present);
    }
    
    static RHIBarrier TextureToCopySrc(IRHITexture* tex, RHIResourceState from = RHIResourceState::ShaderResource) {
        return Texture(tex, from, RHIResourceState::CopySrc);
    }
    
    static RHIBarrier TextureToCopyDst(IRHITexture* tex, RHIResourceState from = RHIResourceState::Undefined) {
        return Texture(tex, from, RHIResourceState::CopyDst);
    }
};

//=============================================================================
// Draw/Dispatch Commands
//=============================================================================

struct RHIDrawArguments {
    uint32_t vertexCount = 0;
    uint32_t instanceCount = 1;
    uint32_t firstVertex = 0;
    uint32_t firstInstance = 0;
};

struct RHIDrawIndexedArguments {
    uint32_t indexCount = 0;
    uint32_t instanceCount = 1;
    uint32_t firstIndex = 0;
    int32_t vertexOffset = 0;
    uint32_t firstInstance = 0;
};

struct RHIDispatchArguments {
    uint32_t groupCountX = 1;
    uint32_t groupCountY = 1;
    uint32_t groupCountZ = 1;
};

struct RHIDispatchMeshArguments {
    uint32_t groupCountX = 1;
    uint32_t groupCountY = 1;
    uint32_t groupCountZ = 1;
};

//=============================================================================
// Ray Tracing Types
//=============================================================================

struct RHIDispatchRaysDesc {
    // Shader binding table regions
    struct {
        IRHIBuffer* buffer = nullptr;
        uint64_t offset = 0;
        uint64_t size = 0;
        uint64_t stride = 0;
    } rayGenShaderTable, missShaderTable, hitGroupTable, callableShaderTable;
    
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t depth = 1;
};

// Acceleration structure build info
struct RHIAccelerationStructureGeometry {
    enum class Type {
        Triangles,
        AABBs,
        Instances,
    };
    
    Type type = Type::Triangles;
    bool opaque = true;
    
    // For triangles
    struct {
        IRHIBuffer* vertexBuffer = nullptr;
        uint64_t vertexOffset = 0;
        uint32_t vertexCount = 0;
        uint32_t vertexStride = 0;
        RHIFormat vertexFormat = RHIFormat::R32G32B32_FLOAT;
        
        IRHIBuffer* indexBuffer = nullptr;
        uint64_t indexOffset = 0;
        uint32_t indexCount = 0;
        RHIIndexType indexType = RHIIndexType::UInt32;
        
        IRHIBuffer* transformBuffer = nullptr;  // Optional 3x4 transform
        uint64_t transformOffset = 0;
    } triangles;
    
    // For AABBs
    struct {
        IRHIBuffer* buffer = nullptr;
        uint64_t offset = 0;
        uint32_t count = 0;
        uint32_t stride = 0;
    } aabbs;
    
    // For instances (TLAS)
    struct {
        IRHIBuffer* buffer = nullptr;
        uint64_t offset = 0;
        uint32_t count = 0;
    } instances;
};

struct RHIAccelerationStructureBuildInfo {
    bool isTopLevel = false;
    bool allowUpdate = false;
    bool preferFastTrace = true;
    bool preferFastBuild = false;
    
    std::vector<RHIAccelerationStructureGeometry> geometries;
    
    IRHIBuffer* scratchBuffer = nullptr;
    uint64_t scratchOffset = 0;
    
    IRHIAccelerationStructure* destination = nullptr;
    IRHIAccelerationStructure* source = nullptr;  // For updates
};

//=============================================================================
// Command List Interface
//=============================================================================

class IRHICommandList : public IRHIResource {
public:
    virtual ~IRHICommandList() = default;
    
    //-------------------------------------------------------------------------
    // Lifecycle
    //-------------------------------------------------------------------------
    
    // Begin recording commands
    virtual void begin() = 0;
    
    // End recording commands
    virtual void end() = 0;
    
    // Reset command list for reuse (must not be in flight on GPU)
    virtual void reset() = 0;
    
    //-------------------------------------------------------------------------
    // Resource Barriers
    //-------------------------------------------------------------------------
    
    // Insert resource barriers
    virtual void barrier(const RHIBarrier* barriers, uint32_t count) = 0;
    
    // Helper for single barrier
    void barrier(const RHIBarrier& b) { barrier(&b, 1); }
    
    // UAV barrier (for read-after-write hazards within compute)
    virtual void uavBarrier(IRHIBuffer* buffer = nullptr) = 0;
    virtual void uavBarrier(IRHITexture* texture = nullptr) = 0;
    
    //-------------------------------------------------------------------------
    // Render Pass (Graphics)
    //-------------------------------------------------------------------------
    
    // Begin render pass with render targets
    virtual void beginRenderPass(const RHIRenderPassBeginInfo& info) = 0;
    
    // End current render pass
    virtual void endRenderPass() = 0;
    
    // Helper for simple render pass
    void beginRenderPass(IRHITexture** colorTargets, uint32_t colorCount,
                        IRHITexture* depthTarget,
                        const glm::vec4* clearColors = nullptr,
                        float clearDepth = 1.0f, uint8_t clearStencil = 0) {
        RHIRenderPassBeginInfo info;
        info.colorAttachments = colorTargets;
        info.colorAttachmentCount = colorCount;
        info.depthStencilAttachment = depthTarget;
        
        // Set render area from first attachment
        if (colorCount > 0 && colorTargets[0]) {
            info.width = colorTargets[0]->getWidth();
            info.height = colorTargets[0]->getHeight();
        } else if (depthTarget) {
            info.width = depthTarget->getWidth();
            info.height = depthTarget->getHeight();
        }
        
        // Create clear values
        std::vector<RHIRenderPassBeginInfo::ClearValue> clears;
        if (clearColors) {
            for (uint32_t i = 0; i < colorCount; i++) {
                RHIRenderPassBeginInfo::ClearValue cv;
                cv.color[0] = clearColors[i].r;
                cv.color[1] = clearColors[i].g;
                cv.color[2] = clearColors[i].b;
                cv.color[3] = clearColors[i].a;
                clears.push_back(cv);
            }
        }
        if (depthTarget) {
            RHIRenderPassBeginInfo::ClearValue cv;
            cv.depthStencil.depth = clearDepth;
            cv.depthStencil.stencil = clearStencil;
            clears.push_back(cv);
        }
        info.clearValues = clears.empty() ? nullptr : clears.data();
        
        beginRenderPass(info);
    }
    
    //-------------------------------------------------------------------------
    // Pipeline State
    //-------------------------------------------------------------------------
    
    // Bind graphics, compute, or ray tracing pipeline
    virtual void setPipeline(IRHIPipeline* pipeline) = 0;
    
    // Dynamic state
    virtual void setViewport(const RHIViewport& viewport) = 0;
    virtual void setViewports(const RHIViewport* viewports, uint32_t count) = 0;
    virtual void setScissor(const RHIScissor& scissor) = 0;
    virtual void setScissors(const RHIScissor* scissors, uint32_t count) = 0;
    virtual void setBlendConstants(const float constants[4]) = 0;
    virtual void setStencilReference(uint32_t reference) = 0;
    virtual void setDepthBias(float constantFactor, float clamp, float slopeFactor) = 0;
    virtual void setLineWidth(float width) = 0;
    
    // Helper for viewport + scissor matching
    void setViewportAndScissor(uint32_t width, uint32_t height) {
        setViewport(RHIViewport(static_cast<float>(width), static_cast<float>(height)));
        setScissor(RHIScissor(width, height));
    }
    
    //-------------------------------------------------------------------------
    // Resource Binding
    //-------------------------------------------------------------------------
    
    // Vertex and index buffers
    virtual void setVertexBuffer(uint32_t slot, IRHIBuffer* buffer, uint64_t offset = 0) = 0;
    virtual void setVertexBuffers(uint32_t firstSlot, IRHIBuffer** buffers, 
                                  const uint64_t* offsets, uint32_t count) = 0;
    virtual void setIndexBuffer(IRHIBuffer* buffer, uint64_t offset = 0, 
                                RHIIndexType indexType = RHIIndexType::UInt32) = 0;
    
    // Push constants (root constants in D3D12)
    virtual void pushConstants(RHIShaderStage stages, uint32_t offset, 
                               uint32_t size, const void* data) = 0;
    
    // Descriptor binding (bindless approach - set/binding or root parameter)
    virtual void bindBuffer(uint32_t set, uint32_t binding, IRHIBuffer* buffer,
                           uint64_t offset = 0, uint64_t range = ~0ull) = 0;
    virtual void bindTexture(uint32_t set, uint32_t binding, IRHITexture* texture,
                            IRHISampler* sampler = nullptr) = 0;
    virtual void bindStorageTexture(uint32_t set, uint32_t binding, IRHITexture* texture,
                                    uint32_t mipLevel = 0) = 0;
    virtual void bindSampler(uint32_t set, uint32_t binding, IRHISampler* sampler) = 0;
    virtual void bindAccelerationStructure(uint32_t set, uint32_t binding,
                                           IRHIAccelerationStructure* as) = 0;
    
    //-------------------------------------------------------------------------
    // Draw Commands
    //-------------------------------------------------------------------------
    
    virtual void draw(uint32_t vertexCount, uint32_t instanceCount = 1,
                     uint32_t firstVertex = 0, uint32_t firstInstance = 0) = 0;
    
    virtual void drawIndexed(uint32_t indexCount, uint32_t instanceCount = 1,
                            uint32_t firstIndex = 0, int32_t vertexOffset = 0,
                            uint32_t firstInstance = 0) = 0;
    
    virtual void drawIndirect(IRHIBuffer* buffer, uint64_t offset,
                             uint32_t drawCount, uint32_t stride) = 0;
    
    virtual void drawIndexedIndirect(IRHIBuffer* buffer, uint64_t offset,
                                    uint32_t drawCount, uint32_t stride) = 0;
    
    // Draw with count buffer (multi-draw indirect count)
    virtual void drawIndirectCount(IRHIBuffer* argBuffer, uint64_t argOffset,
                                   IRHIBuffer* countBuffer, uint64_t countOffset,
                                   uint32_t maxDrawCount, uint32_t stride) = 0;
    
    virtual void drawIndexedIndirectCount(IRHIBuffer* argBuffer, uint64_t argOffset,
                                          IRHIBuffer* countBuffer, uint64_t countOffset,
                                          uint32_t maxDrawCount, uint32_t stride) = 0;
    
    //-------------------------------------------------------------------------
    // Mesh Shader Commands
    //-------------------------------------------------------------------------
    
    virtual void dispatchMesh(uint32_t groupCountX, uint32_t groupCountY = 1, 
                              uint32_t groupCountZ = 1) = 0;
    
    virtual void dispatchMeshIndirect(IRHIBuffer* buffer, uint64_t offset) = 0;
    
    virtual void dispatchMeshIndirectCount(IRHIBuffer* argBuffer, uint64_t argOffset,
                                           IRHIBuffer* countBuffer, uint64_t countOffset,
                                           uint32_t maxDispatchCount, uint32_t stride) = 0;
    
    //-------------------------------------------------------------------------
    // Compute Commands
    //-------------------------------------------------------------------------
    
    virtual void dispatch(uint32_t groupCountX, uint32_t groupCountY = 1, 
                         uint32_t groupCountZ = 1) = 0;
    
    virtual void dispatchIndirect(IRHIBuffer* buffer, uint64_t offset) = 0;
    
    //-------------------------------------------------------------------------
    // Ray Tracing Commands
    //-------------------------------------------------------------------------
    
    virtual void dispatchRays(const RHIDispatchRaysDesc& desc) = 0;
    
    virtual void buildAccelerationStructure(const RHIAccelerationStructureBuildInfo& info) = 0;
    
    virtual void copyAccelerationStructure(IRHIAccelerationStructure* dst,
                                           IRHIAccelerationStructure* src,
                                           bool compact = false) = 0;
    
    //-------------------------------------------------------------------------
    // Copy Commands
    //-------------------------------------------------------------------------
    
    virtual void copyBuffer(IRHIBuffer* src, IRHIBuffer* dst, 
                           const RHIBufferCopy* regions, uint32_t regionCount) = 0;
    
    // Helper for full buffer copy
    void copyBuffer(IRHIBuffer* src, IRHIBuffer* dst, uint64_t size = 0,
                   uint64_t srcOffset = 0, uint64_t dstOffset = 0) {
        RHIBufferCopy region;
        region.srcOffset = srcOffset;
        region.dstOffset = dstOffset;
        region.size = size > 0 ? size : src->getSize();
        copyBuffer(src, dst, &region, 1);
    }
    
    virtual void copyTexture(IRHITexture* src, IRHITexture* dst,
                            const RHITextureCopy* regions, uint32_t regionCount) = 0;
    
    // Helper for full texture copy (single mip/layer)
    void copyTexture(IRHITexture* src, IRHITexture* dst) {
        RHITextureCopy region;
        region.width = src->getWidth();
        region.height = src->getHeight();
        region.depth = src->getDepth();
        copyTexture(src, dst, &region, 1);
    }
    
    virtual void copyBufferToTexture(IRHIBuffer* src, IRHITexture* dst,
                                     const RHIBufferTextureCopy* regions, 
                                     uint32_t regionCount) = 0;
    
    // Helper for simple upload
    void copyBufferToTexture(IRHIBuffer* src, IRHITexture* dst,
                            uint32_t mipLevel = 0, uint32_t arrayLayer = 0) {
        RHIBufferTextureCopy region;
        region.textureMipLevel = mipLevel;
        region.textureArrayLayer = arrayLayer;
        region.width = std::max(1u, dst->getWidth() >> mipLevel);
        region.height = std::max(1u, dst->getHeight() >> mipLevel);
        region.depth = std::max(1u, dst->getDepth() >> mipLevel);
        copyBufferToTexture(src, dst, &region, 1);
    }
    
    virtual void copyTextureToBuffer(IRHITexture* src, IRHIBuffer* dst,
                                     const RHIBufferTextureCopy* regions,
                                     uint32_t regionCount) = 0;
    
    //-------------------------------------------------------------------------
    // Clear Commands
    //-------------------------------------------------------------------------
    
    virtual void clearBuffer(IRHIBuffer* buffer, uint32_t value,
                            uint64_t offset = 0, uint64_t size = ~0ull) = 0;
    
    virtual void clearTexture(IRHITexture* texture, const float color[4],
                             uint32_t baseMip = 0, uint32_t mipCount = ~0u,
                             uint32_t baseLayer = 0, uint32_t layerCount = ~0u) = 0;
    
    virtual void clearDepthStencil(IRHITexture* texture, float depth, uint8_t stencil,
                                   bool clearDepth = true, bool clearStencil = true) = 0;
    
    //-------------------------------------------------------------------------
    // Query Commands
    //-------------------------------------------------------------------------
    
    virtual void beginQuery(IRHIQueryPool* pool, uint32_t index) = 0;
    virtual void endQuery(IRHIQueryPool* pool, uint32_t index) = 0;
    virtual void resetQueryPool(IRHIQueryPool* pool, uint32_t firstQuery, uint32_t count) = 0;
    virtual void writeTimestamp(IRHIQueryPool* pool, uint32_t index) = 0;
    virtual void resolveQueryData(IRHIQueryPool* pool, uint32_t firstQuery, uint32_t count,
                                  IRHIBuffer* destination, uint64_t offset) = 0;
    
    //-------------------------------------------------------------------------
    // Debug Markers
    //-------------------------------------------------------------------------
    
    virtual void beginDebugLabel(const char* name, 
                                 const glm::vec4& color = glm::vec4(1.0f)) = 0;
    virtual void endDebugLabel() = 0;
    virtual void insertDebugLabel(const char* name,
                                  const glm::vec4& color = glm::vec4(1.0f)) = 0;
    
    // RAII debug scope helper
    class DebugScope {
    public:
        DebugScope(IRHICommandList* cmd, const char* name, 
                   const glm::vec4& color = glm::vec4(1.0f))
            : m_cmd(cmd) {
            m_cmd->beginDebugLabel(name, color);
        }
        ~DebugScope() {
            m_cmd->endDebugLabel();
        }
    private:
        IRHICommandList* m_cmd;
    };
    
    //-------------------------------------------------------------------------
    // Miscellaneous
    //-------------------------------------------------------------------------
    
    // Fill buffer with pattern
    virtual void fillBuffer(IRHIBuffer* buffer, uint64_t offset, 
                           uint64_t size, uint32_t data) = 0;
    
    // Update buffer inline (small updates)
    virtual void updateBuffer(IRHIBuffer* buffer, uint64_t offset,
                             uint64_t size, const void* data) = 0;
    
    // Generate mipmaps (using compute or blit)
    virtual void generateMipmaps(IRHITexture* texture) = 0;
    
    // Resolve MSAA texture
    virtual void resolveTexture(IRHITexture* src, IRHITexture* dst,
                               uint32_t srcMip = 0, uint32_t srcLayer = 0,
                               uint32_t dstMip = 0, uint32_t dstLayer = 0) = 0;
};

// Macro for debug scopes
#define RHI_DEBUG_SCOPE(cmd, name) \
    IRHICommandList::DebugScope _debugScope##__LINE__(cmd, name)

#define RHI_DEBUG_SCOPE_COLOR(cmd, name, color) \
    IRHICommandList::DebugScope _debugScope##__LINE__(cmd, name, color)

} // namespace Sanic
