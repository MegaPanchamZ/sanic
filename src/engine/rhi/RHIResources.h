#pragma once
#include "RHITypes.h"
#include <vector>
#include <memory>
#include <cstdint>
#include <limits>

namespace Sanic {

//=============================================================================
// Resource Descriptors (Creation Parameters)
//=============================================================================

// Buffer creation descriptor
struct RHIBufferDesc {
    uint64_t size = 0;
    RHIBufferUsage usage = RHIBufferUsage::None;
    RHIMemoryType memoryType = RHIMemoryType::Default;
    bool persistentlyMapped = false;    // Keep mapped for upload heaps
    const char* debugName = nullptr;
    
    // Helper constructors
    static RHIBufferDesc Vertex(uint64_t size, const char* name = nullptr) {
        RHIBufferDesc desc;
        desc.size = size;
        desc.usage = RHIBufferUsage::VertexBuffer | RHIBufferUsage::TransferDst;
        desc.memoryType = RHIMemoryType::Default;
        desc.debugName = name;
        return desc;
    }
    
    static RHIBufferDesc Index(uint64_t size, const char* name = nullptr) {
        RHIBufferDesc desc;
        desc.size = size;
        desc.usage = RHIBufferUsage::IndexBuffer | RHIBufferUsage::TransferDst;
        desc.memoryType = RHIMemoryType::Default;
        desc.debugName = name;
        return desc;
    }
    
    static RHIBufferDesc Uniform(uint64_t size, const char* name = nullptr) {
        RHIBufferDesc desc;
        desc.size = size;
        desc.usage = RHIBufferUsage::UniformBuffer;
        desc.memoryType = RHIMemoryType::Upload;
        desc.persistentlyMapped = true;
        desc.debugName = name;
        return desc;
    }
    
    static RHIBufferDesc Storage(uint64_t size, const char* name = nullptr) {
        RHIBufferDesc desc;
        desc.size = size;
        desc.usage = RHIBufferUsage::StorageBuffer | RHIBufferUsage::TransferDst;
        desc.memoryType = RHIMemoryType::Default;
        desc.debugName = name;
        return desc;
    }
    
    static RHIBufferDesc Staging(uint64_t size, const char* name = nullptr) {
        RHIBufferDesc desc;
        desc.size = size;
        desc.usage = RHIBufferUsage::TransferSrc;
        desc.memoryType = RHIMemoryType::Upload;
        desc.persistentlyMapped = true;
        desc.debugName = name;
        return desc;
    }
    
    static RHIBufferDesc Readback(uint64_t size, const char* name = nullptr) {
        RHIBufferDesc desc;
        desc.size = size;
        desc.usage = RHIBufferUsage::TransferDst;
        desc.memoryType = RHIMemoryType::Readback;
        desc.debugName = name;
        return desc;
    }
    
    static RHIBufferDesc Indirect(uint64_t size, const char* name = nullptr) {
        RHIBufferDesc desc;
        desc.size = size;
        desc.usage = RHIBufferUsage::IndirectBuffer | RHIBufferUsage::StorageBuffer | RHIBufferUsage::TransferDst;
        desc.memoryType = RHIMemoryType::Default;
        desc.debugName = name;
        return desc;
    }
};

// Texture creation descriptor
struct RHITextureDesc {
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t depth = 1;
    uint32_t mipLevels = 1;
    uint32_t arrayLayers = 1;
    RHIFormat format = RHIFormat::R8G8B8A8_UNORM;
    RHITextureUsage usage = RHITextureUsage::Sampled;
    RHITextureDimension dimension = RHITextureDimension::Texture2D;
    RHISampleCount sampleCount = RHISampleCount::Count1;
    const char* debugName = nullptr;
    
    // Calculate full mip chain
    uint32_t calculateMipLevels() const {
        uint32_t maxDim = std::max(width, std::max(height, depth));
        uint32_t levels = 1;
        while (maxDim > 1) {
            maxDim >>= 1;
            levels++;
        }
        return levels;
    }
    
