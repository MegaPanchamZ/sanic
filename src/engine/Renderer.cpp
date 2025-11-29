#include "Renderer.h"
#include "Vertex.h"
#include "Input.h"
#include <iostream>
#include <vector>
#include <fstream>
#include <chrono>
#include <cmath>
#include <limits>
#include <array>
#include <glm/gtc/matrix_transform.hpp>
#include "../external/stb_image.h"
#include "../external/tiny_obj_loader.h"
#include <unordered_map>
#include <set>
#include "DescriptorManager.h"

Renderer::Renderer(Window& window)
    : window(window)
    , camera(static_cast<float>(window.getWidth()) / static_cast<float>(window.getHeight()))
    , vulkanContext(window)
{
    // Initialize cached handles
    instance = vulkanContext.getInstance();
    physicalDevice = vulkanContext.getPhysicalDevice();
    device = vulkanContext.getDevice();
    surface = vulkanContext.getSurface();
    graphicsQueue = vulkanContext.getGraphicsQueue();
    presentQueue = vulkanContext.getPresentQueue();
    rayTracingSupported = vulkanContext.isRayTracingSupported();

    createSwapchain();
    createImageViews();
    
    // Forward render pass (needed for skybox after deferred)
    createRenderPass();
    
    // Descriptor layouts must be created before pipelines that use them
    createDescriptorSetLayout();
    createSkyboxDescriptorSetLayout();
    
    createCommandPool();
    createDepthResources(); // needed for skybox pass and forward passes
    
    createDescriptorPool(); // Needed for DeferredRenderer's composition sets

    // Initialize Renderers
    shadowRenderer = std::make_unique<ShadowRenderer>(vulkanContext, descriptorSetLayout);
    deferredRenderer = std::make_unique<DeferredRenderer>(
        vulkanContext, 
        swapchainExtent.width, swapchainExtent.height, 
        swapchainImageFormat, 
        descriptorSetLayout, 
        descriptorPool
    );
    deferredRenderer->createFramebuffers(swapchainImageViews, depthImageView);

    // Shadow pass setup (CSM) - managed by ShadowRenderer now
    // CSM resources managed by ShadowRenderer
    // Shadow pipeline managed by ShadowRenderer
    
    // Forward framebuffers (for skybox pass)
    createFramebuffers();

    // Pipelines
    createGraphicsPipeline(); // Keep basic graphics pipeline? It's unused in main loop now.
    
    createSkyboxGraphicsPipeline();

    createUniformBuffers();
    createCommandBuffers();
    
    skybox = std::make_unique<Skybox>(physicalDevice, device, commandPool, graphicsQueue);
    skybox->createDescriptorSet(descriptorPool, skyboxDescriptorSetLayout, uniformBuffer, sizeof(UniformBufferObject));
    
    // Update Deferred Renderer Composition Descriptor Set
    // We need to call this AFTER skybox and shadow renderer are ready
    deferredRenderer->updateCompositionDescriptorSet(
        uniformBuffer, sizeof(UniformBufferObject),
        shadowRenderer->getShadowImageView(), shadowRenderer->getShadowSampler(),
        skybox->getCubemapImageView(), skybox->getCubemapSampler()
    );

    createSyncObjects();
    
    // Initialize Global Descriptor Manager
    DescriptorManager::getInstance().init(device, physicalDevice);
    
    loadGameObjects();
    
    // Initialize Acceleration Structure Builder
    asBuilder = std::make_unique<AccelerationStructureBuilder>(device, physicalDevice, commandPool, graphicsQueue);
    buildAccelerationStructures();

    // RT Pipeline initialization  
    if (rayTracingSupported) {
        createRTDescriptorSetLayout();
        createRTOutputImage();
        rtPipeline = std::make_unique<RTPipeline>(device, physicalDevice, commandPool, graphicsQueue);
        rtPipeline->createPipeline(rtDescriptorSetLayout);
        createRTDescriptorSet();
        std::cout << "=== RT PIPELINE INITIALIZED ===" << std::endl;
        
        // Initialize DDGI System (requires ray tracing)
        ddgiSystem = std::make_unique<DDGISystem>(vulkanContext, descriptorPool);
        DDGIConfig ddgiConfig;
        ddgiConfig.probeCount = glm::ivec3(8, 4, 8);  // 256 probes
        ddgiConfig.probeSpacing = glm::vec3(4.0f, 3.0f, 4.0f);
        ddgiConfig.gridOrigin = glm::vec3(-16.0f, 0.0f, -16.0f);
        ddgiSystem->initialize(ddgiConfig);
        ddgiSystem->setAccelerationStructure(tlas.handle);
        std::cout << "=== DDGI SYSTEM INITIALIZED ===" << std::endl;
        
        // Initialize SSR System (Hybrid Screen-Space + Ray-Traced Reflections)
        ssrSystem = std::make_unique<SSRSystem>(
            vulkanContext, 
            swapchainExtent.width, 
            swapchainExtent.height,
            descriptorPool
        );
        ssrSystem->setTLAS(tlas.handle);
        SSRConfig ssrConfig;
        ssrConfig.maxDistance = 50.0f;
        ssrConfig.thickness = 0.5f;
        ssrConfig.maxSteps = 64.0f;
        ssrConfig.roughnessThreshold = 0.3f;
        ssrConfig.rtFallbackEnabled = true;
        ssrSystem->setConfig(ssrConfig);
        std::cout << "=== SSR SYSTEM INITIALIZED ===" << std::endl;
    }

    std::cout << "=== RAY TRACING ACCELERATION STRUCTURES BUILT ===" << std::endl;
    
    std::cout << "=== DEFERRED RENDERING ENABLED ===" << std::endl;
    std::cout << "G-Buffer: Position, Normal, Albedo, PBR" << std::endl;
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
    static uint32_t currentFrame = 0;
    currentFrame++;
    
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
        std::cout << "Drawing " << gameObjects.size() << " objects (" 
                  << ((rtPipeline && useRayTracing) ? "RAY TRACING" : "DEFERRED")
                  << (ddgiEnabled && ddgiSystem ? " + DDGI" : "")
                  << (ssrEnabled && ssrSystem ? " + SSR" : "") << ")." << std::endl;
    }

    vkResetCommandBuffer(commandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin recording command buffer!");
    }
    
    // Choose rendering path based on RT toggle
    if (rtPipeline && useRayTracing) {
        // ========================================================================
        // RAY TRACING PATH
        // ========================================================================
        
        // Transition RT output image to GENERAL layout for ray tracing
        VkImageMemoryBarrier rtBarrier{};
        rtBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        rtBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        rtBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        rtBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        rtBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        rtBarrier.image = rtOutputImage;
        rtBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        rtBarrier.subresourceRange.baseMipLevel = 0;
        rtBarrier.subresourceRange.levelCount = 1;
        rtBarrier.subresourceRange.baseArrayLayer = 0;
        rtBarrier.subresourceRange.layerCount = 1;
        rtBarrier.srcAccessMask = 0;
        rtBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
            0, 0, nullptr, 0, nullptr, 1, &rtBarrier);

        // Dispatch ray tracing
        dispatchRayTracing(commandBuffer);

        // Transition RT output to TRANSFER_SRC for blitting
        rtBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        rtBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        rtBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        rtBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &rtBarrier);

        // Transition swapchain image to TRANSFER_DST
        VkImageMemoryBarrier swapBarrier{};
        swapBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        swapBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        swapBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        swapBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        swapBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        swapBarrier.image = swapchainImages[imageIndex];
        swapBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        swapBarrier.subresourceRange.baseMipLevel = 0;
        swapBarrier.subresourceRange.levelCount = 1;
        swapBarrier.subresourceRange.baseArrayLayer = 0;
        swapBarrier.subresourceRange.layerCount = 1;
        swapBarrier.srcAccessMask = 0;
        swapBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &swapBarrier);

        // Blit RT output to swapchain
        VkImageBlit blitRegion{};
        blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blitRegion.srcSubresource.mipLevel = 0;
        blitRegion.srcSubresource.baseArrayLayer = 0;
        blitRegion.srcSubresource.layerCount = 1;
        blitRegion.srcOffsets[0] = {0, 0, 0};
        blitRegion.srcOffsets[1] = {static_cast<int32_t>(swapchainExtent.width), static_cast<int32_t>(swapchainExtent.height), 1};
        blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blitRegion.dstSubresource.mipLevel = 0;
        blitRegion.dstSubresource.baseArrayLayer = 0;
        blitRegion.dstSubresource.layerCount = 1;
        blitRegion.dstOffsets[0] = {0, 0, 0};
        blitRegion.dstOffsets[1] = {static_cast<int32_t>(swapchainExtent.width), static_cast<int32_t>(swapchainExtent.height), 1};

        vkCmdBlitImage(commandBuffer,
            rtOutputImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            swapchainImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blitRegion, VK_FILTER_NEAREST);

        // Transition swapchain back to PRESENT_SRC
        swapBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        swapBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        swapBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        swapBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;

        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0, 0, nullptr, 0, nullptr, 1, &swapBarrier);
            
    } else {
        // ========================================================================
        // DEFERRED RENDERING PATH
        // ========================================================================
        
        // PASS 0: Update DDGI probes (ray trace from probes, update irradiance)
        if (ddgiSystem && ddgiEnabled) {
            ddgiSystem->update(commandBuffer, currentFrame);
        }
        
        // PASS 1: Cascaded Shadow Maps (via ShadowRenderer)
        shadowRenderer->render(commandBuffer, gameObjects);
        
        // PASS 1.5: SSR (Screen-Space Reflections) - needs G-Buffer from previous frame
        // SSR runs before composition to provide reflection data
        if (ssrSystem && ssrEnabled) {
            ssrSystem->update(commandBuffer,
                              camera.getViewMatrix(),
                              camera.getProjectionMatrix(),
                              camera.getPosition(),
                              deferredRenderer->getPositionImageView(),
                              deferredRenderer->getNormalImageView(),
                              deferredRenderer->getAlbedoImageView(),
                              deferredRenderer->getPBRImageView(),
                              deferredRenderer->getDepthImageView(),
                              deferredRenderer->getSceneColorImageView(),
                              deferredRenderer->getGBufferSampler());
        }

        // PASS 2: Deferred Rendering (G-Buffer + Composition) via DeferredRenderer
        // DeferredRenderer outputs directly to swapchain (attachment 5) with PRESENT_SRC layout
        // Composition shader now samples DDGI for indirect diffuse lighting
        deferredRenderer->render(commandBuffer, imageIndex, gameObjects);
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
    return vulkanContext.findMemoryType(typeFilter, properties);
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
    return vulkanContext.findDepthFormat();
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
    std::array<VkDescriptorPoolSize, 6> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = static_cast<uint32_t>(100);
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = static_cast<uint32_t>(700); // Increased for RT textures
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    poolSizes[2].descriptorCount = static_cast<uint32_t>(10);  // For G-Buffer input attachments
    poolSizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[3].descriptorCount = static_cast<uint32_t>(10);  // For RT geometry info
    poolSizes[4].type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    poolSizes[4].descriptorCount = static_cast<uint32_t>(5);   // For TLAS
    poolSizes[5].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[5].descriptorCount = static_cast<uint32_t>(5);   // For RT output

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 120; // Increased for RT descriptor sets

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
    shadowInfo.imageView = shadowRenderer->getShadowImageView();
    shadowInfo.sampler = shadowRenderer->getShadowSampler();

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
    
    // Toggle RT rendering with R key
    static bool rKeyWasPressed = false;
    bool rKeyPressed = input.isKeyDown(GLFW_KEY_R);
    if (rKeyPressed && !rKeyWasPressed) {
        useRayTracing = !useRayTracing;
        std::cout << "Rendering Mode: " << (useRayTracing ? "RAY TRACING" : "DEFERRED") << std::endl;
    }
    rKeyWasPressed = rKeyPressed;
    
    // Toggle DDGI with G key
    static bool gKeyWasPressed = false;
    bool gKeyPressed = input.isKeyDown(GLFW_KEY_G);
    if (gKeyPressed && !gKeyWasPressed) {
        ddgiEnabled = !ddgiEnabled;
        std::cout << "DDGI: " << (ddgiEnabled ? "ENABLED" : "DISABLED") << std::endl;
    }
    gKeyWasPressed = gKeyPressed;
    
    // Toggle SSR with F key
    static bool fKeyWasPressed = false;
    bool fKeyPressed = input.isKeyDown(GLFW_KEY_F);
    if (fKeyPressed && !fKeyWasPressed) {
        ssrEnabled = !ssrEnabled;
        std::cout << "SSR: " << (ssrEnabled ? "ENABLED" : "DISABLED") << std::endl;
    }
    fKeyWasPressed = fKeyPressed;
    
    // ESC to close window
    if (input.isKeyDown(GLFW_KEY_ESCAPE)) {
        glfwSetWindowShouldClose(window.getHandle(), GLFW_TRUE);
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
    
    // Get shadow data from ShadowRenderer
    ShadowUBOData shadowData = shadowRenderer->computeShadowData(camera, lightDir, swapchainExtent.width, swapchainExtent.height);
    
    // Fill UBO with shadow data
    ubo.cascadeSplits = shadowData.cascadeSplits;
    ubo.shadowParams = shadowData.shadowParams;
    ubo.lightSpaceMatrix = shadowData.lightSpaceMatrix;
    for(int i = 0; i < 4; i++) {
        ubo.cascadeViewProj[i] = shadowData.cascadeViewProj[i];
    }

    memcpy(uniformBufferMapped, &ubo, sizeof(ubo));
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

    // Push Constants - still used by standard pipeline (non-mesh)
    // Size matches what the vertex shader expects (model matrix + normal matrix + mesh shader addresses)
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = 2 * sizeof(glm::mat4) + 4 * sizeof(uint64_t) + sizeof(uint32_t); // 128 + 32 + 4 = 164 bytes, aligned to 176

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

// Note: Renderer still has createMeshPipeline but it's not used in drawFrame anymore (DeferredRenderer handles it).
// Keeping it or removing it? The 'meshPipeline' member variable was removed from Renderer header, so we should remove this function implementation too.
// Waiting to see if I missed removing it from header... Yes I removed it from header. So I should remove implementation.
// I will comment it out or remove it. Removing it is cleaner.

// void Renderer::createMeshPipeline() { ... } // REMOVED

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

// RT Integration
void Renderer::createRTDescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 5> bindings{};
    
    // Binding 0: TLAS
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    
    // Binding 1: Output Image
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    
    // Binding 2: UBO
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    
    // Binding 3: Geometry Info Buffer (vertex/index addresses per instance)
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    
    // Binding 4: Texture Array (bindless textures)
    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[4].descriptorCount = RT_MAX_TEXTURES;
    bindings[4].stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    
    // Enable partially bound descriptors for texture array
    VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{};
    bindingFlagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    std::array<VkDescriptorBindingFlags, 5> bindingFlags = {
        0, 0, 0, 0,
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT  // For texture array
    };
    bindingFlagsInfo.bindingCount = static_cast<uint32_t>(bindingFlags.size());
    bindingFlagsInfo.pBindingFlags = bindingFlags.data();
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.pNext = &bindingFlagsInfo;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &rtDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create RT descriptor set layout!");
    }
    std::cout << "RT Descriptor Set Layout created (with textures)" << std::endl;
}

