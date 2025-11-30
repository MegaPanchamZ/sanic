#pragma once
#include <cstdint>
#include <string>

namespace Sanic {

// Forward declarations
class IRHIBuffer;
class IRHITexture;
class IRHIPipeline;
class IRHICommandList;
class IRHIFence;
class IRHISampler;

// Handle types (opaque)
using RHIBufferHandle = IRHIBuffer*;
using RHITextureHandle = IRHITexture*;
using RHIPipelineHandle = IRHIPipeline*;

// Bitwise operators helper macro
#define RHI_ENUM_FLAGS(EnumType) \
    inline EnumType operator|(EnumType a, EnumType b) { \
        return static_cast<EnumType>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b)); \
    } \
    inline EnumType operator&(EnumType a, EnumType b) { \
        return static_cast<EnumType>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b)); \
    } \
    inline EnumType& operator|=(EnumType& a, EnumType b) { \
        a = a | b; return a; \
    } \
    inline EnumType& operator&=(EnumType& a, EnumType b) { \
        a = a & b; return a; \
    } \
    inline bool hasFlag(EnumType flags, EnumType flag) { \
        return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag)) != 0; \
    }

// Texture and buffer formats matching Vulkan/DX12 concepts
enum class RHIFormat : uint32_t {
    Unknown = 0,
    
    // 8-bit formats
    R8_UNORM,
    R8_SNORM,
    R8_UINT,
    R8_SINT,
    
    // 16-bit formats
    R8G8_UNORM,
    R8G8_SNORM,
    R8G8_UINT,
    R8G8_SINT,
    R16_FLOAT,
    R16_UNORM,
    R16_SNORM,
    R16_UINT,
    R16_SINT,
    
    // 32-bit formats
    R8G8B8A8_UNORM,
    R8G8B8A8_SRGB,
    R8G8B8A8_SNORM,
    R8G8B8A8_UINT,
    R8G8B8A8_SINT,
    B8G8R8A8_UNORM,
    B8G8R8A8_SRGB,
    R10G10B10A2_UNORM,
    R10G10B10A2_UINT,
    R11G11B10_FLOAT,
    R16G16_FLOAT,
    R16G16_UNORM,
    R16G16_SNORM,
    R16G16_UINT,
    R16G16_SINT,
    R32_FLOAT,
    R32_UINT,
    R32_SINT,
    
    // 64-bit formats
    R16G16B16A16_FLOAT,
    R16G16B16A16_UNORM,
    R16G16B16A16_SNORM,
    R16G16B16A16_UINT,
    R16G16B16A16_SINT,
    R32G32_FLOAT,
    R32G32_UINT,
    R32G32_SINT,
    
    // 96-bit formats
    R32G32B32_FLOAT,
    R32G32B32_UINT,
    R32G32B32_SINT,
    
    // 128-bit formats
    R32G32B32A32_FLOAT,
    R32G32B32A32_UINT,
    R32G32B32A32_SINT,
    
    // Depth/stencil formats
    D16_UNORM,
    D24_UNORM_S8_UINT,
    D32_FLOAT,
    D32_FLOAT_S8_UINT,
    
    // Compressed formats (BC/DXT)
    BC1_UNORM,      // DXT1 RGB
    BC1_SRGB,
    BC2_UNORM,      // DXT3 RGBA
    BC2_SRGB,
    BC3_UNORM,      // DXT5 RGBA
    BC3_SRGB,
    BC4_UNORM,      // Single channel
    BC4_SNORM,
    BC5_UNORM,      // Two channels (normal maps)
    BC5_SNORM,
    BC6H_UF16,      // HDR RGB
    BC6H_SF16,
    BC7_UNORM,      // High quality RGBA
    BC7_SRGB,
    
    // ASTC formats (for mobile/future)
    ASTC_4x4_UNORM,
    ASTC_4x4_SRGB,
    ASTC_6x6_UNORM,
    ASTC_6x6_SRGB,
    ASTC_8x8_UNORM,
    ASTC_8x8_SRGB,
};

