#include "ShadowRenderer.h"
#include "Vertex.h"
#include <array>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>

struct ShadowPushConstant {
    glm::mat4 model;
};

ShadowRenderer::ShadowRenderer(VulkanContext& context, VkDescriptorSetLayout descriptorSetLayout)
    : context(context)
{
    createRenderPass();
    createResources();
    createPipeline(descriptorSetLayout);
}

ShadowRenderer::~ShadowRenderer() {
    vkDestroyImageView(context.getDevice(), shadowArrayImageView, nullptr);
    for (uint32_t i = 0; i < CASCADE_COUNT; i++) {
        vkDestroyImageView(context.getDevice(), cascadeViews[i], nullptr);
        vkDestroyFramebuffer(context.getDevice(), cascadeFramebuffers[i], nullptr);
    }
    vkDestroyImage(context.getDevice(), shadowArrayImage, nullptr);
    vkFreeMemory(context.getDevice(), shadowArrayImageMemory, nullptr);
    vkDestroySampler(context.getDevice(), shadowSampler, nullptr);
    vkDestroyPipeline(context.getDevice(), pipeline, nullptr);
    vkDestroyPipelineLayout(context.getDevice(), pipelineLayout, nullptr);
    vkDestroyRenderPass(context.getDevice(), renderPass, nullptr);
}

void ShadowRenderer::render(VkCommandBuffer cmd, const std::vector<GameObject>& gameObjects) {
    for (uint32_t cascade = 0; cascade < CASCADE_COUNT; cascade++) {
        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = renderPass;
        renderPassInfo.framebuffer = cascadeFramebuffers[cascade];
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE};

        VkClearValue clearValue = {1.0f, 0};
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearValue;

        vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        for (const auto& gameObject : gameObjects) {
            // Bind main descriptor set for UBO/Textures if needed (shadow shader usually only needs vertex pos)
            // But pipeline was created with main descriptor layout, so we might need to bind it?
            // The original code bound it:
            // vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipelineLayout, 0, 1, &gameObject.descriptorSet, 0, nullptr);
            // Yes, we need to bind it.
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &gameObject.descriptorSet, 0, nullptr);

            ShadowPushConstant push{};
            push.model = gameObject.transform;
            vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ShadowPushConstant), &push);

            gameObject.mesh->bind(cmd);
            gameObject.mesh->draw(cmd);
        }
        vkCmdEndRenderPass(cmd);
    }
    
    // Transition shadow array to shader read
    VkImageMemoryBarrier shadowBarrier{};
    shadowBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    shadowBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    shadowBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    shadowBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    shadowBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    shadowBarrier.image = shadowArrayImage;
    shadowBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    shadowBarrier.subresourceRange.baseMipLevel = 0;
    shadowBarrier.subresourceRange.levelCount = 1;
    shadowBarrier.subresourceRange.baseArrayLayer = 0;
    shadowBarrier.subresourceRange.layerCount = CASCADE_COUNT;
    shadowBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    shadowBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &shadowBarrier);
}