    // Helper constructors
    static RHITextureDesc Texture2D(uint32_t w, uint32_t h, RHIFormat fmt,
                                    RHITextureUsage use = RHITextureUsage::Sampled,
                                    uint32_t mips = 1, const char* name = nullptr) {
        RHITextureDesc desc;
        desc.width = w;
        desc.height = h;
        desc.format = fmt;
        desc.usage = use;
        desc.mipLevels = mips;
        desc.dimension = RHITextureDimension::Texture2D;
        desc.debugName = name;
        return desc;
    }
    
    static RHITextureDesc RenderTarget2D(uint32_t w, uint32_t h, RHIFormat fmt,
                                         const char* name = nullptr) {
        RHITextureDesc desc;
        desc.width = w;
        desc.height = h;
        desc.format = fmt;
        desc.usage = RHITextureUsage::RenderTarget | RHITextureUsage::Sampled;
        desc.dimension = RHITextureDimension::Texture2D;
        desc.debugName = name;
        return desc;
    }
    
    static RHITextureDesc DepthStencil2D(uint32_t w, uint32_t h, 
                                         RHIFormat fmt = RHIFormat::D32_FLOAT,
                                         const char* name = nullptr) {
        RHITextureDesc desc;
        desc.width = w;
        desc.height = h;
        desc.format = fmt;
        desc.usage = RHITextureUsage::DepthStencil | RHITextureUsage::Sampled;
        desc.dimension = RHITextureDimension::Texture2D;
        desc.debugName = name;
        return desc;
    }
    
    static RHITextureDesc Storage2D(uint32_t w, uint32_t h, RHIFormat fmt,
                                    const char* name = nullptr) {
        RHITextureDesc desc;
        desc.width = w;
        desc.height = h;
        desc.format = fmt;
        desc.usage = RHITextureUsage::Storage | RHITextureUsage::Sampled;
        desc.dimension = RHITextureDimension::Texture2D;
        desc.debugName = name;
        return desc;
    }
    
    static RHITextureDesc Cubemap(uint32_t size, RHIFormat fmt,
                                  uint32_t mips = 1, const char* name = nullptr) {
        RHITextureDesc desc;
        desc.width = size;
        desc.height = size;
        desc.arrayLayers = 6;
        desc.format = fmt;
        desc.usage = RHITextureUsage::Sampled | RHITextureUsage::TransferDst;
        desc.mipLevels = mips;
        desc.dimension = RHITextureDimension::TextureCube;
        desc.debugName = name;
        return desc;
    }
    
    static RHITextureDesc Texture3D(uint32_t w, uint32_t h, uint32_t d, RHIFormat fmt,
                                    RHITextureUsage use = RHITextureUsage::Sampled,
                                    const char* name = nullptr) {
        RHITextureDesc desc;
        desc.width = w;
        desc.height = h;
        desc.depth = d;
        desc.format = fmt;
        desc.usage = use;
        desc.dimension = RHITextureDimension::Texture3D;
        desc.debugName = name;
        return desc;
    }
};

// Sampler creation descriptor
struct RHISamplerDesc {
    RHIFilter minFilter = RHIFilter::Linear;
    RHIFilter magFilter = RHIFilter::Linear;
    RHIMipmapMode mipFilter = RHIMipmapMode::Linear;
    RHIAddressMode addressU = RHIAddressMode::Repeat;
    RHIAddressMode addressV = RHIAddressMode::Repeat;
    RHIAddressMode addressW = RHIAddressMode::Repeat;
    float mipLodBias = 0.0f;
    bool anisotropyEnable = true;
    float maxAnisotropy = 16.0f;
    bool compareEnable = false;
    RHICompareOp compareOp = RHICompareOp::Never;
    float minLod = 0.0f;
    float maxLod = 1000.0f;
    RHIBorderColor borderColor = RHIBorderColor::OpaqueBlack;
    bool unnormalizedCoordinates = false;
    
