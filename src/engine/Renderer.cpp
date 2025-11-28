#include "Renderer.h"
#include "Vertex.h"
#include "Input.h"
#include <iostream>
#include <vector>
#include <fstream>
#include <chrono>
#include <cmath>
#include <limits>
#include <glm/gtc/matrix_transform.hpp>
#include "../external/stb_image.h"
#include "../external/tiny_obj_loader.h"
#include <unordered_map>
#include <set>
#include "DescriptorManager.h"

Renderer::Renderer(Window& window) 
    : window(window)
    , camera(static_cast<float>(window.getWidth()) / static_cast<float>(window.getHeight()))
{
    createInstance();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    createSwapchain();
    createImageViews();
    
    // Forward render pass (needed for skybox after deferred)
    createRenderPass();
    
    // Descriptor layouts must be created before pipelines that use them
    createDescriptorSetLayout();
    createSkyboxDescriptorSetLayout();
    createGBufferDescriptorSetLayout();
    createCompositionDescriptorSetLayout();
    
    // Shadow pass setup (CSM)
    createShadowResources();
    createShadowRenderPass();
    createCSMResources();  // Needs shadowRenderPass for framebuffers
    createShadowGraphicsPipeline();  // Needs descriptorSetLayout
    
    createCommandPool();
    createDepthResources();
    
    // Forward framebuffers (for skybox pass)
    createFramebuffers();

    // G-Buffer resources (must be after depth resources)
    createGBufferResources();

    // Deferred render pass with 2 subpasses
    createDeferredRenderPass();
    createDeferredFramebuffers();

    // Pipelines
    createGraphicsPipeline();
    
    loadMeshShaderFunctions();
    createMeshPipeline();
    
    createSkyboxGraphicsPipeline();
    createGBufferPipeline();
    createCompositionPipeline();

    createUniformBuffers();
    createDescriptorPool();
    createCommandBuffers();
    
    skybox = std::make_unique<Skybox>(physicalDevice, device, commandPool, graphicsQueue);
    skybox->createDescriptorSet(descriptorPool, skyboxDescriptorSetLayout, uniformBuffer, sizeof(UniformBufferObject));
    
    // Create composition descriptor set (needs skybox to be initialized first)
    createCompositionDescriptorSets();

    createSyncObjects();
    
    // Initialize Global Descriptor Manager
    DescriptorManager::getInstance().init(device, physicalDevice);
    
    loadGameObjects();
    
    std::cout << "=== DEFERRED RENDERING ENABLED ===" << std::endl;
    std::cout << "G-Buffer: Position, Normal, Albedo, PBR" << std::endl;
    std::cout << "CSM: " << CSM_CASCADE_COUNT << " cascades at " << SHADOW_MAP_SIZE << "x" << SHADOW_MAP_SIZE << std::endl;
    std::cout << "==================================" << std::endl;
}

void Renderer::createSyncObjects() {
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphore) != VK_SUCCESS ||
        vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphore) != VK_SUCCESS ||
        vkCreateFence(device, &fenceInfo, nullptr, &inFlightFence) != VK_SUCCESS) {
        throw std::runtime_error("failed to create sync objects!");
    }
    std::cout << "Sync Objects created successfully!" << std::endl;
}

void Renderer::waitIdle() {
    vkDeviceWaitIdle(device);
}

void Renderer::drawFrame() {
    vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &inFlightFence);

    uint32_t imageIndex;
    vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

    updateUniformBuffer(imageIndex);

    static auto lastLogTime = std::chrono::high_resolution_clock::now();
    auto currentTime = std::chrono::high_resolution_clock::now();
    float timeDiff = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - lastLogTime).count();

    if (timeDiff > 1.0f) {
        lastLogTime = currentTime;
        glm::vec3 camPos = camera.getPosition();
        std::cout << "Camera Pos: " << camPos.x << ", " << camPos.y << ", " << camPos.z << std::endl;
        std::cout << "Drawing " << gameObjects.size() << " objects (Deferred)." << std::endl;
    }

    vkResetCommandBuffer(commandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin recording command buffer!");
    }
    
    // ========================================================================
    // PASS 1: Cascaded Shadow Maps (render each cascade)
    // ========================================================================
    for (uint32_t cascade = 0; cascade < CSM_CASCADE_COUNT; cascade++) {
        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = shadowRenderPass;
        renderPassInfo.framebuffer = shadowCascadeFramebuffers[cascade];
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE};

        VkClearValue clearValue = {1.0f, 0};
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearValue;

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline);

        for (const auto& gameObject : gameObjects) {
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipelineLayout, 0, 1, &gameObject.descriptorSet, 0, nullptr);

            PushConstantData push{};
            push.model = gameObject.transform;
            vkCmdPushConstants(commandBuffer, shadowPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstantData), &push);

            gameObject.mesh->bind(commandBuffer);
            gameObject.mesh->draw(commandBuffer);
        }
        vkCmdEndRenderPass(commandBuffer);
    }
    
    // Transition shadow array to shader read after all cascades rendered
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
    shadowBarrier.subresourceRange.layerCount = CSM_CASCADE_COUNT;
    shadowBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    shadowBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &shadowBarrier);

    // ========================================================================
    // PASS 2: Deferred Rendering (G-Buffer + Composition)
    // ========================================================================
    {
        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = deferredRenderPass;
        renderPassInfo.framebuffer = deferredFramebuffers[imageIndex];
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = swapchainExtent;

        // Clear values: 4 G-Buffer + Depth + Swapchain
        std::array<VkClearValue, 6> clearValues{};
        clearValues[0].color = {{0.0f, 0.0f, 0.0f, 0.0f}};  // Position
        clearValues[1].color = {{0.5f, 0.5f, 1.0f, 0.0f}};  // Normal (default up)
        clearValues[2].color = {{0.0f, 0.0f, 0.0f, 0.0f}};  // Albedo
        clearValues[3].color = {{0.5f, 1.0f, 0.0f, 0.0f}};  // PBR (roughness=0.5, ao=1)
        clearValues[4].depthStencil = {1.0f, 0};            // Depth
        clearValues[5].color = {{0.0f, 0.0f, 0.0f, 1.0f}};  // Swapchain

        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        // ------ Subpass 0: Geometry Pass (G-Buffer) ------
        // Switch to Mesh Pipeline
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipeline);

        for (const auto& gameObject : gameObjects) {
            PushConstantData push{};
            push.model = gameObject.transform;
            push.normalMatrix = glm::transpose(glm::inverse(gameObject.transform));
            push.meshletBufferAddress = gameObject.mesh->getMeshletBufferAddress();
            push.meshletVerticesAddress = gameObject.mesh->getMeshletVerticesBufferAddress();
            push.meshletTrianglesAddress = gameObject.mesh->getMeshletTrianglesBufferAddress();
            push.vertexBufferAddress = gameObject.mesh->getVertexBufferAddress();
            push.meshletCount = gameObject.mesh->getMeshletCount();
            
            vkCmdPushConstants(commandBuffer, meshPipelineLayout, VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstantData), &push);
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipelineLayout, 0, 1, &gameObject.descriptorSet, 0, nullptr);
            
            // Bind mesh resources (vertex/index buffers are not used by mesh shader, but we might need them later for mixed mode)
            // For now, we just need the meshlet count to dispatch tasks
            
            // Dispatch Mesh Tasks
            // Group count X = (meshlet count + 31) / 32
            // Group count Y = 1
            // Group count Z = 1
            uint32_t groupCountX = (gameObject.mesh->getMeshletCount() + 31) / 32;
            std::cout << "Drawing object with " << gameObject.mesh->getMeshletCount() << " meshlets. GroupCountX: " << groupCountX << std::endl;
            vkCmdDrawMeshTasksEXT(commandBuffer, groupCountX, 1, 1);
        }

        // ------ Subpass 1: Composition Pass (Lighting) ------
        vkCmdNextSubpass(commandBuffer, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, compositionPipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, compositionPipelineLayout, 0, 1, &compositionDescriptorSet, 0, nullptr);
        
        // Draw fullscreen triangle (3 vertices, no vertex buffer needed)
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);

        vkCmdEndRenderPass(commandBuffer);
    }
    
    // ========================================================================
    // PASS 3: Skybox (rendered after deferred, uses forward render pass)
    // ========================================================================
    {
        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = renderPass;
        renderPassInfo.framebuffer = swapchainFramebuffers[imageIndex];
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = swapchainExtent;

        // Don't clear - we want to preserve the deferred output
        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        clearValues[1].depthStencil = {1.0f, 0};
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        
        // Draw Skybox
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxPipeline);
        skybox->draw(commandBuffer, skyboxPipelineLayout);

        vkCmdEndRenderPass(commandBuffer);
    }

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to record command buffer!");
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {imageAvailableSemaphore};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    VkSemaphore signalSemaphores[] = {renderFinishedSemaphore};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFence) != VK_SUCCESS) {
        throw std::runtime_error("failed to submit draw command buffer!");
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapchains[] = {swapchain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;

    vkQueuePresentKHR(presentQueue, &presentInfo);
}

uint32_t Renderer::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("failed to find suitable memory type!");
}

