#ifdef SANIC_ENABLE_VULKAN

#include "VulkanRHI.h"

namespace Sanic {

// ============================================================================
// VulkanBuffer Implementation
// ============================================================================
VulkanBuffer::VulkanBuffer(VulkanRHI* rhi, const RHIBufferDesc& desc)
    : m_rhi(rhi), m_desc(desc) {
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = desc.size;
    bufferInfo.usage = ToVkBufferUsage(desc.usage);
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    // Always enable buffer device address for storage buffers
    if (hasFlag(desc.usage, RHIBufferUsage::StorageBuffer) ||
        hasFlag(desc.usage, RHIBufferUsage::AccelerationStructure)) {
        bufferInfo.usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    }
    
    VmaAllocationCreateInfo allocInfo{};
    switch (desc.memoryType) {
        case RHIMemoryType::Default:
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            break;
        case RHIMemoryType::Upload:
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
            allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                             VMA_ALLOCATION_CREATE_MAPPED_BIT;
            break;
        case RHIMemoryType::Readback:
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
            allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                             VMA_ALLOCATION_CREATE_MAPPED_BIT;
            break;
    }
    
    VmaAllocationInfo allocationInfo;
    vmaCreateBuffer(m_rhi->getAllocator(), &bufferInfo, &allocInfo,
                    &m_buffer, &m_allocation, &allocationInfo);
    
    if (desc.persistentlyMapped && allocationInfo.pMappedData) {
        m_mappedPtr = allocationInfo.pMappedData;
    }
    
    // Get GPU address if needed
    if (bufferInfo.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
        VkBufferDeviceAddressInfo addressInfo{};
        addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        addressInfo.buffer = m_buffer;
        m_gpuAddress = vkGetBufferDeviceAddress(m_rhi->getDevice(), &addressInfo);
    }
}

VulkanBuffer::~VulkanBuffer() {
    if (m_buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_rhi->getAllocator(), m_buffer, m_allocation);
    }
}

void* VulkanBuffer::map() {
    if (m_mappedPtr) return m_mappedPtr;
    
    void* data;
    vmaMapMemory(m_rhi->getAllocator(), m_allocation, &data);
    return data;
}

void VulkanBuffer::unmap() {
    if (!m_mappedPtr) {
        vmaUnmapMemory(m_rhi->getAllocator(), m_allocation);
    }
}

uint64_t VulkanBuffer::getGPUAddress() const {
    return m_gpuAddress;
}

// ============================================================================
// VulkanTexture Implementation
// ============================================================================
VulkanTexture::VulkanTexture(VulkanRHI* rhi, const RHITextureDesc& desc)
    : m_rhi(rhi), m_desc(desc), m_ownsImage(true) {
    
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = ToVkImageType(desc.dimension);
    imageInfo.format = ToVkFormat(desc.format);
    imageInfo.extent.width = desc.width;
    imageInfo.extent.height = desc.height;
    imageInfo.extent.depth = desc.depth;
    imageInfo.mipLevels = desc.mipLevels;
    imageInfo.arrayLayers = desc.arrayLayers;
    imageInfo.samples = ToVkSampleCount(desc.sampleCount);
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = ToVkImageUsage(desc.usage);
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    
    if (desc.dimension == RHITextureDimension::TextureCube) {
        imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    }
    
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    
    vmaCreateImage(m_rhi->getAllocator(), &imageInfo, &allocInfo,
                   &m_image, &m_allocation, nullptr);
    
    createDefaultView();
}

VulkanTexture::VulkanTexture(VulkanRHI* rhi, VkImage image, VkImageView view, const RHITextureDesc& desc)
    : m_rhi(rhi), m_desc(desc), m_image(image), m_defaultView(view), m_ownsImage(false) {
}

VulkanTexture::~VulkanTexture() {
    if (m_ownsImage) {
        if (m_defaultView != VK_NULL_HANDLE) {
            vkDestroyImageView(m_rhi->getDevice(), m_defaultView, nullptr);
        }
        if (m_image != VK_NULL_HANDLE) {
            vmaDestroyImage(m_rhi->getAllocator(), m_image, m_allocation);
        }
    }
}