    // Common presets
    static RHISamplerDesc PointClamp() {
        RHISamplerDesc desc;
        desc.minFilter = RHIFilter::Nearest;
        desc.magFilter = RHIFilter::Nearest;
        desc.mipFilter = RHIMipmapMode::Nearest;
        desc.addressU = RHIAddressMode::ClampToEdge;
        desc.addressV = RHIAddressMode::ClampToEdge;
        desc.addressW = RHIAddressMode::ClampToEdge;
        desc.anisotropyEnable = false;
        return desc;
    }
    
    static RHISamplerDesc PointRepeat() {
        RHISamplerDesc desc;
        desc.minFilter = RHIFilter::Nearest;
        desc.magFilter = RHIFilter::Nearest;
        desc.mipFilter = RHIMipmapMode::Nearest;
        desc.anisotropyEnable = false;
        return desc;
    }
    
    static RHISamplerDesc LinearClamp() {
        RHISamplerDesc desc;
        desc.addressU = RHIAddressMode::ClampToEdge;
        desc.addressV = RHIAddressMode::ClampToEdge;
        desc.addressW = RHIAddressMode::ClampToEdge;
        return desc;
    }
    
    static RHISamplerDesc LinearRepeat() {
        return RHISamplerDesc();
    }
    
    static RHISamplerDesc Anisotropic(float maxAniso = 16.0f) {
        RHISamplerDesc desc;
        desc.anisotropyEnable = true;
        desc.maxAnisotropy = maxAniso;
        return desc;
    }
    
    static RHISamplerDesc Shadow() {
        RHISamplerDesc desc;
        desc.minFilter = RHIFilter::Linear;
        desc.magFilter = RHIFilter::Linear;
        desc.mipFilter = RHIMipmapMode::Nearest;
        desc.addressU = RHIAddressMode::ClampToBorder;
        desc.addressV = RHIAddressMode::ClampToBorder;
        desc.addressW = RHIAddressMode::ClampToBorder;
        desc.compareEnable = true;
        desc.compareOp = RHICompareOp::LessOrEqual;
        desc.borderColor = RHIBorderColor::OpaqueWhite;
        desc.anisotropyEnable = false;
        return desc;
    }
};

//=============================================================================
// Pipeline Descriptors
//=============================================================================

// Vertex attribute description
struct RHIVertexAttribute {
    uint32_t location;
    uint32_t binding;
    RHIFormat format;
    uint32_t offset;
};

// Vertex binding description
struct RHIVertexBinding {
    uint32_t binding;
    uint32_t stride;
    RHIVertexInputRate inputRate = RHIVertexInputRate::PerVertex;
};

// Depth stencil state
struct RHIDepthStencilState {
    bool depthTestEnable = true;
    bool depthWriteEnable = true;
    RHICompareOp depthCompareOp = RHICompareOp::Less;
    bool depthBoundsTestEnable = false;
    bool stencilTestEnable = false;
    
    struct StencilOpState {
        RHIStencilOp failOp = RHIStencilOp::Keep;
        RHIStencilOp passOp = RHIStencilOp::Keep;
        RHIStencilOp depthFailOp = RHIStencilOp::Keep;
        RHICompareOp compareOp = RHICompareOp::Always;
        uint32_t compareMask = 0xFF;
        uint32_t writeMask = 0xFF;
        uint32_t reference = 0;
    };
    
    StencilOpState front;
    StencilOpState back;
    float minDepthBounds = 0.0f;
    float maxDepthBounds = 1.0f;
    
    static RHIDepthStencilState Disabled() {
        RHIDepthStencilState state;
        state.depthTestEnable = false;
        state.depthWriteEnable = false;
        return state;
    }
    
    static RHIDepthStencilState DepthReadOnly() {
        RHIDepthStencilState state;
        state.depthWriteEnable = false;
        return state;
    }
    