void Renderer::createDepthResources() {
    VkFormat depthFormat = findDepthFormat();
    
    createImage(swapchainExtent.width, swapchainExtent.height, depthFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depthImage, depthImageMemory);
    depthImageView = createImageView(depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
    
    std::cout << "Depth Resources created successfully!" << std::endl;
}

void Renderer::createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory) {
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

    if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
        throw std::runtime_error("failed to create image!");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate image memory!");
    }

    vkBindImageMemory(device, image, imageMemory, 0);
}

void Renderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
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
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate buffer memory!");
    }

    vkBindBufferMemory(device, buffer, bufferMemory, 0);
}

void Renderer::copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
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

VkImageView Renderer::createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) {
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
    if (vkCreateImageView(device, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
        throw std::runtime_error("failed to create image view!");
    }

    return imageView;
}

void Renderer::createImageViews() {
    swapchainImageViews.resize(swapchainImages.size());

    for (size_t i = 0; i < swapchainImages.size(); i++) {
        swapchainImageViews[i] = createImageView(swapchainImages[i], swapchainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT);
    }
    std::cout << "Image Views created successfully!" << std::endl;
}

VkFormat Renderer::findDepthFormat() {
    return findSupportedFormat(
        {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
    );
}

VkFormat Renderer::findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) {
    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
            return format;
        } else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }

    throw std::runtime_error("failed to find supported format!");
}

void Renderer::createDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT;
    uboLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding diffuseLayoutBinding{};
    diffuseLayoutBinding.binding = 1;
    diffuseLayoutBinding.descriptorCount = 1;
    diffuseLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    diffuseLayoutBinding.pImmutableSamplers = nullptr;
    diffuseLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding specularLayoutBinding{};
    specularLayoutBinding.binding = 2;
    specularLayoutBinding.descriptorCount = 1;
    specularLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    specularLayoutBinding.pImmutableSamplers = nullptr;
    specularLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding normalLayoutBinding{};
    normalLayoutBinding.binding = 3;
    normalLayoutBinding.descriptorCount = 1;
    normalLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    normalLayoutBinding.pImmutableSamplers = nullptr;
    normalLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding shadowMapLayoutBinding{};
    shadowMapLayoutBinding.binding = 4;
    shadowMapLayoutBinding.descriptorCount = 1;
    shadowMapLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    shadowMapLayoutBinding.pImmutableSamplers = nullptr;
    shadowMapLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Environment map (skybox cubemap) for IBL
    VkDescriptorSetLayoutBinding envMapLayoutBinding{};
    envMapLayoutBinding.binding = 5;
    envMapLayoutBinding.descriptorCount = 1;
    envMapLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    envMapLayoutBinding.pImmutableSamplers = nullptr;
    envMapLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::array<VkDescriptorSetLayoutBinding, 6> bindings = {uboLayoutBinding, diffuseLayoutBinding, specularLayoutBinding, normalLayoutBinding, shadowMapLayoutBinding, envMapLayoutBinding};
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor set layout!");
    }
    std::cout << "Descriptor Set Layout created successfully!" << std::endl;
}

void Renderer::createUniformBuffers() {
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);
    
    createBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, uniformBuffer, uniformBufferMemory);
    vkMapMemory(device, uniformBufferMemory, 0, bufferSize, 0, &uniformBufferMapped);
    
    std::cout << "Uniform Buffers created successfully!" << std::endl;
}

void Renderer::createDescriptorPool() {
    std::array<VkDescriptorPoolSize, 3> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = static_cast<uint32_t>(100);
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = static_cast<uint32_t>(600); // Increased for env map + CSM
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    poolSizes[2].descriptorCount = static_cast<uint32_t>(10);  // For G-Buffer input attachments

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 110; // Increased for deferred descriptor sets

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor pool!");
    }
    std::cout << "Descriptor Pool created successfully!" << std::endl;
}

