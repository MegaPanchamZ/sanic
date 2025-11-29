#include "DeferredRenderer.h"
#include "Vertex.h"
#include <iostream>
#include <fstream>
#include <array>

DeferredRenderer::DeferredRenderer(VulkanContext& context, 
                                   uint32_t width, uint32_t height, 
                                   VkFormat swapchainFormat,
                                   VkDescriptorSetLayout sceneDescriptorSetLayout,
                                   VkDescriptorPool descriptorPool)
    : context(context), width(width), height(height), 
      swapchainFormat(swapchainFormat), 
      sceneDescriptorSetLayout(sceneDescriptorSetLayout),
      descriptorPool(descriptorPool)
{
    createGBufferResources();
    createRenderPass();
    createCompositionDescriptorSetLayout();
    loadMeshShaderFunctions();
    createPipelines();
}

DeferredRenderer::~DeferredRenderer() {
    VkDevice device = context.getDevice();
    
    // Cleanup pipelines
    vkDestroyPipeline(device, meshPipeline, nullptr);
    vkDestroyPipelineLayout(device, meshPipelineLayout, nullptr);
    vkDestroyPipeline(device, compositionPipeline, nullptr);
    vkDestroyPipelineLayout(device, compositionPipelineLayout, nullptr);
    
    // Cleanup render pass
    vkDestroyRenderPass(device, renderPass, nullptr);
    
    // Cleanup descriptor layouts
    vkDestroyDescriptorSetLayout(device, compositionDescriptorSetLayout, nullptr);
    
    // Cleanup framebuffers
    for (auto framebuffer : framebuffers) {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }
    
    // Cleanup sampler
    if (gBufferSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, gBufferSampler, nullptr);
    }
    
    // Cleanup G-Buffer resources
    vkDestroyImageView(device, position.view, nullptr);
    vkDestroyImage(device, position.image, nullptr);
    vkFreeMemory(device, position.memory, nullptr);
    
    vkDestroyImageView(device, normal.view, nullptr);
    vkDestroyImage(device, normal.image, nullptr);
    vkFreeMemory(device, normal.memory, nullptr);
    
    vkDestroyImageView(device, albedo.view, nullptr);
    vkDestroyImage(device, albedo.image, nullptr);
    vkFreeMemory(device, albedo.memory, nullptr);
    
    vkDestroyImageView(device, pbr.view, nullptr);
    vkDestroyImage(device, pbr.image, nullptr);
    vkFreeMemory(device, pbr.memory, nullptr);
}

void DeferredRenderer::createGBufferAttachment(GBufferAttachment& attachment, VkFormat format, VkImageUsageFlags usage) {
    createImage(width, height, format, VK_IMAGE_TILING_OPTIMAL, usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, attachment.image, attachment.memory);
    attachment.view = createImageView(attachment.image, format, VK_IMAGE_ASPECT_COLOR_BIT);
    attachment.format = format;
}