    static RHIDepthStencilState ReverseZ() {
        RHIDepthStencilState state;
        state.depthCompareOp = RHICompareOp::Greater;
        return state;
    }
};

// Blend state per render target
struct RHIBlendState {
    bool blendEnable = false;
    RHIBlendFactor srcColorBlendFactor = RHIBlendFactor::One;
    RHIBlendFactor dstColorBlendFactor = RHIBlendFactor::Zero;
    RHIBlendOp colorBlendOp = RHIBlendOp::Add;
    RHIBlendFactor srcAlphaBlendFactor = RHIBlendFactor::One;
    RHIBlendFactor dstAlphaBlendFactor = RHIBlendFactor::Zero;
    RHIBlendOp alphaBlendOp = RHIBlendOp::Add;
    RHIColorWriteMask colorWriteMask = RHIColorWriteMask::All;
    
    static RHIBlendState Opaque() {
        return RHIBlendState();
    }
    
    static RHIBlendState AlphaBlend() {
        RHIBlendState state;
        state.blendEnable = true;
        state.srcColorBlendFactor = RHIBlendFactor::SrcAlpha;
        state.dstColorBlendFactor = RHIBlendFactor::OneMinusSrcAlpha;
        state.srcAlphaBlendFactor = RHIBlendFactor::One;
        state.dstAlphaBlendFactor = RHIBlendFactor::OneMinusSrcAlpha;
        return state;
    }
    
    static RHIBlendState Additive() {
        RHIBlendState state;
        state.blendEnable = true;
        state.srcColorBlendFactor = RHIBlendFactor::One;
        state.dstColorBlendFactor = RHIBlendFactor::One;
        state.srcAlphaBlendFactor = RHIBlendFactor::One;
        state.dstAlphaBlendFactor = RHIBlendFactor::One;
        return state;
    }
    
    static RHIBlendState Premultiplied() {
        RHIBlendState state;
        state.blendEnable = true;
        state.srcColorBlendFactor = RHIBlendFactor::One;
        state.dstColorBlendFactor = RHIBlendFactor::OneMinusSrcAlpha;
        state.srcAlphaBlendFactor = RHIBlendFactor::One;
        state.dstAlphaBlendFactor = RHIBlendFactor::OneMinusSrcAlpha;
        return state;
    }
};

// Rasterizer state
struct RHIRasterizerState {
    RHIFillMode fillMode = RHIFillMode::Solid;
    RHICullMode cullMode = RHICullMode::Back;
    RHIFrontFace frontFace = RHIFrontFace::CounterClockwise;
    bool depthClampEnable = false;
    bool depthBiasEnable = false;
    float depthBiasConstantFactor = 0.0f;
    float depthBiasClamp = 0.0f;
    float depthBiasSlopeFactor = 0.0f;
    float lineWidth = 1.0f;
    bool conservativeRasterization = false;
    
    static RHIRasterizerState Default() {
        return RHIRasterizerState();
    }
    
    static RHIRasterizerState NoCull() {
        RHIRasterizerState state;
        state.cullMode = RHICullMode::None;
        return state;
    }
    
    static RHIRasterizerState FrontCull() {
        RHIRasterizerState state;
        state.cullMode = RHICullMode::Front;
        return state;
    }
    
    static RHIRasterizerState Wireframe() {
        RHIRasterizerState state;
        state.fillMode = RHIFillMode::Wireframe;
        state.cullMode = RHICullMode::None;
        return state;
    }
    
    static RHIRasterizerState ShadowMap() {
        RHIRasterizerState state;
        state.depthBiasEnable = true;
        state.depthBiasConstantFactor = 1.25f;
        state.depthBiasSlopeFactor = 1.75f;
        return state;
    }
};

// Multisample state
struct RHIMultisampleState {
    RHISampleCount sampleCount = RHISampleCount::Count1;
    bool sampleShadingEnable = false;
    float minSampleShading = 1.0f;
    bool alphaToCoverageEnable = false;
    bool alphaToOneEnable = false;
};

// Graphics pipeline descriptor
struct RHIGraphicsPipelineDesc {
    // Shader bytecode (SPIR-V for Vulkan, DXIL for D3D12)
    std::vector<uint32_t> vertexShaderSpirv;
    std::vector<uint32_t> fragmentShaderSpirv;
    std::vector<uint32_t> geometryShaderSpirv;   // Optional
    std::vector<uint32_t> hullShaderSpirv;       // Optional (tessellation)
    std::vector<uint32_t> domainShaderSpirv;     // Optional (tessellation)
    