void Renderer::createRTOutputImage() {
    createImage(swapchainExtent.width, swapchainExtent.height, VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_TILING_OPTIMAL, 
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                rtOutputImage, rtOutputImageMemory);
    
    rtOutputImageView = createImageView(rtOutputImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);
    std::cout << "RT Output Image created" << std::endl;
}

void Renderer::createRTDescriptorSet() {
    // First, create the geometry info buffer
    VkDeviceSize geomBufferSize = sizeof(RTGeometryInfo) * gameObjects.size();
    createBuffer(geomBufferSize,
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 rtGeometryInfoBuffer, rtGeometryInfoBufferMemory);
    
    // Fill geometry info buffer
    std::vector<RTGeometryInfo> geomInfos(gameObjects.size());
    for (size_t i = 0; i < gameObjects.size(); i++) {
        geomInfos[i].vertexBufferAddress = gameObjects[i].mesh->getVertexBufferAddress();
        geomInfos[i].indexBufferAddress = gameObjects[i].mesh->getIndexBufferAddress();
        geomInfos[i].textureIndex = static_cast<uint32_t>(i % RT_MAX_TEXTURES);  // Map to texture slot
        geomInfos[i].padding = 0;
    }
    
    void* data;
    vkMapMemory(device, rtGeometryInfoBufferMemory, 0, geomBufferSize, 0, &data);
    memcpy(data, geomInfos.data(), geomBufferSize);
    vkUnmapMemory(device, rtGeometryInfoBufferMemory);
    
    std::cout << "RT Geometry Info Buffer created for " << gameObjects.size() << " objects" << std::endl;
    
    // Allocate descriptor set
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &rtDescriptorSetLayout;
    
    if (vkAllocateDescriptorSets(device, &allocInfo, &rtDescriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate RT descriptor set!");
    }
    
    // Prepare writes
    std::vector<VkWriteDescriptorSet> writes;
    
    // TLAS
    VkWriteDescriptorSetAccelerationStructureKHR asInfo{};
    asInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    asInfo.accelerationStructureCount = 1;
    asInfo.pAccelerationStructures = &tlas.handle;
    
    VkWriteDescriptorSet tlasWrite{};
    tlasWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    tlasWrite.pNext = &asInfo;
    tlasWrite.dstSet = rtDescriptorSet;
    tlasWrite.dstBinding = 0;
    tlasWrite.descriptorCount = 1;
    tlasWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    writes.push_back(tlasWrite);
    
    // Output Image
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageView = rtOutputImageView;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    
    VkWriteDescriptorSet imageWrite{};
    imageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    imageWrite.dstSet = rtDescriptorSet;
    imageWrite.dstBinding = 1;
    imageWrite.descriptorCount = 1;
    imageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    imageWrite.pImageInfo = &imageInfo;
    writes.push_back(imageWrite);
    
    // UBO
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = uniformBuffer;
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(UniformBufferObject);
    
    VkWriteDescriptorSet uboWrite{};
    uboWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    uboWrite.dstSet = rtDescriptorSet;
    uboWrite.dstBinding = 2;
    uboWrite.descriptorCount = 1;
    uboWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboWrite.pBufferInfo = &bufferInfo;
    writes.push_back(uboWrite);
    
    // Geometry Info Buffer
    VkDescriptorBufferInfo geomBufferInfo{};
    geomBufferInfo.buffer = rtGeometryInfoBuffer;
    geomBufferInfo.offset = 0;
    geomBufferInfo.range = geomBufferSize;
    
    VkWriteDescriptorSet geomWrite{};
    geomWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    geomWrite.dstSet = rtDescriptorSet;
    geomWrite.dstBinding = 3;
    geomWrite.descriptorCount = 1;
    geomWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    geomWrite.pBufferInfo = &geomBufferInfo;
    writes.push_back(geomWrite);
    
    // Texture Array - gather unique textures from game objects
    std::vector<VkDescriptorImageInfo> textureInfos(RT_MAX_TEXTURES);
    for (uint32_t i = 0; i < RT_MAX_TEXTURES; i++) {
        if (i < gameObjects.size() && gameObjects[i].material && gameObjects[i].material->diffuse) {
            textureInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            textureInfos[i].imageView = gameObjects[i].material->diffuse->getImageView();
            textureInfos[i].sampler = gameObjects[i].material->diffuse->getSampler();
        } else if (gameObjects.size() > 0 && gameObjects[0].material && gameObjects[0].material->diffuse) {
            // Use first texture as fallback
            textureInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            textureInfos[i].imageView = gameObjects[0].material->diffuse->getImageView();
            textureInfos[i].sampler = gameObjects[0].material->diffuse->getSampler();
        }
    }
    
    VkWriteDescriptorSet textureWrite{};
    textureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    textureWrite.dstSet = rtDescriptorSet;
    textureWrite.dstBinding = 4;
    textureWrite.descriptorCount = RT_MAX_TEXTURES;
    textureWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    textureWrite.pImageInfo = textureInfos.data();
    writes.push_back(textureWrite);
    
    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    std::cout << "RT Descriptor Set created with textures and geometry info" << std::endl;
}

void Renderer::dispatchRayTracing(VkCommandBuffer commandBuffer) {
    if (!rtPipeline) return; 
    
    rtPipeline->trace(commandBuffer, rtDescriptorSet, swapchainExtent.width, swapchainExtent.height);
}

Renderer::~Renderer() {
    vkDeviceWaitIdle(device);
    
    // Cleanup RT resources
    if (rtOutputImage != VK_NULL_HANDLE) {
        vkDestroyImageView(device, rtOutputImageView, nullptr);
        vkDestroyImage(device, rtOutputImage, nullptr);
        vkFreeMemory(device, rtOutputImageMemory, nullptr);
    }
    if (rtGeometryInfoBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, rtGeometryInfoBuffer, nullptr);
        vkFreeMemory(device, rtGeometryInfoBufferMemory, nullptr);
    }
    if (rtDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, rtDescriptorSetLayout, nullptr);
    }
    
    rtPipeline.reset();
    asBuilder.reset();
    ssrSystem.reset();   // Cleanup SSR before DDGI
    ddgiSystem.reset();  // Cleanup DDGI before shadow/deferred renderers
    shadowRenderer.reset();
    deferredRenderer.reset();
    
    // Cleanup skybox
    if (skyboxPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, skyboxPipeline, nullptr);
    }
    if (skyboxPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, skyboxPipelineLayout, nullptr);
    }
    if (skyboxDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, skyboxDescriptorSetLayout, nullptr);
    }
    skybox.reset();
    
    // Cleanup sync objects
    vkDestroySemaphore(device, imageAvailableSemaphore, nullptr);
    vkDestroySemaphore(device, renderFinishedSemaphore, nullptr);
    vkDestroyFence(device, inFlightFence, nullptr);
    
    // Cleanup command pool
    vkDestroyCommandPool(device, commandPool, nullptr);
    
    // Cleanup framebuffers
    for (auto framebuffer : swapchainFramebuffers) {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }
    
    // Cleanup pipeline
    vkDestroyPipeline(device, graphicsPipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyRenderPass(device, renderPass, nullptr);
    
    // Cleanup depth
    vkDestroyImageView(device, depthImageView, nullptr);
    vkDestroyImage(device, depthImage, nullptr);
    vkFreeMemory(device, depthImageMemory, nullptr);
    
    // Cleanup swapchain views
    for (auto imageView : swapchainImageViews) {
        vkDestroyImageView(device, imageView, nullptr);
    }
    
    // Cleanup swapchain
    vkDestroySwapchainKHR(device, swapchain, nullptr);
    
    // Cleanup descriptors
    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
    
    // Cleanup uniform buffer
    vkDestroyBuffer(device, uniformBuffer, nullptr);
    vkFreeMemory(device, uniformBufferMemory, nullptr);
    
    // GameObjects cleanup themselves
    gameObjects.clear();
}

