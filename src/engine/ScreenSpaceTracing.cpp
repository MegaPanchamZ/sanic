/**
 * ScreenSpaceTracing.cpp
 * 
 * Implementation of screen-space ray tracing system.
 */

#include "ScreenSpaceTracing.h"
#include "VulkanContext.h"
#include <fstream>

ScreenSpaceTracing::~ScreenSpaceTracing() {
    cleanup();
}

bool ScreenSpaceTracing::initialize(VulkanContext* context, uint32_t width, uint32_t height) {
    if (initialized_) return true;
    
    context_ = context;
    width_ = width;
    height_ = height;
    
    if (!createImages()) { cleanup(); return false; }
    if (!createDescriptorSets()) { cleanup(); return false; }
    if (!createPipelines()) { cleanup(); return false; }
    
    initialized_ = true;
    return true;
}

void ScreenSpaceTracing::cleanup() {
    if (!context_) return;
    VkDevice device = context_->getDevice();
    
    // Pipelines
    if (ssrPipeline_) vkDestroyPipeline(device, ssrPipeline_, nullptr);
    if (ssrLayout_) vkDestroyPipelineLayout(device, ssrLayout_, nullptr);
    if (coneTracePipeline_) vkDestroyPipeline(device, coneTracePipeline_, nullptr);
    if (coneTraceLayout_) vkDestroyPipelineLayout(device, coneTraceLayout_, nullptr);
    if (temporalPipeline_) vkDestroyPipeline(device, temporalPipeline_, nullptr);
    if (temporalLayout_) vkDestroyPipelineLayout(device, temporalLayout_, nullptr);
    
    // Descriptors
    if (descPool_) vkDestroyDescriptorPool(device, descPool_, nullptr);
    if (ssrDescLayout_) vkDestroyDescriptorSetLayout(device, ssrDescLayout_, nullptr);
    if (coneTraceDescLayout_) vkDestroyDescriptorSetLayout(device, coneTraceDescLayout_, nullptr);
    
    // Samplers
    if (linearSampler_) vkDestroySampler(device, linearSampler_, nullptr);
    if (pointSampler_) vkDestroySampler(device, pointSampler_, nullptr);
    
    // Images
    auto destroyImage = [device](VkImage& img, VkImageView& view, VkDeviceMemory& mem) {
        if (view) vkDestroyImageView(device, view, nullptr);
        if (img) vkDestroyImage(device, img, nullptr);
        if (mem) vkFreeMemory(device, mem, nullptr);
        img = VK_NULL_HANDLE; view = VK_NULL_HANDLE; mem = VK_NULL_HANDLE;
    };
    
    destroyImage(reflectionImage_, reflectionView_, reflectionMemory_);
    destroyImage(hitBufferImage_, hitBufferView_, hitBufferMemory_);
    destroyImage(coneTraceImage_, coneTraceView_, coneTraceMemory_);
    destroyImage(historyImage_, historyView_, historyMemory_);
    
    initialized_ = false;
}

bool ScreenSpaceTracing::resize(uint32_t width, uint32_t height) {
    if (width == width_ && height == height_) return true;
    cleanup();
    return initialize(context_, width, height);
}

bool ScreenSpaceTracing::createImages() {
    VkDevice device = context_->getDevice();
    
    auto findMemoryType = [this](uint32_t typeFilter, VkMemoryPropertyFlags properties) -> uint32_t {
        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(context_->getPhysicalDevice(), &memProps);
        for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
            if ((typeFilter & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }
        return 0;
    };
    
    auto createImage = [&](VkFormat format, VkImage& image, VkImageView& view, VkDeviceMemory& memory) {
        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = format;
        imageInfo.extent = {width_, height_, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        
        if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) return false;
        
        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(device, image, &memReqs);
        
        VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        
        if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) return false;
        vkBindImageMemory(device, image, memory, 0);
        
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        
        return vkCreateImageView(device, &viewInfo, nullptr, &view) == VK_SUCCESS;
    };
    
    if (!createImage(VK_FORMAT_R16G16B16A16_SFLOAT, reflectionImage_, reflectionView_, reflectionMemory_)) return false;
    if (!createImage(VK_FORMAT_R16G16_SFLOAT, hitBufferImage_, hitBufferView_, hitBufferMemory_)) return false;
    if (!createImage(VK_FORMAT_R16G16B16A16_SFLOAT, coneTraceImage_, coneTraceView_, coneTraceMemory_)) return false;
    if (!createImage(VK_FORMAT_R16G16B16A16_SFLOAT, historyImage_, historyView_, historyMemory_)) return false;
    
    // Samplers
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    
    if (vkCreateSampler(device, &samplerInfo, nullptr, &linearSampler_) != VK_SUCCESS) return false;
    
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    
    return vkCreateSampler(device, &samplerInfo, nullptr, &pointSampler_) == VK_SUCCESS;
}