// Buffer usage flags
enum class RHIBufferUsage : uint32_t {
    None = 0,
    VertexBuffer = 1 << 0,
    IndexBuffer = 1 << 1,
    UniformBuffer = 1 << 2,
    StorageBuffer = 1 << 3,
    IndirectBuffer = 1 << 4,
    TransferSrc = 1 << 5,
    TransferDst = 1 << 6,
    AccelerationStructure = 1 << 7,
    ShaderBindingTable = 1 << 8,
    AccelerationStructureBuildInput = 1 << 9,
};
RHI_ENUM_FLAGS(RHIBufferUsage)

// Texture usage flags
enum class RHITextureUsage : uint32_t {
    None = 0,
    Sampled = 1 << 0,
    Storage = 1 << 1,
    RenderTarget = 1 << 2,
    DepthStencil = 1 << 3,
    TransferSrc = 1 << 4,
    TransferDst = 1 << 5,
    InputAttachment = 1 << 6,
    ShadingRate = 1 << 7,
};
RHI_ENUM_FLAGS(RHITextureUsage)

// Resource state for barriers
enum class RHIResourceState : uint32_t {
    Undefined,
    Common,
    VertexBuffer,
    IndexBuffer,
    UniformBuffer,
    ShaderResource,
    UnorderedAccess,
    RenderTarget,
    DepthWrite,
    DepthRead,
    IndirectArgument,
    CopySrc,
    CopyDst,
    Present,
    AccelerationStructure,
    AccelerationStructureBuildInput,
    RayTracingShaderResource,
    ShadingRateSource,
};

// Pipeline types
enum class RHIPipelineType : uint32_t {
    Graphics,
    Compute,
    RayTracing,
    MeshShader,
};

// Shader stages
enum class RHIShaderStage : uint32_t {
    None = 0,
    Vertex = 1 << 0,
    Hull = 1 << 1,           // Tessellation control
    Domain = 1 << 2,         // Tessellation evaluation
    Geometry = 1 << 3,
    Fragment = 1 << 4,
    Compute = 1 << 5,
    Task = 1 << 6,           // Mesh shader task/amplification
    Mesh = 1 << 7,           // Mesh shader
    RayGen = 1 << 8,
    Miss = 1 << 9,
    ClosestHit = 1 << 10,
    AnyHit = 1 << 11,
    Intersection = 1 << 12,
    Callable = 1 << 13,
    
    // Common combinations
    AllGraphics = Vertex | Hull | Domain | Geometry | Fragment,
    AllRayTracing = RayGen | Miss | ClosestHit | AnyHit | Intersection | Callable,
    All = 0xFFFFFFFF,
};
RHI_ENUM_FLAGS(RHIShaderStage)

// Queue types
enum class RHIQueueType : uint32_t {
    Graphics,
    Compute,
    Transfer,
    VideoDecode,
    VideoEncode,
};

// Descriptor types
enum class RHIDescriptorType : uint32_t {
    Sampler,
    SampledImage,
    StorageImage,
    UniformTexelBuffer,
    StorageTexelBuffer,
    UniformBuffer,
    StorageBuffer,
    UniformBufferDynamic,
    StorageBufferDynamic,
    InputAttachment,
    AccelerationStructure,
    CombinedImageSampler,
};

// Texture dimension/type
enum class RHITextureDimension : uint32_t {
    Texture1D,
    Texture2D,
    Texture3D,
    TextureCube,
    Texture1DArray,
    Texture2DArray,
    TextureCubeArray,
};

// Sample count for MSAA
enum class RHISampleCount : uint32_t {
    Count1 = 1,
    Count2 = 2,
    Count4 = 4,
    Count8 = 8,
    Count16 = 16,
    Count32 = 32,
    Count64 = 64,
};

// Primitive topology
enum class RHIPrimitiveTopology : uint32_t {
    PointList,
    LineList,
    LineStrip,
    TriangleList,
    TriangleStrip,
    TriangleFan,
    LineListWithAdjacency,
    LineStripWithAdjacency,
    TriangleListWithAdjacency,
    TriangleStripWithAdjacency,
    PatchList,
};

// Memory heap types
enum class RHIMemoryType : uint32_t {
    Default,        // GPU-only memory, fastest for GPU access
    Upload,         // CPU-writable, for uploading data to GPU
    Readback,       // CPU-readable, for reading GPU results
};

