/**
 * PreviewRenderer.cpp
 * 
 * Preview renderer implementation.
 */

#include "PreviewRenderer.h"
#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>
#include <array>

namespace Sanic::Editor {

PreviewRenderer::PreviewRenderer() = default;
PreviewRenderer::~PreviewRenderer() = default;

bool PreviewRenderer::initialize(const InitInfo& info) {
    info_ = info;
    
    // Create render pass
    VkAttachmentDescription colorAttachment = {};
    colorAttachment.format = VK_FORMAT_R8G8B8A8_UNORM;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    
    VkAttachmentDescription depthAttachment = {};
    depthAttachment.format = VK_FORMAT_D32_SFLOAT;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    
    VkAttachmentReference colorRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depthRef = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;
    
    std::array<VkAttachmentDescription, 2> attachments = { colorAttachment, depthAttachment };
    
    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    
    if (vkCreateRenderPass(info_.device, &renderPassInfo, nullptr, &renderPass_) != VK_SUCCESS) {
        return false;
    }
    
    // Create sampler
    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    
    if (vkCreateSampler(info_.device, &samplerInfo, nullptr, &sampler_) != VK_SUCCESS) {
        return false;
    }
    
    // Create render target
    resize(info_.width, info_.height);
    
    // Create subclass resources
    createResources();
    
    initialized_ = true;
    return true;
}

void PreviewRenderer::shutdown() {
    if (!initialized_) return;
    
    vkDeviceWaitIdle(info_.device);
    
    destroyResources();
    
    if (framebuffer_) vkDestroyFramebuffer(info_.device, framebuffer_, nullptr);
    if (colorView_) vkDestroyImageView(info_.device, colorView_, nullptr);
    if (colorImage_) vkDestroyImage(info_.device, colorImage_, nullptr);
    if (colorMemory_) vkFreeMemory(info_.device, colorMemory_, nullptr);
    if (depthView_) vkDestroyImageView(info_.device, depthView_, nullptr);
    if (depthImage_) vkDestroyImage(info_.device, depthImage_, nullptr);
    if (depthMemory_) vkFreeMemory(info_.device, depthMemory_, nullptr);
    if (sampler_) vkDestroySampler(info_.device, sampler_, nullptr);
    if (renderPass_) vkDestroyRenderPass(info_.device, renderPass_, nullptr);
    
    initialized_ = false;
}

void PreviewRenderer::resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return;
    
    vkDeviceWaitIdle(info_.device);
    
    // Destroy old resources
    if (framebuffer_) vkDestroyFramebuffer(info_.device, framebuffer_, nullptr);
    if (colorView_) vkDestroyImageView(info_.device, colorView_, nullptr);
    if (colorImage_) vkDestroyImage(info_.device, colorImage_, nullptr);
    if (colorMemory_) vkFreeMemory(info_.device, colorMemory_, nullptr);
    if (depthView_) vkDestroyImageView(info_.device, depthView_, nullptr);
    if (depthImage_) vkDestroyImage(info_.device, depthImage_, nullptr);
    if (depthMemory_) vkFreeMemory(info_.device, depthMemory_, nullptr);
    
    info_.width = width;
    info_.height = height;
    
