#include "SSRDenoiser.h"
#include "ShaderManager.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cstring>

SSRDenoiser::SSRDenoiser(VulkanContext& context, uint32_t width, uint32_t height, VkDescriptorPool descriptorPool)
    : context(context), width(width), height(height), descriptorPool(descriptorPool)
{
    createImages();
    createTemporalPipeline();
    createSpatialPipeline();
    allocateDescriptorSets();
    
    std::cout << "SSR Denoiser initialized: " << width << "x" << height << std::endl;
}

SSRDenoiser::~SSRDenoiser() {
    destroyResources();
}

void SSRDenoiser::destroyResources() {
    VkDevice device = context.getDevice();
    vkDeviceWaitIdle(device);
    
    // Temporal pipeline
    if (temporalPipeline) vkDestroyPipeline(device, temporalPipeline, nullptr);
    if (temporalPipelineLayout) vkDestroyPipelineLayout(device, temporalPipelineLayout, nullptr);
    if (temporalSetLayout) vkDestroyDescriptorSetLayout(device, temporalSetLayout, nullptr);
    
    // Spatial pipeline
    if (spatialPipeline) vkDestroyPipeline(device, spatialPipeline, nullptr);
    if (spatialPipelineLayout) vkDestroyPipelineLayout(device, spatialPipelineLayout, nullptr);
    if (spatialSetLayout) vkDestroyDescriptorSetLayout(device, spatialSetLayout, nullptr);
    
    // Images
    for (int i = 0; i < 2; i++) {
        if (pingPongViews[i]) vkDestroyImageView(device, pingPongViews[i], nullptr);
        if (pingPongImages[i]) vkDestroyImage(device, pingPongImages[i], nullptr);
        if (pingPongMemory[i]) vkFreeMemory(device, pingPongMemory[i], nullptr);
    }
    
    if (historyView) vkDestroyImageView(device, historyView, nullptr);
    if (historyImage) vkDestroyImage(device, historyImage, nullptr);
    if (historyMemory) vkFreeMemory(device, historyMemory, nullptr);
}

void SSRDenoiser::createImages() {
    VkDevice device = context.getDevice();
    
    auto createImage = [&](VkImage& image, VkDeviceMemory& memory, VkImageView& view) {
        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        imageInfo.extent = {width, height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        
        if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create SSR denoiser image");
        }
        
        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(device, image, &memReqs);
        
        VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = context.findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        
        if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate SSR denoiser memory");
        }
        
        vkBindImageMemory(device, image, memory, 0);
        
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        
        if (vkCreateImageView(device, &viewInfo, nullptr, &view) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create SSR denoiser image view");
        }
    };
    
    // Create ping-pong buffers
    for (int i = 0; i < 2; i++) {
        createImage(pingPongImages[i], pingPongMemory[i], pingPongViews[i]);
    }
    
    // Create history buffer
    createImage(historyImage, historyMemory, historyView);
}

void SSRDenoiser::createTemporalPipeline() {
    VkDevice device = context.getDevice();
    
    // Descriptor set layout
    VkDescriptorSetLayoutBinding bindings[5] = {};
    
    bindings[0].binding = 0;  // Current reflection
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    bindings[1].binding = 1;  // History reflection
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    bindings[2].binding = 2;  // Velocity buffer
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    bindings[3].binding = 3;  // Depth
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    bindings[4].binding = 4;  // Output
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 5;
    layoutInfo.pBindings = bindings;
    
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &temporalSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create temporal descriptor set layout");
    }
    
    // Pipeline layout
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(TemporalPushConstants);
    
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &temporalSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    
    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &temporalPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create temporal pipeline layout");
    }
    
    // Create pipeline
    VkShaderModule shaderModule = ShaderManager::loadShader("shaders/ssr_temporal.comp");
    
    VkPipelineShaderStageCreateInfo stageInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaderModule;
    stageInfo.pName = "main";
    
    VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = temporalPipelineLayout;
    
    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &temporalPipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create temporal compute pipeline");
    }
}

