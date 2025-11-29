#include "VirtualShadowMap.h"
#include <stdexcept>
#include <cstring>
#include <iostream>
#include <cmath>
#include <fstream>
#include <array>

VirtualShadowMap::VirtualShadowMap(VulkanContext& context) : context(context) {
    std::cout << "VSM: Creating resources..." << std::endl;
    createResources();
    std::cout << "VSM: Creating page requests buffer..." << std::endl;
    createPageRequestsBuffer();
    std::cout << "VSM: Creating pipelines..." << std::endl;
    createPipelines();
    
    // Shadow pipeline must be created AFTER createPipelines() because it needs the descriptor pool
    std::cout << "VSM: Creating shadow render pass..." << std::endl;
    createShadowRenderPass();
    std::cout << "VSM: Creating shadow pipeline..." << std::endl;
    createShadowPipeline();
    std::cout << "VSM: Creating shadow descriptor set..." << std::endl;
    createShadowDescriptorSet();
    std::cout << "VSM: Creating shadow framebuffer..." << std::endl;
    createShadowFramebuffer();
    
    // Initialize page table data (0xFFFFFFFF = unallocated)
    pageTableData.resize(PAGE_TABLE_SIZE * PAGE_TABLE_SIZE, 0xFFFFFFFF);
    physicalPageAllocated.resize(TOTAL_PHYSICAL_PAGES, false);
    std::cout << "VSM: Initialization complete!" << std::endl;
}

