/**
 * SelectionOutline.cpp
 * 
 * Selection outline rendering implementation.
 */

#include "SelectionOutline.h"
#include "../core/Selection.h"
#include "../../engine/ECS.h"
#include <array>

namespace Sanic::Editor {

SelectionOutline::SelectionOutline() = default;
SelectionOutline::~SelectionOutline() = default;

bool SelectionOutline::initialize(const InitInfo& info) {
    info_ = info;
    
    if (!createStencilResources()) {
        return false;
    }
    
    if (!createPipelines()) {
        return false;
    }
    
    initialized_ = true;
    return true;
}

void SelectionOutline::shutdown() {
    if (!initialized_) return;
    
    vkDeviceWaitIdle(info_.device);
    
    destroyPipelines();
    destroyStencilResources();
    
    initialized_ = false;
}

void SelectionOutline::resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return;
    if (width == info_.width && height == info_.height) return;
    
    info_.width = width;
    info_.height = height;
    
    vkDeviceWaitIdle(info_.device);
    
    destroyStencilResources();
    createStencilResources();
}

void SelectionOutline::render(VkCommandBuffer cmd,
                              const Selection& selection,
                              const Sanic::ECSManager& ecs,
                              const glm::mat4& view,
                              const glm::mat4& proj) {
    if (!selection.hasSelection()) return;
    
    // For each selected entity:
    // 1. Render to stencil buffer
    // 2. Render outline using stencil test
    
    for (Entity entity : selection.getSelection()) {
        if (!ecs.isEntityValid(entity)) continue;
        
        // Get transform
        auto* transform = ecs.getComponent<Transform>(entity);
        if (!transform) continue;
        
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, transform->position);
        model = model * glm::mat4_cast(transform->rotation);
        model = glm::scale(model, transform->scale);
        
        glm::mat4 mvp = proj * view * model;
        
        // Stencil pass
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, stencilPipeline_);
        
        PushConstants pc;
        pc.mvp = mvp;
        pc.color = outlineColor_;
        pc.outlineWidth = outlineWidth_;
        
        vkCmdPushConstants(cmd, stencilPipelineLayout_, 
                          VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                          0, sizeof(PushConstants), &pc);
        
        // TODO: Get mesh and render
        // For now, this is a placeholder - actual implementation would
        // get MeshRenderer component and draw the mesh
        
        // Outline pass
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, outlinePipeline_);
        
        vkCmdPushConstants(cmd, outlinePipelineLayout_,
                          VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                          0, sizeof(PushConstants), &pc);
        
        // Draw slightly scaled mesh or outline quad
    }
}

bool SelectionOutline::createPipelines() {
    // Pipeline layout with push constants
    VkPushConstantRange pushConstantRange = {};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstants);
    
    VkPipelineLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;
    
    if (vkCreatePipelineLayout(info_.device, &layoutInfo, nullptr, &stencilPipelineLayout_) != VK_SUCCESS) {
        return false;
    }
    
    if (vkCreatePipelineLayout(info_.device, &layoutInfo, nullptr, &outlinePipelineLayout_) != VK_SUCCESS) {
        return false;
    }
    
    // TODO: Create actual pipelines with stencil operations
    // For now, these are placeholders
    // Stencil pipeline: write 1 to stencil where mesh is rendered
    // Outline pipeline: render outline where stencil != 1 but adjacent to 1
    
    return true;
}

void SelectionOutline::destroyPipelines() {
    if (stencilPipeline_) vkDestroyPipeline(info_.device, stencilPipeline_, nullptr);
    if (stencilPipelineLayout_) vkDestroyPipelineLayout(info_.device, stencilPipelineLayout_, nullptr);
    if (outlinePipeline_) vkDestroyPipeline(info_.device, outlinePipeline_, nullptr);
    if (outlinePipelineLayout_) vkDestroyPipelineLayout(info_.device, outlinePipelineLayout_, nullptr);
    
    stencilPipeline_ = VK_NULL_HANDLE;
    stencilPipelineLayout_ = VK_NULL_HANDLE;
    outlinePipeline_ = VK_NULL_HANDLE;
    outlinePipelineLayout_ = VK_NULL_HANDLE;
}

bool SelectionOutline::createStencilResources() {
    // Create stencil image
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_S8_UINT;
    imageInfo.extent = { info_.width, info_.height, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    
    if (vkCreateImage(info_.device, &imageInfo, nullptr, &stencilImage_) != VK_SUCCESS) {
        return false;
    }
    
    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(info_.device, stencilImage_, &memReqs);
    
    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(info_.device, &allocInfo, nullptr, &stencilMemory_) != VK_SUCCESS) {
        return false;
    }
    
    vkBindImageMemory(info_.device, stencilImage_, stencilMemory_, 0);
    
    // Create image view
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = stencilImage_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_S8_UINT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(info_.device, &viewInfo, nullptr, &stencilView_) != VK_SUCCESS) {
        return false;
    }
    
    return true;
}

void SelectionOutline::destroyStencilResources() {
    if (stencilView_) vkDestroyImageView(info_.device, stencilView_, nullptr);
    if (stencilImage_) vkDestroyImage(info_.device, stencilImage_, nullptr);
    if (stencilMemory_) vkFreeMemory(info_.device, stencilMemory_, nullptr);
    
    stencilView_ = VK_NULL_HANDLE;
    stencilImage_ = VK_NULL_HANDLE;
    stencilMemory_ = VK_NULL_HANDLE;
}

uint32_t SelectionOutline::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(info_.physicalDevice, &memProperties);
    
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && 
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    
    return 0;
}

} // namespace Sanic::Editor
