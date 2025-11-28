#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include "Vertex.h"

struct Meshlet {
    float center[3];
    float radius;
    int8_t cone_axis[3];
    int8_t cone_cutoff;
    uint32_t vertex_offset;
    uint32_t triangle_offset;
    uint8_t vertex_count;
    uint8_t triangle_count;
    uint8_t padding[2]; // Explicit padding to match GLSL (32 bytes total)
};

class Mesh {
public:
    Mesh(VkPhysicalDevice physicalDevice, VkDevice device, VkCommandPool commandPool, VkQueue graphicsQueue, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
    ~Mesh();

    void bind(VkCommandBuffer commandBuffer);
    void draw(VkCommandBuffer commandBuffer);

    // Getters for meshlet buffers (for binding in mesh shader)
    VkBuffer getMeshletBuffer() const { return meshletBuffer; }
    VkBuffer getMeshletVerticesBuffer() const { return meshletVerticesBuffer; }
    VkBuffer getMeshletTrianglesBuffer() const { return meshletTrianglesBuffer; }
    uint32_t getMeshletCount() const { return meshletCount; }

    // Getters for Buffer Device Addresses (BDA)
    VkDeviceAddress getVertexBufferAddress() const { return vertexBufferAddress; }
    VkDeviceAddress getMeshletBufferAddress() const { return meshletBufferAddress; }
    VkDeviceAddress getMeshletVerticesBufferAddress() const { return meshletVerticesBufferAddress; }
    VkDeviceAddress getMeshletTrianglesBufferAddress() const { return meshletTrianglesBufferAddress; }

private:
    VkDevice device;
    VkBuffer vertexBuffer;
    VkDeviceMemory vertexBufferMemory;
    VkDeviceAddress vertexBufferAddress;
    VkBuffer indexBuffer;
    VkDeviceMemory indexBufferMemory;
    uint32_t indexCount;

    // Meshlet Resources
    VkBuffer meshletBuffer;
    VkDeviceMemory meshletBufferMemory;
    VkDeviceAddress meshletBufferAddress;
    VkBuffer meshletVerticesBuffer;
    VkDeviceMemory meshletVerticesBufferMemory;
    VkDeviceAddress meshletVerticesBufferAddress;
    VkBuffer meshletTrianglesBuffer;
    VkDeviceMemory meshletTrianglesBufferMemory;
    VkDeviceAddress meshletTrianglesBufferAddress;
    uint32_t meshletCount = 0;

    VkDeviceAddress getBufferAddress(VkBuffer buffer);

    void createVertexBuffer(VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue graphicsQueue, const std::vector<Vertex>& vertices);
    void createIndexBuffer(VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue graphicsQueue, const std::vector<uint32_t>& indices);
    void buildMeshlets(VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue graphicsQueue, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
    
    void createBuffer(VkPhysicalDevice physicalDevice, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
    void copyBuffer(VkCommandPool commandPool, VkQueue graphicsQueue, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
    uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties);
};
