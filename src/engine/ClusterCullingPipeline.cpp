#include "ClusterCullingPipeline.h"
#include "VulkanContext.h"
#include <fstream>
#include <iostream>
#include <cstring>
#include <stdexcept>

// ============================================================================
// Constructor / Destructor
// ============================================================================

ClusterCullingPipeline::ClusterCullingPipeline(VulkanContext& context, const CullingConfig& config)
    : context(context), config(config)
{
    createDescriptorSetLayout();
    createPipelineLayout();
    createComputePipelines();
    createBuffers();
    createDescriptorSets();
    updateDescriptorSets();
    
    std::cout << "[ClusterCullingPipeline] Initialized with:" << std::endl;
    std::cout << "  Max instances: " << config.maxInstances << std::endl;
    std::cout << "  Max candidate nodes: " << config.maxCandidateNodes << std::endl;
    std::cout << "  Max visible clusters: " << config.maxVisibleClusters << std::endl;
}

ClusterCullingPipeline::~ClusterCullingPipeline() {
    VkDevice device = context.getDevice();
    
    // Destroy pipelines
    if (instanceCullPipeline) vkDestroyPipeline(device, instanceCullPipeline, nullptr);
    if (clusterCullPipeline) vkDestroyPipeline(device, clusterCullPipeline, nullptr);
    if (hierarchicalCullPipeline) vkDestroyPipeline(device, hierarchicalCullPipeline, nullptr);
    if (cullPipelineLayout) vkDestroyPipelineLayout(device, cullPipelineLayout, nullptr);
    
    // Destroy descriptor resources
    if (descriptorPool) vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    if (cullDescriptorSetLayout) vkDestroyDescriptorSetLayout(device, cullDescriptorSetLayout, nullptr);
    if (outputDescriptorSetLayout) vkDestroyDescriptorSetLayout(device, outputDescriptorSetLayout, nullptr);
    
    // Destroy buffers
    auto destroyBuffer = [device](VkBuffer& buffer, VkDeviceMemory& memory) {
        if (buffer) vkDestroyBuffer(device, buffer, nullptr);
        if (memory) vkFreeMemory(device, memory, nullptr);
        buffer = VK_NULL_HANDLE;
        memory = VK_NULL_HANDLE;
    };
    
    destroyBuffer(instanceBuffer, instanceBufferMemory);
    destroyBuffer(clusterBuffer, clusterBufferMemory);
    destroyBuffer(hierarchyNodeBuffer, hierarchyNodeBufferMemory);
    destroyBuffer(candidateBufferA, candidateBufferAMemory);
    destroyBuffer(candidateBufferB, candidateBufferBMemory);
    destroyBuffer(queueStateBuffer, queueStateBufferMemory);
    destroyBuffer(visibleClusterBuffer, visibleClusterBufferMemory);
    destroyBuffer(drawIndirectBuffer, drawIndirectBufferMemory);
    destroyBuffer(statsReadbackBuffer, statsReadbackBufferMemory);
}

// ============================================================================
// Instance Management
// ============================================================================

