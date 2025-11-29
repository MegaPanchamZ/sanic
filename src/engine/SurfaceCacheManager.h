#pragma once
#include "VulkanContext.h"
#include "GameObject.h"
#include <vector>

class SurfaceCacheManager {
public:
    SurfaceCacheManager(VulkanContext& context);
    ~SurfaceCacheManager();

    void allocateAtlas(uint32_t width, uint32_t height);
    void captureSnapshot(const GameObject& gameObject);
    
    VkImageView getAtlasImageView() const { return atlasView; }
    VkSampler getAtlasSampler() const { return atlasSampler; }

private:
    VulkanContext& context;
    
    VkImage atlasImage;
    VkDeviceMemory atlasMemory;
    VkImageView atlasView;
    VkSampler atlasSampler;
    
    uint32_t atlasWidth;
    uint32_t atlasHeight;
    
    // Simple allocator (cursor)
    uint32_t currentX = 0;
    uint32_t currentY = 0;
    uint32_t rowHeight = 0;

    void createAtlasImage(uint32_t width, uint32_t height);
    void createAtlasSampler();
    
    void createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory);
    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
};