void Renderer::createDescriptorSet(GameObject& gameObject) {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout;

    if (vkAllocateDescriptorSets(device, &allocInfo, &gameObject.descriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate descriptor sets!");
    }

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = uniformBuffer;
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(UniformBufferObject);

    VkDescriptorImageInfo diffuseInfo{};
    diffuseInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    diffuseInfo.imageView = gameObject.material->diffuse->getImageView();
    diffuseInfo.sampler = gameObject.material->diffuse->getSampler();

    VkDescriptorImageInfo specularInfo{};
    specularInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    specularInfo.imageView = gameObject.material->specular->getImageView();
    specularInfo.sampler = gameObject.material->specular->getSampler();

    VkDescriptorImageInfo normalInfo{};
    normalInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    normalInfo.imageView = gameObject.material->normal->getImageView();
    normalInfo.sampler = gameObject.material->normal->getSampler();

    VkDescriptorImageInfo shadowInfo{};
    shadowInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    shadowInfo.imageView = shadowImageView;
    shadowInfo.sampler = shadowSampler;

    // Environment map (skybox cubemap) for IBL
    VkDescriptorImageInfo envMapInfo{};
    envMapInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    envMapInfo.imageView = skybox->getImageView();
    envMapInfo.sampler = skybox->getSampler();

    std::array<VkWriteDescriptorSet, 6> descriptorWrites{};

    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = gameObject.descriptorSet;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pBufferInfo = &bufferInfo;

    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = gameObject.descriptorSet;
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].dstArrayElement = 0;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pImageInfo = &diffuseInfo;

    descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[2].dstSet = gameObject.descriptorSet;
    descriptorWrites[2].dstBinding = 2;
    descriptorWrites[2].dstArrayElement = 0;
    descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[2].descriptorCount = 1;
    descriptorWrites[2].pImageInfo = &specularInfo;

    descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[3].dstSet = gameObject.descriptorSet;
    descriptorWrites[3].dstBinding = 3;
    descriptorWrites[3].dstArrayElement = 0;
    descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[3].descriptorCount = 1;
    descriptorWrites[3].pImageInfo = &normalInfo;

    descriptorWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[4].dstSet = gameObject.descriptorSet;
    descriptorWrites[4].dstBinding = 4;
    descriptorWrites[4].dstArrayElement = 0;
    descriptorWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[4].descriptorCount = 1;
    descriptorWrites[4].pImageInfo = &shadowInfo;

    descriptorWrites[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[5].dstSet = gameObject.descriptorSet;
    descriptorWrites[5].dstBinding = 5;
    descriptorWrites[5].dstArrayElement = 0;
    descriptorWrites[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[5].descriptorCount = 1;
    descriptorWrites[5].pImageInfo = &envMapInfo;

    vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
}

void Renderer::processInput(float deltaTime) {
    Input& input = Input::getInstance();
    
    bool turbo = input.isKeyDown(GLFW_KEY_LEFT_SHIFT) || input.isKeyDown(GLFW_KEY_RIGHT_SHIFT);
    
    if (input.isKeyDown(GLFW_KEY_W))
        camera.processKeyboard(Camera::FORWARD, deltaTime, turbo);
    if (input.isKeyDown(GLFW_KEY_S))
        camera.processKeyboard(Camera::BACKWARD, deltaTime, turbo);
    if (input.isKeyDown(GLFW_KEY_A))
        camera.processKeyboard(Camera::LEFT, deltaTime, turbo);
    if (input.isKeyDown(GLFW_KEY_D))
        camera.processKeyboard(Camera::RIGHT, deltaTime, turbo);
    if (input.isKeyDown(GLFW_KEY_SPACE))
        camera.processKeyboard(Camera::UP, deltaTime, turbo);
    if (input.isKeyDown(GLFW_KEY_LEFT_CONTROL))
        camera.processKeyboard(Camera::DOWN, deltaTime, turbo);
    
    glm::vec2 mouseDelta = input.getMouseDelta();
    if (glm::length(mouseDelta) > 0.0f) {
        camera.processMouseMovement(mouseDelta.x, mouseDelta.y);
    }
}

void Renderer::updateUniformBuffer(uint32_t currentImage) {
    UniformBufferObject ubo{};
    ubo.view = camera.getViewMatrix();
    ubo.proj = camera.getProjectionMatrix();
    
    ubo.viewPos = glm::vec4(camera.getPosition(), 1.0f);
    ubo.lightColor = glm::vec4(1.0f, 0.98f, 0.95f, 1.0f);  // Warm white

    // ========================================================================
    // AAA STANDARD: Directional Light Setup
    // ========================================================================
    // lightDir points FROM the scene TOWARD the light source (sun direction)
    glm::vec3 lightDir = glm::normalize(glm::vec3(1.0f, 2.0f, 1.0f));
    ubo.lightPos = glm::vec4(lightDir, 0.0f);  // w=0 indicates directional light
    
    // ========================================================================
    // AAA STANDARD: Cascaded Shadow Maps with Proper Frustum Fitting
    // ========================================================================
    float nearClip = 0.1f;
    float farClip = 100.0f;
    float aspectRatio = static_cast<float>(swapchainExtent.width) / static_cast<float>(swapchainExtent.height);
    float fov = glm::radians(45.0f);  // Should match camera's FOV
    
    // Calculate cascade splits
    calculateCascadeSplits(nearClip, farClip, 0.5f);
    
    // Get camera matrices
    glm::mat4 camView = camera.getViewMatrix();
    glm::mat4 invCamView = glm::inverse(camView);
    
    // Light direction for shadow casting (FROM light TO scene = negative of lightDir)
    glm::vec3 lightDirNorm = -lightDir;
    
    // Cascade boundaries in view space
    float cascadeEnds[5] = {
        nearClip,
        cascadeSplitDistances[0] * farClip,
        cascadeSplitDistances[1] * farClip,
        cascadeSplitDistances[2] * farClip,
        cascadeSplitDistances[3] * farClip
    };
    
    // Pack cascade split distances (view-space Z values for shader)
    ubo.cascadeSplits = glm::vec4(
        cascadeEnds[1], cascadeEnds[2], cascadeEnds[3], cascadeEnds[4]
    );
    
    // Calculate view-projection matrix for each cascade
    for (int i = 0; i < 4; i++) {
        float cascadeNear = cascadeEnds[i];
        float cascadeFar = cascadeEnds[i + 1];
        
        // Calculate frustum corners in view space
        float tanHalfFov = tan(fov * 0.5f);
        float nearHeight = cascadeNear * tanHalfFov;
        float nearWidth = nearHeight * aspectRatio;
        float farHeight = cascadeFar * tanHalfFov;
        float farWidth = farHeight * aspectRatio;
        
        // 8 corners of the cascade frustum (in view space, looking down -Z)
        glm::vec3 frustumCornersVS[8] = {
            // Near plane
            glm::vec3(-nearWidth, -nearHeight, -cascadeNear),
            glm::vec3( nearWidth, -nearHeight, -cascadeNear),
            glm::vec3( nearWidth,  nearHeight, -cascadeNear),
            glm::vec3(-nearWidth,  nearHeight, -cascadeNear),
            // Far plane
            glm::vec3(-farWidth, -farHeight, -cascadeFar),
            glm::vec3( farWidth, -farHeight, -cascadeFar),
            glm::vec3( farWidth,  farHeight, -cascadeFar),
            glm::vec3(-farWidth,  farHeight, -cascadeFar)
        };
        
        // Transform frustum corners to world space
        glm::vec3 frustumCornersWS[8];
        glm::vec3 frustumCenter(0.0f);
        for (int j = 0; j < 8; j++) {
            glm::vec4 cornerWS = invCamView * glm::vec4(frustumCornersVS[j], 1.0f);
            frustumCornersWS[j] = glm::vec3(cornerWS);
            frustumCenter += frustumCornersWS[j];
        }
        frustumCenter /= 8.0f;
        
        // Calculate the radius of a sphere containing the frustum
        float radius = 0.0f;
        for (int j = 0; j < 8; j++) {
            float dist = glm::length(frustumCornersWS[j] - frustumCenter);
            radius = std::max(radius, dist);
        }
        radius = std::ceil(radius * 16.0f) / 16.0f;  // Round up for stability
        
        // Create light view matrix - light looks at frustum center from light direction
        glm::vec3 lightPos = frustumCenter - lightDirNorm * radius;
        glm::mat4 lightView = glm::lookAt(
            lightPos,
            frustumCenter,
            glm::vec3(0.0f, 1.0f, 0.0f)
        );
        
        // Use a square ortho projection based on frustum sphere radius
        float orthoSize = radius;
        
        // Stabilize shadow map - snap to texel grid to reduce shimmer
        float worldUnitsPerTexel = (orthoSize * 2.0f) / 2048.0f;
        
        // Snap the frustum center in light space to texel boundaries
        glm::vec4 shadowOrigin = lightView * glm::vec4(frustumCenter, 1.0f);
        shadowOrigin.x = floor(shadowOrigin.x / worldUnitsPerTexel) * worldUnitsPerTexel;
        shadowOrigin.y = floor(shadowOrigin.y / worldUnitsPerTexel) * worldUnitsPerTexel;
        glm::vec4 snappedOriginWS = glm::inverse(lightView) * shadowOrigin;
        glm::vec3 snappedCenter = glm::vec3(snappedOriginWS);
        
        // Recalculate light view with snapped center
        lightPos = snappedCenter - lightDirNorm * radius;
        lightView = glm::lookAt(
            lightPos,
            snappedCenter,
            glm::vec3(0.0f, 1.0f, 0.0f)
        );
        
        // Create orthographic projection
        // Near/far extend behind and in front to catch all shadow casters
        glm::mat4 lightProj = glm::ortho(
            -orthoSize, orthoSize,
            -orthoSize, orthoSize,
            0.0f, radius * 2.0f
        );
        lightProj[1][1] *= -1;  // Vulkan Y-flip
        
        ubo.cascadeViewProj[i] = lightProj * lightView;
        
        // Store first cascade matrix as the main light space matrix
        if (i == 0) {
            ubo.lightSpaceMatrix = ubo.cascadeViewProj[0];
        }
    }
    
    // Shadow parameters: x=mapSize, y=pcfRadius, z=bias, w=cascadeBlendRange
    ubo.shadowParams = glm::vec4(2048.0f, 2.0f, 0.0005f, 0.1f);

    memcpy(uniformBufferMapped, &ubo, sizeof(ubo));
}

void Renderer::createInstance() {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Sanic Engine";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions;
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    createInfo.enabledExtensionCount = glfwExtensionCount;
    createInfo.ppEnabledExtensionNames = glfwExtensions;
    createInfo.enabledLayerCount = 0;

    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
        throw std::runtime_error("failed to create instance!");
    }
    std::cout << "Instance created successfully!" << std::endl;
}

void Renderer::createSurface() {
    if (glfwCreateWindowSurface(instance, window.getHandle(), nullptr, &surface) != VK_SUCCESS) {
        throw std::runtime_error("failed to create window surface!");
    }
    std::cout << "Surface created successfully!" << std::endl;
}

void Renderer::pickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

    if (deviceCount == 0) {
        throw std::runtime_error("failed to find GPUs with Vulkan support!");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    for (const auto& device : devices) {
        if (isDeviceSuitable(device)) {
            physicalDevice = device;
            break;
        }
    }

    if (physicalDevice == VK_NULL_HANDLE) {
        throw std::runtime_error("failed to find a suitable GPU!");
    }
    std::cout << "Physical Device picked successfully!" << std::endl;
}

void Renderer::createLogicalDevice() {
    QueueFamilyIndices indices = findQueueFamilies(physicalDevice);

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {indices.graphicsFamily, indices.presentFamily};

    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    // --- Features Chain ---
    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = VK_TRUE;
    // Enable standard features if needed (e.g. geometryShader, etc.)

    // Vulkan 1.2 Features (Descriptor Indexing, Buffer Device Address)
    VkPhysicalDeviceVulkan12Features vulkan12Features{};
    vulkan12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    vulkan12Features.bufferDeviceAddress = VK_TRUE;
    vulkan12Features.descriptorBindingPartiallyBound = VK_TRUE;
    vulkan12Features.descriptorBindingVariableDescriptorCount = VK_TRUE;
    vulkan12Features.runtimeDescriptorArray = VK_TRUE;
    vulkan12Features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    vulkan12Features.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE; // For bindless buffers

    // Mesh Shader Features
    VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures{};
    meshShaderFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
    meshShaderFeatures.meshShader = VK_TRUE;
    meshShaderFeatures.taskShader = VK_TRUE;

    // Ray Tracing Features
    VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeatures{};
    asFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    asFeatures.accelerationStructure = VK_TRUE;

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtPipelineFeatures{};
    rtPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
    rtPipelineFeatures.rayTracingPipeline = VK_TRUE;

    // Chain them together
    void* pNextChain = &vulkan12Features;
    vulkan12Features.pNext = &meshShaderFeatures;
    meshShaderFeatures.pNext = &asFeatures;
    asFeatures.pNext = &rtPipelineFeatures;
    rtPipelineFeatures.pNext = nullptr;

    // --- Device Creation ---
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = pNextChain; // Attach the feature chain
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;

    // ========================================================================
    // AAA STANDARD: Device Extensions
    // ========================================================================
    std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_MAINTENANCE3_EXTENSION_NAME, // Required for descriptor indexing (pre-1.2)
        VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME, // Required if not 1.2, but good to have
        
        // Bindless / RT / Mesh
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
        VK_EXT_MESH_SHADER_EXTENSION_NAME,
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_KHR_SPIRV_1_4_EXTENSION_NAME,
        VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME
    };
    
    // Check for ray tracing support (optional logic, but we enforce it now)
    rayTracingSupported = checkRayTracingSupport(physicalDevice);
    if (rayTracingSupported) {
        std::cout << "=== RAY TRACING SUPPORT CONFIRMED ===" << std::endl;
    } else {
        std::cerr << "WARNING: Ray Tracing extensions missing! Engine might crash if features are used." << std::endl;
    }

    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();
    createInfo.enabledLayerCount = 0;

    if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS) {
        throw std::runtime_error("failed to create logical device!");
    }

    vkGetDeviceQueue(device, indices.graphicsFamily, 0, &graphicsQueue);
    vkGetDeviceQueue(device, indices.presentFamily, 0, &presentQueue);
    std::cout << "Logical Device created successfully with AAA extensions!" << std::endl;
}

