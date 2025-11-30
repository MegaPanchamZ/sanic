#include "D3D12RHI.h"

#ifdef SANIC_ENABLE_D3D12

namespace Sanic {

//=============================================================================
// Format Conversion
//=============================================================================

DXGI_FORMAT ToDXGIFormat(RHIFormat format) {
    switch (format) {
        case RHIFormat::Unknown: return DXGI_FORMAT_UNKNOWN;
        
        // 8-bit formats
        case RHIFormat::R8_UNORM: return DXGI_FORMAT_R8_UNORM;
        case RHIFormat::R8_SNORM: return DXGI_FORMAT_R8_SNORM;
        case RHIFormat::R8_UINT: return DXGI_FORMAT_R8_UINT;
        case RHIFormat::R8_SINT: return DXGI_FORMAT_R8_SINT;
        
        // 16-bit formats
        case RHIFormat::R8G8_UNORM: return DXGI_FORMAT_R8G8_UNORM;
        case RHIFormat::R8G8_SNORM: return DXGI_FORMAT_R8G8_SNORM;
        case RHIFormat::R8G8_UINT: return DXGI_FORMAT_R8G8_UINT;
        case RHIFormat::R8G8_SINT: return DXGI_FORMAT_R8G8_SINT;
        case RHIFormat::R16_FLOAT: return DXGI_FORMAT_R16_FLOAT;
        case RHIFormat::R16_UNORM: return DXGI_FORMAT_R16_UNORM;
        case RHIFormat::R16_SNORM: return DXGI_FORMAT_R16_SNORM;
        case RHIFormat::R16_UINT: return DXGI_FORMAT_R16_UINT;
        case RHIFormat::R16_SINT: return DXGI_FORMAT_R16_SINT;
        
        // 32-bit formats
        case RHIFormat::R8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case RHIFormat::R8G8B8A8_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case RHIFormat::R8G8B8A8_SNORM: return DXGI_FORMAT_R8G8B8A8_SNORM;
        case RHIFormat::R8G8B8A8_UINT: return DXGI_FORMAT_R8G8B8A8_UINT;
        case RHIFormat::R8G8B8A8_SINT: return DXGI_FORMAT_R8G8B8A8_SINT;
        case RHIFormat::B8G8R8A8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM;
        case RHIFormat::B8G8R8A8_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        case RHIFormat::R10G10B10A2_UNORM: return DXGI_FORMAT_R10G10B10A2_UNORM;
        case RHIFormat::R10G10B10A2_UINT: return DXGI_FORMAT_R10G10B10A2_UINT;
        case RHIFormat::R11G11B10_FLOAT: return DXGI_FORMAT_R11G11B10_FLOAT;
        case RHIFormat::R16G16_FLOAT: return DXGI_FORMAT_R16G16_FLOAT;
        case RHIFormat::R16G16_UNORM: return DXGI_FORMAT_R16G16_UNORM;
        case RHIFormat::R16G16_SNORM: return DXGI_FORMAT_R16G16_SNORM;
        case RHIFormat::R16G16_UINT: return DXGI_FORMAT_R16G16_UINT;
        case RHIFormat::R16G16_SINT: return DXGI_FORMAT_R16G16_SINT;
        case RHIFormat::R32_FLOAT: return DXGI_FORMAT_R32_FLOAT;
        case RHIFormat::R32_UINT: return DXGI_FORMAT_R32_UINT;
        case RHIFormat::R32_SINT: return DXGI_FORMAT_R32_SINT;
        
        // 64-bit formats
        case RHIFormat::R16G16B16A16_FLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case RHIFormat::R16G16B16A16_UNORM: return DXGI_FORMAT_R16G16B16A16_UNORM;
        case RHIFormat::R16G16B16A16_SNORM: return DXGI_FORMAT_R16G16B16A16_SNORM;
        case RHIFormat::R16G16B16A16_UINT: return DXGI_FORMAT_R16G16B16A16_UINT;
        case RHIFormat::R16G16B16A16_SINT: return DXGI_FORMAT_R16G16B16A16_SINT;
        case RHIFormat::R32G32_FLOAT: return DXGI_FORMAT_R32G32_FLOAT;
        case RHIFormat::R32G32_UINT: return DXGI_FORMAT_R32G32_UINT;
        case RHIFormat::R32G32_SINT: return DXGI_FORMAT_R32G32_SINT;
        
        // 96-bit formats
        case RHIFormat::R32G32B32_FLOAT: return DXGI_FORMAT_R32G32B32_FLOAT;
        case RHIFormat::R32G32B32_UINT: return DXGI_FORMAT_R32G32B32_UINT;
        case RHIFormat::R32G32B32_SINT: return DXGI_FORMAT_R32G32B32_SINT;
        
        // 128-bit formats
        case RHIFormat::R32G32B32A32_FLOAT: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case RHIFormat::R32G32B32A32_UINT: return DXGI_FORMAT_R32G32B32A32_UINT;
        case RHIFormat::R32G32B32A32_SINT: return DXGI_FORMAT_R32G32B32A32_SINT;
        
        // Depth/stencil formats
        case RHIFormat::D16_UNORM: return DXGI_FORMAT_D16_UNORM;
        case RHIFormat::D24_UNORM_S8_UINT: return DXGI_FORMAT_D24_UNORM_S8_UINT;
        case RHIFormat::D32_FLOAT: return DXGI_FORMAT_D32_FLOAT;
        case RHIFormat::D32_FLOAT_S8_UINT: return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        
        // Compressed formats
        case RHIFormat::BC1_UNORM: return DXGI_FORMAT_BC1_UNORM;
        case RHIFormat::BC1_SRGB: return DXGI_FORMAT_BC1_UNORM_SRGB;
        case RHIFormat::BC2_UNORM: return DXGI_FORMAT_BC2_UNORM;
        case RHIFormat::BC2_SRGB: return DXGI_FORMAT_BC2_UNORM_SRGB;
        case RHIFormat::BC3_UNORM: return DXGI_FORMAT_BC3_UNORM;
        case RHIFormat::BC3_SRGB: return DXGI_FORMAT_BC3_UNORM_SRGB;
        case RHIFormat::BC4_UNORM: return DXGI_FORMAT_BC4_UNORM;
        case RHIFormat::BC4_SNORM: return DXGI_FORMAT_BC4_SNORM;
        case RHIFormat::BC5_UNORM: return DXGI_FORMAT_BC5_UNORM;
        case RHIFormat::BC5_SNORM: return DXGI_FORMAT_BC5_SNORM;
        case RHIFormat::BC6H_UF16: return DXGI_FORMAT_BC6H_UF16;
        case RHIFormat::BC6H_SF16: return DXGI_FORMAT_BC6H_SF16;
        case RHIFormat::BC7_UNORM: return DXGI_FORMAT_BC7_UNORM;
        case RHIFormat::BC7_SRGB: return DXGI_FORMAT_BC7_UNORM_SRGB;
        
        // ASTC not supported in D3D12
        case RHIFormat::ASTC_4x4_UNORM:
        case RHIFormat::ASTC_4x4_SRGB:
        case RHIFormat::ASTC_6x6_UNORM:
        case RHIFormat::ASTC_6x6_SRGB:
        case RHIFormat::ASTC_8x8_UNORM:
        case RHIFormat::ASTC_8x8_SRGB:
            return DXGI_FORMAT_UNKNOWN;
        
        default: return DXGI_FORMAT_UNKNOWN;
    }
}

RHIFormat FromDXGIFormat(DXGI_FORMAT format) {
    switch (format) {
        case DXGI_FORMAT_UNKNOWN: return RHIFormat::Unknown;
        
        case DXGI_FORMAT_R8_UNORM: return RHIFormat::R8_UNORM;
        case DXGI_FORMAT_R8_SNORM: return RHIFormat::R8_SNORM;
        case DXGI_FORMAT_R8_UINT: return RHIFormat::R8_UINT;
        case DXGI_FORMAT_R8_SINT: return RHIFormat::R8_SINT;
        
        case DXGI_FORMAT_R8G8_UNORM: return RHIFormat::R8G8_UNORM;
        case DXGI_FORMAT_R8G8_SNORM: return RHIFormat::R8G8_SNORM;
        case DXGI_FORMAT_R8G8_UINT: return RHIFormat::R8G8_UINT;
        case DXGI_FORMAT_R8G8_SINT: return RHIFormat::R8G8_SINT;
        
        case DXGI_FORMAT_R8G8B8A8_UNORM: return RHIFormat::R8G8B8A8_UNORM;
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return RHIFormat::R8G8B8A8_SRGB;
        case DXGI_FORMAT_R8G8B8A8_SNORM: return RHIFormat::R8G8B8A8_SNORM;
        case DXGI_FORMAT_R8G8B8A8_UINT: return RHIFormat::R8G8B8A8_UINT;
        case DXGI_FORMAT_R8G8B8A8_SINT: return RHIFormat::R8G8B8A8_SINT;
        case DXGI_FORMAT_B8G8R8A8_UNORM: return RHIFormat::B8G8R8A8_UNORM;
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return RHIFormat::B8G8R8A8_SRGB;
        
        case DXGI_FORMAT_R16_FLOAT: return RHIFormat::R16_FLOAT;
        case DXGI_FORMAT_R16G16_FLOAT: return RHIFormat::R16G16_FLOAT;
        case DXGI_FORMAT_R16G16B16A16_FLOAT: return RHIFormat::R16G16B16A16_FLOAT;
        
        case DXGI_FORMAT_R32_FLOAT: return RHIFormat::R32_FLOAT;
        case DXGI_FORMAT_R32_UINT: return RHIFormat::R32_UINT;
        case DXGI_FORMAT_R32_SINT: return RHIFormat::R32_SINT;
        case DXGI_FORMAT_R32G32_FLOAT: return RHIFormat::R32G32_FLOAT;
        case DXGI_FORMAT_R32G32B32_FLOAT: return RHIFormat::R32G32B32_FLOAT;
        case DXGI_FORMAT_R32G32B32A32_FLOAT: return RHIFormat::R32G32B32A32_FLOAT;
        
        case DXGI_FORMAT_D16_UNORM: return RHIFormat::D16_UNORM;
        case DXGI_FORMAT_D24_UNORM_S8_UINT: return RHIFormat::D24_UNORM_S8_UINT;
        case DXGI_FORMAT_D32_FLOAT: return RHIFormat::D32_FLOAT;
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return RHIFormat::D32_FLOAT_S8_UINT;
        
        default: return RHIFormat::Unknown;
    }
}

//=============================================================================
// Resource State Conversion
//=============================================================================

D3D12_RESOURCE_STATES ToD3D12ResourceState(RHIResourceState state) {
    switch (state) {
        case RHIResourceState::Undefined: return D3D12_RESOURCE_STATE_COMMON;
        case RHIResourceState::Common: return D3D12_RESOURCE_STATE_COMMON;
        case RHIResourceState::VertexBuffer: return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        case RHIResourceState::IndexBuffer: return D3D12_RESOURCE_STATE_INDEX_BUFFER;
        case RHIResourceState::UniformBuffer: return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        case RHIResourceState::ShaderResource: return D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
        case RHIResourceState::UnorderedAccess: return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        case RHIResourceState::RenderTarget: return D3D12_RESOURCE_STATE_RENDER_TARGET;
        case RHIResourceState::DepthWrite: return D3D12_RESOURCE_STATE_DEPTH_WRITE;
        case RHIResourceState::DepthRead: return D3D12_RESOURCE_STATE_DEPTH_READ;
        case RHIResourceState::IndirectArgument: return D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        case RHIResourceState::CopySrc: return D3D12_RESOURCE_STATE_COPY_SOURCE;
        case RHIResourceState::CopyDst: return D3D12_RESOURCE_STATE_COPY_DEST;
        case RHIResourceState::Present: return D3D12_RESOURCE_STATE_PRESENT;
        case RHIResourceState::AccelerationStructure: return D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
        case RHIResourceState::ShadingRate: return D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE;
        default: return D3D12_RESOURCE_STATE_COMMON;
    }
}

//=============================================================================
// Heap Type Conversion
//=============================================================================

D3D12_HEAP_TYPE ToD3D12HeapType(RHIMemoryType memoryType) {
    switch (memoryType) {
        case RHIMemoryType::GpuOnly: return D3D12_HEAP_TYPE_DEFAULT;
        case RHIMemoryType::Upload: return D3D12_HEAP_TYPE_UPLOAD;
        case RHIMemoryType::Readback: return D3D12_HEAP_TYPE_READBACK;
        default: return D3D12_HEAP_TYPE_DEFAULT;
    }
}

//=============================================================================
// Comparison and Blend Conversion
//=============================================================================

D3D12_COMPARISON_FUNC ToD3D12CompareFunc(RHICompareOp op) {
    switch (op) {
        case RHICompareOp::Never: return D3D12_COMPARISON_FUNC_NEVER;
        case RHICompareOp::Less: return D3D12_COMPARISON_FUNC_LESS;
        case RHICompareOp::Equal: return D3D12_COMPARISON_FUNC_EQUAL;
        case RHICompareOp::LessOrEqual: return D3D12_COMPARISON_FUNC_LESS_EQUAL;
        case RHICompareOp::Greater: return D3D12_COMPARISON_FUNC_GREATER;
        case RHICompareOp::NotEqual: return D3D12_COMPARISON_FUNC_NOT_EQUAL;
        case RHICompareOp::GreaterOrEqual: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        case RHICompareOp::Always: return D3D12_COMPARISON_FUNC_ALWAYS;
        default: return D3D12_COMPARISON_FUNC_LESS;
    }
}

D3D12_BLEND ToD3D12Blend(RHIBlendFactor factor) {
    switch (factor) {
        case RHIBlendFactor::Zero: return D3D12_BLEND_ZERO;
        case RHIBlendFactor::One: return D3D12_BLEND_ONE;
        case RHIBlendFactor::SrcColor: return D3D12_BLEND_SRC_COLOR;
        case RHIBlendFactor::OneMinusSrcColor: return D3D12_BLEND_INV_SRC_COLOR;
        case RHIBlendFactor::DstColor: return D3D12_BLEND_DEST_COLOR;
        case RHIBlendFactor::OneMinusDstColor: return D3D12_BLEND_INV_DEST_COLOR;
        case RHIBlendFactor::SrcAlpha: return D3D12_BLEND_SRC_ALPHA;
        case RHIBlendFactor::OneMinusSrcAlpha: return D3D12_BLEND_INV_SRC_ALPHA;
        case RHIBlendFactor::DstAlpha: return D3D12_BLEND_DEST_ALPHA;
        case RHIBlendFactor::OneMinusDstAlpha: return D3D12_BLEND_INV_DEST_ALPHA;
        case RHIBlendFactor::ConstantColor: return D3D12_BLEND_BLEND_FACTOR;
        case RHIBlendFactor::OneMinusConstantColor: return D3D12_BLEND_INV_BLEND_FACTOR;
        case RHIBlendFactor::ConstantAlpha: return D3D12_BLEND_ALPHA_FACTOR;
        case RHIBlendFactor::OneMinusConstantAlpha: return D3D12_BLEND_INV_ALPHA_FACTOR;
        case RHIBlendFactor::SrcAlphaSaturate: return D3D12_BLEND_SRC_ALPHA_SAT;
        case RHIBlendFactor::Src1Color: return D3D12_BLEND_SRC1_COLOR;
        case RHIBlendFactor::OneMinusSrc1Color: return D3D12_BLEND_INV_SRC1_COLOR;
        case RHIBlendFactor::Src1Alpha: return D3D12_BLEND_SRC1_ALPHA;
        case RHIBlendFactor::OneMinusSrc1Alpha: return D3D12_BLEND_INV_SRC1_ALPHA;
        default: return D3D12_BLEND_ONE;
    }
}

D3D12_BLEND_OP ToD3D12BlendOp(RHIBlendOp op) {
    switch (op) {
        case RHIBlendOp::Add: return D3D12_BLEND_OP_ADD;
        case RHIBlendOp::Subtract: return D3D12_BLEND_OP_SUBTRACT;
        case RHIBlendOp::ReverseSubtract: return D3D12_BLEND_OP_REV_SUBTRACT;
        case RHIBlendOp::Min: return D3D12_BLEND_OP_MIN;
        case RHIBlendOp::Max: return D3D12_BLEND_OP_MAX;
        default: return D3D12_BLEND_OP_ADD;
    }
}

//=============================================================================
// Rasterizer State Conversion
//=============================================================================

D3D12_CULL_MODE ToD3D12CullMode(RHICullMode mode) {
    switch (mode) {
        case RHICullMode::None: return D3D12_CULL_MODE_NONE;
        case RHICullMode::Front: return D3D12_CULL_MODE_FRONT;
        case RHICullMode::Back: return D3D12_CULL_MODE_BACK;
        default: return D3D12_CULL_MODE_BACK;
    }
}

D3D12_FILL_MODE ToD3D12FillMode(RHIFillMode mode) {
    switch (mode) {
        case RHIFillMode::Solid: return D3D12_FILL_MODE_SOLID;
        case RHIFillMode::Wireframe: return D3D12_FILL_MODE_WIREFRAME;
        default: return D3D12_FILL_MODE_SOLID;
    }
}

//=============================================================================
// Topology Conversion
//=============================================================================

D3D12_PRIMITIVE_TOPOLOGY_TYPE ToD3D12TopologyType(RHIPrimitiveTopology topology) {
    switch (topology) {
        case RHIPrimitiveTopology::PointList:
            return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
        case RHIPrimitiveTopology::LineList:
        case RHIPrimitiveTopology::LineStrip:
        case RHIPrimitiveTopology::LineListWithAdjacency:
        case RHIPrimitiveTopology::LineStripWithAdjacency:
            return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
        case RHIPrimitiveTopology::TriangleList:
        case RHIPrimitiveTopology::TriangleStrip:
        case RHIPrimitiveTopology::TriangleFan:
        case RHIPrimitiveTopology::TriangleListWithAdjacency:
        case RHIPrimitiveTopology::TriangleStripWithAdjacency:
            return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        case RHIPrimitiveTopology::PatchList:
            return D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
        default:
            return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    }
}

D3D_PRIMITIVE_TOPOLOGY ToD3DTopology(RHIPrimitiveTopology topology) {
    switch (topology) {
        case RHIPrimitiveTopology::PointList: return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
        case RHIPrimitiveTopology::LineList: return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
        case RHIPrimitiveTopology::LineStrip: return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
        case RHIPrimitiveTopology::TriangleList: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        case RHIPrimitiveTopology::TriangleStrip: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        case RHIPrimitiveTopology::LineListWithAdjacency: return D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ;
        case RHIPrimitiveTopology::LineStripWithAdjacency: return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ;
        case RHIPrimitiveTopology::TriangleListWithAdjacency: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ;
        case RHIPrimitiveTopology::TriangleStripWithAdjacency: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ;
        case RHIPrimitiveTopology::PatchList: return D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST;
        default: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    }
}

//=============================================================================
// Filter Conversion
//=============================================================================

D3D12_FILTER ToD3D12Filter(RHIFilter minFilter, RHIFilter magFilter, RHIMipmapMode mipMode, bool comparison, bool anisotropic) {
    if (anisotropic) {
        return comparison ? D3D12_FILTER_COMPARISON_ANISOTROPIC : D3D12_FILTER_ANISOTROPIC;
    }
    
    int filterBits = 0;
    
    // Mip filter
    if (mipMode == RHIMipmapMode::Linear) filterBits |= 0x01;
    
    // Mag filter
    if (magFilter == RHIFilter::Linear) filterBits |= 0x04;
    
    // Min filter
    if (minFilter == RHIFilter::Linear) filterBits |= 0x10;
    
    if (comparison) filterBits |= 0x80;
    
    return static_cast<D3D12_FILTER>(filterBits);
}

D3D12_TEXTURE_ADDRESS_MODE ToD3D12AddressMode(RHIAddressMode mode) {
    switch (mode) {
        case RHIAddressMode::Repeat: return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        case RHIAddressMode::MirroredRepeat: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
        case RHIAddressMode::ClampToEdge: return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        case RHIAddressMode::ClampToBorder: return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        case RHIAddressMode::MirrorClampToEdge: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE;
        default: return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    }
}

//=============================================================================
// Descriptor Type Conversion
//=============================================================================

D3D12_DESCRIPTOR_RANGE_TYPE ToD3D12DescriptorRangeType(RHIDescriptorType type) {
    switch (type) {
        case RHIDescriptorType::Sampler:
            return D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        case RHIDescriptorType::SampledImage:
        case RHIDescriptorType::UniformTexelBuffer:
        case RHIDescriptorType::CombinedImageSampler:
            return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        case RHIDescriptorType::StorageImage:
        case RHIDescriptorType::StorageTexelBuffer:
        case RHIDescriptorType::StorageBuffer:
        case RHIDescriptorType::StorageBufferDynamic:
            return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        case RHIDescriptorType::UniformBuffer:
        case RHIDescriptorType::UniformBufferDynamic:
            return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
        default:
            return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    }
}

//=============================================================================
// Command List Type Conversion
//=============================================================================

D3D12_COMMAND_LIST_TYPE ToD3D12CommandListType(RHICommandListType type) {
    switch (type) {
        case RHICommandListType::Direct: return D3D12_COMMAND_LIST_TYPE_DIRECT;
        case RHICommandListType::Compute: return D3D12_COMMAND_LIST_TYPE_COMPUTE;
        case RHICommandListType::Copy: return D3D12_COMMAND_LIST_TYPE_COPY;
        default: return D3D12_COMMAND_LIST_TYPE_DIRECT;
    }
}

//=============================================================================
// Shader Visibility Conversion
//=============================================================================

D3D12_SHADER_VISIBILITY ToD3D12ShaderVisibility(RHIShaderStage stage) {
    // Handle single stage cases
    switch (stage) {
        case RHIShaderStage::Vertex: return D3D12_SHADER_VISIBILITY_VERTEX;
        case RHIShaderStage::Hull: return D3D12_SHADER_VISIBILITY_HULL;
        case RHIShaderStage::Domain: return D3D12_SHADER_VISIBILITY_DOMAIN;
        case RHIShaderStage::Geometry: return D3D12_SHADER_VISIBILITY_GEOMETRY;
        case RHIShaderStage::Fragment: return D3D12_SHADER_VISIBILITY_PIXEL;
        case RHIShaderStage::Task: return D3D12_SHADER_VISIBILITY_AMPLIFICATION;
        case RHIShaderStage::Mesh: return D3D12_SHADER_VISIBILITY_MESH;
        default: return D3D12_SHADER_VISIBILITY_ALL;
    }
}

//=============================================================================
// Resource Dimension Conversion
//=============================================================================

D3D12_RESOURCE_DIMENSION ToD3D12ResourceDimension(RHITextureDimension dimension) {
    switch (dimension) {
        case RHITextureDimension::Texture1D: return D3D12_RESOURCE_DIMENSION_TEXTURE1D;
        case RHITextureDimension::Texture2D: return D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        case RHITextureDimension::Texture3D: return D3D12_RESOURCE_DIMENSION_TEXTURE3D;
        case RHITextureDimension::TextureCube: return D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        default: return D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    }
}

D3D12_SRV_DIMENSION ToD3D12SRVDimension(RHITextureDimension dimension, uint32_t arraySize) {
    switch (dimension) {
        case RHITextureDimension::Texture1D:
            return arraySize > 1 ? D3D12_SRV_DIMENSION_TEXTURE1DARRAY : D3D12_SRV_DIMENSION_TEXTURE1D;
        case RHITextureDimension::Texture2D:
            return arraySize > 1 ? D3D12_SRV_DIMENSION_TEXTURE2DARRAY : D3D12_SRV_DIMENSION_TEXTURE2D;
        case RHITextureDimension::Texture3D:
            return D3D12_SRV_DIMENSION_TEXTURE3D;
        case RHITextureDimension::TextureCube:
            return arraySize > 6 ? D3D12_SRV_DIMENSION_TEXTURECUBEARRAY : D3D12_SRV_DIMENSION_TEXTURECUBE;
        default:
            return D3D12_SRV_DIMENSION_TEXTURE2D;
    }
}

D3D12_UAV_DIMENSION ToD3D12UAVDimension(RHITextureDimension dimension, uint32_t arraySize) {
    switch (dimension) {
        case RHITextureDimension::Texture1D:
            return arraySize > 1 ? D3D12_UAV_DIMENSION_TEXTURE1DARRAY : D3D12_UAV_DIMENSION_TEXTURE1D;
        case RHITextureDimension::Texture2D:
        case RHITextureDimension::TextureCube:
            return arraySize > 1 ? D3D12_UAV_DIMENSION_TEXTURE2DARRAY : D3D12_UAV_DIMENSION_TEXTURE2D;
        case RHITextureDimension::Texture3D:
            return D3D12_UAV_DIMENSION_TEXTURE3D;
        default:
            return D3D12_UAV_DIMENSION_TEXTURE2D;
    }
}

D3D12_RTV_DIMENSION ToD3D12RTVDimension(RHITextureDimension dimension, uint32_t arraySize) {
    switch (dimension) {
        case RHITextureDimension::Texture1D:
            return arraySize > 1 ? D3D12_RTV_DIMENSION_TEXTURE1DARRAY : D3D12_RTV_DIMENSION_TEXTURE1D;
        case RHITextureDimension::Texture2D:
        case RHITextureDimension::TextureCube:
            return arraySize > 1 ? D3D12_RTV_DIMENSION_TEXTURE2DARRAY : D3D12_RTV_DIMENSION_TEXTURE2D;
        case RHITextureDimension::Texture3D:
            return D3D12_RTV_DIMENSION_TEXTURE3D;
        default:
            return D3D12_RTV_DIMENSION_TEXTURE2D;
    }
}

D3D12_DSV_DIMENSION ToD3D12DSVDimension(RHITextureDimension dimension, uint32_t arraySize) {
    switch (dimension) {
        case RHITextureDimension::Texture1D:
            return arraySize > 1 ? D3D12_DSV_DIMENSION_TEXTURE1DARRAY : D3D12_DSV_DIMENSION_TEXTURE1D;
        case RHITextureDimension::Texture2D:
        case RHITextureDimension::TextureCube:
            return arraySize > 1 ? D3D12_DSV_DIMENSION_TEXTURE2DARRAY : D3D12_DSV_DIMENSION_TEXTURE2D;
        default:
            return D3D12_DSV_DIMENSION_TEXTURE2D;
    }
}

//=============================================================================
// Index Format Conversion
//=============================================================================

DXGI_FORMAT ToDXGIIndexFormat(RHIIndexType type) {
    switch (type) {
        case RHIIndexType::UInt16: return DXGI_FORMAT_R16_UINT;
        case RHIIndexType::UInt32: return DXGI_FORMAT_R32_UINT;
        default: return DXGI_FORMAT_R32_UINT;
    }
}

//=============================================================================
// Stencil Op Conversion
//=============================================================================

D3D12_STENCIL_OP ToD3D12StencilOp(RHIStencilOp op) {
    switch (op) {
        case RHIStencilOp::Keep: return D3D12_STENCIL_OP_KEEP;
        case RHIStencilOp::Zero: return D3D12_STENCIL_OP_ZERO;
        case RHIStencilOp::Replace: return D3D12_STENCIL_OP_REPLACE;
        case RHIStencilOp::IncrementClamp: return D3D12_STENCIL_OP_INCR_SAT;
        case RHIStencilOp::DecrementClamp: return D3D12_STENCIL_OP_DECR_SAT;
        case RHIStencilOp::Invert: return D3D12_STENCIL_OP_INVERT;
        case RHIStencilOp::IncrementWrap: return D3D12_STENCIL_OP_INCR;
        case RHIStencilOp::DecrementWrap: return D3D12_STENCIL_OP_DECR;
        default: return D3D12_STENCIL_OP_KEEP;
    }
}

} // namespace Sanic

#endif // SANIC_ENABLE_D3D12