void VulkanTexture::createDefaultView() {
    if (m_defaultView != VK_NULL_HANDLE) return;
    
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_image;
    viewInfo.viewType = ToVkImageViewType(m_desc.dimension, m_desc.arrayLayers > 1);
    viewInfo.format = ToVkFormat(m_desc.format);
    
    // Determine aspect mask
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    if (hasFlag(m_desc.usage, RHITextureUsage::DepthStencil)) {
        aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (m_desc.format == RHIFormat::D24_UNORM_S8_UINT ||
            m_desc.format == RHIFormat::D32_FLOAT_S8_UINT) {
            aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
    }
    
    viewInfo.subresourceRange.aspectMask = aspect;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = m_desc.mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = m_desc.arrayLayers;
    
    vkCreateImageView(m_rhi->getDevice(), &viewInfo, nullptr, &m_defaultView);
}

// ============================================================================
// VulkanTextureView Implementation
// ============================================================================
VulkanTextureView::VulkanTextureView(VulkanRHI* rhi, VulkanTexture* texture, RHIFormat format,
                                     uint32_t baseMip, uint32_t mipCount,
                                     uint32_t baseLayer, uint32_t layerCount)
    : m_rhi(rhi), m_texture(texture)
    , m_format(format == RHIFormat::Unknown ? texture->getFormat() : format)
    , m_baseMip(baseMip)
    , m_mipCount(mipCount == ~0u ? texture->getMipLevels() - baseMip : mipCount)
    , m_baseLayer(baseLayer)
    , m_layerCount(layerCount == ~0u ? texture->getArrayLayers() - baseLayer : layerCount) {
    
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = texture->getImage();
    viewInfo.viewType = ToVkImageViewType(texture->getDimension(), m_layerCount > 1);
    viewInfo.format = ToVkFormat(m_format);
    
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    if (hasFlag(texture->getUsage(), RHITextureUsage::DepthStencil)) {
        aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    }
    
    viewInfo.subresourceRange.aspectMask = aspect;
    viewInfo.subresourceRange.baseMipLevel = m_baseMip;
    viewInfo.subresourceRange.levelCount = m_mipCount;
    viewInfo.subresourceRange.baseArrayLayer = m_baseLayer;
    viewInfo.subresourceRange.layerCount = m_layerCount;
    
    vkCreateImageView(m_rhi->getDevice(), &viewInfo, nullptr, &m_view);
}

VulkanTextureView::~VulkanTextureView() {
    if (m_view != VK_NULL_HANDLE) {
        vkDestroyImageView(m_rhi->getDevice(), m_view, nullptr);
    }
}

// ============================================================================
// VulkanSampler Implementation
// ============================================================================
VulkanSampler::VulkanSampler(VulkanRHI* rhi, const RHISamplerDesc& desc)
    : m_rhi(rhi) {
    
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = ToVkFilter(desc.magFilter);
    samplerInfo.minFilter = ToVkFilter(desc.minFilter);
    samplerInfo.mipmapMode = desc.mipFilter == RHIMipmapMode::Linear ?
        VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = ToVkAddressMode(desc.addressU);
    samplerInfo.addressModeV = ToVkAddressMode(desc.addressV);
    samplerInfo.addressModeW = ToVkAddressMode(desc.addressW);
    samplerInfo.mipLodBias = desc.mipLodBias;
    samplerInfo.anisotropyEnable = desc.anisotropyEnable ? VK_TRUE : VK_FALSE;
    samplerInfo.maxAnisotropy = desc.maxAnisotropy;
    samplerInfo.compareEnable = desc.compareEnable ? VK_TRUE : VK_FALSE;
    samplerInfo.compareOp = ToVkCompareOp(desc.compareOp);
    samplerInfo.minLod = desc.minLod;
    samplerInfo.maxLod = desc.maxLod;
    samplerInfo.borderColor = ToVkBorderColor(desc.borderColor);
    samplerInfo.unnormalizedCoordinates = desc.unnormalizedCoordinates ? VK_TRUE : VK_FALSE;
    
    vkCreateSampler(m_rhi->getDevice(), &samplerInfo, nullptr, &m_sampler);
}

VulkanSampler::~VulkanSampler() {
    if (m_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_rhi->getDevice(), m_sampler, nullptr);
    }
}

// ============================================================================
// VulkanPipeline Implementation
// ============================================================================
VulkanPipeline::VulkanPipeline(VulkanRHI* rhi, RHIPipelineType type)
    : m_rhi(rhi), m_type(type) {
}

VulkanPipeline::~VulkanPipeline() {
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_rhi->getDevice(), m_pipeline, nullptr);
    }
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_rhi->getDevice(), m_pipelineLayout, nullptr);
    }
    for (auto layout : m_descriptorSetLayouts) {
        vkDestroyDescriptorSetLayout(m_rhi->getDevice(), layout, nullptr);
    }
}

VkPipelineBindPoint VulkanPipeline::getBindPoint() const {
    switch (m_type) {
        case RHIPipelineType::Graphics: return VK_PIPELINE_BIND_POINT_GRAPHICS;
        case RHIPipelineType::Compute: return VK_PIPELINE_BIND_POINT_COMPUTE;
        case RHIPipelineType::RayTracing: return VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR;
        default: return VK_PIPELINE_BIND_POINT_GRAPHICS;
    }
}