void SSRDenoiser::createSpatialPipeline() {
    VkDevice device = context.getDevice();
    
    // Descriptor set layout
    VkDescriptorSetLayoutBinding bindings[4] = {};
    
    bindings[0].binding = 0;  // Input reflection
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    bindings[1].binding = 1;  // Normal
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    bindings[2].binding = 2;  // Depth
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    bindings[3].binding = 3;  // Output
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 4;
    layoutInfo.pBindings = bindings;
    
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &spatialSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create spatial descriptor set layout");
    }
    
    // Pipeline layout
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(SpatialPushConstants);
    
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &spatialSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    
    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &spatialPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create spatial pipeline layout");
    }
    
    // Create pipeline
    VkShaderModule shaderModule = ShaderManager::loadShader("shaders/ssr_denoise.comp");
    
    VkPipelineShaderStageCreateInfo stageInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaderModule;
    stageInfo.pName = "main";
    
    VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = spatialPipelineLayout;
    
    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &spatialPipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create spatial compute pipeline");
    }
}

void SSRDenoiser::allocateDescriptorSets() {
    VkDevice device = context.getDevice();
    
    // Allocate temporal descriptor set
    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &temporalSetLayout;
    
    if (vkAllocateDescriptorSets(device, &allocInfo, &temporalDescriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate temporal descriptor set");
    }
    
    // Allocate spatial descriptor sets (ping-pong)
    VkDescriptorSetLayout layouts[2] = {spatialSetLayout, spatialSetLayout};
    allocInfo.descriptorSetCount = 2;
    allocInfo.pSetLayouts = layouts;
    
    if (vkAllocateDescriptorSets(device, &allocInfo, spatialDescriptorSets) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate spatial descriptor sets");
    }
}

void SSRDenoiser::denoise(VkCommandBuffer cmd,
                          VkImageView inputReflection,
                          VkImageView velocityView,
                          VkImageView normalView,
                          VkImageView depthView,
                          VkSampler sampler)
{
    VkDevice device = context.getDevice();
    
    // Transition ping-pong images to GENERAL
    VkImageMemoryBarrier barriers[3] = {};
    for (int i = 0; i < 2; i++) {
        barriers[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barriers[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barriers[i].newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[i].image = pingPongImages[i];
        barriers[i].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barriers[i].subresourceRange.baseMipLevel = 0;
        barriers[i].subresourceRange.levelCount = 1;
        barriers[i].subresourceRange.baseArrayLayer = 0;
        barriers[i].subresourceRange.layerCount = 1;
        barriers[i].srcAccessMask = 0;
        barriers[i].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    }
    barriers[2] = barriers[0];
    barriers[2].image = historyImage;
    
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 3, barriers);
    
    int currentBuffer = 0;
    
    // ================== TEMPORAL PASS ==================
    if (config.enableTemporal && !firstFrame) {
        // Update temporal descriptor set
        VkDescriptorImageInfo imageInfos[5] = {};
        imageInfos[0] = {sampler, inputReflection, VK_IMAGE_LAYOUT_GENERAL};
        imageInfos[1] = {sampler, historyView, VK_IMAGE_LAYOUT_GENERAL};
        imageInfos[2] = {sampler, velocityView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        imageInfos[3] = {sampler, depthView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        imageInfos[4] = {VK_NULL_HANDLE, pingPongViews[currentBuffer], VK_IMAGE_LAYOUT_GENERAL};
        
        VkWriteDescriptorSet writes[5] = {};
        for (int i = 0; i < 5; i++) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = temporalDescriptorSet;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = (i < 4) ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[i].pImageInfo = &imageInfos[i];
        }
        
        vkUpdateDescriptorSets(device, 5, writes, 0, nullptr);
        
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, temporalPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, temporalPipelineLayout, 0, 1, &temporalDescriptorSet, 0, nullptr);
        
        TemporalPushConstants tpc{};
        tpc.width = static_cast<int>(width);
        tpc.height = static_cast<int>(height);
        tpc.blendFactor = config.temporalBlend;
        tpc.velocityScale = 1.0f;
        
        vkCmdPushConstants(cmd, temporalPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(tpc), &tpc);
        
        uint32_t groupCountX = (width + 7) / 8;
        uint32_t groupCountY = (height + 7) / 8;
        vkCmdDispatch(cmd, groupCountX, groupCountY, 1);
        
        // Barrier between passes
        VkImageMemoryBarrier passBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        passBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        passBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        passBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        passBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        passBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        passBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        passBarrier.image = pingPongImages[currentBuffer];
        passBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        passBarrier.subresourceRange.baseMipLevel = 0;
        passBarrier.subresourceRange.levelCount = 1;
        passBarrier.subresourceRange.baseArrayLayer = 0;
        passBarrier.subresourceRange.layerCount = 1;
        
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &passBarrier);
    } else {
        // No temporal, just copy input to ping-pong buffer
        // (In a real implementation, you'd blit or copy here)
        // For now, we'll use the input directly in spatial passes
        firstFrame = false;
    }
    
    // ================== SPATIAL PASSES ==================
    if (config.enableSpatial) {
        VkImageView currentInput = config.enableTemporal ? pingPongViews[currentBuffer] : inputReflection;
        
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, spatialPipeline);
        
        for (int pass = 0; pass < config.spatialPasses; pass++) {
            int outputBuffer = 1 - currentBuffer;
            
            // Update spatial descriptor set
            VkDescriptorImageInfo imageInfos[4] = {};
            imageInfos[0] = {sampler, currentInput, VK_IMAGE_LAYOUT_GENERAL};
            imageInfos[1] = {sampler, normalView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            imageInfos[2] = {sampler, depthView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            imageInfos[3] = {VK_NULL_HANDLE, pingPongViews[outputBuffer], VK_IMAGE_LAYOUT_GENERAL};
            
            VkWriteDescriptorSet writes[4] = {};
            for (int i = 0; i < 4; i++) {
                writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[i].dstSet = spatialDescriptorSets[pass % 2];
                writes[i].dstBinding = i;
                writes[i].descriptorCount = 1;
                writes[i].descriptorType = (i < 3) ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                writes[i].pImageInfo = &imageInfos[i];
            }
            
            vkUpdateDescriptorSets(device, 4, writes, 0, nullptr);
            
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, spatialPipelineLayout, 0, 1, &spatialDescriptorSets[pass % 2], 0, nullptr);
            
            SpatialPushConstants spc{};
            spc.passIndex = pass;
            spc.width = static_cast<int>(width);
            spc.height = static_cast<int>(height);
            spc.sigmaLuminance = config.sigmaLuminance;
            spc.sigmaNormal = config.sigmaNormal;
            spc.sigmaDepth = config.sigmaDepth;
            
            vkCmdPushConstants(cmd, spatialPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(spc), &spc);
            
            uint32_t groupCountX = (width + 7) / 8;
            uint32_t groupCountY = (height + 7) / 8;
            vkCmdDispatch(cmd, groupCountX, groupCountY, 1);
            
            // Barrier between passes
            if (pass < config.spatialPasses - 1) {
                VkImageMemoryBarrier passBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                passBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                passBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                passBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
                passBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
                passBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                passBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                passBarrier.image = pingPongImages[outputBuffer];
                passBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                passBarrier.subresourceRange.baseMipLevel = 0;
                passBarrier.subresourceRange.levelCount = 1;
                passBarrier.subresourceRange.baseArrayLayer = 0;
                passBarrier.subresourceRange.layerCount = 1;
                
                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &passBarrier);
            }
            
            currentInput = pingPongViews[outputBuffer];
            currentBuffer = outputBuffer;
        }
    }
    
    // Copy result to history for next frame
    swapHistory();
}

