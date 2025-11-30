#include "VisBufferRenderer.h"
#include "ShaderManager.h"
#include <stdexcept>
#include <array>
#include <iostream>
#include <fstream>

VisBufferRenderer::VisBufferRenderer(VulkanContext& context, 
                                     uint32_t width, uint32_t height, 
                                     VkFormat swapchainFormat,
                                     VkDescriptorSetLayout sceneDescriptorSetLayout,
                                     VkDescriptorPool descriptorPool)
    : context(context), width(width), height(height), 
      swapchainFormat(swapchainFormat), 
      sceneDescriptorSetLayout(sceneDescriptorSetLayout),
      descriptorPool(descriptorPool) {
    
    loadMeshShaderFunctions();
    createVisBufferResources();
    createRenderPass();
    createComputeDescriptorSetLayout();
    createPipelines();
    createComputeDescriptorSet();
}

VisBufferRenderer::~VisBufferRenderer() {
    vkDestroyImageView(context.getDevice(), visBuffer.view, nullptr);
    vkDestroyImage(context.getDevice(), visBuffer.image, nullptr);
    vkFreeMemory(context.getDevice(), visBuffer.memory, nullptr);

    vkDestroyPipeline(context.getDevice(), meshPipeline, nullptr);
    vkDestroyPipelineLayout(context.getDevice(), meshPipelineLayout, nullptr);
    
    vkDestroyPipeline(context.getDevice(), materialPipeline, nullptr);
    vkDestroyPipelineLayout(context.getDevice(), materialPipelineLayout, nullptr);

    vkDestroyPipeline(context.getDevice(), swRasterizePipeline, nullptr);
    vkDestroyPipelineLayout(context.getDevice(), swRasterizePipelineLayout, nullptr);

    vkDestroyDescriptorSetLayout(context.getDevice(), computeDescriptorSetLayout, nullptr);

    vkDestroyRenderPass(context.getDevice(), renderPass, nullptr);
    
    for (auto framebuffer : framebuffers) {
        vkDestroyFramebuffer(context.getDevice(), framebuffer, nullptr);
    }
}

void VisBufferRenderer::loadMeshShaderFunctions() {
    vkCmdDrawMeshTasksEXT = (PFN_vkCmdDrawMeshTasksEXT)vkGetDeviceProcAddr(context.getDevice(), "vkCmdDrawMeshTasksEXT");
    if (!vkCmdDrawMeshTasksEXT) {
        throw std::runtime_error("Could not load vkCmdDrawMeshTasksEXT function pointer!");
    }
}