bool ScreenSpaceTracing::createDescriptorSets() {
    VkDevice device = context_->getDevice();
    
    // SSR descriptor layout
    VkDescriptorSetLayoutBinding bindings[7] = {};
    for (int i = 0; i < 5; i++) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    bindings[5].binding = 5;
    bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[6].binding = 6;
    bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[6].descriptorCount = 1;
    bindings[6].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 7;
    layoutInfo.pBindings = bindings;
    
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &ssrDescLayout_) != VK_SUCCESS) return false;
    
    // Pool
    VkDescriptorPoolSize poolSizes[2] = {};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = 10;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[1].descriptorCount = 4;
    
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 2;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descPool_) != VK_SUCCESS) return false;
    
    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = descPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &ssrDescLayout_;
    
    return vkAllocateDescriptorSets(device, &allocInfo, &ssrDescSet_) == VK_SUCCESS;
}

bool ScreenSpaceTracing::createPipelines() {
    VkDevice device = context_->getDevice();
    
    // SSR pipeline
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.size = 256;  // Generous size for push constants
    
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &ssrDescLayout_;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    
    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &ssrLayout_) != VK_SUCCESS) return false;
    
    VkShaderModule shaderModule;
    if (!loadShader("shaders/ssr_hierarchical.comp.spv", shaderModule)) return false;
    
    VkPipelineShaderStageCreateInfo stageInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaderModule;
    stageInfo.pName = "main";
    
    VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = ssrLayout_;
    
    VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &ssrPipeline_);
    vkDestroyShaderModule(device, shaderModule, nullptr);
    
    return result == VK_SUCCESS;
}

bool ScreenSpaceTracing::loadShader(const std::string& path, VkShaderModule& outModule) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;
    
    size_t size = file.tellg();
    std::vector<char> code(size);
    file.seekg(0);
    file.read(code.data(), size);
    
    VkShaderModuleCreateInfo createInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    createInfo.codeSize = size;
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    
    return vkCreateShaderModule(context_->getDevice(), &createInfo, nullptr, &outModule) == VK_SUCCESS;
}

void ScreenSpaceTracing::traceReflections(VkCommandBuffer cmd,
                                           VkImageView colorBuffer,
                                           VkImageView depthBuffer,
                                           VkImageView normalBuffer,
                                           VkImageView materialBuffer,
                                           VkImageView hizBuffer,
                                           const glm::mat4& viewProj,
                                           const glm::mat4& invViewProj,
                                           const glm::mat4& view,
                                           const SSRConfig& config) {
    // Dispatch hierarchical SSR
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, ssrPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, ssrLayout_, 0, 1, &ssrDescSet_, 0, nullptr);
    
    struct {
        glm::mat4 viewProj;
        glm::mat4 invViewProj;
        glm::mat4 view;
        glm::mat4 invView;
        glm::mat4 proj;
        glm::vec2 screenSize;
        glm::vec2 invScreenSize;
        float maxDistance;
        float thickness;
        float stride;
        float jitter;
        uint32_t maxSteps;
        uint32_t hizMipLevels;
        float roughnessThreshold;
        float fadeStart;
    } pc;
    
    pc.viewProj = viewProj;
    pc.invViewProj = invViewProj;
    pc.view = view;
    pc.invView = glm::inverse(view);
    pc.proj = viewProj * pc.invView;
    pc.screenSize = glm::vec2(width_, height_);
    pc.invScreenSize = 1.0f / pc.screenSize;
    pc.maxDistance = config.maxDistance;
    pc.thickness = config.thickness;
    pc.stride = config.stride;
    pc.jitter = 0.0f;
    pc.maxSteps = config.maxSteps;
    pc.hizMipLevels = 8;
    pc.roughnessThreshold = config.roughnessThreshold;
    pc.fadeStart = config.fadeStart;
    
    vkCmdPushConstants(cmd, ssrLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
    
    uint32_t groupsX = (width_ + 7) / 8;
    uint32_t groupsY = (height_ + 7) / 8;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);
}

void ScreenSpaceTracing::coneTrace(VkCommandBuffer cmd,
                                    VkImageView colorBuffer,
                                    VkImageView depthBuffer,
                                    VkImageView normalBuffer,
                                    VkImageView materialBuffer,
                                    VkImageView hizBuffer,
                                    VkImageView globalSDF,
                                    const glm::vec3& sdfOrigin,
                                    const glm::vec3& sdfExtent,
                                    float sdfVoxelSize,
                                    const glm::mat4& viewProj,
                                    const glm::mat4& invViewProj,
                                    const ConeTraceConfig& config) {
    // Would dispatch cone_trace.comp
}

void ScreenSpaceTracing::temporalFilter(VkCommandBuffer cmd,
                                         VkImageView currentSSR,
                                         VkImageView historySSR,
                                         VkImageView motionVectors,
                                         VkImageView outputSSR) {
    // Would dispatch temporal filtering
}