// ============================================================================
// AAA STANDARD: Ray Tracing Support Check
// ============================================================================
bool Renderer::checkRayTracingSupport(VkPhysicalDevice device) {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    // Required extensions for ray tracing
    std::set<std::string> requiredRTExtensions = {
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME
    };

    for (const auto& extension : availableExtensions) {
        requiredRTExtensions.erase(extension.extensionName);
    }

    return requiredRTExtensions.empty();
}

void Renderer::initRayTracingProperties() {
    if (!rayTracingSupported) return;
    
    // Would query VkPhysicalDeviceRayTracingPipelinePropertiesKHR here
    // For now, set reasonable defaults
    rtProperties.shaderGroupHandleSize = 32;
    rtProperties.maxRayRecursionDepth = 31;
    rtProperties.maxShaderGroupStride = 4096;
}

// ============================================================================
// AAA STANDARD: Cascaded Shadow Map Split Calculation
// ============================================================================
void Renderer::calculateCascadeSplits(float nearClip, float farClip, float lambda) {
    // Practical Split Scheme (GPU Gems 3, Chapter 10)
    // Combines logarithmic and uniform split schemes
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

void Renderer::createSwapchain() {
    SwapChainSupportDetails swapChainSupport = querySwapChainSupport(physicalDevice);

    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
    if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
        imageCount = swapChainSupport.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
    uint32_t queueFamilyIndices[] = {indices.graphicsFamily, indices.presentFamily};

    if (indices.graphicsFamily != indices.presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain) != VK_SUCCESS) {
        throw std::runtime_error("failed to create swap chain!");
    }

    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
    swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data());

    swapchainImageFormat = surfaceFormat.format;
    swapchainExtent = extent;
    std::cout << "Swapchain created successfully!" << std::endl;
}

void Renderer::createRenderPass() {
    std::cout << "Creating Render Pass (for skybox after deferred)..." << std::endl;
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapchainImageFormat;
    // ...
    std::cout << "Finding depth format..." << std::endl;
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = findDepthFormat();
    std::cout << "Depth format found: " << depthAttachment.format << std::endl;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    // LOAD_OP_LOAD to preserve deferred composition output
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;  // Coming from deferred
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    // Load depth to use for skybox depth testing
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
        throw std::runtime_error("failed to create render pass!");
    }
    std::cout << "Render Pass created successfully!" << std::endl;
}

void Renderer::createFramebuffers() {
    swapchainFramebuffers.resize(swapchainImageViews.size());

    for (size_t i = 0; i < swapchainImageViews.size(); i++) {
        std::array<VkImageView, 2> attachments = {
            swapchainImageViews[i],
            depthImageView
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = swapchainExtent.width;
        framebufferInfo.height = swapchainExtent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapchainFramebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create framebuffer!");
        }
    }
    std::cout << "Framebuffers created successfully!" << std::endl;
}

void Renderer::createCommandPool() {
    QueueFamilyIndices queueFamilyIndices = findQueueFamilies(physicalDevice);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily;

    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create command pool!");
    }
    std::cout << "Command Pool created successfully!" << std::endl;
}

void Renderer::createCommandBuffers() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate command buffers!");
    }
    std::cout << "Command Buffers created successfully!" << std::endl;
}

VkSurfaceFormatKHR Renderer::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
    for (const auto& availableFormat : availableFormats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return availableFormat;
        }
    }

    return availableFormats[0];
}

VkPresentModeKHR Renderer::chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
    for (const auto& availablePresentMode : availablePresentModes) {
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return availablePresentMode;
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D Renderer::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
    if (capabilities.currentExtent.width != UINT32_MAX) {
        return capabilities.currentExtent;
    } else {
        int width, height;
        glfwGetFramebufferSize(window.getHandle(), &width, &height);

        VkExtent2D actualExtent = {
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height)
        };

        actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

        return actualExtent;
    }
}

bool Renderer::checkDeviceExtensionSupport(VkPhysicalDevice device) {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    for (const auto& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty();
}

bool Renderer::isDeviceSuitable(VkPhysicalDevice device) {
    QueueFamilyIndices indices = findQueueFamilies(device);

    bool extensionsSupported = checkDeviceExtensionSupport(device);

    bool swapChainAdequate = false;
    if (extensionsSupported) {
        SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);
        swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
    }

    VkPhysicalDeviceFeatures supportedFeatures;
    vkGetPhysicalDeviceFeatures(device, &supportedFeatures);

    return indices.isComplete() && extensionsSupported && swapChainAdequate && supportedFeatures.samplerAnisotropy;
}

Renderer::QueueFamilyIndices Renderer::findQueueFamilies(VkPhysicalDevice device) {
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    int i = 0;
    for (const auto& queueFamily : queueFamilies) {
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = i;
        }

        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);

        if (presentSupport) {
            indices.presentFamily = i;
        }

        if (indices.isComplete()) {
            break;
        }

        i++;
    }

    return indices;
}

Renderer::SwapChainSupportDetails Renderer::querySwapChainSupport(VkPhysicalDevice device) {
    SwapChainSupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);

    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);

    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
    }

    return details;
}

std::vector<char> Renderer::readFile(const std::string& filename) {
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

VkShaderModule Renderer::createShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shader module!");
    }

    return shaderModule;
}

Renderer::~Renderer() {
    vkDeviceWaitIdle(device);

    // Cleanup G-Buffer resources
    vkDestroyImageView(device, gBufferPosition.view, nullptr);
    vkDestroyImage(device, gBufferPosition.image, nullptr);
    vkFreeMemory(device, gBufferPosition.memory, nullptr);
    
    vkDestroyImageView(device, gBufferNormal.view, nullptr);
    vkDestroyImage(device, gBufferNormal.image, nullptr);
    vkFreeMemory(device, gBufferNormal.memory, nullptr);
    
    vkDestroyImageView(device, gBufferAlbedo.view, nullptr);
    vkDestroyImage(device, gBufferAlbedo.image, nullptr);
    vkFreeMemory(device, gBufferAlbedo.memory, nullptr);
    
    vkDestroyImageView(device, gBufferPBR.view, nullptr);
    vkDestroyImage(device, gBufferPBR.image, nullptr);
    vkFreeMemory(device, gBufferPBR.memory, nullptr);
    
    // Cleanup deferred framebuffers
    for (auto framebuffer : deferredFramebuffers) {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }
    
    // Cleanup deferred pipelines
    vkDestroyPipeline(device, gBufferPipeline, nullptr);
    vkDestroyPipelineLayout(device, gBufferPipelineLayout, nullptr);
    vkDestroyPipeline(device, compositionPipeline, nullptr);
    vkDestroyPipelineLayout(device, compositionPipelineLayout, nullptr);
    vkDestroyRenderPass(device, deferredRenderPass, nullptr);
    
    // Cleanup deferred descriptor layouts
    vkDestroyDescriptorSetLayout(device, gBufferDescriptorSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, compositionDescriptorSetLayout, nullptr);
    
    // Cleanup CSM resources
    vkDestroyImageView(device, shadowArrayImageView, nullptr);
    for (uint32_t i = 0; i < CSM_CASCADE_COUNT; i++) {
        vkDestroyImageView(device, shadowCascadeViews[i], nullptr);
        vkDestroyFramebuffer(device, shadowCascadeFramebuffers[i], nullptr);
    }
    vkDestroyImage(device, shadowArrayImage, nullptr);
    vkFreeMemory(device, shadowArrayImageMemory, nullptr);
    
    // Cleanup shadow resources
    vkDestroyImageView(device, shadowImageView, nullptr);
    vkDestroyImage(device, shadowImage, nullptr);
    vkFreeMemory(device, shadowImageMemory, nullptr);
    vkDestroySampler(device, shadowSampler, nullptr);
    vkDestroyFramebuffer(device, shadowFramebuffer, nullptr);
    vkDestroyPipeline(device, shadowPipeline, nullptr);
    vkDestroyPipelineLayout(device, shadowPipelineLayout, nullptr);
    vkDestroyRenderPass(device, shadowRenderPass, nullptr);

    vkDestroyImageView(device, depthImageView, nullptr);
    vkDestroyImage(device, depthImage, nullptr);
    vkFreeMemory(device, depthImageMemory, nullptr);

    for (auto framebuffer : swapchainFramebuffers) {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }

    vkDestroyPipeline(device, graphicsPipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyRenderPass(device, renderPass, nullptr);
    
    // Cleanup Mesh Pipeline
    vkDestroyPipeline(device, meshPipeline, nullptr);
    vkDestroyPipelineLayout(device, meshPipelineLayout, nullptr);

    // Cleanup skybox
    vkDestroyPipeline(device, skyboxPipeline, nullptr);
    vkDestroyPipelineLayout(device, skyboxPipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, skyboxDescriptorSetLayout, nullptr);
    skybox.reset();

    for (auto imageView : swapchainImageViews) {
        vkDestroyImageView(device, imageView, nullptr);
    }

    vkDestroySwapchainKHR(device, swapchain, nullptr);

    vkDestroyBuffer(device, uniformBuffer, nullptr);
    vkFreeMemory(device, uniformBufferMemory, nullptr);

    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

    vkDestroySemaphore(device, renderFinishedSemaphore, nullptr);
    vkDestroySemaphore(device, imageAvailableSemaphore, nullptr);
    vkDestroyFence(device, inFlightFence, nullptr);

    vkDestroyCommandPool(device, commandPool, nullptr);

    DescriptorManager::getInstance().cleanup();

    vkDestroyDevice(device, nullptr);

    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);
}

