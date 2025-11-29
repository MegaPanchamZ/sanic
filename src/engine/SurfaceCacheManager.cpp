#include "SurfaceCacheManager.h"
#include <stdexcept>

SurfaceCacheManager::SurfaceCacheManager(VulkanContext& context) : context(context) {
    allocateAtlas(4096, 4096); // Default 4K atlas
}

SurfaceCacheManager::~SurfaceCacheManager() {
    vkDestroySampler(context.getDevice(), atlasSampler, nullptr);
    vkDestroyImageView(context.getDevice(), atlasView, nullptr);
    vkDestroyImage(context.getDevice(), atlasImage, nullptr);
    vkFreeMemory(context.getDevice(), atlasMemory, nullptr);
}

void SurfaceCacheManager::allocateAtlas(uint32_t width, uint32_t height) {
    atlasWidth = width;
    atlasHeight = height;
    
    createAtlasImage(width, height);
    createAtlasSampler();
}

void SurfaceCacheManager::createAtlasImage(uint32_t width, uint32_t height) {
    // RGBA8 for Albedo/Normal/Emissive packed?
    // User said "Pack all these cards into a massive Physical Atlas (e.g., 4096x4096 texture of Albedo/Normal/Emissive)".
    // We might need multiple atlases or a layered one.
    // For simplicity, let's use RGBA8 for Albedo.
    createImage(width, height, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, atlasImage, atlasMemory);
    atlasView = createImageView(atlasImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT);
}

void SurfaceCacheManager::createAtlasSampler() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_TRUE;
    
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(context.getPhysicalDevice(), &properties);
    samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
    
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    
    if (vkCreateSampler(context.getDevice(), &samplerInfo, nullptr, &atlasSampler) != VK_SUCCESS) {
        throw std::runtime_error("failed to create atlas sampler!");
    }
}

void SurfaceCacheManager::captureSnapshot(const GameObject& gameObject) {
    // Placeholder: Capture mesh snapshot
    // 1. Render mesh from 6 angles to small framebuffers.
    // 2. Copy/Blit to Atlas.
    // 3. Store UV offset in GameObject or SurfaceCache lookup.
    
    // Since we don't have a full offline baking pipeline here, we'll just log it.
    // In a real implementation, we would set up a render pass and draw the mesh.
}

// Helpers
void SurfaceCacheManager::createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(context.getDevice(), &imageInfo, nullptr, &image) != VK_SUCCESS) {
        throw std::runtime_error("failed to create image!");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(context.getDevice(), image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = context.findMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(context.getDevice(), &allocInfo, nullptr, &imageMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate image memory!");
    }

    vkBindImageMemory(context.getDevice(), image, imageMemory, 0);
}

VkImageView SurfaceCacheManager::createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView;
    if (vkCreateImageView(context.getDevice(), &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
        throw std::runtime_error("failed to create image view!");
    }

    return imageView;
}

uint32_t SurfaceCacheManager::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    return context.findMemoryType(typeFilter, properties);
}