    // For mesh shader pipeline
    std::vector<uint32_t> taskShaderSpirv;       // Optional
    std::vector<uint32_t> meshShaderSpirv;       // Optional
    
    // Vertex input
    std::vector<RHIVertexAttribute> vertexAttributes;
    std::vector<RHIVertexBinding> vertexBindings;
    
    // Fixed function state
    RHIRasterizerState rasterizerState;
    RHIDepthStencilState depthStencilState;
    RHIMultisampleState multisampleState;
    std::vector<RHIBlendState> blendStates;   // One per render target
    
    // Primitive topology
    RHIPrimitiveTopology primitiveTopology = RHIPrimitiveTopology::TriangleList;
    uint32_t patchControlPoints = 0;  // For tessellation
    
    // Render target formats
    std::vector<RHIFormat> colorFormats;
    RHIFormat depthStencilFormat = RHIFormat::Unknown;
    
    // Push constants size
    uint32_t pushConstantsSize = 0;
    
    // Dynamic state (viewport/scissor always dynamic)
    bool dynamicLineWidth = false;
    bool dynamicDepthBias = false;
    bool dynamicBlendConstants = false;
    bool dynamicStencilReference = false;
    
    const char* debugName = nullptr;
    
    // Check if this is a mesh shader pipeline
    bool isMeshPipeline() const {
        return !meshShaderSpirv.empty();
    }
};

// Compute pipeline descriptor
struct RHIComputePipelineDesc {
    std::vector<uint32_t> computeShaderSpirv;
    uint32_t pushConstantsSize = 0;
    const char* debugName = nullptr;
};

// Ray tracing shader group
struct RHIRayTracingShaderGroup {
    enum class Type {
        General,        // Ray gen, miss, callable
        TrianglesHit,   // Closest hit + any hit for triangles
        ProceduralHit,  // Closest hit + any hit + intersection for procedurals
    };
    
    Type type = Type::General;
    uint32_t generalShader = ~0u;
    uint32_t closestHitShader = ~0u;
    uint32_t anyHitShader = ~0u;
    uint32_t intersectionShader = ~0u;
};

// Ray tracing pipeline descriptor
struct RHIRayTracingPipelineDesc {
    std::vector<std::vector<uint32_t>> shaderSpirv;  // All shaders
    std::vector<RHIShaderStage> shaderStages;        // Stage for each shader
    std::vector<RHIRayTracingShaderGroup> groups;
    uint32_t maxRecursionDepth = 1;
    uint32_t maxPayloadSize = 32;
    uint32_t maxAttributeSize = 8;
    uint32_t pushConstantsSize = 0;
    const char* debugName = nullptr;
};

//=============================================================================
// Resource Interfaces
//=============================================================================

// Base class for all RHI resources
class IRHIResource {
public:
    virtual ~IRHIResource() = default;
    virtual const char* getDebugName() const { return nullptr; }
    virtual void setDebugName(const char* name) {}
};

// Buffer resource
class IRHIBuffer : public IRHIResource {
public:
    virtual ~IRHIBuffer() = default;
    
    // Properties
    virtual uint64_t getSize() const = 0;
    virtual RHIBufferUsage getUsage() const = 0;
    virtual RHIMemoryType getMemoryType() const = 0;
    
    // Mapping (only valid for Upload/Readback heaps)
    virtual void* map() = 0;
    virtual void unmap() = 0;
    virtual void* getMappedPointer() const = 0;  // For persistently mapped buffers
    
    // For buffer device address / GPU virtual address
    virtual uint64_t getGPUAddress() const = 0;
};

// Texture resource
class IRHITexture : public IRHIResource {
public:
    virtual ~IRHITexture() = default;
    