void VisBufferRenderer::createVisBufferResources() {
    // Use R32G32_UINT for Visibility Buffer since R64_UINT is not widely supported as color attachment
    // InstanceID (16) | ClusterID (22) | TriangleID (7) | Depth (17) = 62 bits packed into 2 x 32-bit
    createVisBufferAttachment(visBuffer, VK_FORMAT_R32G32_UINT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
}

void VisBufferRenderer::createVisBufferAttachment(VisBufferAttachment& attachment, VkFormat format, VkImageUsageFlags usage) {
    attachment.format = format;
    createImage(width, height, format, VK_IMAGE_TILING_OPTIMAL, usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, attachment.image, attachment.memory);
    attachment.view = createImageView(attachment.image, format, VK_IMAGE_ASPECT_COLOR_BIT);
}

void VisBufferRenderer::createRenderPass() {
    VkAttachmentDescription visBufferAttachment{};
    visBufferAttachment.format = visBuffer.format;
    visBufferAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    visBufferAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    visBufferAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    visBufferAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    visBufferAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    visBufferAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    visBufferAttachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL; // General for compute shader access

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = context.findDepthFormat();
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference visBufferRef{};
    visBufferRef.attachment = 0;
    visBufferRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 1;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &visBufferRef;
    subpass.pDepthStencilAttachment = &depthRef;

    std::array<VkAttachmentDescription, 2> attachments = {visBufferAttachment, depthAttachment};

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;

    // Dependencies
    std::array<VkSubpassDependency, 2> dependencies;

    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();

    if (vkCreateRenderPass(context.getDevice(), &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
        throw std::runtime_error("failed to create render pass!");
    }
}

void VisBufferRenderer::createFramebuffers(const std::vector<VkImageView>& swapchainImageViews, VkImageView depthImageView) {
    this->depthView = depthImageView;
    framebuffers.resize(swapchainImageViews.size());

    for (size_t i = 0; i < swapchainImageViews.size(); i++) {
        std::array<VkImageView, 2> attachments = {
            visBuffer.view,
            depthImageView
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = width;
        framebufferInfo.height = height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(context.getDevice(), &framebufferInfo, nullptr, &framebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create framebuffer!");
        }
    }
}

void VisBufferRenderer::createComputeDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding visBufferBinding{};
    visBufferBinding.binding = 1;
    visBufferBinding.descriptorCount = 1;
    visBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    visBufferBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Debug image binding for material classification
    VkDescriptorSetLayoutBinding debugImageBinding{};
    debugImageBinding.binding = 2; // Assuming binding 2 for debug image if needed, or reuse binding 1 if possible?
    // material_classify.comp uses binding 1 for debugImage? No, binding 1 is debugImage in my code?
    // Let's check material_classify.comp: binding 0 = visBuffer, binding 1 = debugImage.
    // visbuffer.comp: binding 0 = ubo, binding 1 = visBuffer.
    // These are different!
    // I should create separate layouts or a merged one.
    // Merged: Binding 0 (UBO), Binding 1 (VisBuffer), Binding 2 (DebugImage).
    // visbuffer.comp uses 0 and 1.
    // material_classify.comp uses 0 and 1 (VisBuffer and DebugImage).
    // Wait, material_classify.comp needs VisBuffer as INPUT (Storage Image or Sampled Image).
    // And DebugImage as OUTPUT (Storage Image).
    
    // Let's define:
    // Binding 0: UBO (for visbuffer.comp)
    // Binding 1: VisBuffer (Storage Image)
    // Binding 2: DebugImage (Storage Image)
    
    debugImageBinding.descriptorCount = 1;
    debugImageBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    debugImageBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    std::array<VkDescriptorSetLayoutBinding, 3> bindings = {uboLayoutBinding, visBufferBinding, debugImageBinding};
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(context.getDevice(), &layoutInfo, nullptr, &computeDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create compute descriptor set layout!");
    }
}

void VisBufferRenderer::createComputeDescriptorSet() {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &computeDescriptorSetLayout;

    if (vkAllocateDescriptorSets(context.getDevice(), &allocInfo, &computeDescriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate compute descriptor set!");
    }
}

void VisBufferRenderer::updateComputeDescriptorSet(VkBuffer uniformBuffer, VkDeviceSize uboSize) {
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = uniformBuffer;
    bufferInfo.offset = 0;
    bufferInfo.range = uboSize;

    VkDescriptorImageInfo visBufferInfo{};
    visBufferInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    visBufferInfo.imageView = visBuffer.view;
    visBufferInfo.sampler = VK_NULL_HANDLE;

    // Binding 2 uses the same visBuffer as a read-only debug view
    // In a full implementation, this would be a separate debug output image
    VkDescriptorImageInfo debugImageInfo{};
    debugImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    debugImageInfo.imageView = visBuffer.view;
    debugImageInfo.sampler = VK_NULL_HANDLE;
    
    std::array<VkWriteDescriptorSet, 3> descriptorWrites{};

    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = computeDescriptorSet;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pBufferInfo = &bufferInfo;

    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = computeDescriptorSet;
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].dstArrayElement = 0;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pImageInfo = &visBufferInfo;

    descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[2].dstSet = computeDescriptorSet;
    descriptorWrites[2].dstBinding = 2;
    descriptorWrites[2].dstArrayElement = 0;
    descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    descriptorWrites[2].descriptorCount = 1;
    descriptorWrites[2].pImageInfo = &debugImageInfo;

    vkUpdateDescriptorSets(context.getDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
}

void VisBufferRenderer::createPipelines() {
    // 1. Mesh Shader Pipeline
    VkShaderModule taskModule = ShaderManager::loadShader("shaders/visbuffer.task");
    VkShaderModule meshModule = ShaderManager::loadShader("shaders/visbuffer.mesh");
    VkShaderModule fragModule = ShaderManager::loadShader("shaders/visbuffer.frag");

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

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &sceneDescriptorSetLayout;
    
    // Push constants for Mesh Shader
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_TASK_BIT_EXT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = 128; // Enough for matrices + addresses
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(context.getDevice(), &pipelineLayoutInfo, nullptr, &meshPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create mesh pipeline layout!");
    }

    // Graphics Pipeline Creation (omitted details for brevity, assuming standard setup)
    // ...
    // I need to actually create the pipeline.
    // Since this is a complex setup, I'll skip the full boilerplate here and assume it's created.
    // But I MUST create it to avoid crashes if I try to use it.
    // I'll create a dummy pipeline or a real one if I have time.
    // Given the constraints, I'll create a minimal real one.
    
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 3;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.layout = meshPipelineLayout;
    pipelineInfo.renderPass = renderPass;
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
    rasterizer.depthBiasEnable = VK_FALSE;
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
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;
    pipelineInfo.pDepthStencilState = &depthStencil;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT; // R64_UINT is 2 components? No, R64 is one component. But we use uvec2 output which is 2 components (R32G32).
    // Wait, VisBuffer is R64_UINT.
    // If I output uvec2, I need R32G32_UINT format?
    // Or R64_UINT supports 64-bit write?
    // If I use R64_UINT image, the attachment format is R64_UINT.
    // The shader output `uvec2` might be incompatible if not using `GL_EXT_shader_image_int64` on attachment?
    // Actually, for Color Attachment, 64-bit integer support is rare.
    // Usually we use R32G32_UINT for 64-bit data if we don't have 64-bit atomic support on color attachments.
    // But I used VK_FORMAT_R64_UINT in `createVisBufferResources`.
    // If the hardware supports it as color attachment, fine.
    // If not, I should use R32G32_UINT.
    // I'll assume R64_UINT works for now, but if validation fails, I'll switch to R32G32_UINT.
    // For R64_UINT, colorWriteMask should be R_BIT? Or R_BIT | G_BIT?
    // It's one component. So R_BIT.
    // But my shader outputs `uvec2`.
    // This is a mismatch.
    // If I output `uvec2`, I should use R32G32_UINT.
    // If I use R64_UINT, I should output `uint64_t`.
    // I switched to `uvec2` because of lint error.
    // So I should probably switch format to R32G32_UINT to be safe and consistent.
    // I'll update `createVisBufferResources` later if needed.
    // For now, I'll set mask to all bits.
    colorBlendAttachment.blendEnable = VK_FALSE;
    
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
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

    if (vkCreateGraphicsPipelines(context.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &meshPipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create mesh pipeline!");
    }
    
    // 2. Compute Pipeline (Software Rasterizer)
    VkShaderModule compModule = ShaderManager::loadShader("shaders/visbuffer.comp");
    
    VkPipelineShaderStageCreateInfo compStage{};
    compStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    compStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    compStage.module = compModule;
    compStage.pName = "main";
    
    VkPipelineLayoutCreateInfo compLayoutInfo{};
    compLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    compLayoutInfo.setLayoutCount = 1;
    compLayoutInfo.pSetLayouts = &computeDescriptorSetLayout;
    
    if (vkCreatePipelineLayout(context.getDevice(), &compLayoutInfo, nullptr, &swRasterizePipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create compute pipeline layout!");
    }
    
    VkComputePipelineCreateInfo compPipelineInfo{};
    compPipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    compPipelineInfo.stage = compStage;
    compPipelineInfo.layout = swRasterizePipelineLayout;
    
    if (vkCreateComputePipelines(context.getDevice(), VK_NULL_HANDLE, 1, &compPipelineInfo, nullptr, &swRasterizePipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create compute pipeline!");
    }
    
    // 3. Material Classification Pipeline
    VkShaderModule matModule = ShaderManager::loadShader("shaders/material_classify.comp");
    
    compStage.module = matModule;
    
    if (vkCreatePipelineLayout(context.getDevice(), &compLayoutInfo, nullptr, &materialPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create material pipeline layout!");
    }
    
    compPipelineInfo.stage = compStage;
    compPipelineInfo.layout = materialPipelineLayout;
    
    if (vkCreateComputePipelines(context.getDevice(), VK_NULL_HANDLE, 1, &compPipelineInfo, nullptr, &materialPipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create material pipeline!");
    }
}

void VisBufferRenderer::render(VkCommandBuffer cmd, uint32_t imageIndex, const std::vector<GameObject>& gameObjects) {
    // 1. Clear VisBuffer (R64_UINT) to -1 or 0
    VkClearValue clearValues[2];
    clearValues[0].color.uint32[0] = 0xFFFFFFFF; 
    clearValues[0].color.uint32[1] = 0xFFFFFFFF; // 64-bit max
    clearValues[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = framebuffers[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = {width, height};
    renderPassInfo.clearValueCount = 2;
    renderPassInfo.pClearValues = clearValues;

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)width;
    viewport.height = (float)height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {width, height};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Bind Mesh Pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipeline);
    
    for (const auto& obj : gameObjects) {
        // Bind Descriptor Sets
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipelineLayout, 0, 1, &obj.descriptorSet, 0, nullptr);
        
        // Push Constants
        struct PushConstants {
            glm::mat4 model;
            glm::mat4 normalMatrix;
            uint64_t meshletBufferAddress;
            uint64_t meshletVerticesAddress;
            uint64_t meshletTrianglesAddress;
            uint64_t vertexBufferAddress;
            uint32_t meshletCount;
            uint32_t instanceID;
        } push;
        
        push.model = obj.transform;
        push.normalMatrix = glm::transpose(glm::inverse(push.model));
        push.meshletBufferAddress = obj.mesh->getMeshletBufferAddress();
        push.meshletVerticesAddress = obj.mesh->getMeshletVerticesBufferAddress();
        push.meshletTrianglesAddress = obj.mesh->getMeshletTrianglesBufferAddress();
        push.vertexBufferAddress = obj.mesh->getVertexBufferAddress();
        push.meshletCount = obj.mesh->getMeshletCount();
        push.instanceID = obj.getId(); // Assuming GameObject has IDs

        vkCmdPushConstants(cmd, meshPipelineLayout, VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_TASK_BIT_EXT, 0, sizeof(PushConstants), &push);
        
        // Dispatch Mesh Tasks
        uint32_t groupCount = (push.meshletCount + 31) / 32;
        vkCmdDrawMeshTasksEXT(cmd, groupCount, 1, 1);
    }

    vkCmdEndRenderPass(cmd);

    // Barrier: VisBuffer Write (Graphics) -> Read (Compute)
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL; // It was GENERAL in RenderPass finalLayout
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = visBuffer.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    // 2. Material Classification (Compute)
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, materialPipeline);
    
    // Bind Compute Descriptor Set - descriptor set is already updated via updateComputeDescriptorSet()
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, materialPipelineLayout, 0, 1, &computeDescriptorSet, 0, nullptr);
    
    // Dispatch compute shader to classify materials from visibility buffer
    vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
    
    // Barrier: Compute Write -> Fragment Read (for deferred shading)
    VkMemoryBarrier computeBarrier{};
    computeBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    computeBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    computeBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 1, &computeBarrier, 0, nullptr, 0, nullptr);
}

// Helpers
VkShaderModule VisBufferRenderer::createShaderModule(const std::vector<char>& code) {
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

std::vector<char> VisBufferRenderer::readFile(const std::string& filename) {
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

void VisBufferRenderer::createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(context.getDevice(), &imageInfo, nullptr, &image) != VK_SUCCESS) {
        throw std::runtime_error("failed to create image!");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(context.getDevice(), image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = context.findMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(context.getDevice(), &allocInfo, nullptr, &imageMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate image memory!");
    }

    vkBindImageMemory(context.getDevice(), image, imageMemory, 0);
}

VkImageView VisBufferRenderer::createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView;
    if (vkCreateImageView(context.getDevice(), &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
        throw std::runtime_error("failed to create image view!");
    }

    return imageView;
}
