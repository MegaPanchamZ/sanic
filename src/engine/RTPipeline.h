#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <memory>

struct RayTracingShaderGroup {
    VkRayTracingShaderGroupCreateInfoKHR groupInfo{};
};

class RTPipeline {
public:
    RTPipeline(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue queue);
    ~RTPipeline();

    void createPipeline(VkDescriptorSetLayout descriptorSetLayout);
    void trace(VkCommandBuffer commandBuffer, VkDescriptorSet descriptorSet, uint32_t width, uint32_t height);

    VkPipeline getPipeline() const { return rtPipeline; }
    VkPipelineLayout getPipelineLayout() const { return rtPipelineLayout; }

private:
    VkDevice device;
    VkPhysicalDevice physicalDevice;
    VkCommandPool commandPool;
    VkQueue queue;

    VkPipeline rtPipeline = VK_NULL_HANDLE;
    VkPipelineLayout rtPipelineLayout = VK_NULL_HANDLE;

    // Shader Binding Table
    VkBuffer sbtBuffer = VK_NULL_HANDLE;
    VkDeviceMemory sbtBufferMemory = VK_NULL_HANDLE;
    
    VkStridedDeviceAddressRegionKHR raygenRegion{};
    VkStridedDeviceAddressRegionKHR missRegion{};
    VkStridedDeviceAddressRegionKHR hitRegion{};
    VkStridedDeviceAddressRegionKHR callableRegion{};

    // Ray Tracing function pointers
    PFN_vkCreateRayTracingPipelinesKHR vkCreateRayTracingPipelinesKHR;
    PFN_vkGetRayTracingShaderGroupHandlesKHR vkGetRayTracingShaderGroupHandlesKHR;
    PFN_vkCmdTraceRaysKHR vkCmdTraceRaysKHR;
    PFN_vkGetBufferDeviceAddressKHR vkGetBufferDeviceAddressKHR;

    // Helpers
    void loadRayTracingFunctions();
    void createShaderBindingTable();
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    uint64_t getBufferDeviceAddress(VkBuffer buffer);
    VkShaderModule createShaderModule(const std::vector<char>& code);
    std::vector<char> readFile(const std::string& filename);
};