    // Create color image
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent = { width, height, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    
    vkCreateImage(info_.device, &imageInfo, nullptr, &colorImage_);
    
    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(info_.device, colorImage_, &memReqs);
    
    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    vkAllocateMemory(info_.device, &allocInfo, nullptr, &colorMemory_);
    vkBindImageMemory(info_.device, colorImage_, colorMemory_, 0);
    
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = colorImage_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    
    vkCreateImageView(info_.device, &viewInfo, nullptr, &colorView_);
    
    // Create depth image
    imageInfo.format = VK_FORMAT_D32_SFLOAT;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    
    vkCreateImage(info_.device, &imageInfo, nullptr, &depthImage_);
    
    vkGetImageMemoryRequirements(info_.device, depthImage_, &memReqs);
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    vkAllocateMemory(info_.device, &allocInfo, nullptr, &depthMemory_);
    vkBindImageMemory(info_.device, depthImage_, depthMemory_, 0);
    
    viewInfo.image = depthImage_;
    viewInfo.format = VK_FORMAT_D32_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    
    vkCreateImageView(info_.device, &viewInfo, nullptr, &depthView_);
    
    // Create framebuffer
    std::array<VkImageView, 2> fbAttachments = { colorView_, depthView_ };
    
    VkFramebufferCreateInfo fbInfo = {};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = renderPass_;
    fbInfo.attachmentCount = static_cast<uint32_t>(fbAttachments.size());
    fbInfo.pAttachments = fbAttachments.data();
    fbInfo.width = width;
    fbInfo.height = height;
    fbInfo.layers = 1;
    
    vkCreateFramebuffer(info_.device, &fbInfo, nullptr, &framebuffer_);
}

void PreviewRenderer::setCamera(const glm::vec3& position, const glm::vec3& target) {
    cameraTarget_ = target;
    glm::vec3 dir = glm::normalize(position - target);
    cameraDistance_ = glm::length(position - target);
    cameraPitch_ = glm::degrees(asin(dir.y));
    cameraYaw_ = glm::degrees(atan2(dir.x, dir.z));
}

void PreviewRenderer::orbit(float deltaYaw, float deltaPitch) {
    cameraYaw_ += deltaYaw;
    cameraPitch_ = glm::clamp(cameraPitch_ + deltaPitch, -89.0f, 89.0f);
}

void PreviewRenderer::zoom(float delta) {
    cameraDistance_ = glm::max(0.1f, cameraDistance_ - delta);
}

void PreviewRenderer::resetCamera() {
    cameraDistance_ = 3.0f;
    cameraYaw_ = 45.0f;
    cameraPitch_ = 30.0f;
    cameraTarget_ = glm::vec3(0.0f);
}

glm::mat4 PreviewRenderer::getViewMatrix() const {
    float yawRad = glm::radians(cameraYaw_);
    float pitchRad = glm::radians(cameraPitch_);
    
    glm::vec3 cameraPos;
    cameraPos.x = cameraDistance_ * cos(pitchRad) * sin(yawRad);
    cameraPos.y = cameraDistance_ * sin(pitchRad);
    cameraPos.z = cameraDistance_ * cos(pitchRad) * cos(yawRad);
    cameraPos += cameraTarget_;
    
    return glm::lookAt(cameraPos, cameraTarget_, glm::vec3(0, 1, 0));
}

glm::mat4 PreviewRenderer::getProjectionMatrix() const {
    float aspect = static_cast<float>(info_.width) / static_cast<float>(info_.height);
    return glm::perspective(glm::radians(fov_), aspect, 0.01f, 100.0f);
}

void PreviewRenderer::beginRender(VkCommandBuffer cmd) {
    std::array<VkClearValue, 2> clearValues = {};
    clearValues[0].color = {{ 0.15f, 0.15f, 0.18f, 1.0f }};
    clearValues[1].depthStencil = { 1.0f, 0 };
    
    VkRenderPassBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    beginInfo.renderPass = renderPass_;
    beginInfo.framebuffer = framebuffer_;
    beginInfo.renderArea = { {0, 0}, {info_.width, info_.height} };
    beginInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    beginInfo.pClearValues = clearValues.data();
    
    vkCmdBeginRenderPass(cmd, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
    
    VkViewport viewport = { 0, 0, (float)info_.width, (float)info_.height, 0, 1 };
    VkRect2D scissor = { {0, 0}, {info_.width, info_.height} };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

void PreviewRenderer::endRender(VkCommandBuffer cmd) {
    vkCmdEndRenderPass(cmd);
}

uint32_t PreviewRenderer::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
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

// MeshPreview implementation
void MeshPreview::render() {
    if (!mesh_) return;
    
    // Allocate command buffer
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = info_.commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(info_.device, &allocInfo, &cmd);
    
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    vkBeginCommandBuffer(cmd, &beginInfo);
    
    beginRender(cmd);
    renderContent(cmd);
    endRender(cmd);
    
    vkEndCommandBuffer(cmd);
    
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    
    vkQueueSubmit(info_.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(info_.graphicsQueue);
    
    vkFreeCommandBuffers(info_.device, info_.commandPool, 1, &cmd);
    
    needsUpdate_ = false;
}

void MeshPreview::createResources() {
    // TODO: Create preview pipeline
}

void MeshPreview::destroyResources() {
    if (pipeline_) vkDestroyPipeline(info_.device, pipeline_, nullptr);
    if (pipelineLayout_) vkDestroyPipelineLayout(info_.device, pipelineLayout_, nullptr);
}

void MeshPreview::renderContent(VkCommandBuffer cmd) {
    // TODO: Render mesh with basic shading
}

// MaterialPreview implementation
void MaterialPreview::render() {
    if (!material_) return;
    
    // Similar to MeshPreview::render()
    needsUpdate_ = false;
}

void MaterialPreview::createResources() {
    createPreviewShapes();
}

void MaterialPreview::destroyResources() {
    sphereMesh_.reset();
    cubeMesh_.reset();
    planeMesh_.reset();
    cylinderMesh_.reset();
}

void MaterialPreview::createPreviewShapes() {
    // TODO: Create primitive meshes for preview
}

void MaterialPreview::renderContent(VkCommandBuffer cmd) {
    // TODO: Render shape with material
}

// TexturePreview implementation
void TexturePreview::setTexture(VkImageView imageView, VkSampler sampler) {
    // TODO: Create descriptor set for the texture
}

void TexturePreview::draw(float width, float height) {
    if (descriptor_ == VK_NULL_HANDLE) {
        ImGui::TextDisabled("No texture");
        return;
    }
    
    ImGui::Image(descriptor_, ImVec2(width, height));
    
    // Controls
    ImGui::SliderFloat("Exposure", &exposure_, 0.1f, 10.0f);
    
    const char* channelNames[] = { "RGB", "R", "G", "B", "A" };
    int channelIndex = static_cast<int>(channel_);
    if (ImGui::Combo("Channel", &channelIndex, channelNames, 5)) {
        channel_ = static_cast<Channel>(channelIndex);
    }
}

} // namespace Sanic::Editor
