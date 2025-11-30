#include "Mesh.h"
#include "ClusterHierarchy.h"
#include "VulkanContext.h"
#include <cstring>
#include <iostream>
#include <cstdint>
#include <stdexcept>
#include <meshoptimizer.h>

Mesh::Mesh(VkPhysicalDevice physicalDevice, VkDevice device, VkCommandPool commandPool, VkQueue graphicsQueue, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
    : device(device) {
    std::cout << "  Mesh: Creating with " << vertices.size() << " vertices and " << indices.size() << " indices" << std::endl;
    
    // Cache vertex and index data for later cluster hierarchy building
    cachedVertices = vertices;
    cachedIndices = indices;
    
    std::cout << "  Mesh: Creating vertex buffer..." << std::endl;
    createVertexBuffer(physicalDevice, commandPool, graphicsQueue, vertices);
    std::cout << "  Mesh: Creating index buffer..." << std::endl;
    createIndexBuffer(physicalDevice, commandPool, graphicsQueue, indices);
    std::cout << "  Mesh: Building meshlets..." << std::endl;
    buildMeshlets(physicalDevice, commandPool, graphicsQueue, vertices, indices);
    std::cout << "  Mesh: Construction complete" << std::endl;
}

Mesh::~Mesh() {
    vkDestroyBuffer(device, indexBuffer, nullptr);
    vkFreeMemory(device, indexBufferMemory, nullptr);
    vkDestroyBuffer(device, vertexBuffer, nullptr);
    vkFreeMemory(device, vertexBufferMemory, nullptr);

    vkDestroyBuffer(device, meshletBuffer, nullptr);
    vkFreeMemory(device, meshletBufferMemory, nullptr);
    vkDestroyBuffer(device, meshletVerticesBuffer, nullptr);
    vkFreeMemory(device, meshletVerticesBufferMemory, nullptr);
    vkDestroyBuffer(device, meshletTrianglesBuffer, nullptr);
    vkFreeMemory(device, meshletTrianglesBufferMemory, nullptr);
}

void Mesh::bind(VkCommandBuffer commandBuffer) {
    VkBuffer vertexBuffers[] = {vertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
}

void Mesh::draw(VkCommandBuffer commandBuffer) {
    vkCmdDrawIndexed(commandBuffer, indexCount, 1, 0, 0, 0);
}

void Mesh::createVertexBuffer(VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue graphicsQueue, const std::vector<Vertex>& vertices) {
    VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();
    std::cout << "    VB: bufferSize=" << bufferSize << std::endl;

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    std::cout << "    VB: creating staging buffer..." << std::endl;
    createBuffer(physicalDevice, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);
    std::cout << "    VB: staging buffer created" << std::endl;

    void* data;
    std::cout << "    VB: mapping memory..." << std::endl;
    vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, vertices.data(), (size_t)bufferSize);
    vkUnmapMemory(device, stagingBufferMemory);
    std::cout << "    VB: memory mapped and copied" << std::endl;

    std::cout << "    VB: creating device local buffer..." << std::endl;
    createBuffer(physicalDevice, bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vertexBuffer, vertexBufferMemory);
    std::cout << "    VB: getting buffer address..." << std::endl;
    vertexBufferAddress = getBufferAddress(vertexBuffer);
    std::cout << "    VB: buffer address = " << vertexBufferAddress << std::endl;

    std::cout << "    VB: copying buffer..." << std::endl;
    copyBuffer(commandPool, graphicsQueue, stagingBuffer, vertexBuffer, bufferSize);
    std::cout << "    VB: buffer copied" << std::endl;

    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);
    std::cout << "    VB: done" << std::endl;
}

VkDeviceAddress Mesh::getBufferAddress(VkBuffer buffer) {
    VkBufferDeviceAddressInfo bufferDeviceAddressInfo{};
    bufferDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    bufferDeviceAddressInfo.buffer = buffer;
    return vkGetBufferDeviceAddress(device, &bufferDeviceAddressInfo);
}