void Renderer::createGraphicsPipeline() {
    auto vertShaderCode = readFile("shaders/shader.vert.spv");
    auto fragShaderCode = readFile("shaders/shader.frag.spv");

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
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)swapchainExtent.width;
    viewport.height = (float)swapchainExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchainExtent;

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
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

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

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    colorBlending.blendConstants[0] = 0.0f;
    colorBlending.blendConstants[1] = 0.0f;
    colorBlending.blendConstants[2] = 0.0f;
    colorBlending.blendConstants[3] = 0.0f;

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstantData);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create pipeline layout!");
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

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create graphics pipeline!");
    }
    std::cout << "Graphics Pipeline created successfully!" << std::endl;

    vkDestroyShaderModule(device, fragShaderModule, nullptr);
    vkDestroyShaderModule(device, vertShaderModule, nullptr);
}

void Renderer::loadMeshShaderFunctions() {
    vkCmdDrawMeshTasksEXT = (PFN_vkCmdDrawMeshTasksEXT)vkGetDeviceProcAddr(device, "vkCmdDrawMeshTasksEXT");
    if (!vkCmdDrawMeshTasksEXT) {
        throw std::runtime_error("Could not load vkCmdDrawMeshTasksEXT function pointer!");
    }
}

void Renderer::createMeshPipeline() {
    auto taskShaderCode = readFile("shaders/nanite.task.spv");
    auto meshShaderCode = readFile("shaders/nanite.mesh.spv");
    auto fragShaderCode = readFile("shaders/gbuffer.frag.spv"); // Use G-Buffer frag shader

    VkShaderModule taskShaderModule = createShaderModule(taskShaderCode);
    VkShaderModule meshShaderModule = createShaderModule(meshShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

    VkPipelineShaderStageCreateInfo taskShaderStageInfo{};
    taskShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    taskShaderStageInfo.stage = VK_SHADER_STAGE_TASK_BIT_EXT;
    taskShaderStageInfo.module = taskShaderModule;
    taskShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo meshShaderStageInfo{};
    meshShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    meshShaderStageInfo.stage = VK_SHADER_STAGE_MESH_BIT_EXT;
    meshShaderStageInfo.module = meshShaderModule;
    meshShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {taskShaderStageInfo, meshShaderStageInfo, fragShaderStageInfo};

    // Vertex Input State is ignored for mesh shaders, but must be valid
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)swapchainExtent.width;
    viewport.height = (float)swapchainExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchainExtent;

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
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

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

    // MRT: 4 color attachments for G-Buffer
    std::array<VkPipelineColorBlendAttachmentState, 4> colorBlendAttachments{};
    for (auto& attachment : colorBlendAttachments) {
        attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | 
                                    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        attachment.blendEnable = VK_FALSE;
    }

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
    colorBlending.pAttachments = colorBlendAttachments.data();

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstantData);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &meshPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create mesh pipeline layout!");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 3;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = meshPipelineLayout;
    pipelineInfo.renderPass = deferredRenderPass; // Use Deferred Render Pass
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &meshPipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create mesh pipeline!");
    }
    std::cout << "Mesh Pipeline created successfully!" << std::endl;

    vkDestroyShaderModule(device, fragShaderModule, nullptr);
    vkDestroyShaderModule(device, meshShaderModule, nullptr);
    vkDestroyShaderModule(device, taskShaderModule, nullptr);
}

std::shared_ptr<Mesh> Renderer::createTerrainMesh() {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    int width = 20;
    int depth = 20;
    float scale = 1.0f;

    // Generate vertices
    for (int z = 0; z < depth; ++z) {
        for (int x = 0; x < width; ++x) {
            Vertex vertex{};
            vertex.pos = { (float)x * scale - (width * scale) / 2.0f, 0.0f, (float)z * scale - (depth * scale) / 2.0f };
            vertex.color = { 1.0f, 1.0f, 1.0f };
            vertex.texCoord = { (float)x, (float)z }; // Repeat texture
            vertex.normal = { 0.0f, 1.0f, 0.0f };
            vertices.push_back(vertex);
        }
    }

    // Generate indices
    for (int z = 0; z < depth - 1; ++z) {
        for (int x = 0; x < width - 1; ++x) {
            int topLeft = z * width + x;
            int topRight = topLeft + 1;
            int bottomLeft = (z + 1) * width + x;
            int bottomRight = bottomLeft + 1;

            indices.push_back(topLeft);
            indices.push_back(bottomLeft);
            indices.push_back(topRight);

            indices.push_back(topRight);
            indices.push_back(bottomLeft);
            indices.push_back(bottomRight);
        }
    }

    return std::make_shared<Mesh>(physicalDevice, device, commandPool, graphicsQueue, vertices, indices);
}

std::shared_ptr<Mesh> Renderer::createSphereMesh(int segments, int rings) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    
    const float PI = 3.14159265359f;
    
    // Generate vertices
    for (int ring = 0; ring <= rings; ++ring) {
        float phi = PI * float(ring) / float(rings);
        for (int seg = 0; seg <= segments; ++seg) {
            float theta = 2.0f * PI * float(seg) / float(segments);
            
            Vertex vertex{};
            vertex.pos.x = sin(phi) * cos(theta);
            vertex.pos.y = cos(phi);
            vertex.pos.z = sin(phi) * sin(theta);
            vertex.normal = vertex.pos; // Normal points outward from center
            vertex.color = {1.0f, 1.0f, 1.0f};
            vertex.texCoord = {float(seg) / float(segments), float(ring) / float(rings)};
            vertices.push_back(vertex);
        }
    }
    
    // Generate indices
    for (int ring = 0; ring < rings; ++ring) {
        for (int seg = 0; seg < segments; ++seg) {
            int current = ring * (segments + 1) + seg;
            int next = current + segments + 1;
            
            indices.push_back(current);
            indices.push_back(next);
            indices.push_back(current + 1);
            
            indices.push_back(current + 1);
            indices.push_back(next);
            indices.push_back(next + 1);
        }
    }
    
    return std::make_shared<Mesh>(physicalDevice, device, commandPool, graphicsQueue, vertices, indices);
}