// Comparison function
enum class RHICompareOp : uint32_t {
    Never,
    Less,
    Equal,
    LessOrEqual,
    Greater,
    NotEqual,
    GreaterOrEqual,
    Always,
};

// Stencil operation
enum class RHIStencilOp : uint32_t {
    Keep,
    Zero,
    Replace,
    IncrementClamp,
    DecrementClamp,
    Invert,
    IncrementWrap,
    DecrementWrap,
};

// Blend factor
enum class RHIBlendFactor : uint32_t {
    Zero,
    One,
    SrcColor,
    OneMinusSrcColor,
    DstColor,
    OneMinusDstColor,
    SrcAlpha,
    OneMinusSrcAlpha,
    DstAlpha,
    OneMinusDstAlpha,
    ConstantColor,
    OneMinusConstantColor,
    ConstantAlpha,
    OneMinusConstantAlpha,
    SrcAlphaSaturate,
    Src1Color,
    OneMinusSrc1Color,
    Src1Alpha,
    OneMinusSrc1Alpha,
};

// Blend operation
enum class RHIBlendOp : uint32_t {
    Add,
    Subtract,
    ReverseSubtract,
    Min,
    Max,
};

// Color write mask
enum class RHIColorWriteMask : uint32_t {
    None = 0,
    Red = 1 << 0,
    Green = 1 << 1,
    Blue = 1 << 2,
    Alpha = 1 << 3,
    All = Red | Green | Blue | Alpha,
};
RHI_ENUM_FLAGS(RHIColorWriteMask)

// Cull mode
enum class RHICullMode : uint32_t {
    None,
    Front,
    Back,
};

// Fill mode
enum class RHIFillMode : uint32_t {
    Solid,
    Wireframe,
};

// Front face winding
enum class RHIFrontFace : uint32_t {
    CounterClockwise,
    Clockwise,
};

// Sampler filter
enum class RHIFilter : uint32_t {
    Nearest,
    Linear,
};

// Sampler mipmap mode
enum class RHIMipmapMode : uint32_t {
    Nearest,
    Linear,
};

// Sampler address mode
enum class RHIAddressMode : uint32_t {
    Repeat,
    MirroredRepeat,
    ClampToEdge,
    ClampToBorder,
    MirrorClampToEdge,
};

// Border color for samplers
enum class RHIBorderColor : uint32_t {
    TransparentBlack,
    OpaqueBlack,
    OpaqueWhite,
};

// Index buffer format
enum class RHIIndexType : uint32_t {
    UInt16,
    UInt32,
};

// Load and store operations for attachments
enum class RHILoadOp : uint32_t {
    Load,
    Clear,
    DontCare,
};

enum class RHIStoreOp : uint32_t {
    Store,
    DontCare,
};

// Vertex input rate
enum class RHIVertexInputRate : uint32_t {
    PerVertex,
    PerInstance,
};