void Renderer::createSwapchain() {
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);
    
    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());
    
    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, presentModes.data());
    
    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(presentModes);
    VkExtent2D extent = chooseSwapExtent(capabilities);
    
    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }
    
    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = capabilities.currentTransform;
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
}

void Renderer::createRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapchainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = findDepthFormat();
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
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
}

void Renderer::createCommandPool() {
    QueueFamilyIndices queueFamilyIndices = vulkanContext.findQueueFamilies(physicalDevice);
    
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = static_cast<uint32_t>(queueFamilyIndices.graphicsFamily);
    
    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create command pool!");
    }
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
}

void Renderer::buildAccelerationStructures() {
    if (!rayTracingSupported || gameObjects.empty()) return;
    
    asBuilder = std::make_unique<AccelerationStructureBuilder>(device, physicalDevice, commandPool, graphicsQueue);
    
    // Collect meshes from game objects
    std::vector<std::shared_ptr<Mesh>> meshes;
    for (auto& go : gameObjects) {
        if (go.mesh) {
            meshes.push_back(go.mesh);
        }
    }
    
    // Build BLAS for all meshes
    blasList = asBuilder->buildBLAS(meshes);
    
    // Build TLAS using game objects and BLAS list
    tlas = asBuilder->buildTLAS(gameObjects, blasList);
    
    std::cout << "Acceleration structures built: " << blasList.size() << " BLAS, 1 TLAS" << std::endl;
}

std::vector<char> Renderer::readFile(const std::string& filename) {
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

VkSurfaceFormatKHR Renderer::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
    for (const auto& availableFormat : availableFormats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && 
            availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
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
        int width = window.getWidth();
        int height = window.getHeight();
        
        VkExtent2D actualExtent = {
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height)
        };
        
        actualExtent.width = std::max(capabilities.minImageExtent.width, 
                                       std::min(capabilities.maxImageExtent.width, actualExtent.width));
        actualExtent.height = std::max(capabilities.minImageExtent.height, 
                                        std::min(capabilities.maxImageExtent.height, actualExtent.height));
        
        return actualExtent;
    }
}