void Renderer::loadGameObjects() {
    std::cout << "\n=== SANIC ENGINE FEATURE TEST SCENE ===" << std::endl;
    std::cout << "Loading test scene to verify rendering features..." << std::endl;
    
    // Load Terrain Textures
    auto terrainDiffuse = std::make_shared<Texture>(physicalDevice, device, commandPool, graphicsQueue, "../assets/ground/diffuse/ghz_ground_sk1_earth05_dif.png");
    auto terrainSpecular = std::make_shared<Texture>(physicalDevice, device, commandPool, graphicsQueue, "../assets/ground/specular/ghz_ground_sk1_earth05_pow.png");
    auto terrainNormal = std::make_shared<Texture>(physicalDevice, device, commandPool, graphicsQueue, "../assets/ground/normal/ghz_ground_sk1_earth05_nrm.png");

    auto terrainMaterial = std::make_shared<Material>();
    terrainMaterial->diffuse = terrainDiffuse;
    terrainMaterial->specular = terrainSpecular;
    terrainMaterial->normal = terrainNormal;
    terrainMaterial->shininess = 32.0f;

    // Load Rock Textures (high specular for reflection testing)
    auto rockDiffuse = std::make_shared<Texture>(physicalDevice, device, commandPool, graphicsQueue, "../assets/rock/diffuse/ghz_rock_sk1_wall01_dif.png");
    auto rockSpecular = std::make_shared<Texture>(physicalDevice, device, commandPool, graphicsQueue, "../assets/rock/specular/ghz_rock_sk1_wall01_pow.png");
    auto rockNormal = std::make_shared<Texture>(physicalDevice, device, commandPool, graphicsQueue, "../assets/rock/normal/ghz_rock_sk1_wall01_nrm.png");

    auto rockMaterial = std::make_shared<Material>();
    rockMaterial->diffuse = rockDiffuse;
    rockMaterial->specular = rockSpecular;
    rockMaterial->normal = rockNormal;
    rockMaterial->shininess = 64.0f;

    // Load alternate ground texture for variety
    auto terrainDiffuse2 = std::make_shared<Texture>(physicalDevice, device, commandPool, graphicsQueue, "../assets/ground/diffuse/ghz_ground_sk1_earth03_dif.png");
    auto terrainMaterial2 = std::make_shared<Material>();
    terrainMaterial2->diffuse = terrainDiffuse2;
    terrainMaterial2->specular = terrainSpecular;
    terrainMaterial2->normal = terrainNormal;
    terrainMaterial2->shininess = 16.0f;

    // Load Cube Mesh
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, "../assets/cube.obj")) {
        throw std::runtime_error(warn + err);
    }

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::unordered_map<Vertex, uint32_t> uniqueVertices{};

    for (const auto& shape : shapes) {
        for (const auto& index : shape.mesh.indices) {
            Vertex vertex{};
            vertex.pos = {
                attrib.vertices[3 * index.vertex_index + 0],
                attrib.vertices[3 * index.vertex_index + 1],
                attrib.vertices[3 * index.vertex_index + 2]
            };
            vertex.texCoord = {
                attrib.texcoords[2 * index.texcoord_index + 0],
                1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
            };
            vertex.normal = {
                attrib.normals[3 * index.normal_index + 0],
                attrib.normals[3 * index.normal_index + 1],
                attrib.normals[3 * index.normal_index + 2]
            };
            vertex.color = {1.0f, 1.0f, 1.0f};
            if (uniqueVertices.count(vertex) == 0) {
                uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
                vertices.push_back(vertex);
            }
            indices.push_back(uniqueVertices[vertex]);
        }
    }

    auto cubeMesh = std::make_shared<Mesh>(physicalDevice, device, commandPool, graphicsQueue, vertices, indices);

    // ============================================================
    // TEST SCENE LAYOUT - Designed to verify rendering features
    // ============================================================
    
    // 1. TERRAIN - Tests: Diffuse, Normal mapping, receives shadows
    auto terrainMesh = createTerrainMesh();
    GameObject terrain;
    terrain.mesh = terrainMesh;
    terrain.material = terrainMaterial;
    terrain.transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f));
    createDescriptorSet(terrain);
    gameObjects.push_back(terrain);
    std::cout << "[TERRAIN] Ground plane - Tests: Normal mapping, shadow receiving" << std::endl;

    // 2. SHADOW CASTER CUBE - Elevated to cast visible shadow on ground
    GameObject shadowCaster;
    shadowCaster.mesh = cubeMesh;
    shadowCaster.material = rockMaterial;
    shadowCaster.transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.0f, 0.0f));
    createDescriptorSet(shadowCaster);
    gameObjects.push_back(shadowCaster);
    std::cout << "[CUBE 1] Shadow caster at (0, 2, 0) - Tests: Shadow casting" << std::endl;

    // 3. SHADOW RECEIVER CUBE - On ground, should have shadow from cube above
    GameObject shadowReceiver;
    shadowReceiver.mesh = cubeMesh;
    shadowReceiver.material = terrainMaterial2;
    shadowReceiver.transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.5f, 0.0f));
    createDescriptorSet(shadowReceiver);
    gameObjects.push_back(shadowReceiver);
    std::cout << "[CUBE 2] Shadow receiver at (0, 0.5, 0) - Tests: Shadow receiving" << std::endl;

    // 4. SPECULAR TEST CUBES - Arranged to show specular highlights
    for (int i = 0; i < 3; i++) {
        GameObject specCube;
        specCube.mesh = cubeMesh;
        specCube.material = rockMaterial;
        float x = -3.0f + i * 3.0f;
        specCube.transform = glm::translate(glm::mat4(1.0f), glm::vec3(x, 0.5f, -3.0f));
        createDescriptorSet(specCube);
        gameObjects.push_back(specCube);
    }
    std::cout << "[CUBES 3-5] Specular test row at z=-3 - Tests: Specular highlights" << std::endl;

    // 5. ROTATED CUBES - Test normal mapping on angled surfaces
    GameObject rotatedCube1;
    rotatedCube1.mesh = cubeMesh;
    rotatedCube1.material = rockMaterial;
    glm::mat4 rot1 = glm::translate(glm::mat4(1.0f), glm::vec3(-4.0f, 0.5f, 0.0f));
    rot1 = glm::rotate(rot1, glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    rotatedCube1.transform = rot1;
    createDescriptorSet(rotatedCube1);
    gameObjects.push_back(rotatedCube1);
    std::cout << "[CUBE 6] 45-degree rotated at (-4, 0.5, 0) - Tests: Normal mapping on angles" << std::endl;

    GameObject rotatedCube2;
    rotatedCube2.mesh = cubeMesh;
    rotatedCube2.material = rockMaterial;
    glm::mat4 rot2 = glm::translate(glm::mat4(1.0f), glm::vec3(4.0f, 0.5f, 0.0f));
    rot2 = glm::rotate(rot2, glm::radians(30.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    rot2 = glm::rotate(rot2, glm::radians(30.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    rotatedCube2.transform = rot2;
    createDescriptorSet(rotatedCube2);
    gameObjects.push_back(rotatedCube2);
    std::cout << "[CUBE 7] Multi-axis rotated at (4, 0.5, 0) - Tests: Complex normal transforms" << std::endl;

    // 6. STACKED CUBES - Test self-shadowing
    for (int i = 0; i < 3; i++) {
        GameObject stackCube;
        stackCube.mesh = cubeMesh;
        stackCube.material = (i % 2 == 0) ? rockMaterial : terrainMaterial2;
        stackCube.transform = glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, 0.5f + i * 1.0f, 3.0f));
        createDescriptorSet(stackCube);
        gameObjects.push_back(stackCube);
    }
    std::cout << "[CUBES 8-10] Stacked tower at (3, y, 3) - Tests: Self-shadowing" << std::endl;

    // 7. LIGHT INDICATOR SPHERE - Shows where the light source direction points from
    auto sphereMesh = createSphereMesh(16, 12);
    
    // Create a bright yellow material for the light indicator
    auto lightMaterial = std::make_shared<Material>();
    lightMaterial->diffuse = terrainSpecular;  // Use specular map (white-ish)
    lightMaterial->specular = terrainSpecular;
    lightMaterial->normal = terrainNormal;
    lightMaterial->shininess = 1.0f;
    
    // Light direction is (1, 2, 1) normalized, position at 15 units out
    glm::vec3 lightDir = glm::normalize(glm::vec3(1.0f, 2.0f, 1.0f));
    glm::vec3 lightIndicatorPos = lightDir * 15.0f;  // Visible in scene
    
    GameObject lightIndicator;
    lightIndicator.mesh = sphereMesh;
    lightIndicator.material = lightMaterial;
    lightIndicator.transform = glm::scale(
        glm::translate(glm::mat4(1.0f), lightIndicatorPos),
        glm::vec3(1.0f)  // 1 unit radius
    );
    createDescriptorSet(lightIndicator);
    gameObjects.push_back(lightIndicator);
    std::cout << "[SPHERE] Light indicator at " << lightIndicatorPos.x << ", " 
              << lightIndicatorPos.y << ", " << lightIndicatorPos.z << " - Shows light direction" << std::endl;

    // 8. MIRROR CUBE - Tests: Perfect reflections (metallic=1, roughness=0)
    // Create a "mirror" material - we'll use a white diffuse and set shader to treat it as mirror
    // The shader will detect this by checking for very high specular (white texture)
    auto mirrorMaterial = std::make_shared<Material>();
    mirrorMaterial->diffuse = terrainSpecular;   // White-ish base
    mirrorMaterial->specular = terrainSpecular;  // High specular signals mirror
    mirrorMaterial->normal = terrainNormal;      // Flat normal
    mirrorMaterial->shininess = 1000.0f;         // Very high shininess = mirror flag
    
    // Create a flat plane for the mirror
    std::vector<Vertex> mirrorVerts = {
        {{-2.0f, 0.0f, -0.05f}, {1,1,1}, {0,0}, {0,0,1}},
        {{ 2.0f, 0.0f, -0.05f}, {1,1,1}, {1,0}, {0,0,1}},
        {{ 2.0f, 3.0f, -0.05f}, {1,1,1}, {1,1}, {0,0,1}},
        {{-2.0f, 3.0f, -0.05f}, {1,1,1}, {0,1}, {0,0,1}},
    };
    std::vector<uint32_t> mirrorInds = {0, 1, 2, 2, 3, 0};
    auto mirrorMesh = std::make_shared<Mesh>(physicalDevice, device, commandPool, graphicsQueue, mirrorVerts, mirrorInds);
    
    GameObject mirror;
    mirror.mesh = mirrorMesh;
    mirror.material = mirrorMaterial;
    // Position mirror standing upright behind the scene
    mirror.transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -8.0f));
    createDescriptorSet(mirror);
    gameObjects.push_back(mirror);
    std::cout << "[MIRROR] Reflective plane at (0, 0, -8) - Tests: IBL reflections" << std::endl;

    std::cout << "\n=== FEATURE VERIFICATION GUIDE ===" << std::endl;
    std::cout << "SHADOWS: Look for dark areas under/beside elevated cubes" << std::endl;
    std::cout << "NORMALS: Surface details visible on cubes/terrain" << std::endl;
    std::cout << "SPECULAR: Bright highlights when viewing at correct angle" << std::endl;
    std::cout << "SKYBOX: Background should show cubemap (currently placeholder)" << std::endl;
    std::cout << "DIFFUSE: Textures visible on all surfaces" << std::endl;
    std::cout << "MIRROR: Should reflect the skybox clearly" << std::endl;
    std::cout << "\nControls: WASD=move, Mouse=look, Shift=turbo, Space/Ctrl=up/down" << std::endl;
    std::cout << "=================================\n" << std::endl;

    std::cout << "GameObjects loaded: " << gameObjects.size() << " objects" << std::endl;
}