ShadowUBOData ShadowRenderer::computeShadowData(const Camera& camera, const glm::vec3& lightDir, uint32_t screenWidth, uint32_t screenHeight) {
    ShadowUBOData data{};
    
    float nearClip = 0.1f;
    float farClip = 100.0f;
    float aspectRatio = static_cast<float>(screenWidth) / static_cast<float>(screenHeight);
    float fov = glm::radians(45.0f); 
    
    calculateCascadeSplits(nearClip, farClip, 0.5f);
    
    glm::mat4 camView = camera.getViewMatrix();
    glm::mat4 invCamView = glm::inverse(camView);
    
    glm::vec3 lightDirNorm = -glm::normalize(lightDir);
    
    float cascadeEnds[5] = {
        nearClip,
        cascadeSplitDistances[0] * farClip,
        cascadeSplitDistances[1] * farClip,
        cascadeSplitDistances[2] * farClip,
        cascadeSplitDistances[3] * farClip
    };
    
    data.cascadeSplits = glm::vec4(
        cascadeEnds[1], cascadeEnds[2], cascadeEnds[3], cascadeEnds[4]
    );
    
    for (int i = 0; i < 4; i++) {
        float cascadeNear = cascadeEnds[i];
        float cascadeFar = cascadeEnds[i + 1];
        
        float tanHalfFov = tan(fov * 0.5f);
        float nearHeight = cascadeNear * tanHalfFov;
        float nearWidth = nearHeight * aspectRatio;
        float farHeight = cascadeFar * tanHalfFov;
        float farWidth = farHeight * aspectRatio;
        
        glm::vec3 frustumCornersVS[8] = {
            glm::vec3(-nearWidth, -nearHeight, -cascadeNear),
            glm::vec3( nearWidth, -nearHeight, -cascadeNear),
            glm::vec3( nearWidth,  nearHeight, -cascadeNear),
            glm::vec3(-nearWidth,  nearHeight, -cascadeNear),
            glm::vec3(-farWidth, -farHeight, -cascadeFar),
            glm::vec3( farWidth, -farHeight, -cascadeFar),
            glm::vec3( farWidth,  farHeight, -cascadeFar),
            glm::vec3(-farWidth,  farHeight, -cascadeFar)
        };
        
        glm::vec3 frustumCornersWS[8];
        glm::vec3 frustumCenter(0.0f);
        for (int j = 0; j < 8; j++) {
            glm::vec4 cornerWS = invCamView * glm::vec4(frustumCornersVS[j], 1.0f);
            frustumCornersWS[j] = glm::vec3(cornerWS);
            frustumCenter += frustumCornersWS[j];
        }
        frustumCenter /= 8.0f;
        
        float radius = 0.0f;
        for (int j = 0; j < 8; j++) {
            float dist = glm::length(frustumCornersWS[j] - frustumCenter);
            radius = std::max(radius, dist);
        }
        radius = std::ceil(radius * 16.0f) / 16.0f;
        
        glm::vec3 lightPos = frustumCenter - lightDirNorm * radius;
        glm::mat4 lightView = glm::lookAt(lightPos, frustumCenter, glm::vec3(0.0f, 1.0f, 0.0f));
        
        float orthoSize = radius;
        float worldUnitsPerTexel = (orthoSize * 2.0f) / 2048.0f;
        
        glm::vec4 shadowOrigin = lightView * glm::vec4(frustumCenter, 1.0f);
        shadowOrigin.x = floor(shadowOrigin.x / worldUnitsPerTexel) * worldUnitsPerTexel;
        shadowOrigin.y = floor(shadowOrigin.y / worldUnitsPerTexel) * worldUnitsPerTexel;
        glm::vec4 snappedOriginWS = glm::inverse(lightView) * shadowOrigin;
        glm::vec3 snappedCenter = glm::vec3(snappedOriginWS);
        
        lightPos = snappedCenter - lightDirNorm * radius;
        lightView = glm::lookAt(lightPos, snappedCenter, glm::vec3(0.0f, 1.0f, 0.0f));
        
        glm::mat4 lightProj = glm::ortho(-orthoSize, orthoSize, -orthoSize, orthoSize, 0.0f, radius * 2.0f);
        lightProj[1][1] *= -1;
        
        data.cascadeViewProj[i] = lightProj * lightView;
        
        if (i == 0) {
            data.lightSpaceMatrix = data.cascadeViewProj[0];
        }
    }
    
    data.shadowParams = glm::vec4(2048.0f, 2.0f, 0.0005f, 0.1f);
    return data;
}

void ShadowRenderer::calculateCascadeSplits(float nearClip, float farClip, float lambda) {
    const int NUM_CASCADES = 4;
    float clipRange = farClip - nearClip;
    float minZ = nearClip;
    float maxZ = nearClip + clipRange;
    float range = maxZ - minZ;
    float ratio = maxZ / minZ;
    
    for (int i = 0; i < NUM_CASCADES; i++) {
        float p = (i + 1) / static_cast<float>(NUM_CASCADES);
        float log = minZ * std::pow(ratio, p);
        float uniform = minZ + range * p;
        float d = lambda * (log - uniform) + uniform;
        cascadeSplitDistances[i] = (d - nearClip) / clipRange;
    }
}

