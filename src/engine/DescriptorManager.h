#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <array>
#include <memory>
#include <stdexcept>

class DescriptorManager {
public:
    static DescriptorManager& getInstance() {
        static DescriptorManager instance;
        return instance;
    }

    void init(VkDevice device, VkPhysicalDevice physicalDevice);
    void cleanup();

    VkDescriptorSetLayout getGlobalLayout() const { return globalLayout; }
    VkDescriptorSet getGlobalDescriptorSet() const { return globalDescriptorSet; }

    // Registration methods return the index in the bindless array
    uint32_t registerTexture(VkImageView imageView, VkSampler sampler);
    // uint32_t registerBuffer(VkBuffer buffer); // Future use

    void updateGlobalDescriptorSet();

private:
    DescriptorManager() = default;
    ~DescriptorManager() = default;

    VkDevice device{VK_NULL_HANDLE};
    VkPhysicalDevice physicalDevice{VK_NULL_HANDLE};

    VkDescriptorSetLayout globalLayout{VK_NULL_HANDLE};
    VkDescriptorPool descriptorPool{VK_NULL_HANDLE};
    VkDescriptorSet globalDescriptorSet{VK_NULL_HANDLE};

    // Bindless limits
    const uint32_t MAX_TEXTURES = 4096;
    const uint32_t MAX_BUFFERS = 1024;
    const uint32_t MAX_SAMPLERS = 128;

    void createGlobalLayout();
    void createDescriptorPool();
    void allocateDescriptorSet();
};
