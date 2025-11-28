#include "DescriptorManager.h"
#include <iostream>

void DescriptorManager::init(VkDevice device, VkPhysicalDevice physicalDevice) {
    this->device = device;
    this->physicalDevice = physicalDevice;

    createGlobalLayout();
    createDescriptorPool();
    allocateDescriptorSet();
}

void DescriptorManager::cleanup() {
    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    }
    if (globalLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, globalLayout, nullptr);
    }
}

void DescriptorManager::createGlobalLayout() {
    std::vector<VkDescriptorSetLayoutBinding> bindings;

    // Binding 0: Global UBO
    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding = 0;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_ALL;
    bindings.push_back(uboBinding);

    // Binding 1: Storage Buffers (Unbounded)
    VkDescriptorSetLayoutBinding storageBinding{};
    storageBinding.binding = 1;
    storageBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    storageBinding.descriptorCount = MAX_BUFFERS;
    storageBinding.stageFlags = VK_SHADER_STAGE_ALL;
    bindings.push_back(storageBinding);

    // Binding 2: Samplers (Unbounded)
    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding = 2;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    samplerBinding.descriptorCount = MAX_SAMPLERS;
    samplerBinding.stageFlags = VK_SHADER_STAGE_ALL;
    bindings.push_back(samplerBinding);

    // Binding 3: Sampled Images (Unbounded)
    VkDescriptorSetLayoutBinding imageBinding{};
    imageBinding.binding = 3;
    imageBinding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    imageBinding.descriptorCount = MAX_TEXTURES;
    imageBinding.stageFlags = VK_SHADER_STAGE_ALL;
    bindings.push_back(imageBinding);

    // Binding flags for partial binding
    std::vector<VkDescriptorBindingFlags> bindingFlags = {
        0, // UBO
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT, // Storage
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT, // Samplers
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT  // Images
    };

    VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{};
    bindingFlagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    bindingFlagsInfo.bindingCount = static_cast<uint32_t>(bindingFlags.size());
    bindingFlagsInfo.pBindingFlags = bindingFlags.data();

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.pNext = &bindingFlagsInfo;
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &globalLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create global descriptor set layout!");
    }
}

void DescriptorManager::createDescriptorPool() {
    std::vector<VkDescriptorPoolSize> poolSizes = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, MAX_BUFFERS},
        {VK_DESCRIPTOR_TYPE_SAMPLER, MAX_SAMPLERS},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, MAX_TEXTURES}
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 1;

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create global descriptor pool!");
    }
}

void DescriptorManager::allocateDescriptorSet() {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &globalLayout;

    if (vkAllocateDescriptorSets(device, &allocInfo, &globalDescriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate global descriptor set!");
    }
}

uint32_t DescriptorManager::registerTexture(VkImageView imageView, VkSampler sampler) {
    // TODO: Implement actual registration logic (tracking free slots)
    // For now, just a placeholder that would write to the descriptor set
    static uint32_t currentTextureIndex = 0;
    
    if (currentTextureIndex >= MAX_TEXTURES) {
        throw std::runtime_error("Max textures exceeded!");
    }

    uint32_t index = currentTextureIndex++;

    // Update Sampled Image
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = imageView;
    // imageInfo.sampler = sampler; // Sampler is separate in our new layout

    VkWriteDescriptorSet imageWrite{};
    imageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    imageWrite.dstSet = globalDescriptorSet;
    imageWrite.dstBinding = 3; // Sampled Images
    imageWrite.dstArrayElement = index;
    imageWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    imageWrite.descriptorCount = 1;
    imageWrite.pImageInfo = &imageInfo;

    // Update Sampler (assuming 1:1 mapping for now, or use a separate sampler manager)
    VkDescriptorImageInfo samplerInfo{};
    samplerInfo.sampler = sampler;

    VkWriteDescriptorSet samplerWrite{};
    samplerWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    samplerWrite.dstSet = globalDescriptorSet;
    samplerWrite.dstBinding = 2; // Samplers
    samplerWrite.dstArrayElement = index; // Using same index for simplicity for now
    samplerWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    samplerWrite.descriptorCount = 1;
    samplerWrite.pImageInfo = &samplerInfo;

    std::array<VkWriteDescriptorSet, 2> writes = {imageWrite, samplerWrite};
    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

    return index;
}