void ShadowRenderer::createRenderPass() {
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = context.findDepthFormat();
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 0;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 0;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    std::array<VkSubpassDependency, 2> dependencies;
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &depthAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();

    if (vkCreateRenderPass(context.getDevice(), &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shadow render pass!");
    }
}

void ShadowRenderer::createResources() {
    VkFormat depthFormat = context.findDepthFormat();
    
    createImage(SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, CASCADE_COUNT, depthFormat,
                VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                shadowArrayImage, shadowArrayImageMemory);
    
    VkImageViewCreateInfo arrayViewInfo{};
    arrayViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    arrayViewInfo.image = shadowArrayImage;
    arrayViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    arrayViewInfo.format = depthFormat;
    arrayViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    arrayViewInfo.subresourceRange.baseMipLevel = 0;
    arrayViewInfo.subresourceRange.levelCount = 1;
    arrayViewInfo.subresourceRange.baseArrayLayer = 0;
    arrayViewInfo.subresourceRange.layerCount = CASCADE_COUNT;
    
    if (vkCreateImageView(context.getDevice(), &arrayViewInfo, nullptr, &shadowArrayImageView) != VK_SUCCESS) {
        throw std::runtime_error("failed to create CSM array image view!");
    }
    
    for (uint32_t i = 0; i < CASCADE_COUNT; i++) {
        cascadeViews[i] = createImageView(shadowArrayImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, i, 1, VK_IMAGE_VIEW_TYPE_2D);
        
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = &cascadeViews[i];
        framebufferInfo.width = SHADOW_MAP_SIZE;
        framebufferInfo.height = SHADOW_MAP_SIZE;
        framebufferInfo.layers = 1;
        
        if (vkCreateFramebuffer(context.getDevice(), &framebufferInfo, nullptr, &cascadeFramebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create CSM cascade framebuffer!");
        }
    }
    
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;

    if (vkCreateSampler(context.getDevice(), &samplerInfo, nullptr, &shadowSampler) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shadow sampler!");
    }
}

void ShadowRenderer::createPipeline(VkDescriptorSetLayout descriptorSetLayout) {
    auto vertShaderCode = readFile("shaders/shadow.vert.spv");
    auto fragShaderCode = readFile("shaders/shadow.frag.spv");

    VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 1;
    vertexInputInfo.pVertexAttributeDescriptions = &attributeDescriptions[0];

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)SHADOW_MAP_SIZE;
    viewport.height = (float)SHADOW_MAP_SIZE;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE};

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = 1.25f;
    rasterizer.depthBiasSlopeFactor = 1.75f;
    rasterizer.depthBiasClamp = 0.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 0;

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(ShadowPushConstant);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(context.getDevice(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shadow pipeline layout!");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    if (vkCreateGraphicsPipelines(context.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shadow graphics pipeline!");
    }

    vkDestroyShaderModule(context.getDevice(), fragShaderModule, nullptr);
    vkDestroyShaderModule(context.getDevice(), vertShaderModule, nullptr);
}

// Helpers
void ShadowRenderer::createImage(uint32_t width, uint32_t height, uint32_t layers, VkFormat format, 
                                      VkImageTiling tiling, VkImageUsageFlags usage, 
                                      VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = layers;
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

VkImageView ShadowRenderer::createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, 
                                            uint32_t baseLayer, uint32_t layerCount, VkImageViewType viewType) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = viewType;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = baseLayer;
    viewInfo.subresourceRange.layerCount = layerCount;

    VkImageView imageView;
    if (vkCreateImageView(context.getDevice(), &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
        throw std::runtime_error("failed to create image view!");
    }
    return imageView;
}

VkShaderModule ShadowRenderer::createShaderModule(const std::vector<char>& code) {
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

std::vector<char> ShadowRenderer::readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open file!");
    }
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
}
