#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <glm/glm.hpp>
#include <memory>
#include "Texture.h"
#include "Mesh.h"

class Skybox {
public:
    Skybox(VkPhysicalDevice physicalDevice, VkDevice device, VkCommandPool commandPool, VkQueue queue);
    ~Skybox();

    void draw(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout);
    void createDescriptorSet(VkDescriptorPool descriptorPool, VkDescriptorSetLayout descriptorSetLayout, VkBuffer uniformBuffer, VkDeviceSize range);
    
    VkDescriptorSet getDescriptorSet() const { return descriptorSet; }
    VkImageView getImageView() const { return textureImageView; }
    VkSampler getSampler() const { return textureSampler; }
    uint32_t getMipLevels() const { return mipLevels; }

private:
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkCommandPool commandPool;
    VkQueue queue;

    std::shared_ptr<Mesh> mesh;
    VkImage textureImage;
    VkDeviceMemory textureImageMemory;
    VkImageView textureImageView;
    VkSampler textureSampler;
    uint32_t mipLevels = 1;

    VkDescriptorSet descriptorSet;

    void loadCubemap(const std::vector<std::string>& faces);
    void createTextureImageView();
    void createTextureSampler();
    void generateMipmaps(VkImage image, VkFormat format, int32_t texWidth, int32_t texHeight, uint32_t mipLevels);
    
    void createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory);
    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);
    void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);
    void transitionImageLayoutMips(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels);
    void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer commandBuffer);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
};
