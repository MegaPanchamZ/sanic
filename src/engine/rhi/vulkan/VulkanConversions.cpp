#ifdef SANIC_ENABLE_VULKAN

#include "VulkanRHI.h"

namespace Sanic {

//=============================================================================
// Format Conversion
//=============================================================================

VkFormat ToVkFormat(RHIFormat format) {
    switch (format) {
        case RHIFormat::Unknown: return VK_FORMAT_UNDEFINED;
        
        // 8-bit formats
        case RHIFormat::R8_UNORM: return VK_FORMAT_R8_UNORM;
        case RHIFormat::R8_SNORM: return VK_FORMAT_R8_SNORM;
        case RHIFormat::R8_UINT: return VK_FORMAT_R8_UINT;
        case RHIFormat::R8_SINT: return VK_FORMAT_R8_SINT;
        
        // 16-bit formats
        case RHIFormat::R8G8_UNORM: return VK_FORMAT_R8G8_UNORM;
        case RHIFormat::R8G8_SNORM: return VK_FORMAT_R8G8_SNORM;
        case RHIFormat::R8G8_UINT: return VK_FORMAT_R8G8_UINT;
        case RHIFormat::R8G8_SINT: return VK_FORMAT_R8G8_SINT;
        case RHIFormat::R16_FLOAT: return VK_FORMAT_R16_SFLOAT;
        case RHIFormat::R16_UNORM: return VK_FORMAT_R16_UNORM;
        case RHIFormat::R16_SNORM: return VK_FORMAT_R16_SNORM;
        case RHIFormat::R16_UINT: return VK_FORMAT_R16_UINT;
        case RHIFormat::R16_SINT: return VK_FORMAT_R16_SINT;
        
        // 32-bit formats
        case RHIFormat::R8G8B8A8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
        case RHIFormat::R8G8B8A8_SRGB: return VK_FORMAT_R8G8B8A8_SRGB;
        case RHIFormat::R8G8B8A8_SNORM: return VK_FORMAT_R8G8B8A8_SNORM;
        case RHIFormat::R8G8B8A8_UINT: return VK_FORMAT_R8G8B8A8_UINT;
        case RHIFormat::R8G8B8A8_SINT: return VK_FORMAT_R8G8B8A8_SINT;
        case RHIFormat::B8G8R8A8_UNORM: return VK_FORMAT_B8G8R8A8_UNORM;
        case RHIFormat::B8G8R8A8_SRGB: return VK_FORMAT_B8G8R8A8_SRGB;
        case RHIFormat::R10G10B10A2_UNORM: return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
        case RHIFormat::R10G10B10A2_UINT: return VK_FORMAT_A2B10G10R10_UINT_PACK32;
        case RHIFormat::R11G11B10_FLOAT: return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
        case RHIFormat::R16G16_FLOAT: return VK_FORMAT_R16G16_SFLOAT;
        case RHIFormat::R16G16_UNORM: return VK_FORMAT_R16G16_UNORM;
        case RHIFormat::R16G16_SNORM: return VK_FORMAT_R16G16_SNORM;
        case RHIFormat::R16G16_UINT: return VK_FORMAT_R16G16_UINT;
        case RHIFormat::R16G16_SINT: return VK_FORMAT_R16G16_SINT;
        case RHIFormat::R32_FLOAT: return VK_FORMAT_R32_SFLOAT;
        case RHIFormat::R32_UINT: return VK_FORMAT_R32_UINT;
        case RHIFormat::R32_SINT: return VK_FORMAT_R32_SINT;
        
        // 64-bit formats
        case RHIFormat::R16G16B16A16_FLOAT: return VK_FORMAT_R16G16B16A16_SFLOAT;
        case RHIFormat::R16G16B16A16_UNORM: return VK_FORMAT_R16G16B16A16_UNORM;
        case RHIFormat::R16G16B16A16_SNORM: return VK_FORMAT_R16G16B16A16_SNORM;
        case RHIFormat::R16G16B16A16_UINT: return VK_FORMAT_R16G16B16A16_UINT;
        case RHIFormat::R16G16B16A16_SINT: return VK_FORMAT_R16G16B16A16_SINT;
        case RHIFormat::R32G32_FLOAT: return VK_FORMAT_R32G32_SFLOAT;
        case RHIFormat::R32G32_UINT: return VK_FORMAT_R32G32_UINT;
        case RHIFormat::R32G32_SINT: return VK_FORMAT_R32G32_SINT;
        
        // 96-bit formats
        case RHIFormat::R32G32B32_FLOAT: return VK_FORMAT_R32G32B32_SFLOAT;
        case RHIFormat::R32G32B32_UINT: return VK_FORMAT_R32G32B32_UINT;
        case RHIFormat::R32G32B32_SINT: return VK_FORMAT_R32G32B32_SINT;
        
        // 128-bit formats
        case RHIFormat::R32G32B32A32_FLOAT: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case RHIFormat::R32G32B32A32_UINT: return VK_FORMAT_R32G32B32A32_UINT;
        case RHIFormat::R32G32B32A32_SINT: return VK_FORMAT_R32G32B32A32_SINT;
        
        // Depth/stencil formats
        case RHIFormat::D16_UNORM: return VK_FORMAT_D16_UNORM;
        case RHIFormat::D24_UNORM_S8_UINT: return VK_FORMAT_D24_UNORM_S8_UINT;
        case RHIFormat::D32_FLOAT: return VK_FORMAT_D32_SFLOAT;
        case RHIFormat::D32_FLOAT_S8_UINT: return VK_FORMAT_D32_SFLOAT_S8_UINT;
        
        // Compressed formats
        case RHIFormat::BC1_UNORM: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        case RHIFormat::BC1_SRGB: return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
        case RHIFormat::BC2_UNORM: return VK_FORMAT_BC2_UNORM_BLOCK;
        case RHIFormat::BC2_SRGB: return VK_FORMAT_BC2_SRGB_BLOCK;
        case RHIFormat::BC3_UNORM: return VK_FORMAT_BC3_UNORM_BLOCK;
        case RHIFormat::BC3_SRGB: return VK_FORMAT_BC3_SRGB_BLOCK;
        case RHIFormat::BC4_UNORM: return VK_FORMAT_BC4_UNORM_BLOCK;
        case RHIFormat::BC4_SNORM: return VK_FORMAT_BC4_SNORM_BLOCK;
        case RHIFormat::BC5_UNORM: return VK_FORMAT_BC5_UNORM_BLOCK;
        case RHIFormat::BC5_SNORM: return VK_FORMAT_BC5_SNORM_BLOCK;
        case RHIFormat::BC6H_UF16: return VK_FORMAT_BC6H_UFLOAT_BLOCK;
        case RHIFormat::BC6H_SF16: return VK_FORMAT_BC6H_SFLOAT_BLOCK;
        case RHIFormat::BC7_UNORM: return VK_FORMAT_BC7_UNORM_BLOCK;
        case RHIFormat::BC7_SRGB: return VK_FORMAT_BC7_SRGB_BLOCK;
        
        // ASTC formats
        case RHIFormat::ASTC_4x4_UNORM: return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
        case RHIFormat::ASTC_4x4_SRGB: return VK_FORMAT_ASTC_4x4_SRGB_BLOCK;
        case RHIFormat::ASTC_6x6_UNORM: return VK_FORMAT_ASTC_6x6_UNORM_BLOCK;
        case RHIFormat::ASTC_6x6_SRGB: return VK_FORMAT_ASTC_6x6_SRGB_BLOCK;
        case RHIFormat::ASTC_8x8_UNORM: return VK_FORMAT_ASTC_8x8_UNORM_BLOCK;
        case RHIFormat::ASTC_8x8_SRGB: return VK_FORMAT_ASTC_8x8_SRGB_BLOCK;
        
        default: return VK_FORMAT_UNDEFINED;
    }
}

RHIFormat FromVkFormat(VkFormat format) {
    switch (format) {
        case VK_FORMAT_UNDEFINED: return RHIFormat::Unknown;
        
        case VK_FORMAT_R8_UNORM: return RHIFormat::R8_UNORM;
        case VK_FORMAT_R8_SNORM: return RHIFormat::R8_SNORM;
        case VK_FORMAT_R8_UINT: return RHIFormat::R8_UINT;
        case VK_FORMAT_R8_SINT: return RHIFormat::R8_SINT;
        
        case VK_FORMAT_R8G8_UNORM: return RHIFormat::R8G8_UNORM;
        case VK_FORMAT_R8G8_SNORM: return RHIFormat::R8G8_SNORM;
        case VK_FORMAT_R8G8_UINT: return RHIFormat::R8G8_UINT;
        case VK_FORMAT_R8G8_SINT: return RHIFormat::R8G8_SINT;
        
        case VK_FORMAT_R8G8B8A8_UNORM: return RHIFormat::R8G8B8A8_UNORM;
        case VK_FORMAT_R8G8B8A8_SRGB: return RHIFormat::R8G8B8A8_SRGB;
        case VK_FORMAT_R8G8B8A8_SNORM: return RHIFormat::R8G8B8A8_SNORM;
        case VK_FORMAT_R8G8B8A8_UINT: return RHIFormat::R8G8B8A8_UINT;
        case VK_FORMAT_R8G8B8A8_SINT: return RHIFormat::R8G8B8A8_SINT;
        case VK_FORMAT_B8G8R8A8_UNORM: return RHIFormat::B8G8R8A8_UNORM;
        case VK_FORMAT_B8G8R8A8_SRGB: return RHIFormat::B8G8R8A8_SRGB;
        
        case VK_FORMAT_R16_SFLOAT: return RHIFormat::R16_FLOAT;
        case VK_FORMAT_R16G16_SFLOAT: return RHIFormat::R16G16_FLOAT;
        case VK_FORMAT_R16G16B16A16_SFLOAT: return RHIFormat::R16G16B16A16_FLOAT;
        
        case VK_FORMAT_R32_SFLOAT: return RHIFormat::R32_FLOAT;
        case VK_FORMAT_R32_UINT: return RHIFormat::R32_UINT;
        case VK_FORMAT_R32_SINT: return RHIFormat::R32_SINT;
        case VK_FORMAT_R32G32_SFLOAT: return RHIFormat::R32G32_FLOAT;
        case VK_FORMAT_R32G32B32_SFLOAT: return RHIFormat::R32G32B32_FLOAT;
        case VK_FORMAT_R32G32B32A32_SFLOAT: return RHIFormat::R32G32B32A32_FLOAT;
        
        case VK_FORMAT_D16_UNORM: return RHIFormat::D16_UNORM;
        case VK_FORMAT_D24_UNORM_S8_UINT: return RHIFormat::D24_UNORM_S8_UINT;
        case VK_FORMAT_D32_SFLOAT: return RHIFormat::D32_FLOAT;
        case VK_FORMAT_D32_SFLOAT_S8_UINT: return RHIFormat::D32_FLOAT_S8_UINT;
        
        default: return RHIFormat::Unknown;
    }
}

//=============================================================================
// Resource State Conversion
//=============================================================================

VkImageLayout ToVkImageLayout(RHIResourceState state) {
    switch (state) {
        case RHIResourceState::Undefined: return VK_IMAGE_LAYOUT_UNDEFINED;
        case RHIResourceState::Common: return VK_IMAGE_LAYOUT_GENERAL;
        case RHIResourceState::ShaderResource: return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        case RHIResourceState::UnorderedAccess: return VK_IMAGE_LAYOUT_GENERAL;
        case RHIResourceState::RenderTarget: return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        case RHIResourceState::DepthWrite: return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        case RHIResourceState::DepthRead: return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        case RHIResourceState::CopySrc: return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        case RHIResourceState::CopyDst: return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        case RHIResourceState::Present: return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        default: return VK_IMAGE_LAYOUT_GENERAL;
    }
}

VkAccessFlags ToVkAccessFlags(RHIResourceState state) {
    switch (state) {
        case RHIResourceState::Undefined: return 0;
        case RHIResourceState::Common: return VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        case RHIResourceState::VertexBuffer: return VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
        case RHIResourceState::IndexBuffer: return VK_ACCESS_INDEX_READ_BIT;
        case RHIResourceState::UniformBuffer: return VK_ACCESS_UNIFORM_READ_BIT;
        case RHIResourceState::ShaderResource: return VK_ACCESS_SHADER_READ_BIT;
        case RHIResourceState::UnorderedAccess: return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        case RHIResourceState::RenderTarget: return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        case RHIResourceState::DepthWrite: return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        case RHIResourceState::DepthRead: return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        case RHIResourceState::IndirectArgument: return VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        case RHIResourceState::CopySrc: return VK_ACCESS_TRANSFER_READ_BIT;
        case RHIResourceState::CopyDst: return VK_ACCESS_TRANSFER_WRITE_BIT;
        case RHIResourceState::Present: return VK_ACCESS_MEMORY_READ_BIT;
        case RHIResourceState::AccelerationStructure: return VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        default: return 0;
    }
}

VkPipelineStageFlags ToVkPipelineStage(RHIResourceState state) {
    switch (state) {
        case RHIResourceState::Undefined: return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        case RHIResourceState::Common: return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        case RHIResourceState::VertexBuffer:
        case RHIResourceState::IndexBuffer: return VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
        case RHIResourceState::UniformBuffer:
        case RHIResourceState::ShaderResource:
        case RHIResourceState::UnorderedAccess: return VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        case RHIResourceState::RenderTarget: return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        case RHIResourceState::DepthWrite:
        case RHIResourceState::DepthRead: return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        case RHIResourceState::IndirectArgument: return VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
        case RHIResourceState::CopySrc:
        case RHIResourceState::CopyDst: return VK_PIPELINE_STAGE_TRANSFER_BIT;
        case RHIResourceState::Present: return VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        case RHIResourceState::AccelerationStructure: return VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        default: return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }
}

//=============================================================================
// Buffer Usage Conversion
//=============================================================================

VkBufferUsageFlags ToVkBufferUsage(RHIBufferUsage usage) {
    VkBufferUsageFlags flags = 0;
    
    if (hasFlag(usage, RHIBufferUsage::VertexBuffer))
        flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (hasFlag(usage, RHIBufferUsage::IndexBuffer))
        flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (hasFlag(usage, RHIBufferUsage::UniformBuffer))
        flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (hasFlag(usage, RHIBufferUsage::StorageBuffer))
        flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (hasFlag(usage, RHIBufferUsage::IndirectBuffer))
        flags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    if (hasFlag(usage, RHIBufferUsage::TransferSrc))
        flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (hasFlag(usage, RHIBufferUsage::TransferDst))
        flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (hasFlag(usage, RHIBufferUsage::AccelerationStructure))
        flags |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;
    if (hasFlag(usage, RHIBufferUsage::ShaderBindingTable))
        flags |= VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR;
    if (hasFlag(usage, RHIBufferUsage::AccelerationStructureBuildInput))
        flags |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    
    // Always enable shader device address for modern usage
    flags |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    
    return flags;
}

//=============================================================================
// Texture Usage Conversion
//=============================================================================

VkImageUsageFlags ToVkImageUsage(RHITextureUsage usage) {
    VkImageUsageFlags flags = 0;
    
    if (hasFlag(usage, RHITextureUsage::Sampled))
        flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (hasFlag(usage, RHITextureUsage::Storage))
        flags |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (hasFlag(usage, RHITextureUsage::RenderTarget))
        flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (hasFlag(usage, RHITextureUsage::DepthStencil))
        flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (hasFlag(usage, RHITextureUsage::TransferSrc))
        flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (hasFlag(usage, RHITextureUsage::TransferDst))
        flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (hasFlag(usage, RHITextureUsage::InputAttachment))
        flags |= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
    if (hasFlag(usage, RHITextureUsage::ShadingRate))
        flags |= VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR;
    
    return flags;
}

//=============================================================================
// Shader Stage Conversion
//=============================================================================

VkShaderStageFlagBits ToVkShaderStage(RHIShaderStage stage) {
    switch (stage) {
        case RHIShaderStage::Vertex: return VK_SHADER_STAGE_VERTEX_BIT;
        case RHIShaderStage::Hull: return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
        case RHIShaderStage::Domain: return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        case RHIShaderStage::Geometry: return VK_SHADER_STAGE_GEOMETRY_BIT;
        case RHIShaderStage::Fragment: return VK_SHADER_STAGE_FRAGMENT_BIT;
        case RHIShaderStage::Compute: return VK_SHADER_STAGE_COMPUTE_BIT;
        case RHIShaderStage::Task: return VK_SHADER_STAGE_TASK_BIT_EXT;
        case RHIShaderStage::Mesh: return VK_SHADER_STAGE_MESH_BIT_EXT;
        case RHIShaderStage::RayGen: return VK_SHADER_STAGE_RAYGEN_BIT_KHR;
        case RHIShaderStage::Miss: return VK_SHADER_STAGE_MISS_BIT_KHR;
        case RHIShaderStage::ClosestHit: return VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        case RHIShaderStage::AnyHit: return VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
        case RHIShaderStage::Intersection: return VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
        case RHIShaderStage::Callable: return VK_SHADER_STAGE_CALLABLE_BIT_KHR;
        default: return VK_SHADER_STAGE_ALL;
    }
}

VkShaderStageFlags ToVkShaderStageFlags(RHIShaderStage stages) {
    VkShaderStageFlags flags = 0;
    
    if (hasFlag(stages, RHIShaderStage::Vertex)) flags |= VK_SHADER_STAGE_VERTEX_BIT;
    if (hasFlag(stages, RHIShaderStage::Hull)) flags |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
    if (hasFlag(stages, RHIShaderStage::Domain)) flags |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
    if (hasFlag(stages, RHIShaderStage::Geometry)) flags |= VK_SHADER_STAGE_GEOMETRY_BIT;
    if (hasFlag(stages, RHIShaderStage::Fragment)) flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
    if (hasFlag(stages, RHIShaderStage::Compute)) flags |= VK_SHADER_STAGE_COMPUTE_BIT;
    if (hasFlag(stages, RHIShaderStage::Task)) flags |= VK_SHADER_STAGE_TASK_BIT_EXT;
    if (hasFlag(stages, RHIShaderStage::Mesh)) flags |= VK_SHADER_STAGE_MESH_BIT_EXT;
    if (hasFlag(stages, RHIShaderStage::RayGen)) flags |= VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    if (hasFlag(stages, RHIShaderStage::Miss)) flags |= VK_SHADER_STAGE_MISS_BIT_KHR;
    if (hasFlag(stages, RHIShaderStage::ClosestHit)) flags |= VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    if (hasFlag(stages, RHIShaderStage::AnyHit)) flags |= VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
    if (hasFlag(stages, RHIShaderStage::Intersection)) flags |= VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
    if (hasFlag(stages, RHIShaderStage::Callable)) flags |= VK_SHADER_STAGE_CALLABLE_BIT_KHR;
    
    return flags;
}

//=============================================================================
// Descriptor Type Conversion
//=============================================================================

VkDescriptorType ToVkDescriptorType(RHIDescriptorType type) {
    switch (type) {
        case RHIDescriptorType::Sampler: return VK_DESCRIPTOR_TYPE_SAMPLER;
        case RHIDescriptorType::SampledImage: return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case RHIDescriptorType::StorageImage: return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        case RHIDescriptorType::UniformTexelBuffer: return VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
        case RHIDescriptorType::StorageTexelBuffer: return VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
        case RHIDescriptorType::UniformBuffer: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case RHIDescriptorType::StorageBuffer: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case RHIDescriptorType::UniformBufferDynamic: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        case RHIDescriptorType::StorageBufferDynamic: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
        case RHIDescriptorType::InputAttachment: return VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        case RHIDescriptorType::AccelerationStructure: return VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        case RHIDescriptorType::CombinedImageSampler: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        default: return VK_DESCRIPTOR_TYPE_MAX_ENUM;
    }
}

//=============================================================================
// Sampler Conversion
//=============================================================================

VkFilter ToVkFilter(RHIFilter filter) {
    switch (filter) {
        case RHIFilter::Nearest: return VK_FILTER_NEAREST;
        case RHIFilter::Linear: return VK_FILTER_LINEAR;
        default: return VK_FILTER_LINEAR;
    }
}

VkSamplerMipmapMode ToVkMipmapMode(RHIMipmapMode mode) {
    switch (mode) {
        case RHIMipmapMode::Nearest: return VK_SAMPLER_MIPMAP_MODE_NEAREST;
        case RHIMipmapMode::Linear: return VK_SAMPLER_MIPMAP_MODE_LINEAR;
        default: return VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }
}

VkSamplerAddressMode ToVkAddressMode(RHIAddressMode mode) {
    switch (mode) {
        case RHIAddressMode::Repeat: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case RHIAddressMode::MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case RHIAddressMode::ClampToEdge: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case RHIAddressMode::ClampToBorder: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        case RHIAddressMode::MirrorClampToEdge: return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
        default: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

//=============================================================================
// Comparison and Blend Conversion
//=============================================================================

VkCompareOp ToVkCompareOp(RHICompareOp op) {
    switch (op) {
        case RHICompareOp::Never: return VK_COMPARE_OP_NEVER;
        case RHICompareOp::Less: return VK_COMPARE_OP_LESS;
        case RHICompareOp::Equal: return VK_COMPARE_OP_EQUAL;
        case RHICompareOp::LessOrEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
        case RHICompareOp::Greater: return VK_COMPARE_OP_GREATER;
        case RHICompareOp::NotEqual: return VK_COMPARE_OP_NOT_EQUAL;
        case RHICompareOp::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case RHICompareOp::Always: return VK_COMPARE_OP_ALWAYS;
        default: return VK_COMPARE_OP_LESS;
    }
}

VkBlendFactor ToVkBlendFactor(RHIBlendFactor factor) {
    switch (factor) {
        case RHIBlendFactor::Zero: return VK_BLEND_FACTOR_ZERO;
        case RHIBlendFactor::One: return VK_BLEND_FACTOR_ONE;
        case RHIBlendFactor::SrcColor: return VK_BLEND_FACTOR_SRC_COLOR;
        case RHIBlendFactor::OneMinusSrcColor: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case RHIBlendFactor::DstColor: return VK_BLEND_FACTOR_DST_COLOR;
        case RHIBlendFactor::OneMinusDstColor: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case RHIBlendFactor::SrcAlpha: return VK_BLEND_FACTOR_SRC_ALPHA;
        case RHIBlendFactor::OneMinusSrcAlpha: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case RHIBlendFactor::DstAlpha: return VK_BLEND_FACTOR_DST_ALPHA;
        case RHIBlendFactor::OneMinusDstAlpha: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case RHIBlendFactor::ConstantColor: return VK_BLEND_FACTOR_CONSTANT_COLOR;
        case RHIBlendFactor::OneMinusConstantColor: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
        case RHIBlendFactor::ConstantAlpha: return VK_BLEND_FACTOR_CONSTANT_ALPHA;
        case RHIBlendFactor::OneMinusConstantAlpha: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
        case RHIBlendFactor::SrcAlphaSaturate: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
        case RHIBlendFactor::Src1Color: return VK_BLEND_FACTOR_SRC1_COLOR;
        case RHIBlendFactor::OneMinusSrc1Color: return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
        case RHIBlendFactor::Src1Alpha: return VK_BLEND_FACTOR_SRC1_ALPHA;
        case RHIBlendFactor::OneMinusSrc1Alpha: return VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
        default: return VK_BLEND_FACTOR_ONE;
    }
}

VkBlendOp ToVkBlendOp(RHIBlendOp op) {
    switch (op) {
        case RHIBlendOp::Add: return VK_BLEND_OP_ADD;
        case RHIBlendOp::Subtract: return VK_BLEND_OP_SUBTRACT;
        case RHIBlendOp::ReverseSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
        case RHIBlendOp::Min: return VK_BLEND_OP_MIN;
        case RHIBlendOp::Max: return VK_BLEND_OP_MAX;
        default: return VK_BLEND_OP_ADD;
    }
}

//=============================================================================
// Rasterizer State Conversion
//=============================================================================

VkCullModeFlags ToVkCullMode(RHICullMode mode) {
    switch (mode) {
        case RHICullMode::None: return VK_CULL_MODE_NONE;
        case RHICullMode::Front: return VK_CULL_MODE_FRONT_BIT;
        case RHICullMode::Back: return VK_CULL_MODE_BACK_BIT;
        default: return VK_CULL_MODE_BACK_BIT;
    }
}

VkPolygonMode ToVkPolygonMode(RHIFillMode mode) {
    switch (mode) {
        case RHIFillMode::Solid: return VK_POLYGON_MODE_FILL;
        case RHIFillMode::Wireframe: return VK_POLYGON_MODE_LINE;
        default: return VK_POLYGON_MODE_FILL;
    }
}

VkFrontFace ToVkFrontFace(RHIFrontFace face) {
    switch (face) {
        case RHIFrontFace::CounterClockwise: return VK_FRONT_FACE_COUNTER_CLOCKWISE;
        case RHIFrontFace::Clockwise: return VK_FRONT_FACE_CLOCKWISE;
        default: return VK_FRONT_FACE_COUNTER_CLOCKWISE;
    }
}

//=============================================================================
// Topology Conversion
//=============================================================================

VkPrimitiveTopology ToVkTopology(RHIPrimitiveTopology topology) {
    switch (topology) {
        case RHIPrimitiveTopology::PointList: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case RHIPrimitiveTopology::LineList: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case RHIPrimitiveTopology::LineStrip: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        case RHIPrimitiveTopology::TriangleList: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case RHIPrimitiveTopology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case RHIPrimitiveTopology::TriangleFan: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
        case RHIPrimitiveTopology::LineListWithAdjacency: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY;
        case RHIPrimitiveTopology::LineStripWithAdjacency: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY;
        case RHIPrimitiveTopology::TriangleListWithAdjacency: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY;
        case RHIPrimitiveTopology::TriangleStripWithAdjacency: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY;
        case RHIPrimitiveTopology::PatchList: return VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
        default: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

VkIndexType ToVkIndexType(RHIIndexType type) {
    switch (type) {
        case RHIIndexType::UInt16: return VK_INDEX_TYPE_UINT16;
        case RHIIndexType::UInt32: return VK_INDEX_TYPE_UINT32;
        default: return VK_INDEX_TYPE_UINT32;
    }
}

//=============================================================================
// Stencil Op Conversion
//=============================================================================

VkStencilOp ToVkStencilOp(RHIStencilOp op) {
    switch (op) {
        case RHIStencilOp::Keep: return VK_STENCIL_OP_KEEP;
        case RHIStencilOp::Zero: return VK_STENCIL_OP_ZERO;
        case RHIStencilOp::Replace: return VK_STENCIL_OP_REPLACE;
        case RHIStencilOp::IncrementClamp: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
        case RHIStencilOp::DecrementClamp: return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
        case RHIStencilOp::Invert: return VK_STENCIL_OP_INVERT;
        case RHIStencilOp::IncrementWrap: return VK_STENCIL_OP_INCREMENT_AND_WRAP;
        case RHIStencilOp::DecrementWrap: return VK_STENCIL_OP_DECREMENT_AND_WRAP;
        default: return VK_STENCIL_OP_KEEP;
    }
}

//=============================================================================
// Image Type Conversion
//=============================================================================

VkImageType ToVkImageType(RHITextureDimension dim) {
    switch (dim) {
        case RHITextureDimension::Texture1D: return VK_IMAGE_TYPE_1D;
        case RHITextureDimension::Texture2D:
        case RHITextureDimension::TextureCube: return VK_IMAGE_TYPE_2D;
        case RHITextureDimension::Texture3D: return VK_IMAGE_TYPE_3D;
        default: return VK_IMAGE_TYPE_2D;
    }
}

VkImageViewType ToVkImageViewType(RHITextureDimension dim, bool isArray) {
    switch (dim) {
        case RHITextureDimension::Texture1D:
            return isArray ? VK_IMAGE_VIEW_TYPE_1D_ARRAY : VK_IMAGE_VIEW_TYPE_1D;
        case RHITextureDimension::Texture2D:
            return isArray ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
        case RHITextureDimension::TextureCube:
            return isArray ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY : VK_IMAGE_VIEW_TYPE_CUBE;
        case RHITextureDimension::Texture3D:
            return VK_IMAGE_VIEW_TYPE_3D;
        default:
            return VK_IMAGE_VIEW_TYPE_2D;
    }
}

//=============================================================================
// Sample Count Conversion
//=============================================================================

VkSampleCountFlagBits ToVkSampleCount(RHISampleCount count) {
    switch (count) {
        case RHISampleCount::Count1: return VK_SAMPLE_COUNT_1_BIT;
        case RHISampleCount::Count2: return VK_SAMPLE_COUNT_2_BIT;
        case RHISampleCount::Count4: return VK_SAMPLE_COUNT_4_BIT;
        case RHISampleCount::Count8: return VK_SAMPLE_COUNT_8_BIT;
        case RHISampleCount::Count16: return VK_SAMPLE_COUNT_16_BIT;
        case RHISampleCount::Count32: return VK_SAMPLE_COUNT_32_BIT;
        case RHISampleCount::Count64: return VK_SAMPLE_COUNT_64_BIT;
        default: return VK_SAMPLE_COUNT_1_BIT;
    }
}

//=============================================================================
// Border Color Conversion
//=============================================================================

VkBorderColor ToVkBorderColor(RHIBorderColor color) {
    switch (color) {
        case RHIBorderColor::TransparentBlack: return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        case RHIBorderColor::OpaqueBlack: return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        case RHIBorderColor::OpaqueWhite: return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        default: return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    }
}

//=============================================================================
// Primitive Topology Conversion (alias for consistency)
//=============================================================================

VkPrimitiveTopology ToVkPrimitiveTopology(RHIPrimitiveTopology topology) {
    return ToVkTopology(topology);
}

} // namespace Sanic

#endif // SANIC_ENABLE_VULKAN
