#pragma once
#include "VulkanContext.h"
#include "GameObject.h"
#include <vector>

class MeshletStreamer {
public:
    MeshletStreamer(VulkanContext& context);
    ~MeshletStreamer();

    void update(VkCommandBuffer cmd, const std::vector<GameObject>& gameObjects);
    
    // Returns the buffer containing indirect draw commands for the hardware rasterizer
    VkBuffer getIndirectDrawBuffer() const { return indirectDrawBuffer; }
    
    // Returns the buffer containing indirect dispatch commands for the software rasterizer
    VkBuffer getIndirectDispatchBuffer() const { return indirectDispatchBuffer; }

private:
    VulkanContext& context;
    
    VkBuffer indirectDrawBuffer;
    VkDeviceMemory indirectDrawBufferMemory;
    
    VkBuffer indirectDispatchBuffer;
    VkDeviceMemory indirectDispatchBufferMemory;
    
    VkPipelineLayout pipelineLayout;
    VkPipeline pipeline;
    VkDescriptorSetLayout descriptorSetLayout;
    VkDescriptorSet descriptorSet;
    VkDescriptorPool descriptorPool;

    void createBuffers();
    void createPipeline();
    void createDescriptorSetLayout();
    void createDescriptorPool();
    void createDescriptorSet();
    
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    VkShaderModule createShaderModule(const std::vector<char>& code);
    std::vector<char> readFile(const std::string& filename);
};