void Mesh::createIndexBuffer(VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue graphicsQueue, const std::vector<uint32_t>& indices) {
    indexCount = static_cast<uint32_t>(indices.size());
    VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    createBuffer(physicalDevice, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, indices.data(), (size_t)bufferSize);
    vkUnmapMemory(device, stagingBufferMemory);

    createBuffer(physicalDevice, bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, indexBuffer, indexBufferMemory);
    indexBufferAddress = getBufferAddress(indexBuffer);

    copyBuffer(commandPool, graphicsQueue, stagingBuffer, indexBuffer, bufferSize);

    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);
}

void Mesh::buildMeshlets(VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue graphicsQueue, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    std::cout << "  buildMeshlets: indices=" << indices.size() << " vertices=" << vertices.size() << std::endl;
    
    size_t max_meshlets = meshopt_buildMeshletsBound(indices.size(), 64, 124);
    std::cout << "  max_meshlets bound = " << max_meshlets << std::endl;
    
    std::vector<meshopt_Meshlet> localMeshlets(max_meshlets);
    std::vector<unsigned int> meshlet_vertices(max_meshlets * 64);
    std::vector<unsigned char> meshlet_triangles(max_meshlets * 124 * 3);

    meshletCount = meshopt_buildMeshlets(localMeshlets.data(), meshlet_vertices.data(), meshlet_triangles.data(),
                                         indices.data(), indices.size(), &vertices[0].pos.x, vertices.size(), sizeof(Vertex),
                                         64, 124, 0.5f);
    
    std::cout << "  meshletCount = " << meshletCount << std::endl;
    
    if (meshletCount == 0) {
        std::cout << "  WARNING: No meshlets generated, skipping meshlet buffer creation" << std::endl;
        // Create empty buffers to avoid null handles
        VkDeviceSize dummySize = sizeof(Meshlet);
        createBuffer(physicalDevice, dummySize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, meshletBuffer, meshletBufferMemory);
        meshletBufferAddress = getBufferAddress(meshletBuffer);
        
        dummySize = sizeof(unsigned int);
        createBuffer(physicalDevice, dummySize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, meshletVerticesBuffer, meshletVerticesBufferMemory);
        meshletVerticesBufferAddress = getBufferAddress(meshletVerticesBuffer);
        
        dummySize = sizeof(unsigned char);
        createBuffer(physicalDevice, dummySize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, meshletTrianglesBuffer, meshletTrianglesBufferMemory);
        meshletTrianglesBufferAddress = getBufferAddress(meshletTrianglesBuffer);
        return;
    }

    const meshopt_Meshlet& last = localMeshlets[meshletCount - 1];
    meshlet_vertices.resize(last.vertex_offset + last.vertex_count);
    meshlet_triangles.resize(last.triangle_offset + ((last.triangle_count * 3 + 3) & ~3));
    localMeshlets.resize(meshletCount);

    // Convert to our GPU-friendly struct and calculate bounds
    std::vector<Meshlet> gpuMeshlets(meshletCount);
    for (size_t i = 0; i < meshletCount; ++i) {
        const auto& m = localMeshlets[i];
        auto& gm = gpuMeshlets[i];
        
        gm.vertex_offset = m.vertex_offset;
        gm.triangle_offset = m.triangle_offset;
        gm.vertex_count = m.vertex_count;
        gm.triangle_count = m.triangle_count;

        meshopt_Bounds bounds = meshopt_computeMeshletBounds(&meshlet_vertices[m.vertex_offset], &meshlet_triangles[m.triangle_offset],
                                                             m.triangle_count, &vertices[0].pos.x, vertices.size(), sizeof(Vertex));
        
        memcpy(gm.center, bounds.center, sizeof(float) * 3);
        gm.radius = bounds.radius;
        memcpy(gm.cone_axis, bounds.cone_axis_s8, sizeof(int8_t) * 3);
        gm.cone_cutoff = bounds.cone_cutoff_s8;
    }

    // Create Buffers
    // 1. Meshlets
    {
        VkDeviceSize bufferSize = sizeof(Meshlet) * gpuMeshlets.size();
        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        createBuffer(physicalDevice, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

        void* data;
        vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
        memcpy(data, gpuMeshlets.data(), (size_t)bufferSize);
        vkUnmapMemory(device, stagingBufferMemory);

        createBuffer(physicalDevice, bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, meshletBuffer, meshletBufferMemory);
        meshletBufferAddress = getBufferAddress(meshletBuffer);
        
        copyBuffer(commandPool, graphicsQueue, stagingBuffer, meshletBuffer, bufferSize);
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);
    }

    // 2. Meshlet Vertices
    {
        VkDeviceSize bufferSize = sizeof(unsigned int) * meshlet_vertices.size();
        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        createBuffer(physicalDevice, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

        void* data;
        vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
        memcpy(data, meshlet_vertices.data(), (size_t)bufferSize);
        vkUnmapMemory(device, stagingBufferMemory);

        createBuffer(physicalDevice, bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, meshletVerticesBuffer, meshletVerticesBufferMemory);
        meshletVerticesBufferAddress = getBufferAddress(meshletVerticesBuffer);

        copyBuffer(commandPool, graphicsQueue, stagingBuffer, meshletVerticesBuffer, bufferSize);
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);
    }

    // 3. Meshlet Triangles
    {
        VkDeviceSize bufferSize = sizeof(unsigned char) * meshlet_triangles.size();
        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        createBuffer(physicalDevice, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

        void* data;
        vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
        memcpy(data, meshlet_triangles.data(), (size_t)bufferSize);
        vkUnmapMemory(device, stagingBufferMemory);

        createBuffer(physicalDevice, bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, meshletTrianglesBuffer, meshletTrianglesBufferMemory);
        meshletTrianglesBufferAddress = getBufferAddress(meshletTrianglesBuffer);

        copyBuffer(commandPool, graphicsQueue, stagingBuffer, meshletTrianglesBuffer, bufferSize);
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);
    }
    
    std::cout << "Generated " << meshletCount << " meshlets." << std::endl;
}