void DeferredRenderer::createGBufferResources() {
    createGBufferAttachment(position, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    createGBufferAttachment(normal, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    createGBufferAttachment(albedo, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    createGBufferAttachment(pbr, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    
    // Create G-Buffer sampler for SSR
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    
    if (vkCreateSampler(context.getDevice(), &samplerInfo, nullptr, &gBufferSampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create G-Buffer sampler!");
    }
    
    std::cout << "G-Buffer resources created at " << width << "x" << height << std::endl;
}

void DeferredRenderer::createRenderPass() {
    VkDevice device = context.getDevice();
    std::array<VkAttachmentDescription, 6> attachments{};
    
    // 0: G-Buffer Position
    attachments[0].format = position.format;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE; 
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    
    // 1: G-Buffer Normal
    attachments[1].format = normal.format;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    
    // 2: G-Buffer Albedo
    attachments[2].format = albedo.format;
    attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[2].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    
    // 3: G-Buffer PBR
    attachments[3].format = pbr.format;
    attachments[3].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[3].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[3].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[3].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[3].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[3].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[3].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    
    // 4: Depth
    attachments[4].format = context.findDepthFormat();
    attachments[4].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[4].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[4].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[4].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[4].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[4].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[4].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    
    // 5: Swapchain
    attachments[5].format = swapchainFormat;
    attachments[5].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[5].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[5].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[5].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[5].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[5].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[5].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    
    // Subpasses
    std::array<VkAttachmentReference, 4> gBufferColorRefs{};
    gBufferColorRefs[0] = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    gBufferColorRefs[1] = {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    gBufferColorRefs[2] = {2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    gBufferColorRefs[3] = {3, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    
    VkAttachmentReference gBufferDepthRef = {4, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    
    VkSubpassDescription geometrySubpass{};
    geometrySubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    geometrySubpass.colorAttachmentCount = static_cast<uint32_t>(gBufferColorRefs.size());
    geometrySubpass.pColorAttachments = gBufferColorRefs.data();
    geometrySubpass.pDepthStencilAttachment = &gBufferDepthRef;
    
    std::array<VkAttachmentReference, 4> inputAttachmentRefs{};
    inputAttachmentRefs[0] = {0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    inputAttachmentRefs[1] = {1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    inputAttachmentRefs[2] = {2, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    inputAttachmentRefs[3] = {3, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    
    VkAttachmentReference compositionColorRef = {5, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference compositionDepthRef = {4, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    
    VkSubpassDescription compositionSubpass{};
    compositionSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    compositionSubpass.colorAttachmentCount = 1;
    compositionSubpass.pColorAttachments = &compositionColorRef;
    compositionSubpass.pDepthStencilAttachment = &compositionDepthRef;
    compositionSubpass.inputAttachmentCount = static_cast<uint32_t>(inputAttachmentRefs.size());
    compositionSubpass.pInputAttachments = inputAttachmentRefs.data();
    
    std::array<VkSubpassDescription, 2> subpasses = {geometrySubpass, compositionSubpass};
    
    std::array<VkSubpassDependency, 3> dependencies{};
    
    // External -> Geometry
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    
    // Geometry -> Composition
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = 1;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    
    // Composition -> External
    dependencies[2].srcSubpass = 1;
    dependencies[2].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[2].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[2].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[2].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[2].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependencies[2].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = static_cast<uint32_t>(subpasses.size());
    renderPassInfo.pSubpasses = subpasses.data();
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();
    
    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
        throw std::runtime_error("failed to create deferred render pass!");
    }
}

void DeferredRenderer::createFramebuffers(const std::vector<VkImageView>& swapchainImageViews, VkImageView depthImageViewIn) {
    VkDevice device = context.getDevice();
    framebuffers.resize(swapchainImageViews.size());
    
    // Store depth view for SSR access
    depthView = depthImageViewIn;
    
    // Also store the first swapchain view as scene color (for SSR - previous frame approximation)
    if (!swapchainImageViews.empty()) {
        sceneColorView = swapchainImageViews[0];
    }
    
    for (size_t i = 0; i < swapchainImageViews.size(); i++) {
        std::array<VkImageView, 6> attachments = {
            position.view,
            normal.view,
            albedo.view,
            pbr.view,
            depthImageViewIn,
            swapchainImageViews[i]
        };
        
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = width;
        framebufferInfo.height = height;
        framebufferInfo.layers = 1;
        
        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &framebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create deferred framebuffer!");
        }
    }
    std::cout << "Deferred Framebuffers created: " << framebuffers.size() << std::endl;
}

void DeferredRenderer::createCompositionDescriptorSetLayout() {
    VkDevice device = context.getDevice();
    std::array<VkDescriptorSetLayoutBinding, 7> bindings{};
    
    // Input attachments
    for(int i = 0; i < 4; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    
    // UBO
    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // CSM
    bindings[5].binding = 5;
    bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Environment
    bindings[6].binding = 6;
    bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[6].descriptorCount = 1;
    bindings[6].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &compositionDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create composition descriptor set layout!");
    }
}

void DeferredRenderer::loadMeshShaderFunctions() {
    vkCmdDrawMeshTasksEXT = (PFN_vkCmdDrawMeshTasksEXT)vkGetDeviceProcAddr(context.getDevice(), "vkCmdDrawMeshTasksEXT");
    if (!vkCmdDrawMeshTasksEXT) {
        throw std::runtime_error("Could not load vkCmdDrawMeshTasksEXT function pointer!");
    }
}

void DeferredRenderer::createPipelines() {
    VkDevice device = context.getDevice();
    
    // --- Mesh Pipeline (Geometry Pass) ---
    auto taskShaderCode = readFile("shaders/nanite.task.spv");
    auto meshShaderCode = readFile("shaders/nanite.mesh.spv");
    auto gbufferFragShaderCode = readFile("shaders/gbuffer.frag.spv");

    VkShaderModule taskModule = createShaderModule(taskShaderCode);
    VkShaderModule meshModule = createShaderModule(meshShaderCode);
    VkShaderModule fragModule = createShaderModule(gbufferFragShaderCode);

    VkPipelineShaderStageCreateInfo stages[] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_TASK_BIT_EXT, taskModule, "main", nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_MESH_BIT_EXT, meshModule, "main", nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, "main", nullptr}
    };

    VkPushConstantRange pushConstant{};
    pushConstant.stageFlags = VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstant.size = sizeof(PushConstantData);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &sceneDescriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstant;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &meshPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create mesh pipeline layout!");
    }

    // Pipeline state (simplified for mesh shader)
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    
    VkViewport viewport{0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, {width, height}};
    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    std::array<VkPipelineColorBlendAttachmentState, 4> blendAttachments{};
    for(auto& att : blendAttachments) {
        att.colorWriteMask = 0xF;
        att.blendEnable = VK_FALSE;
    }

    VkPipelineColorBlendStateCreateInfo colorBlend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlend.attachmentCount = 4;
    colorBlend.pAttachments = blendAttachments.data();

    VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.stageCount = 3;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.layout = meshPipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &meshPipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create mesh pipeline!");
    }

    vkDestroyShaderModule(device, taskModule, nullptr);
    vkDestroyShaderModule(device, meshModule, nullptr);
    vkDestroyShaderModule(device, fragModule, nullptr);

    // --- Composition Pipeline ---
    auto compVertCode = readFile("shaders/composition.vert.spv");
    auto compFragCode = readFile("shaders/composition.frag.spv");
    VkShaderModule compVert = createShaderModule(compVertCode);
    VkShaderModule compFrag = createShaderModule(compFragCode);

    VkPipelineShaderStageCreateInfo compStages[] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, compVert, "main", nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, compFrag, "main", nullptr}
    };

    VkPipelineLayoutCreateInfo compLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    compLayoutInfo.setLayoutCount = 1;
    compLayoutInfo.pSetLayouts = &compositionDescriptorSetLayout;

    if (vkCreatePipelineLayout(device, &compLayoutInfo, nullptr, &compositionPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create composition pipeline layout!");
    }

    VkPipelineColorBlendAttachmentState compBlendAtt{};
    compBlendAtt.colorWriteMask = 0xF;
    compBlendAtt.blendEnable = VK_FALSE;
    
    VkPipelineColorBlendStateCreateInfo compColorBlend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    compColorBlend.attachmentCount = 1;
    compColorBlend.pAttachments = &compBlendAtt;

    VkPipelineDepthStencilStateCreateInfo compDepth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    compDepth.depthTestEnable = VK_FALSE;
    compDepth.depthWriteEnable = VK_FALSE;

    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = compStages;
    pipelineInfo.pDepthStencilState = &compDepth;
    pipelineInfo.pColorBlendState = &compColorBlend;
    pipelineInfo.layout = compositionPipelineLayout;
    pipelineInfo.subpass = 1;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &compositionPipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create composition pipeline!");
    }

    vkDestroyShaderModule(device, compVert, nullptr);
    vkDestroyShaderModule(device, compFrag, nullptr);
}

void DeferredRenderer::updateCompositionDescriptorSet(VkBuffer uniformBuffer, VkDeviceSize uboSize,
                                                      VkImageView shadowView, VkSampler shadowSampler,
                                                      VkImageView envView, VkSampler envSampler) 
{
    VkDevice device = context.getDevice();
    
    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &compositionDescriptorSetLayout;
    
    if (vkAllocateDescriptorSets(device, &allocInfo, &compositionDescriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate composition descriptor set!");
    }

    std::array<VkDescriptorImageInfo, 4> gbufferInfos;
    gbufferInfos[0] = {VK_NULL_HANDLE, position.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    gbufferInfos[1] = {VK_NULL_HANDLE, normal.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    gbufferInfos[2] = {VK_NULL_HANDLE, albedo.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    gbufferInfos[3] = {VK_NULL_HANDLE, pbr.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

    VkDescriptorBufferInfo uboInfo{uniformBuffer, 0, uboSize};
    VkDescriptorImageInfo shadowInfo{shadowSampler, shadowView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo envInfo{envSampler, envView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

    std::vector<VkWriteDescriptorSet> writes;
    for(int i=0; i<4; ++i) {
        writes.push_back({VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, compositionDescriptorSet, 
                          (uint32_t)i, 0, 1, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, &gbufferInfos[i], nullptr, nullptr});
    }
    writes.push_back({VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, compositionDescriptorSet, 
                      4, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &uboInfo, nullptr});
    writes.push_back({VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, compositionDescriptorSet, 
                      5, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &shadowInfo, nullptr, nullptr});
    writes.push_back({VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, compositionDescriptorSet, 
                      6, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &envInfo, nullptr, nullptr});

    vkUpdateDescriptorSets(device, (uint32_t)writes.size(), writes.data(), 0, nullptr);
}

void DeferredRenderer::render(VkCommandBuffer cmd, uint32_t imageIndex, const std::vector<GameObject>& gameObjects) {
    VkRenderPassBeginInfo renderPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = framebuffers[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = {width, height};

    std::array<VkClearValue, 6> clearValues{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    clearValues[1].color = {{0.5f, 0.5f, 1.0f, 0.0f}};
    clearValues[2].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    clearValues[3].color = {{0.5f, 1.0f, 0.0f, 0.0f}};
    clearValues[4].depthStencil = {1.0f, 0};
    clearValues[5].color = {{0.0f, 0.0f, 0.0f, 1.0f}};

    renderPassInfo.clearValueCount = (uint32_t)clearValues.size();
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Subpass 0: Geometry
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipeline);
    
    for (const auto& obj : gameObjects) {
        PushConstantData push{};
        push.model = obj.transform;
        push.normalMatrix = glm::transpose(glm::inverse(obj.transform));
        push.meshletBufferAddress = obj.mesh->getMeshletBufferAddress();
        push.meshletVerticesAddress = obj.mesh->getMeshletVerticesBufferAddress();
        push.meshletTrianglesAddress = obj.mesh->getMeshletTrianglesBufferAddress();
        push.vertexBufferAddress = obj.mesh->getVertexBufferAddress();
        push.meshletCount = obj.mesh->getMeshletCount();

        vkCmdPushConstants(cmd, meshPipelineLayout, VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstantData), &push);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipelineLayout, 0, 1, &obj.descriptorSet, 0, nullptr);
        
        uint32_t groupCountX = (obj.mesh->getMeshletCount() + 31) / 32;
        vkCmdDrawMeshTasksEXT(cmd, groupCountX, 1, 1);
    }

    vkCmdNextSubpass(cmd, VK_SUBPASS_CONTENTS_INLINE);

    // Subpass 1: Composition
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, compositionPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, compositionPipelineLayout, 0, 1, &compositionDescriptorSet, 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdEndRenderPass(cmd);
}

// Helpers
void DeferredRenderer::createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory) {
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
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

    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = context.findMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(context.getDevice(), &allocInfo, nullptr, &imageMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate image memory!");
    }

    vkBindImageMemory(context.getDevice(), image, imageMemory, 0);
}

VkImageView DeferredRenderer::createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) {
    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
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

VkShaderModule DeferredRenderer::createShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(context.getDevice(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shader module!");
    }
    return shaderModule;
}

std::vector<char> DeferredRenderer::readFile(const std::string& filename) {
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