void SSRDenoiser::swapHistory() {
    // The output is now in pingPongImages[currentBuffer]
    // We need to copy it to history for next frame
    // This would be done via vkCmdCopyImage or by just swapping pointers
    // For simplicity, we'll handle this in the next frame's temporal pass
}

void SSRDenoiser::resize(uint32_t newWidth, uint32_t newHeight) {
    width = newWidth;
    height = newHeight;
    
    VkDevice device = context.getDevice();
    vkDeviceWaitIdle(device);
    
    // Destroy old images
    for (int i = 0; i < 2; i++) {
        if (pingPongViews[i]) vkDestroyImageView(device, pingPongViews[i], nullptr);
        if (pingPongImages[i]) vkDestroyImage(device, pingPongImages[i], nullptr);
        if (pingPongMemory[i]) vkFreeMemory(device, pingPongMemory[i], nullptr);
    }
    
    if (historyView) vkDestroyImageView(device, historyView, nullptr);
    if (historyImage) vkDestroyImage(device, historyImage, nullptr);
    if (historyMemory) vkFreeMemory(device, historyMemory, nullptr);
    
    // Recreate
    createImages();
    firstFrame = true;
    
    std::cout << "SSR Denoiser resized: " << width << "x" << height << std::endl;
}

VkShaderModule SSRDenoiser::createShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    
    VkShaderModule shaderModule;
    if (vkCreateShaderModule(context.getDevice(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create denoiser shader module");
    }
    return shaderModule;
}

std::vector<char> SSRDenoiser::readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open denoiser shader file: " + filename);
    }
    
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
}