uint32_t ClusterCullingPipeline::registerHierarchy(ClusterHierarchy* hierarchy, const glm::mat4& worldMatrix) {
    if (!hierarchy) {
        throw std::runtime_error("Cannot register null hierarchy");
    }
    
    uint32_t instanceIndex = static_cast<uint32_t>(instances.size());
    
    InstanceData instance{};
    instance.worldMatrix = worldMatrix;
    
    // Compute world-space bounding sphere
    // For now, use the root node bounds
    glm::vec4 localBounds(0.0f, 0.0f, 0.0f, 10.0f); // Default bounds
    
    // Transform center to world space
    glm::vec4 worldCenter = worldMatrix * glm::vec4(localBounds.x, localBounds.y, localBounds.z, 1.0f);
    
    // Scale radius by max scale factor
    glm::vec3 scale(
        glm::length(glm::vec3(worldMatrix[0])),
        glm::length(glm::vec3(worldMatrix[1])),
        glm::length(glm::vec3(worldMatrix[2]))
    );
    float maxScale = glm::max(scale.x, glm::max(scale.y, scale.z));
    
    instance.boundingSphere = glm::vec4(worldCenter.x, worldCenter.y, worldCenter.z, localBounds.w * maxScale);
    instance.hierarchyOffset = totalNodeCount;
    instance.clusterOffset = totalClusterCount;
    instance.clusterCount = hierarchy->getClusterCount();
    instance.flags = 0;
    
    instances.push_back(instance);
    hierarchies.push_back(hierarchy);
    
    totalClusterCount += hierarchy->getClusterCount();
    totalNodeCount += hierarchy->getNodeCount();
    
    buffersDirty = true;
    
    std::cout << "[ClusterCullingPipeline] Registered hierarchy " << instanceIndex 
              << " with " << hierarchy->getClusterCount() << " clusters, "
              << hierarchy->getNodeCount() << " nodes" << std::endl;
    
    return instanceIndex;
}

void ClusterCullingPipeline::updateInstanceTransform(uint32_t instanceIndex, const glm::mat4& worldMatrix) {
    if (instanceIndex >= instances.size()) {
        throw std::runtime_error("Invalid instance index");
    }
    
    instances[instanceIndex].worldMatrix = worldMatrix;
    
    // Update world-space bounding sphere
    glm::vec4 localBounds(0.0f, 0.0f, 0.0f, 10.0f);
    glm::vec4 worldCenter = worldMatrix * glm::vec4(localBounds.x, localBounds.y, localBounds.z, 1.0f);
    
    glm::vec3 scale(
        glm::length(glm::vec3(worldMatrix[0])),
        glm::length(glm::vec3(worldMatrix[1])),
        glm::length(glm::vec3(worldMatrix[2]))
    );
    float maxScale = glm::max(scale.x, glm::max(scale.y, scale.z));
    
    instances[instanceIndex].boundingSphere = glm::vec4(
        worldCenter.x, worldCenter.y, worldCenter.z, 
        localBounds.w * maxScale
    );
    
    buffersDirty = true;
}

// ============================================================================
// Frame Management
// ============================================================================

void ClusterCullingPipeline::beginFrame(uint32_t frameIndex) {
    currentFrameIndex = frameIndex;
    
    // Rebuild buffers if instances changed
    if (buffersDirty) {
        rebuildBuffers();
        buffersDirty = false;
    }
}

// ============================================================================
// GPU Culling Dispatch
// ============================================================================

void ClusterCullingPipeline::performCulling(VkCommandBuffer cmd, const CullingParams& params) {
    if (instances.empty()) return;
    
    // Reset queue state
    VkBufferMemoryBarrier resetBarrier{};
    resetBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    resetBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    resetBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    resetBarrier.buffer = queueStateBuffer;
    resetBarrier.size = VK_WHOLE_SIZE;
    
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 1, &resetBarrier, 0, nullptr);
    
    // Clear queue state (visible count, etc.)
    vkCmdFillBuffer(cmd, queueStateBuffer, 0, sizeof(QueueState), 0);
    
    // Barrier after clear
    VkBufferMemoryBarrier postClearBarrier = resetBarrier;
    postClearBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    postClearBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 1, &postClearBarrier, 0, nullptr);
    
    // Prepare push constants
    CullPushConstants pc{};
    pc.viewProj = params.viewProjMatrix;
    for (int i = 0; i < 6; i++) {
        pc.frustumPlanes[i] = params.frustumPlanes[i];
    }
    pc.cameraPosition = glm::vec4(params.cameraPosition, params.nearPlane);
    pc.screenParams = glm::vec4(params.screenSize.x, params.screenSize.y, params.lodScale, params.errorThreshold);
    pc.clusterCount = totalClusterCount;
    pc.nodeCount = totalNodeCount;
    pc.frameIndex = params.frameIndex;
    pc.flags = 0;
    if (config.enableFrustumCulling) pc.flags |= 0x1;
    if (config.enableOcclusionCulling) pc.flags |= 0x2;
    if (config.enableBackfaceCulling) pc.flags |= 0x4;
    if (config.enableLODSelection) pc.flags |= 0x8;
    
    // Bind descriptor set
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
        cullPipelineLayout, 0, 1, &cullDescriptorSet, 0, nullptr);
    
    // Push constants
    vkCmdPushConstants(cmd, cullPipelineLayout, 
        VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(CullPushConstants), &pc);
    
    // Dispatch cluster culling
    // Using flat culling for now (simpler than hierarchical for initial implementation)
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, clusterCullPipeline);
    
    // Calculate dispatch size (64 threads per workgroup)
    uint32_t workgroupCount = (totalClusterCount + 63) / 64;
    vkCmdDispatch(cmd, workgroupCount, 1, 1);
    
    // Barrier before mesh shader consumption
    VkBufferMemoryBarrier outputBarrier{};
    outputBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    outputBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    outputBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    outputBarrier.buffer = visibleClusterBuffer;
    outputBarrier.size = VK_WHOLE_SIZE;
    
    VkBufferMemoryBarrier stateBarrier = outputBarrier;
    stateBarrier.buffer = queueStateBuffer;
    
    VkBufferMemoryBarrier barriers[] = { outputBarrier, stateBarrier };
    
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_MESH_SHADER_BIT_EXT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
        0, 0, nullptr, 2, barriers, 0, nullptr);
}