    // Properties
    virtual uint32_t getWidth() const = 0;
    virtual uint32_t getHeight() const = 0;
    virtual uint32_t getDepth() const = 0;
    virtual uint32_t getMipLevels() const = 0;
    virtual uint32_t getArrayLayers() const = 0;
    virtual RHIFormat getFormat() const = 0;
    virtual RHITextureUsage getUsage() const = 0;
    virtual RHITextureDimension getDimension() const = 0;
    virtual RHISampleCount getSampleCount() const = 0;
};

// Texture view for binding
class IRHITextureView : public IRHIResource {
public:
    virtual ~IRHITextureView() = default;
    virtual IRHITexture* getTexture() const = 0;
    virtual RHIFormat getFormat() const = 0;
    virtual uint32_t getBaseMipLevel() const = 0;
    virtual uint32_t getMipLevelCount() const = 0;
    virtual uint32_t getBaseArrayLayer() const = 0;
    virtual uint32_t getArrayLayerCount() const = 0;
};

// Sampler
class IRHISampler : public IRHIResource {
public:
    virtual ~IRHISampler() = default;
};

// Pipeline (graphics, compute, or ray tracing)
class IRHIPipeline : public IRHIResource {
public:
    virtual ~IRHIPipeline() = default;
    virtual RHIPipelineType getType() const = 0;
};

// Fence for GPU-CPU synchronization
class IRHIFence : public IRHIResource {
public:
    virtual ~IRHIFence() = default;
    
    // Wait for the fence to be signaled (CPU blocks)
    virtual void wait(uint64_t timeout = std::numeric_limits<uint64_t>::max()) = 0;
    
    // Reset fence to unsignaled state
    virtual void reset() = 0;
    
    // Check if signaled without blocking
    virtual bool isSignaled() const = 0;
    
    // Get current value (for timeline semaphores)
    virtual uint64_t getValue() const = 0;
    
    // Signal from CPU (for timeline semaphores)
    virtual void signal(uint64_t value) = 0;
};

// Semaphore for GPU-GPU synchronization
class IRHISemaphore : public IRHIResource {
public:
    virtual ~IRHISemaphore() = default;
};

// Query pool
class IRHIQueryPool : public IRHIResource {
public:
    virtual ~IRHIQueryPool() = default;
    virtual uint32_t getQueryCount() const = 0;
    
    // Get results - returns false if not ready
    virtual bool getResults(uint32_t firstQuery, uint32_t queryCount,
                           void* data, size_t dataSize, 
                           size_t stride, bool wait = false) = 0;
};

// Acceleration structure (for ray tracing)
class IRHIAccelerationStructure : public IRHIResource {
public:
    virtual ~IRHIAccelerationStructure() = default;
    virtual uint64_t getGPUAddress() const = 0;
    virtual bool isTopLevel() const = 0;
};

//=============================================================================
// Render Pass Types
//=============================================================================

// Attachment description for render pass
struct RHIAttachmentDesc {
    RHIFormat format = RHIFormat::Unknown;
    RHISampleCount samples = RHISampleCount::Count1;
    RHILoadOp loadOp = RHILoadOp::Clear;
    RHIStoreOp storeOp = RHIStoreOp::Store;
    RHILoadOp stencilLoadOp = RHILoadOp::DontCare;
    RHIStoreOp stencilStoreOp = RHIStoreOp::DontCare;
    RHIResourceState initialState = RHIResourceState::Undefined;
    RHIResourceState finalState = RHIResourceState::ShaderResource;
};

// Render pass begin info
struct RHIRenderPassBeginInfo {
    IRHITexture** colorAttachments = nullptr;
    uint32_t colorAttachmentCount = 0;
    IRHITexture* depthStencilAttachment = nullptr;
    
    struct ClearValue {
        union {
            float color[4];
            struct { float depth; uint8_t stencil; } depthStencil;
        };
    };
    
    ClearValue* clearValues = nullptr;  // One per attachment
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

} // namespace Sanic