// ============================================================================
// VulkanFence Implementation
// ============================================================================
VulkanFence::VulkanFence(VulkanRHI* rhi, bool signaled)
    : m_rhi(rhi) {
    
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (signaled) {
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    }
    
    vkCreateFence(m_rhi->getDevice(), &fenceInfo, nullptr, &m_fence);
}

VulkanFence::~VulkanFence() {
    if (m_fence != VK_NULL_HANDLE) {
        vkDestroyFence(m_rhi->getDevice(), m_fence, nullptr);
    }
}

void VulkanFence::wait(uint64_t timeout) {
    vkWaitForFences(m_rhi->getDevice(), 1, &m_fence, VK_TRUE, timeout);
}

void VulkanFence::reset() {
    vkResetFences(m_rhi->getDevice(), 1, &m_fence);
}

bool VulkanFence::isSignaled() const {
    return vkGetFenceStatus(m_rhi->getDevice(), m_fence) == VK_SUCCESS;
}

void VulkanFence::signal(uint64_t value) {
    m_value = value;
    // Note: Regular fences can't be signaled from CPU; this is for timeline semaphore compatibility
}

// ============================================================================
// VulkanSemaphore Implementation
// ============================================================================
VulkanSemaphore::VulkanSemaphore(VulkanRHI* rhi)
    : m_rhi(rhi) {
    
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    
    vkCreateSemaphore(m_rhi->getDevice(), &semaphoreInfo, nullptr, &m_semaphore);
}

VulkanSemaphore::~VulkanSemaphore() {
    if (m_semaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(m_rhi->getDevice(), m_semaphore, nullptr);
    }
}

// ============================================================================
// VulkanQueryPool Implementation
// ============================================================================
VulkanQueryPool::VulkanQueryPool(VulkanRHI* rhi, VkQueryType type, uint32_t count)
    : m_rhi(rhi), m_count(count) {
    
    VkQueryPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    poolInfo.queryType = type;
    poolInfo.queryCount = count;
    
    if (type == VK_QUERY_TYPE_PIPELINE_STATISTICS) {
        poolInfo.pipelineStatistics = 
            VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT |
            VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT |
            VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT |
            VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT;
    }
    
    vkCreateQueryPool(m_rhi->getDevice(), &poolInfo, nullptr, &m_pool);
}

VulkanQueryPool::~VulkanQueryPool() {
    if (m_pool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(m_rhi->getDevice(), m_pool, nullptr);
    }
}

bool VulkanQueryPool::getResults(uint32_t firstQuery, uint32_t queryCount,
                                  void* data, size_t dataSize,
                                  size_t stride, bool wait) {
    VkQueryResultFlags flags = VK_QUERY_RESULT_64_BIT;
    if (wait) {
        flags |= VK_QUERY_RESULT_WAIT_BIT;
    }
    
    return vkGetQueryPoolResults(m_rhi->getDevice(), m_pool, firstQuery, queryCount,
                                  dataSize, data, stride, flags) == VK_SUCCESS;
}

// ============================================================================
// VulkanAccelerationStructure Implementation
// ============================================================================
VulkanAccelerationStructure::VulkanAccelerationStructure(VulkanRHI* rhi, bool isTopLevel, uint64_t size)
    : m_rhi(rhi), m_isTopLevel(isTopLevel), m_size(size) {
    
    // Create backing buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                       VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    
    vmaCreateBuffer(m_rhi->getAllocator(), &bufferInfo, &allocInfo,
                    &m_buffer, &m_allocation, nullptr);
    
    // Create acceleration structure
    VkAccelerationStructureCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.buffer = m_buffer;
    createInfo.size = size;
    createInfo.type = isTopLevel ? VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR :
                                   VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    
    auto func = (PFN_vkCreateAccelerationStructureKHR)vkGetDeviceProcAddr(
        m_rhi->getDevice(), "vkCreateAccelerationStructureKHR");
    if (func) {
        func(m_rhi->getDevice(), &createInfo, nullptr, &m_handle);
    }
}

VulkanAccelerationStructure::~VulkanAccelerationStructure() {
    if (m_handle != VK_NULL_HANDLE) {
        auto func = (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(
            m_rhi->getDevice(), "vkDestroyAccelerationStructureKHR");
        if (func) {
            func(m_rhi->getDevice(), m_handle, nullptr);
        }
    }
    if (m_buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_rhi->getAllocator(), m_buffer, m_allocation);
    }
}

uint64_t VulkanAccelerationStructure::getGPUAddress() const {
    VkAccelerationStructureDeviceAddressInfoKHR addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addressInfo.accelerationStructure = m_handle;
    
    auto func = (PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetDeviceProcAddr(
        m_rhi->getDevice(), "vkGetAccelerationStructureDeviceAddressKHR");
    if (func) {
        return func(m_rhi->getDevice(), &addressInfo);
    }
    return 0;
}

} // namespace Sanic

#endif // SANIC_ENABLE_VULKAN