void Renderer::createSkyboxDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    uboLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 1;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.pImmutableSamplers = nullptr;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::array<VkDescriptorSetLayoutBinding, 2> bindings = {uboLayoutBinding, samplerLayoutBinding};
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &skyboxDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create skybox descriptor set layout!");
    }
}

void Renderer::createSkyboxGraphicsPipeline() {
    auto vertShaderCode = readFile("shaders/skybox.vert.spv");
    auto fragShaderCode = readFile("shaders/skybox.frag.spv");

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
    vertexInputInfo.vertexAttributeDescriptionCount = 1; // Only position needed for skybox really, but reusing Vertex struct
    vertexInputInfo.pVertexAttributeDescriptions = &attributeDescriptions[0]; // Position is at index 0

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)swapchainExtent.width;
    viewport.height = (float)swapchainExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchainExtent;

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
    rasterizer.cullMode = VK_CULL_MODE_NONE; // Draw inside of cube
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL; // Skybox at z=1.0
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &skyboxDescriptorSetLayout;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &skyboxPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create skybox pipeline layout!");
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
    pipelineInfo.layout = skyboxPipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &skyboxPipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create skybox graphics pipeline!");
    }

    vkDestroyShaderModule(device, fragShaderModule, nullptr);
    vkDestroyShaderModule(device, vertShaderModule, nullptr);
}

void Renderer::createShadowResources() {
    VkFormat depthFormat = findDepthFormat();
    // Shadow map resolution
    uint32_t shadowWidth = 2048;
    uint32_t shadowHeight = 2048;

    createImage(shadowWidth, shadowHeight, depthFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, shadowImage, shadowImageMemory);
    shadowImageView = createImageView(shadowImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);

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

    if (vkCreateSampler(device, &samplerInfo, nullptr, &shadowSampler) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shadow sampler!");
    }
}

void Renderer::createShadowRenderPass() {
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = findDepthFormat();
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

    // Dependencies
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

    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &shadowRenderPass) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shadow render pass!");
    }

    // Create Framebuffer
    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = shadowRenderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = &shadowImageView;
    framebufferInfo.width = 2048;
    framebufferInfo.height = 2048;
    framebufferInfo.layers = 1;

    if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &shadowFramebuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shadow framebuffer!");
    }
}

void Renderer::createShadowGraphicsPipeline() {
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
    vertexInputInfo.vertexAttributeDescriptionCount = 1; // Position
    vertexInputInfo.pVertexAttributeDescriptions = &attributeDescriptions[0];

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = 2048.0f;
    viewport.height = 2048.0f;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {2048, 2048};

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
    rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT; // Cull front faces to reduce shadow acne on edges
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;
    // Since we cull front faces (rendering back faces), the geometry itself acts as a bias.
    // We only need a tiny bit to handle grazing angles.
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
    colorBlending.attachmentCount = 0; // No color attachment

    // Push Constants
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstantData);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout; // Reusing main descriptor set layout for UBO access
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &shadowPipelineLayout) != VK_SUCCESS) {
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
    pipelineInfo.layout = shadowPipelineLayout;
    pipelineInfo.renderPass = shadowRenderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &shadowPipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shadow graphics pipeline!");
    }

    vkDestroyShaderModule(device, fragShaderModule, nullptr);
    vkDestroyShaderModule(device, vertShaderModule, nullptr);
}

// ==================== DEFERRED RENDERING IMPLEMENTATION ====================

void Renderer::createImageWithLayers(uint32_t width, uint32_t height, uint32_t layers, VkFormat format, 
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

    if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
        throw std::runtime_error("failed to create layered image!");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate layered image memory!");
    }

    vkBindImageMemory(device, image, imageMemory, 0);
}

void Renderer::createCSMResources() {
    VkFormat depthFormat = findDepthFormat();
    const uint32_t shadowSize = 2048;
    
    // Create 2D texture array for CSM (4 cascades)
    createImageWithLayers(shadowSize, shadowSize, CSM_CASCADE_COUNT, depthFormat,
                          VK_IMAGE_TILING_OPTIMAL,
                          VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                          shadowArrayImage, shadowArrayImageMemory);
    
    // Create array view for sampling all cascades in shader
    VkImageViewCreateInfo arrayViewInfo{};
    arrayViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    arrayViewInfo.image = shadowArrayImage;
    arrayViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    arrayViewInfo.format = depthFormat;
    arrayViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    arrayViewInfo.subresourceRange.baseMipLevel = 0;
    arrayViewInfo.subresourceRange.levelCount = 1;
    arrayViewInfo.subresourceRange.baseArrayLayer = 0;
    arrayViewInfo.subresourceRange.layerCount = CSM_CASCADE_COUNT;
    
    if (vkCreateImageView(device, &arrayViewInfo, nullptr, &shadowArrayImageView) != VK_SUCCESS) {
        throw std::runtime_error("failed to create CSM array image view!");
    }
    
    // Create individual views for each cascade (for framebuffer attachment)
    for (uint32_t i = 0; i < CSM_CASCADE_COUNT; i++) {
        VkImageViewCreateInfo cascadeViewInfo{};
        cascadeViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        cascadeViewInfo.image = shadowArrayImage;
        cascadeViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        cascadeViewInfo.format = depthFormat;
        cascadeViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        cascadeViewInfo.subresourceRange.baseMipLevel = 0;
        cascadeViewInfo.subresourceRange.levelCount = 1;
        cascadeViewInfo.subresourceRange.baseArrayLayer = i;
        cascadeViewInfo.subresourceRange.layerCount = 1;
        
        if (vkCreateImageView(device, &cascadeViewInfo, nullptr, &shadowCascadeViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create CSM cascade view!");
        }
        
        // Create framebuffer for this cascade
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = shadowRenderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = &shadowCascadeViews[i];
        framebufferInfo.width = shadowSize;
        framebufferInfo.height = shadowSize;
        framebufferInfo.layers = 1;
        
        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &shadowCascadeFramebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create CSM cascade framebuffer!");
        }
    }
    
    std::cout << "CSM Resources created: " << CSM_CASCADE_COUNT << " cascades at " << shadowSize << "x" << shadowSize << std::endl;
}

void Renderer::createGBufferAttachment(GBufferAttachment& attachment, VkFormat format, VkImageUsageFlags usage) {
    // Create image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = swapchainExtent.width;
    imageInfo.extent.height = swapchainExtent.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateImage(device, &imageInfo, nullptr, &attachment.image) != VK_SUCCESS) {
        throw std::runtime_error("failed to create G-Buffer attachment image!");
    }
    
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, attachment.image, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &attachment.memory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate G-Buffer attachment memory!");
    }
    
    vkBindImageMemory(device, attachment.image, attachment.memory, 0);
    
    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = attachment.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(device, &viewInfo, nullptr, &attachment.view) != VK_SUCCESS) {
        throw std::runtime_error("failed to create G-Buffer attachment view!");
    }
    
    attachment.format = format;
}

void Renderer::createGBufferResources() {
    // Position + View Depth (RGBA16F for high precision world-space positions)
    createGBufferAttachment(gBufferPosition, VK_FORMAT_R16G16B16A16_SFLOAT, 
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT);
    
    // Normal (RGBA16F for world-space normals with high precision)
    createGBufferAttachment(gBufferNormal, VK_FORMAT_R16G16B16A16_SFLOAT,
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT);
    
    // Albedo + Metallic (RGBA8 is sufficient for colors)
    createGBufferAttachment(gBufferAlbedo, VK_FORMAT_R8G8B8A8_UNORM,
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT);
    
    // Roughness + AO (RGBA8 for packed material properties)
    createGBufferAttachment(gBufferPBR, VK_FORMAT_R8G8B8A8_UNORM,
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT);
    
    std::cout << "G-Buffer resources created at " << swapchainExtent.width << "x" << swapchainExtent.height << std::endl;
}