// ============================================================================
// Stats and Readback
// ============================================================================

uint32_t ClusterCullingPipeline::getVisibleClusterCount() const {
    // Read back from queue state buffer
    // This is a blocking operation - only use for debugging
    void* data;
    vkMapMemory(context.getDevice(), statsReadbackBufferMemory, 0, sizeof(QueueState), 0, &data);
    QueueState state;
    memcpy(&state, data, sizeof(QueueState));
    vkUnmapMemory(context.getDevice(), statsReadbackBufferMemory);
    
    return state.totalVisibleClusters;
}

ClusterCullingPipeline::CullingStats ClusterCullingPipeline::getStats() const {
    CullingStats stats{};
    stats.instancesProcessed = static_cast<uint32_t>(instances.size());
    
    // Read back queue state
    void* data;
    vkMapMemory(context.getDevice(), statsReadbackBufferMemory, 0, sizeof(QueueState), 0, &data);
    QueueState state;
    memcpy(&state, data, sizeof(QueueState));
    vkUnmapMemory(context.getDevice(), statsReadbackBufferMemory);
    
    stats.clustersVisible = state.totalVisibleClusters;
    stats.nodesTraversed = state.totalNodesProcessed;
    stats.clustersTested = 0; // Not tracked yet
    
    return stats;
}

// ============================================================================
// Initialization
// ============================================================================

void ClusterCullingPipeline::createDescriptorSetLayout() {
    VkDevice device = context.getDevice();
    
    // Culling descriptor set layout
    // Binding 0: Candidate queue A
    // Binding 1: Candidate queue B  
    // Binding 2: Visible cluster output
    // Binding 3: Queue state
    // Binding 4: Buffer addresses (UBO)
    
    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        // Candidate queue A
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        // Candidate queue B
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        // Visible clusters
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        // Queue state
        {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        // Buffer addresses
        {4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    };
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &cullDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create culling descriptor set layout");
    }
    
    // Output descriptor set layout (for mesh shader)
    std::vector<VkDescriptorSetLayoutBinding> outputBindings = {
        // Visible clusters (read-only for mesh shader)
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_TASK_BIT_EXT, nullptr},
        // Queue state (for indirect count)
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_TASK_BIT_EXT, nullptr},
    };
    
    layoutInfo.bindingCount = static_cast<uint32_t>(outputBindings.size());
    layoutInfo.pBindings = outputBindings.data();
    
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &outputDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create output descriptor set layout");
    }
}

