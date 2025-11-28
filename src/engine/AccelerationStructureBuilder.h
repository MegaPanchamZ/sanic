#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include "Mesh.h"
#include "GameObject.h"

struct AccelerationStructure {
    VkAccelerationStructureKHR handle;
    VkDeviceMemory memory;
    VkDeviceAddress deviceAddress;
    VkBuffer buffer; // Backing buffer
};

class AccelerationStructureBuilder {
public:
    AccelerationStructureBuilder(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue queue);
    ~AccelerationStructureBuilder();

    // Build Bottom-Level Acceleration Structure (BLAS) for a list of meshes
    // Returns a vector of BLAS, one for each mesh
    std::vector<AccelerationStructure> buildBLAS(const std::vector<std::shared_ptr<Mesh>>& meshes);

    // Build Top-Level Acceleration Structure (TLAS) for a list of game objects
    // Requires the list of BLAS created previously
    AccelerationStructure buildTLAS(const std::vector<GameObject>& gameObjects, const std::vector<AccelerationStructure>& blasList);

    void cleanupAccelerationStructure(AccelerationStructure& as);

private:
    VkDevice device;
    VkPhysicalDevice physicalDevice;
    VkCommandPool commandPool;
    VkQueue queue;

    // Ray Tracing function pointers
    PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR;
    PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR;
    PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR;
    PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR;
    PFN_vkGetBufferDeviceAddressKHR vkGetBufferDeviceAddressKHR;
    PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR;

    // Helper functions
    uint64_t getBufferDeviceAddress(VkBuffer buffer);
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    
    // Command buffer helpers
    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer commandBuffer);
};