void Renderer::createDeferredRenderPass() {
    std::array<VkAttachmentDescription, 6> attachments{};
    
    // 0: G-Buffer Position
    attachments[0].format = VK_FORMAT_R16G16B16A16_SFLOAT;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE; // Store for composition
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    
    // 1: G-Buffer Normal
    attachments[1].format = VK_FORMAT_R16G16B16A16_SFLOAT;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    
    // 2: G-Buffer Albedo
    attachments[2].format = VK_FORMAT_R8G8B8A8_UNORM;
    attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[2].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    
    // 3: G-Buffer PBR (Roughness + AO)
    attachments[3].format = VK_FORMAT_R8G8B8A8_UNORM;
    attachments[3].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[3].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[3].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[3].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[3].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[3].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[3].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    
    // 4: Depth
    attachments[4].format = findDepthFormat();
    attachments[4].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[4].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[4].storeOp = VK_ATTACHMENT_STORE_OP_STORE; // Keep for composition
    attachments[4].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[4].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[4].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[4].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    
    // 5: Swapchain (final output)
    attachments[5].format = swapchainImageFormat;
    attachments[5].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[5].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[5].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[5].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[5].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[5].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[5].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    
    // ==================== Subpass 0: Geometry Pass (G-Buffer) ====================
    std::array<VkAttachmentReference, 4> gBufferColorRefs{};
    gBufferColorRefs[0] = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}; // Position
    gBufferColorRefs[1] = {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}; // Normal
    gBufferColorRefs[2] = {2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}; // Albedo
    gBufferColorRefs[3] = {3, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}; // PBR
    
    VkAttachmentReference gBufferDepthRef = {4, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    
    VkSubpassDescription geometrySubpass{};
    geometrySubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    geometrySubpass.colorAttachmentCount = static_cast<uint32_t>(gBufferColorRefs.size());
    geometrySubpass.pColorAttachments = gBufferColorRefs.data();
    geometrySubpass.pDepthStencilAttachment = &gBufferDepthRef;
    
    // ==================== Subpass 1: Composition Pass (Lighting) ====================
    std::array<VkAttachmentReference, 4> inputAttachmentRefs{};
    inputAttachmentRefs[0] = {0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}; // Position
    inputAttachmentRefs[1] = {1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}; // Normal
    inputAttachmentRefs[2] = {2, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}; // Albedo
    inputAttachmentRefs[3] = {3, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}; // PBR
    
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
    
    // ==================== Subpass Dependencies ====================
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
    
    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &deferredRenderPass) != VK_SUCCESS) {
        throw std::runtime_error("failed to create deferred render pass!");
    }
    
    std::cout << "Deferred Render Pass created with 2 subpasses (Geometry + Composition)" << std::endl;
}

void Renderer::createDeferredFramebuffers() {
    deferredFramebuffers.resize(swapchainImages.size());
    
    for (size_t i = 0; i < swapchainImages.size(); i++) {
        std::array<VkImageView, 6> attachments = {
            gBufferPosition.view,
            gBufferNormal.view,
            gBufferAlbedo.view,
            gBufferPBR.view,
            depthImageView,
            swapchainImageViews[i]
        };
        
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = deferredRenderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = swapchainExtent.width;
        framebufferInfo.height = swapchainExtent.height;
        framebufferInfo.layers = 1;
        
        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &deferredFramebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create deferred framebuffer!");
        }
    }
    
    std::cout << "Deferred Framebuffers created: " << deferredFramebuffers.size() << std::endl;
}

void Renderer::createGBufferDescriptorSetLayout() {
    // Layout for G-Buffer pipeline (geometry pass) - similar to main but for MRT output
    std::array<VkDescriptorSetLayoutBinding, 4> bindings{};
    
    // UBO
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Diffuse texture
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Normal map
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Specular/PBR map
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &gBufferDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create G-Buffer descriptor set layout!");
    }
}

void Renderer::createCompositionDescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 7> bindings{};
    
    // Input attachments (G-Buffer)
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // UBO
    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // CSM shadow array
    bindings[5].binding = 5;
    bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Environment cubemap
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

void Renderer::createGBufferPipeline() {
    auto vertShaderCode = readFile("shaders/gbuffer.vert.spv");
    auto fragShaderCode = readFile("shaders/gbuffer.frag.spv");
    
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
    
    // Vertex input
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    
    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();
    
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();
    
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;
    
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapchainExtent.width);
    viewport.height = static_cast<float>(swapchainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchainExtent;
    
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
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    
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
    
    // MRT: 4 color attachments for G-Buffer
    std::array<VkPipelineColorBlendAttachmentState, 4> colorBlendAttachments{};
    for (auto& attachment : colorBlendAttachments) {
        attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | 
                                    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        attachment.blendEnable = VK_FALSE;
    }
    
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
    colorBlending.pAttachments = colorBlendAttachments.data();
    
    // Push constants for model matrix
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstantData);
    
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;  // Use main layout for compatibility with game objects
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    
    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &gBufferPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create G-Buffer pipeline layout!");
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
    pipelineInfo.layout = gBufferPipelineLayout;
    pipelineInfo.renderPass = deferredRenderPass;
    pipelineInfo.subpass = 0; // Geometry subpass
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &gBufferPipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create G-Buffer graphics pipeline!");
    }
    
    vkDestroyShaderModule(device, fragShaderModule, nullptr);
    vkDestroyShaderModule(device, vertShaderModule, nullptr);
    
    std::cout << "G-Buffer Pipeline created successfully!" << std::endl;
}

void Renderer::createCompositionPipeline() {
    auto vertShaderCode = readFile("shaders/composition.vert.spv");
    auto fragShaderCode = readFile("shaders/composition.frag.spv");
    
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
    
    // No vertex input for fullscreen triangle
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;
    
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;
    
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapchainExtent.width);
    viewport.height = static_cast<float>(swapchainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchainExtent;
    
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
    rasterizer.cullMode = VK_CULL_MODE_NONE; // Fullscreen triangle
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    
    // No depth write in composition - just read for skybox masking if needed
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;
    
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | 
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;
    
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &compositionDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    
    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &compositionPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create composition pipeline layout!");
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
    pipelineInfo.layout = compositionPipelineLayout;
    pipelineInfo.renderPass = deferredRenderPass;
    pipelineInfo.subpass = 1; // Composition subpass
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &compositionPipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create composition graphics pipeline!");
    }
    
    vkDestroyShaderModule(device, fragShaderModule, nullptr);
    vkDestroyShaderModule(device, vertShaderModule, nullptr);
    
    std::cout << "Composition Pipeline created successfully!" << std::endl;
}

void Renderer::createCompositionDescriptorSets() {
    // Allocate descriptor set for composition pass
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &compositionDescriptorSetLayout;
    
    if (vkAllocateDescriptorSets(device, &allocInfo, &compositionDescriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate composition descriptor set!");
    }
    
    // G-Buffer input attachments
    std::array<VkDescriptorImageInfo, 4> gBufferInfos{};
    gBufferInfos[0].imageView = gBufferPosition.view;
    gBufferInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    gBufferInfos[0].sampler = VK_NULL_HANDLE;
    
    gBufferInfos[1].imageView = gBufferNormal.view;
    gBufferInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    gBufferInfos[1].sampler = VK_NULL_HANDLE;
    
    gBufferInfos[2].imageView = gBufferAlbedo.view;
    gBufferInfos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    gBufferInfos[2].sampler = VK_NULL_HANDLE;
    
    gBufferInfos[3].imageView = gBufferPBR.view;
    gBufferInfos[3].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    gBufferInfos[3].sampler = VK_NULL_HANDLE;
    
    // UBO
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = uniformBuffer;
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(UniformBufferObject);
    
    // CSM shadow array
    VkDescriptorImageInfo shadowInfo{};
    shadowInfo.imageView = shadowArrayImageView;
    shadowInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    shadowInfo.sampler = shadowSampler;
    
    // Environment cubemap (from skybox)
    VkDescriptorImageInfo envInfo{};
    envInfo.imageView = skybox->getCubemapImageView();
    envInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    envInfo.sampler = skybox->getCubemapSampler();
    
    std::array<VkWriteDescriptorSet, 7> descriptorWrites{};
    
    // Input attachments
    for (uint32_t i = 0; i < 4; i++) {
        descriptorWrites[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[i].dstSet = compositionDescriptorSet;
        descriptorWrites[i].dstBinding = i;
        descriptorWrites[i].dstArrayElement = 0;
        descriptorWrites[i].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        descriptorWrites[i].descriptorCount = 1;
        descriptorWrites[i].pImageInfo = &gBufferInfos[i];
    }
    
    // UBO
    descriptorWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[4].dstSet = compositionDescriptorSet;
    descriptorWrites[4].dstBinding = 4;
    descriptorWrites[4].dstArrayElement = 0;
    descriptorWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[4].descriptorCount = 1;
    descriptorWrites[4].pBufferInfo = &bufferInfo;
    
    // CSM
    descriptorWrites[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[5].dstSet = compositionDescriptorSet;
    descriptorWrites[5].dstBinding = 5;
    descriptorWrites[5].dstArrayElement = 0;
    descriptorWrites[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[5].descriptorCount = 1;
    descriptorWrites[5].pImageInfo = &shadowInfo;
    
    // Environment
    descriptorWrites[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[6].dstSet = compositionDescriptorSet;
    descriptorWrites[6].dstBinding = 6;
    descriptorWrites[6].dstArrayElement = 0;
    descriptorWrites[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[6].descriptorCount = 1;
    descriptorWrites[6].pImageInfo = &envInfo;
    
    vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), 
                           descriptorWrites.data(), 0, nullptr);
    
    std::cout << "Composition Descriptor Set created" << std::endl;
}