void Mesh::createBuffer(VkPhysicalDevice physicalDevice, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memRequirements.memoryTypeBits, properties);

    // Enable Buffer Device Address if requested (for RT/Bindless)
    // Note: flagsInfo must stay in scope until vkAllocateMemory is called
    VkMemoryAllocateFlagsInfo flagsInfo{};
    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
        flagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        flagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        allocInfo.pNext = &flagsInfo;
    }

    if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate buffer memory!");
    }

    vkBindBufferMemory(device, buffer, bufferMemory, 0);
}

void Mesh::copyBuffer(VkCommandPool commandPool, VkQueue graphicsQueue, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);

    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

uint32_t Mesh::findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("failed to find suitable memory type!");
}

void Mesh::buildClusterHierarchy(VulkanContext& context, uint32_t maxLodLevels) {
    if (cachedVertices.empty() || cachedIndices.empty()) {
        std::cerr << "Cannot build cluster hierarchy: no cached mesh data" << std::endl;
        return;
    }
    
    // Extract positions from vertices for ClusterHierarchy
    std::vector<glm::vec3> positions;
    positions.reserve(cachedVertices.size());
    for (const auto& v : cachedVertices) {
        positions.push_back(v.pos);
    }
    
    // Create cluster hierarchy with LOD levels
    clusterHierarchy = std::make_unique<ClusterHierarchy>(context);
    
    // Build using LOD-based approach (more sophisticated than simple meshlet-based)
    clusterHierarchy->buildWithLOD(positions, cachedIndices, maxLodLevels);
    
    std::cout << "Built cluster hierarchy with " << clusterHierarchy->getClusterCount() 
              << " clusters across " << clusterHierarchy->getLODLevels().size() << " LOD levels" << std::endl;
}