// Helper functions for format properties
inline uint32_t GetFormatSize(RHIFormat format) {
    switch (format) {
        case RHIFormat::R8_UNORM:
        case RHIFormat::R8_SNORM:
        case RHIFormat::R8_UINT:
        case RHIFormat::R8_SINT:
            return 1;
            
        case RHIFormat::R8G8_UNORM:
        case RHIFormat::R8G8_SNORM:
        case RHIFormat::R8G8_UINT:
        case RHIFormat::R8G8_SINT:
        case RHIFormat::R16_FLOAT:
        case RHIFormat::R16_UNORM:
        case RHIFormat::R16_SNORM:
        case RHIFormat::R16_UINT:
        case RHIFormat::R16_SINT:
        case RHIFormat::D16_UNORM:
            return 2;
            
        case RHIFormat::R8G8B8A8_UNORM:
        case RHIFormat::R8G8B8A8_SRGB:
        case RHIFormat::R8G8B8A8_SNORM:
        case RHIFormat::R8G8B8A8_UINT:
        case RHIFormat::R8G8B8A8_SINT:
        case RHIFormat::B8G8R8A8_UNORM:
        case RHIFormat::B8G8R8A8_SRGB:
        case RHIFormat::R10G10B10A2_UNORM:
        case RHIFormat::R10G10B10A2_UINT:
        case RHIFormat::R11G11B10_FLOAT:
        case RHIFormat::R16G16_FLOAT:
        case RHIFormat::R16G16_UNORM:
        case RHIFormat::R16G16_SNORM:
        case RHIFormat::R16G16_UINT:
        case RHIFormat::R16G16_SINT:
        case RHIFormat::R32_FLOAT:
        case RHIFormat::R32_UINT:
        case RHIFormat::R32_SINT:
        case RHIFormat::D24_UNORM_S8_UINT:
        case RHIFormat::D32_FLOAT:
            return 4;
            
        case RHIFormat::R16G16B16A16_FLOAT:
        case RHIFormat::R16G16B16A16_UNORM:
        case RHIFormat::R16G16B16A16_SNORM:
        case RHIFormat::R16G16B16A16_UINT:
        case RHIFormat::R16G16B16A16_SINT:
        case RHIFormat::R32G32_FLOAT:
        case RHIFormat::R32G32_UINT:
        case RHIFormat::R32G32_SINT:
        case RHIFormat::D32_FLOAT_S8_UINT:
            return 8;
            
        case RHIFormat::R32G32B32_FLOAT:
        case RHIFormat::R32G32B32_UINT:
        case RHIFormat::R32G32B32_SINT:
            return 12;
            
        case RHIFormat::R32G32B32A32_FLOAT:
        case RHIFormat::R32G32B32A32_UINT:
        case RHIFormat::R32G32B32A32_SINT:
            return 16;
            
        default:
            return 0; // Unknown or compressed format
    }
}

inline bool IsDepthFormat(RHIFormat format) {
    switch (format) {
        case RHIFormat::D16_UNORM:
        case RHIFormat::D24_UNORM_S8_UINT:
        case RHIFormat::D32_FLOAT:
        case RHIFormat::D32_FLOAT_S8_UINT:
            return true;
        default:
            return false;
    }
}

inline bool IsStencilFormat(RHIFormat format) {
    switch (format) {
        case RHIFormat::D24_UNORM_S8_UINT:
        case RHIFormat::D32_FLOAT_S8_UINT:
            return true;
        default:
            return false;
    }
}

inline bool IsCompressedFormat(RHIFormat format) {
    switch (format) {
        case RHIFormat::BC1_UNORM:
        case RHIFormat::BC1_SRGB:
        case RHIFormat::BC2_UNORM:
        case RHIFormat::BC2_SRGB:
        case RHIFormat::BC3_UNORM:
        case RHIFormat::BC3_SRGB:
        case RHIFormat::BC4_UNORM:
        case RHIFormat::BC4_SNORM:
        case RHIFormat::BC5_UNORM:
        case RHIFormat::BC5_SNORM:
        case RHIFormat::BC6H_UF16:
        case RHIFormat::BC6H_SF16:
        case RHIFormat::BC7_UNORM:
        case RHIFormat::BC7_SRGB:
        case RHIFormat::ASTC_4x4_UNORM:
        case RHIFormat::ASTC_4x4_SRGB:
        case RHIFormat::ASTC_6x6_UNORM:
        case RHIFormat::ASTC_6x6_SRGB:
        case RHIFormat::ASTC_8x8_UNORM:
        case RHIFormat::ASTC_8x8_SRGB:
            return true;
        default:
            return false;
    }
}

inline bool IsSRGBFormat(RHIFormat format) {
    switch (format) {
        case RHIFormat::R8G8B8A8_SRGB:
        case RHIFormat::B8G8R8A8_SRGB:
        case RHIFormat::BC1_SRGB:
        case RHIFormat::BC2_SRGB:
        case RHIFormat::BC3_SRGB:
        case RHIFormat::BC7_SRGB:
        case RHIFormat::ASTC_4x4_SRGB:
        case RHIFormat::ASTC_6x6_SRGB:
        case RHIFormat::ASTC_8x8_SRGB:
            return true;
        default:
            return false;
    }
}

} // namespace Sanic