void ClusterCullingPipeline::createPipelineLayout() {
    VkDevice device = context.getDevice();
    
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(CullPushConstants);
    
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &cullDescriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;
    
    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &cullPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create culling pipeline layout");
    }
}

void ClusterCullingPipeline::createComputePipelines() {
    VkDevice device = context.getDevice();
    
    // Load cluster cull shader
    auto clusterCullCode = readShaderFile("shaders/cluster_cull.comp.spv");
    VkShaderModule clusterCullModule = createShaderModule(clusterCullCode);
    
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = clusterCullModule;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = cullPipelineLayout;
    
    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &clusterCullPipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create cluster cull pipeline");
    }
    
    vkDestroyShaderModule(device, clusterCullModule, nullptr);
    
    // Load hierarchical cull shader
    auto hierCullCode = readShaderFile("shaders/cluster_cull_hierarchical.comp.spv");
    VkShaderModule hierCullModule = createShaderModule(hierCullCode);
    
    pipelineInfo.stage.module = hierCullModule;
    
    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &hierarchicalCullPipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create hierarchical cull pipeline");
    }
    
    vkDestroyShaderModule(device, hierCullModule, nullptr);
    
    std::cout << "[ClusterCullingPipeline] Compute pipelines created" << std::endl;
}

void ClusterCullingPipeline::createBuffers() {
    VkDevice device = context.getDevice();
    
    // Instance buffer
    VkDeviceSize instanceBufferSize = sizeof(InstanceData) * config.maxInstances;
    createBuffer(instanceBufferSize, 
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        instanceBuffer, instanceBufferMemory);
    instanceBufferAddress = getBufferAddress(instanceBuffer);
    
    // Candidate buffers (ping-pong)
    VkDeviceSize candidateBufferSize = sizeof(CandidateNode) * config.maxCandidateNodes;
    createBuffer(candidateBufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        candidateBufferA, candidateBufferAMemory);
    createBuffer(candidateBufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        candidateBufferB, candidateBufferBMemory);
    
    // Queue state buffer
    createBuffer(sizeof(QueueState),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        queueStateBuffer, queueStateBufferMemory);
    
    // Visible cluster buffer
    VkDeviceSize visibleBufferSize = sizeof(VisibleCluster) * config.maxVisibleClusters;
    createBuffer(visibleBufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        visibleClusterBuffer, visibleClusterBufferMemory);
    visibleClusterBufferAddress = getBufferAddress(visibleClusterBuffer);
    
    // Indirect draw buffer
    createBuffer(sizeof(VkDrawMeshTasksIndirectCommandEXT),
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        drawIndirectBuffer, drawIndirectBufferMemory);
    
    // Stats readback buffer (host visible for CPU access)
    createBuffer(sizeof(QueueState),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        statsReadbackBuffer, statsReadbackBufferMemory);
    
    std::cout << "[ClusterCullingPipeline] Buffers created" << std::endl;
}