VirtualShadowMap::~VirtualShadowMap() {
    vkDestroyImageView(context.getDevice(), pageTableView, nullptr);
    vkDestroyImage(context.getDevice(), pageTable, nullptr);
    vkFreeMemory(context.getDevice(), pageTableMemory, nullptr);

    vkDestroyImageView(context.getDevice(), physicalAtlasView, nullptr);
    vkDestroyImage(context.getDevice(), physicalAtlas, nullptr);
    vkFreeMemory(context.getDevice(), physicalAtlasMemory, nullptr);
    
    vkDestroyBuffer(context.getDevice(), pageRequestsBuffer, nullptr);
    vkFreeMemory(context.getDevice(), pageRequestsBufferMemory, nullptr);
    
    vkDestroyBuffer(context.getDevice(), stagingBuffer, nullptr);
    vkFreeMemory(context.getDevice(), stagingBufferMemory, nullptr);
    
    vkDestroySampler(context.getDevice(), sampler, nullptr);
    
    vkDestroyPipeline(context.getDevice(), markingPipeline, nullptr);
    vkDestroyPipelineLayout(context.getDevice(), markingPipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(context.getDevice(), markingDescriptorSetLayout, nullptr);
    vkDestroyDescriptorPool(context.getDevice(), descriptorPool, nullptr);
    
    vkDestroyFramebuffer(context.getDevice(), shadowFramebuffer, nullptr);
    vkDestroyPipeline(context.getDevice(), shadowPipeline, nullptr);
    vkDestroyPipelineLayout(context.getDevice(), shadowPipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(context.getDevice(), shadowDescriptorSetLayout, nullptr);
    vkDestroyRenderPass(context.getDevice(), shadowRenderPass, nullptr);
}

void VirtualShadowMap::update(VkCommandBuffer cmd, const glm::mat4& viewProj, const glm::vec3& lightDir, VkImageView depthImageView, VkBuffer uniformBuffer) {
    currentLightViewProj = viewProj;
    
    // Frame N+1 approach: Read page requests from previous frame using HOST_VISIBLE buffer
    // This uses a single HOST_VISIBLE buffer which is coherent - slower for GPU atomics
    // but allows frame-delayed readback without explicit synchronization
    //
    // Flow:
    // 1. Map pageRequestsBuffer (contains requests from Frame N-1)
    // 2. Process requests, update pageTableData
    // 3. Update PageTable texture via stagingBuffer
    // 4. Clear pageRequestsBuffer  
    // 5. Dispatch Marking pass (Frame N)
    
    // Step 1: Process Requests (from previous frame)
    void* mappedData;
    vkMapMemory(context.getDevice(), pageRequestsBufferMemory, 0, VK_WHOLE_SIZE, 0, &mappedData);
    uint32_t* requests = static_cast<uint32_t*>(mappedData);
    
    bool dirty = false;
    uint32_t numUints = TOTAL_VIRTUAL_PAGES / 32;
    
    for (uint32_t i = 0; i < numUints; i++) {
        uint32_t mask = requests[i];
        if (mask == 0) continue;
        
        for (uint32_t bit = 0; bit < 32; bit++) {
            if (mask & (1 << bit)) {
                uint32_t pageID = i * 32 + bit;
                if (pageID < TOTAL_VIRTUAL_PAGES && pageTableData[pageID] == 0xFFFFFFFF) {
                    // Allocate physical page using free list (O(1) allocation)
                    int freePage = -1;
                    for (uint32_t p = 0; p < TOTAL_PHYSICAL_PAGES; p++) {
                        if (!physicalPageAllocated[p]) {
                            freePage = static_cast<int>(p);
                            break;
                        }
                    }
                    
                    if (freePage != -1) {
                        physicalPageAllocated[freePage] = true;
                        pageTableData[pageID] = static_cast<uint32_t>(freePage);
                        dirty = true;
                    }
                }
            }
        }
    }
    vkUnmapMemory(context.getDevice(), pageRequestsBufferMemory);
    
    // Step 2: Update Page Table Texture if dirty
    if (dirty) {
        // Map staging buffer
        void* stagingData;
        vkMapMemory(context.getDevice(), stagingBufferMemory, 0, PAGE_TABLE_SIZE * PAGE_TABLE_SIZE * sizeof(uint32_t), 0, &stagingData);
        memcpy(stagingData, pageTableData.data(), pageTableData.size() * sizeof(uint32_t));
        vkUnmapMemory(context.getDevice(), stagingBufferMemory);
        
        // Copy staging to image
        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {PAGE_TABLE_SIZE, PAGE_TABLE_SIZE, 1};
        
        VkImageMemoryBarrier copyBarrier{};
        copyBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        copyBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        copyBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        copyBarrier.srcAccessMask = 0;
        copyBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        copyBarrier.image = pageTable;
        copyBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyBarrier.subresourceRange.baseMipLevel = 0;
        copyBarrier.subresourceRange.levelCount = 1;
        copyBarrier.subresourceRange.baseArrayLayer = 0;
        copyBarrier.subresourceRange.layerCount = 1;
        
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &copyBarrier);
        
        vkCmdCopyBufferToImage(cmd, stagingBuffer, pageTable, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        
        // Transition to Shader Read
        VkImageMemoryBarrier readBarrierImg{};
        readBarrierImg.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        readBarrierImg.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        readBarrierImg.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        readBarrierImg.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        readBarrierImg.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        readBarrierImg.image = pageTable;
        readBarrierImg.subresourceRange = copyBarrier.subresourceRange;
        
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &readBarrierImg);
    }

    // Step 3: Clear Page Requests for next frame
    vkCmdFillBuffer(cmd, pageRequestsBuffer, 0, VK_WHOLE_SIZE, 0);
    
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
    
    // ... (Descriptor Update and Dispatch)
    
    // 2. Update Descriptor Set
    VkDescriptorBufferInfo uboInfo{};
    uboInfo.buffer = uniformBuffer;
    uboInfo.offset = 0;
    uboInfo.range = VK_WHOLE_SIZE; // Or sizeof(UniformBufferObject)
    
    VkDescriptorImageInfo depthInfo{};
    depthInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    depthInfo.imageView = depthImageView;
    depthInfo.sampler = sampler;
    
    VkDescriptorBufferInfo requestInfo{};
    requestInfo.buffer = pageRequestsBuffer;
    requestInfo.offset = 0;
    requestInfo.range = VK_WHOLE_SIZE;
    
    std::vector<VkWriteDescriptorSet> writes;
    
    VkWriteDescriptorSet uboWrite{};
    uboWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    uboWrite.dstSet = markingDescriptorSet;
    uboWrite.dstBinding = 0;
    uboWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboWrite.descriptorCount = 1;
    uboWrite.pBufferInfo = &uboInfo;
    writes.push_back(uboWrite);
    
    VkWriteDescriptorSet depthWrite{};
    depthWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    depthWrite.dstSet = markingDescriptorSet;
    depthWrite.dstBinding = 1;
    depthWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    depthWrite.descriptorCount = 1;
    depthWrite.pImageInfo = &depthInfo;
    writes.push_back(depthWrite);
    
    VkWriteDescriptorSet requestWrite{};
    requestWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    requestWrite.dstSet = markingDescriptorSet;
    requestWrite.dstBinding = 2;
    requestWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    requestWrite.descriptorCount = 1;
    requestWrite.pBufferInfo = &requestInfo;
    writes.push_back(requestWrite);
    
    vkUpdateDescriptorSets(context.getDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    
    // 3. Dispatch Marking Pass
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, markingPipeline);
    
    VSMUniformData pushData = getUniformData();
    pushData.lightViewProj = viewProj; // Use the passed light matrix
    
    vkCmdPushConstants(cmd, markingPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(VSMUniformData), &pushData);
    
    // Dispatch enough groups to cover the screen using actual screen dimensions
    uint32_t dispatchX = (screenWidth + 7) / 8;
    uint32_t dispatchY = (screenHeight + 7) / 8;
    vkCmdDispatch(cmd, dispatchX, dispatchY, 1);
}

VirtualShadowMap::VSMUniformData VirtualShadowMap::getUniformData() const {
    VSMUniformData data{};
    data.lightViewProj = glm::mat4(1.0f); 
    data.pageTableParams = glm::vec4(VIRTUAL_SIZE, PAGE_SIZE, PHYSICAL_SIZE, 0.0f);
    return data;
}

void VirtualShadowMap::createResources() {
    createPageTable();
    createPhysicalAtlas();
    
    // Create staging buffer for page table updates
    VkDeviceSize stagingSize = PAGE_TABLE_SIZE * PAGE_TABLE_SIZE * sizeof(uint32_t);
    createBuffer(stagingSize, 
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 
        stagingBuffer, stagingBufferMemory);
}

void VirtualShadowMap::createPageTable() {
    createImage(PAGE_TABLE_SIZE, PAGE_TABLE_SIZE, VK_FORMAT_R32_UINT, 
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 
        pageTable, pageTableMemory);
    
    pageTableView = createImageView(pageTable, VK_FORMAT_R32_UINT, VK_IMAGE_ASPECT_COLOR_BIT);
}

void VirtualShadowMap::createPhysicalAtlas() {
    createImage(PHYSICAL_SIZE, PHYSICAL_SIZE, VK_FORMAT_D32_SFLOAT, 
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 
        physicalAtlas, physicalAtlasMemory);
        
    physicalAtlasView = createImageView(physicalAtlas, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT);
}

void VirtualShadowMap::createPageRequestsBuffer() {
    // 1 bit per page. Total pages = 16384.
    // Size = 16384 / 8 bytes = 2048 bytes.
    // But we use atomicOr on uints. So 16384 / 32 uints = 512 uints.
    // Size = 512 * 4 = 2048 bytes.
    VkDeviceSize size = 2048;
    createBuffer(size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, pageRequestsBuffer, pageRequestsBufferMemory);
}

void VirtualShadowMap::createShadowRenderPass() {
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = VK_FORMAT_D32_SFLOAT;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL; // Or SHADER_READ_ONLY_OPTIMAL

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 0;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 0;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &depthAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;

    if (vkCreateRenderPass(context.getDevice(), &renderPassInfo, nullptr, &shadowRenderPass) != VK_SUCCESS) {
        throw std::runtime_error("failed to create VSM shadow render pass!");
    }
}

void VirtualShadowMap::createShadowPipeline() {
    // Descriptor Set Layout
    VkDescriptorSetLayoutBinding pageTableBinding{};
    pageTableBinding.binding = 0;
    pageTableBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; // USampler2D
    pageTableBinding.descriptorCount = 1;
    pageTableBinding.stageFlags = VK_SHADER_STAGE_TASK_BIT_EXT; // Used in Task Shader

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &pageTableBinding;

    if (vkCreateDescriptorSetLayout(context.getDevice(), &layoutInfo, nullptr, &shadowDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create VSM shadow descriptor set layout!");
    }

    // Pipeline Layout
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_TASK_BIT_EXT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = 128 + 64; // Matrices + Addresses. 192 bytes.

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &shadowDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(context.getDevice(), &pipelineLayoutInfo, nullptr, &shadowPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create VSM shadow pipeline layout!");
    }

    // Shaders
    auto taskCode = readFile("shaders/vsm.task.spv");
    auto meshCode = readFile("shaders/vsm.mesh.spv");
    auto fragCode = readFile("shaders/vsm.frag.spv");

    VkShaderModule taskModule = createShaderModule(taskCode);
    VkShaderModule meshModule = createShaderModule(meshCode);
    VkShaderModule fragModule = createShaderModule(fragCode);

    VkPipelineShaderStageCreateInfo taskStage{};
    taskStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    taskStage.stage = VK_SHADER_STAGE_TASK_BIT_EXT;
    taskStage.module = taskModule;
    taskStage.pName = "main";

    VkPipelineShaderStageCreateInfo meshStage{};
    meshStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    meshStage.stage = VK_SHADER_STAGE_MESH_BIT_EXT;
    meshStage.module = meshModule;
    meshStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragStage{};
    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragModule;
    fragStage.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {taskStage, meshStage, fragStage};

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 3;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.layout = shadowPipelineLayout;
    pipelineInfo.renderPass = shadowRenderPass;
    pipelineInfo.subpass = 0;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    pipelineInfo.pViewportState = &viewportState;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE; // VSM needs bias? Maybe.
    rasterizer.depthBiasConstantFactor = 1.25f;
    rasterizer.depthBiasSlopeFactor = 1.75f;
    pipelineInfo.pRasterizationState = &rasterizer;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    pipelineInfo.pMultisampleState = &multisampling;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    pipelineInfo.pDepthStencilState = &depthStencil;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 0;
    pipelineInfo.pColorBlendState = &colorBlending;
    
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();
    pipelineInfo.pDynamicState = &dynamicState;

    if (vkCreateGraphicsPipelines(context.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &shadowPipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create VSM shadow pipeline!");
    }

    vkDestroyShaderModule(context.getDevice(), taskModule, nullptr);
    vkDestroyShaderModule(context.getDevice(), meshModule, nullptr);
    vkDestroyShaderModule(context.getDevice(), fragModule, nullptr);
}

void VirtualShadowMap::createShadowDescriptorSet() {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool; // Reuse pool
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &shadowDescriptorSetLayout;

    if (vkAllocateDescriptorSets(context.getDevice(), &allocInfo, &shadowDescriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate VSM shadow descriptor set!");
    }

    VkDescriptorImageInfo pageTableInfo{};
    pageTableInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    pageTableInfo.imageView = pageTableView;
    // We need an integer sampler (unnormalized coordinates usually, or just nearest)
    // R32_UINT texture.
    // Use the existing sampler (Linear) might be bad for UINT?
    // UINT textures are usually sampled with usampler2D and texelFetch, or nearest sampler.
    // texelFetch ignores sampler.
    // But `texture()` needs a sampler.
    // I used `texelFetch` in vsm.task.
    // So sampler doesn't matter much, but I need to provide one.
    pageTableInfo.sampler = sampler;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = shadowDescriptorSet;
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &pageTableInfo;

    vkUpdateDescriptorSets(context.getDevice(), 1, &write, 0, nullptr);
}

void VirtualShadowMap::renderNaniteShadows(VkCommandBuffer cmd, const std::vector<GameObject>& gameObjects) {
    // Use pre-created framebuffer instead of creating/destroying every frame
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = shadowRenderPass;
    renderPassInfo.framebuffer = shadowFramebuffer;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = {PHYSICAL_SIZE, PHYSICAL_SIZE};
    
    VkClearValue clearValue = {1.0f, 0};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;
    
    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    
    VkViewport viewport{};
    viewport.width = (float)PHYSICAL_SIZE;
    viewport.height = (float)PHYSICAL_SIZE;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    
    VkRect2D scissor{};
    scissor.extent = {PHYSICAL_SIZE, PHYSICAL_SIZE};
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline);
    
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipelineLayout, 0, 1, &shadowDescriptorSet, 0, nullptr);
    
    // Load vkCmdDrawMeshTasksEXT
    auto vkCmdDrawMeshTasksEXT = (PFN_vkCmdDrawMeshTasksEXT)vkGetDeviceProcAddr(context.getDevice(), "vkCmdDrawMeshTasksEXT");
    
    for (const auto& obj : gameObjects) {
        struct PushConstants {
            glm::mat4 model;
            glm::mat4 lightViewProj;
            glm::vec4 pageTableParams;
            uint64_t meshletBufferAddress;
            uint64_t meshletVerticesAddress;
            uint64_t meshletTrianglesAddress;
            uint64_t vertexBufferAddress;
            uint32_t meshletCount;
        } push;
        
        push.model = obj.transform;
        push.lightViewProj = currentLightViewProj;
        push.pageTableParams = glm::vec4(VIRTUAL_SIZE, PAGE_SIZE, PHYSICAL_SIZE, 0.0f);
        push.meshletBufferAddress = obj.mesh->getMeshletBufferAddress();
        push.meshletVerticesAddress = obj.mesh->getMeshletVerticesBufferAddress();
        push.meshletTrianglesAddress = obj.mesh->getMeshletTrianglesBufferAddress();
        push.vertexBufferAddress = obj.mesh->getVertexBufferAddress();
        push.meshletCount = obj.mesh->getMeshletCount();
        
        vkCmdPushConstants(cmd, shadowPipelineLayout, VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_TASK_BIT_EXT, 0, sizeof(PushConstants), &push);
        
        uint32_t groupCount = (push.meshletCount + 31) / 32;
        if (vkCmdDrawMeshTasksEXT) {
            vkCmdDrawMeshTasksEXT(cmd, groupCount, 1, 1);
        }
    }
    
    vkCmdEndRenderPass(cmd);
}

void VirtualShadowMap::createShadowFramebuffer() {
    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = shadowRenderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = &physicalAtlasView;
    framebufferInfo.width = PHYSICAL_SIZE;
    framebufferInfo.height = PHYSICAL_SIZE;
    framebufferInfo.layers = 1;
    
    if (vkCreateFramebuffer(context.getDevice(), &framebufferInfo, nullptr, &shadowFramebuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create VSM shadow framebuffer!");
    }
}

void VirtualShadowMap::createPipelines() {
    // Descriptor Set Layout
    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding = 0;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    VkDescriptorSetLayoutBinding depthBinding{};
    depthBinding.binding = 1;
    depthBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    depthBinding.descriptorCount = 1;
    depthBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    VkDescriptorSetLayoutBinding requestsBinding{};
    requestsBinding.binding = 2;
    requestsBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    requestsBinding.descriptorCount = 1;
    requestsBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    std::array<VkDescriptorSetLayoutBinding, 3> bindings = {uboBinding, depthBinding, requestsBinding};
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    
    if (vkCreateDescriptorSetLayout(context.getDevice(), &layoutInfo, nullptr, &markingDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create VSM descriptor set layout!");
    }
    
    // Descriptor Pool - need 2 sets: marking + shadow
    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2}, // 1 for marking (depth), 1 for shadow (pageTable)
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}
    };
    
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 3;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = 2; // marking + shadow
    
    if (vkCreateDescriptorPool(context.getDevice(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create VSM descriptor pool!");
    }
    
    // Allocate Set
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &markingDescriptorSetLayout;
    
    if (vkAllocateDescriptorSets(context.getDevice(), &allocInfo, &markingDescriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate VSM descriptor set!");
    }
    
    // Pipeline Layout
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(VSMUniformData);
    
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &markingDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    
    if (vkCreatePipelineLayout(context.getDevice(), &pipelineLayoutInfo, nullptr, &markingPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create VSM pipeline layout!");
    }
    
    // Pipeline
    auto computeShaderCode = readFile("shaders/vsm_marking.comp.spv");
    VkShaderModule computeShaderModule = createShaderModule(computeShaderCode);
    
    VkPipelineShaderStageCreateInfo shaderStageInfo{};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = computeShaderModule;
    shaderStageInfo.pName = "main";
    
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = markingPipelineLayout;
    
    if (vkCreateComputePipelines(context.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &markingPipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create VSM compute pipeline!");
    }
    
    vkDestroyShaderModule(context.getDevice(), computeShaderModule, nullptr);
    
    // Create Sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxAnisotropy = 1.0f;
    
    if (vkCreateSampler(context.getDevice(), &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
        throw std::runtime_error("failed to create VSM sampler!");
    }
}

void VirtualShadowMap::createImage(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkImage& image, VkDeviceMemory& memory) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(context.getDevice(), &imageInfo, nullptr, &image) != VK_SUCCESS) {
        throw std::runtime_error("failed to create VSM image!");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(context.getDevice(), image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = context.findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(context.getDevice(), &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate VSM image memory!");
    }

    vkBindImageMemory(context.getDevice(), image, memory, 0);
}

VkImageView VirtualShadowMap::createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspect) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspect;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView;
    if (vkCreateImageView(context.getDevice(), &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
        throw std::runtime_error("failed to create VSM image view!");
    }
    return imageView;
}

void VirtualShadowMap::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(context.getDevice(), &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create VSM buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(context.getDevice(), buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = context.findMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(context.getDevice(), &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate VSM buffer memory!");
    }

    vkBindBufferMemory(context.getDevice(), buffer, bufferMemory, 0);
}

VkShaderModule VirtualShadowMap::createShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(context.getDevice(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("failed to create VSM shader module!");
    }
    return shaderModule;
}

std::vector<char> VirtualShadowMap::readFile(const std::string& filename) {
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
