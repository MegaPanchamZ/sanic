#include "MeshletStreamer.h"
#include "ShaderManager.h"
#include <stdexcept>
#include <array>
#include <fstream>

MeshletStreamer::MeshletStreamer(VulkanContext& context) : context(context) {
    createBuffers();
    createDescriptorSetLayout();
    createDescriptorPool();
    createDescriptorSet();
    createPipeline();
}

MeshletStreamer::~MeshletStreamer() {
    vkDestroyBuffer(context.getDevice(), indirectDrawBuffer, nullptr);
    vkFreeMemory(context.getDevice(), indirectDrawBufferMemory, nullptr);
    
    vkDestroyBuffer(context.getDevice(), indirectDispatchBuffer, nullptr);
    vkFreeMemory(context.getDevice(), indirectDispatchBufferMemory, nullptr);
    
    vkDestroyPipeline(context.getDevice(), pipeline, nullptr);
    vkDestroyPipelineLayout(context.getDevice(), pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(context.getDevice(), descriptorSetLayout, nullptr);
    vkDestroyDescriptorPool(context.getDevice(), descriptorPool, nullptr);
}

void MeshletStreamer::createBuffers() {
    // Max 10000 draw calls for now
    VkDeviceSize bufferSize = 10000 * sizeof(VkDrawMeshTasksIndirectCommandEXT);
    createBuffer(bufferSize, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, indirectDrawBuffer, indirectDrawBufferMemory);
    
    VkDeviceSize dispatchBufferSize = 10000 * sizeof(VkDispatchIndirectCommand);
    createBuffer(dispatchBufferSize, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, indirectDispatchBuffer, indirectDispatchBufferMemory);
}

void MeshletStreamer::createDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding drawBufferBinding{};
    drawBufferBinding.binding = 0;
    drawBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    drawBufferBinding.descriptorCount = 1;
    drawBufferBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    VkDescriptorSetLayoutBinding dispatchBufferBinding{};
    dispatchBufferBinding.binding = 1;
    dispatchBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    dispatchBufferBinding.descriptorCount = 1;
    dispatchBufferBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    std::array<VkDescriptorSetLayoutBinding, 2> bindings = {drawBufferBinding, dispatchBufferBinding};
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    
    if (vkCreateDescriptorSetLayout(context.getDevice(), &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create meshlet streamer descriptor set layout!");
    }
}

void MeshletStreamer::createDescriptorPool() {
    std::array<VkDescriptorPoolSize, 1> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[0].descriptorCount = 2;
    
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 1;
    
    if (vkCreateDescriptorPool(context.getDevice(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create meshlet streamer descriptor pool!");
    }
}

void MeshletStreamer::createDescriptorSet() {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout;
    
    if (vkAllocateDescriptorSets(context.getDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate meshlet streamer descriptor set!");
    }
    
    VkDescriptorBufferInfo drawInfo{};
    drawInfo.buffer = indirectDrawBuffer;
    drawInfo.offset = 0;
    drawInfo.range = VK_WHOLE_SIZE;
    
    VkDescriptorBufferInfo dispatchInfo{};
    dispatchInfo.buffer = indirectDispatchBuffer;
    dispatchInfo.offset = 0;
    dispatchInfo.range = VK_WHOLE_SIZE;
    
    std::array<VkWriteDescriptorSet, 2> descriptorWrites{};
    
    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = descriptorSet;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pBufferInfo = &drawInfo;
    
    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = descriptorSet;
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pBufferInfo = &dispatchInfo;
    
    vkUpdateDescriptorSets(context.getDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
}

void MeshletStreamer::createPipeline() {
    VkShaderModule shaderModule = Sanic::ShaderManager::loadShader("shaders/cull_meshlets.comp");
    
    VkPipelineShaderStageCreateInfo shaderStageInfo{};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = shaderModule;
    shaderStageInfo.pName = "main";
    
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    
    if (vkCreatePipelineLayout(context.getDevice(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create meshlet streamer pipeline layout!");
    }
    
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = pipelineLayout;
    
    if (vkCreateComputePipelines(context.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create meshlet streamer pipeline!");
    }
}

void MeshletStreamer::update(VkCommandBuffer cmd, const std::vector<GameObject>& gameObjects) {
    if (gameObjects.empty()) return;
    
    // Dispatch culling shader for meshlet frustum and occlusion culling
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
    
    // Count total meshlets across all game objects
    uint32_t totalMeshlets = 0;
    for (const auto& obj : gameObjects) {
        if (obj.mesh) {
            totalMeshlets += obj.mesh->getMeshletCount();
        }
    }
    
    // Dispatch enough workgroups to process all meshlets (64 threads per workgroup)
    if (totalMeshlets > 0) {
        uint32_t workgroupCount = (totalMeshlets + 63) / 64;
        vkCmdDispatch(cmd, workgroupCount, 1, 1);
        
        // Memory barrier to ensure culling results are visible before rendering
        VkMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
            VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_TASK_SHADER_BIT_EXT, 
            0, 1, &barrier, 0, nullptr, 0, nullptr);
    }
}

// Helpers
void MeshletStreamer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(context.getDevice(), &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(context.getDevice(), buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = context.findMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(context.getDevice(), &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate buffer memory!");
    }

    vkBindBufferMemory(context.getDevice(), buffer, bufferMemory, 0);
}

uint32_t MeshletStreamer::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    return context.findMemoryType(typeFilter, properties);
}

VkShaderModule MeshletStreamer::createShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(context.getDevice(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shader module!");
    }

    return shaderModule;
}

std::vector<char> MeshletStreamer::readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open file: " + filename);
    }
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
}