void ClusterCullingPipeline::createDescriptorSets() {
    VkDevice device = context.getDevice();
    
    // Create descriptor pool
    std::vector<VkDescriptorPoolSize> poolSizes = {
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2},
    };
    
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 2;
    
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor pool");
    }
    
    // Allocate culling descriptor set
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &cullDescriptorSetLayout;
    
    if (vkAllocateDescriptorSets(device, &allocInfo, &cullDescriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate culling descriptor set");
    }
    
    // Allocate output descriptor set
    allocInfo.pSetLayouts = &outputDescriptorSetLayout;
    if (vkAllocateDescriptorSets(device, &allocInfo, &outputDescriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate output descriptor set");
    }
}

void ClusterCullingPipeline::updateDescriptorSets() {
    VkDevice device = context.getDevice();
    
    // Update culling descriptor set
    VkDescriptorBufferInfo candidateAInfo{candidateBufferA, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo candidateBInfo{candidateBufferB, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo visibleInfo{visibleClusterBuffer, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo stateInfo{queueStateBuffer, 0, VK_WHOLE_SIZE};
    
    // Create a small UBO for buffer addresses
    struct BufferAddresses {
        uint64_t clusterBufferAddress;
        uint64_t hierarchyNodeBufferAddress;
    };
    
    std::vector<VkWriteDescriptorSet> writes;
    
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = cullDescriptorSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &candidateAInfo;
    writes.push_back(write);
    
    write.dstBinding = 1;
    write.pBufferInfo = &candidateBInfo;
    writes.push_back(write);
    
    write.dstBinding = 2;
    write.pBufferInfo = &visibleInfo;
    writes.push_back(write);
    
    write.dstBinding = 3;
    write.pBufferInfo = &stateInfo;
    writes.push_back(write);
    
    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    
    // Update output descriptor set
    writes.clear();
    
    write.dstSet = outputDescriptorSet;
    write.dstBinding = 0;
    write.pBufferInfo = &visibleInfo;
    writes.push_back(write);
    
    write.dstBinding = 1;
    write.pBufferInfo = &stateInfo;
    writes.push_back(write);
    
    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

// ============================================================================
// Buffer Management
// ============================================================================

void ClusterCullingPipeline::rebuildBuffers() {
    if (instances.empty()) return;
    
    uploadInstanceData();
    
    // If we have hierarchies, upload cluster data
    if (!hierarchies.empty()) {
        uploadClusterData();
    }
    
    updateDescriptorSets();
}

void ClusterCullingPipeline::uploadInstanceData() {
    // Upload instance data to GPU
    // Using staging buffer approach
    
    VkDevice device = context.getDevice();
    VkDeviceSize bufferSize = sizeof(InstanceData) * instances.size();
    
    // Create staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer, stagingMemory);
    
    // Copy data to staging
    void* data;
    vkMapMemory(device, stagingMemory, 0, bufferSize, 0, &data);
    memcpy(data, instances.data(), bufferSize);
    vkUnmapMemory(device, stagingMemory);
    
    // Copy to device local buffer
    VkCommandBuffer cmd = context.beginSingleTimeCommands();
    
    VkBufferCopy copyRegion{};
    copyRegion.size = bufferSize;
    vkCmdCopyBuffer(cmd, stagingBuffer, instanceBuffer, 1, &copyRegion);
    
    context.endSingleTimeCommands(cmd);
    
    // Cleanup staging
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);
}

void ClusterCullingPipeline::uploadClusterData() {
    // This would upload combined cluster/hierarchy data from all registered hierarchies
    // For now, each hierarchy manages its own buffers
    // In a full implementation, we'd consolidate everything here
    
    std::cout << "[ClusterCullingPipeline] Cluster data ready: " 
              << totalClusterCount << " clusters, " 
              << totalNodeCount << " nodes" << std::endl;
}

// ============================================================================
// Utility Functions
// ============================================================================

VkShaderModule ClusterCullingPipeline::createShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    
    VkShaderModule shaderModule;
    if (vkCreateShaderModule(context.getDevice(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shader module");
    }
    
    return shaderModule;
}

std::vector<char> ClusterCullingPipeline::readShaderFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open shader file: " + filename);
    }
    
    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);
    
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    
    return buffer;
}

void ClusterCullingPipeline::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                          VkMemoryPropertyFlags properties,
                                          VkBuffer& buffer, VkDeviceMemory& memory) {
    VkDevice device = context.getDevice();
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create buffer");
    }
    
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = context.findMemoryType(memRequirements.memoryTypeBits, properties);
    
    // Enable device address if requested
    VkMemoryAllocateFlagsInfo flagsInfo{};
    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
        flagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        flagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        allocInfo.pNext = &flagsInfo;
    }
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate buffer memory");
    }
    
    vkBindBufferMemory(device, buffer, memory, 0);
}

VkDeviceAddress ClusterCullingPipeline::getBufferAddress(VkBuffer buffer) {
    VkBufferDeviceAddressInfo addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addressInfo.buffer = buffer;
    return vkGetBufferDeviceAddress(context.getDevice(), &addressInfo);
}
