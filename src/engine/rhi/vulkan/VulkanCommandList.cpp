#ifdef SANIC_ENABLE_VULKAN

#include "VulkanRHI.h"

namespace Sanic {

// ============================================================================
// VulkanCommandList Implementation
// ============================================================================
VulkanCommandList::VulkanCommandList(VulkanRHI* rhi, RHIQueueType queueType)
    : m_rhi(rhi), m_queueType(queueType) {
    
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = rhi->getGraphicsQueueFamily();
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    
    switch (queueType) {
        case RHIQueueType::Graphics:
            poolInfo.queueFamilyIndex = rhi->getGraphicsQueueFamily();
            break;
        case RHIQueueType::Compute:
            poolInfo.queueFamilyIndex = rhi->getComputeQueueFamily();
            break;
        case RHIQueueType::Transfer:
            poolInfo.queueFamilyIndex = rhi->getTransferQueueFamily();
            break;
    }
    
    vkCreateCommandPool(rhi->getDevice(), &poolInfo, nullptr, &m_commandPool);
    
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    
    vkAllocateCommandBuffers(rhi->getDevice(), &allocInfo, &m_commandBuffer);
}

VulkanCommandList::~VulkanCommandList() {
    if (m_commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_rhi->getDevice(), m_commandPool, nullptr);
    }
}

void VulkanCommandList::begin() {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    vkBeginCommandBuffer(m_commandBuffer, &beginInfo);
}

void VulkanCommandList::end() {
    if (m_insideRenderPass) {
        endRenderPass();
    }
    vkEndCommandBuffer(m_commandBuffer);
}

void VulkanCommandList::reset() {
    vkResetCommandBuffer(m_commandBuffer, 0);
    m_currentPipeline = nullptr;
    m_insideRenderPass = false;
}

// ============================================================================
// Barriers
// ============================================================================
void VulkanCommandList::barrier(const RHIBarrier* barriers, uint32_t count) {
    std::vector<VkBufferMemoryBarrier2> bufferBarriers;
    std::vector<VkImageMemoryBarrier2> imageBarriers;
    VkMemoryBarrier2 memoryBarrier{};
    bool hasGlobalBarrier = false;
    
    for (uint32_t i = 0; i < count; i++) {
        const auto& barrier = barriers[i];
        
        switch (barrier.type) {
            case RHIBarrier::Type::Buffer: {
                auto vkBuffer = static_cast<VulkanBuffer*>(barrier.buffer.buffer);
                
                VkBufferMemoryBarrier2 bufferBarrier{};
                bufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
                bufferBarrier.srcStageMask = ToVkPipelineStage(barrier.buffer.stateBefore);
                bufferBarrier.srcAccessMask = ToVkAccessFlags(barrier.buffer.stateBefore);
                bufferBarrier.dstStageMask = ToVkPipelineStage(barrier.buffer.stateAfter);
                bufferBarrier.dstAccessMask = ToVkAccessFlags(barrier.buffer.stateAfter);
                bufferBarrier.buffer = vkBuffer->getBuffer();
                bufferBarrier.offset = barrier.buffer.offset;
                bufferBarrier.size = barrier.buffer.size == ~0ull ? VK_WHOLE_SIZE : barrier.buffer.size;
                bufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                bufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                bufferBarriers.push_back(bufferBarrier);
                break;
            }
            case RHIBarrier::Type::Texture: {
                auto vkTexture = static_cast<VulkanTexture*>(barrier.texture.texture);
                
                VkImageMemoryBarrier2 imageBarrier{};
                imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                imageBarrier.srcStageMask = ToVkPipelineStage(barrier.texture.stateBefore);
                imageBarrier.srcAccessMask = ToVkAccessFlags(barrier.texture.stateBefore);
                imageBarrier.dstStageMask = ToVkPipelineStage(barrier.texture.stateAfter);
                imageBarrier.dstAccessMask = ToVkAccessFlags(barrier.texture.stateAfter);
                imageBarrier.oldLayout = ToVkImageLayout(barrier.texture.stateBefore);
                imageBarrier.newLayout = ToVkImageLayout(barrier.texture.stateAfter);
                imageBarrier.image = vkTexture->getImage();
                imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                
                VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
                if (hasFlag(vkTexture->getUsage(), RHITextureUsage::DepthStencil)) {
                    aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
                    if (vkTexture->getFormat() == RHIFormat::D24_UNORM_S8_UINT ||
                        vkTexture->getFormat() == RHIFormat::D32_FLOAT_S8_UINT) {
                        aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
                    }
                }
                
                imageBarrier.subresourceRange.aspectMask = aspect;
                imageBarrier.subresourceRange.baseMipLevel = barrier.texture.baseMipLevel;
                imageBarrier.subresourceRange.levelCount = 
                    barrier.texture.mipLevelCount == ~0u ? VK_REMAINING_MIP_LEVELS : barrier.texture.mipLevelCount;
                imageBarrier.subresourceRange.baseArrayLayer = barrier.texture.baseArrayLayer;
                imageBarrier.subresourceRange.layerCount = 
                    barrier.texture.arrayLayerCount == ~0u ? VK_REMAINING_ARRAY_LAYERS : barrier.texture.arrayLayerCount;
                
                imageBarriers.push_back(imageBarrier);
                break;
            }
            case RHIBarrier::Type::Global: {
                hasGlobalBarrier = true;
                memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
                memoryBarrier.srcStageMask = ToVkPipelineStage(barrier.global.stateBefore);
                memoryBarrier.srcAccessMask = ToVkAccessFlags(barrier.global.stateBefore);
                memoryBarrier.dstStageMask = ToVkPipelineStage(barrier.global.stateAfter);
                memoryBarrier.dstAccessMask = ToVkAccessFlags(barrier.global.stateAfter);
                break;
            }
        }
    }
    
    VkDependencyInfo depInfo{};
    depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    if (hasGlobalBarrier) {
        depInfo.memoryBarrierCount = 1;
        depInfo.pMemoryBarriers = &memoryBarrier;
    }
    depInfo.bufferMemoryBarrierCount = static_cast<uint32_t>(bufferBarriers.size());
    depInfo.pBufferMemoryBarriers = bufferBarriers.data();
    depInfo.imageMemoryBarrierCount = static_cast<uint32_t>(imageBarriers.size());
    depInfo.pImageMemoryBarriers = imageBarriers.data();
    
    vkCmdPipelineBarrier2(m_commandBuffer, &depInfo);
}

void VulkanCommandList::uavBarrier(IRHIBuffer* buffer) {
    VkMemoryBarrier2 memoryBarrier{};
    memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    memoryBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    memoryBarrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    memoryBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    memoryBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
    
    VkDependencyInfo depInfo{};
    depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    depInfo.memoryBarrierCount = 1;
    depInfo.pMemoryBarriers = &memoryBarrier;
    
    vkCmdPipelineBarrier2(m_commandBuffer, &depInfo);
}

void VulkanCommandList::uavBarrier(IRHITexture* texture) {
    uavBarrier(static_cast<IRHIBuffer*>(nullptr));
}

// ============================================================================
// Render Pass
// ============================================================================
void VulkanCommandList::beginRenderPass(const RHIRenderPassBeginInfo& info) {
    std::vector<VkRenderingAttachmentInfo> colorAttachments;
    
    for (uint32_t i = 0; i < info.colorAttachmentCount; i++) {
        auto vkTexture = static_cast<VulkanTexture*>(info.colorAttachments[i]);
        
        VkRenderingAttachmentInfo attachment{};
        attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        attachment.imageView = vkTexture->getDefaultView();
        attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachment.loadOp = info.clearValues ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        
        if (info.clearValues) {
            attachment.clearValue.color.float32[0] = info.clearValues[i].color[0];
            attachment.clearValue.color.float32[1] = info.clearValues[i].color[1];
            attachment.clearValue.color.float32[2] = info.clearValues[i].color[2];
            attachment.clearValue.color.float32[3] = info.clearValues[i].color[3];
        }
        
        colorAttachments.push_back(attachment);
    }
    
    VkRenderingAttachmentInfo depthAttachment{};
    bool hasDepth = info.depthStencilAttachment != nullptr;
    
    if (hasDepth) {
        auto vkTexture = static_cast<VulkanTexture*>(info.depthStencilAttachment);
        
        depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAttachment.imageView = vkTexture->getDefaultView();
        depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp = info.clearValues ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        
        if (info.clearValues) {
            depthAttachment.clearValue.depthStencil.depth = info.clearValues[info.colorAttachmentCount].depthStencil.depth;
            depthAttachment.clearValue.depthStencil.stencil = info.clearValues[info.colorAttachmentCount].depthStencil.stencil;
        }
    }
    
    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = { static_cast<int32_t>(info.x), static_cast<int32_t>(info.y) };
    renderingInfo.renderArea.extent = { info.width, info.height };
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size());
    renderingInfo.pColorAttachments = colorAttachments.data();
    renderingInfo.pDepthAttachment = hasDepth ? &depthAttachment : nullptr;
    
    vkCmdBeginRendering(m_commandBuffer, &renderingInfo);
    m_insideRenderPass = true;
}

void VulkanCommandList::endRenderPass() {
    vkCmdEndRendering(m_commandBuffer);
    m_insideRenderPass = false;
}

// ============================================================================
// Pipeline State
// ============================================================================
void VulkanCommandList::setPipeline(IRHIPipeline* pipeline) {
    m_currentPipeline = static_cast<VulkanPipeline*>(pipeline);
    vkCmdBindPipeline(m_commandBuffer, m_currentPipeline->getBindPoint(), m_currentPipeline->getPipeline());
}

void VulkanCommandList::setViewport(const RHIViewport& viewport) {
    VkViewport vp;
    vp.x = viewport.x;
    vp.y = viewport.y + viewport.height;  // Flip for Vulkan
    vp.width = viewport.width;
    vp.height = -viewport.height;  // Negative to flip
    vp.minDepth = viewport.minDepth;
    vp.maxDepth = viewport.maxDepth;
    
    vkCmdSetViewport(m_commandBuffer, 0, 1, &vp);
}

void VulkanCommandList::setViewports(const RHIViewport* viewports, uint32_t count) {
    std::vector<VkViewport> vps(count);
    for (uint32_t i = 0; i < count; i++) {
        vps[i].x = viewports[i].x;
        vps[i].y = viewports[i].y + viewports[i].height;
        vps[i].width = viewports[i].width;
        vps[i].height = -viewports[i].height;
        vps[i].minDepth = viewports[i].minDepth;
        vps[i].maxDepth = viewports[i].maxDepth;
    }
    vkCmdSetViewport(m_commandBuffer, 0, count, vps.data());
}

void VulkanCommandList::setScissor(const RHIScissor& scissor) {
    VkRect2D rect;
    rect.offset = { scissor.x, scissor.y };
    rect.extent = { scissor.width, scissor.height };
    vkCmdSetScissor(m_commandBuffer, 0, 1, &rect);
}

void VulkanCommandList::setScissors(const RHIScissor* scissors, uint32_t count) {
    std::vector<VkRect2D> rects(count);
    for (uint32_t i = 0; i < count; i++) {
        rects[i].offset = { scissors[i].x, scissors[i].y };
        rects[i].extent = { scissors[i].width, scissors[i].height };
    }
    vkCmdSetScissor(m_commandBuffer, 0, count, rects.data());
}

void VulkanCommandList::setBlendConstants(const float constants[4]) {
    vkCmdSetBlendConstants(m_commandBuffer, constants);
}

void VulkanCommandList::setStencilReference(uint32_t reference) {
    vkCmdSetStencilReference(m_commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, reference);
}

void VulkanCommandList::setDepthBias(float constantFactor, float clamp, float slopeFactor) {
    vkCmdSetDepthBias(m_commandBuffer, constantFactor, clamp, slopeFactor);
}

void VulkanCommandList::setLineWidth(float width) {
    vkCmdSetLineWidth(m_commandBuffer, width);
}

// ============================================================================
// Resource Binding
// ============================================================================
void VulkanCommandList::setVertexBuffer(uint32_t slot, IRHIBuffer* buffer, uint64_t offset) {
    auto vkBuffer = static_cast<VulkanBuffer*>(buffer);
    VkBuffer buffers[] = { vkBuffer->getBuffer() };
    VkDeviceSize offsets[] = { offset };
    vkCmdBindVertexBuffers(m_commandBuffer, slot, 1, buffers, offsets);
}

void VulkanCommandList::setVertexBuffers(uint32_t firstSlot, IRHIBuffer** buffers,
                                         const uint64_t* offsets, uint32_t count) {
    std::vector<VkBuffer> vkBuffers(count);
    std::vector<VkDeviceSize> vkOffsets(count);
    
    for (uint32_t i = 0; i < count; i++) {
        vkBuffers[i] = static_cast<VulkanBuffer*>(buffers[i])->getBuffer();
        vkOffsets[i] = offsets ? offsets[i] : 0;
    }
    
    vkCmdBindVertexBuffers(m_commandBuffer, firstSlot, count, vkBuffers.data(), vkOffsets.data());
}

void VulkanCommandList::setIndexBuffer(IRHIBuffer* buffer, uint64_t offset, RHIIndexType indexType) {
    auto vkBuffer = static_cast<VulkanBuffer*>(buffer);
    vkCmdBindIndexBuffer(m_commandBuffer, vkBuffer->getBuffer(), offset, ToVkIndexType(indexType));
}

void VulkanCommandList::pushConstants(RHIShaderStage stages, uint32_t offset,
                                      uint32_t size, const void* data) {
    if (m_currentPipeline) {
        vkCmdPushConstants(m_commandBuffer, m_currentPipeline->getLayout(),
                          ToVkShaderStage(stages), offset, size, data);
    }
}

void VulkanCommandList::bindBuffer(uint32_t set, uint32_t binding, IRHIBuffer* buffer,
                                   uint64_t offset, uint64_t range) {
    // TODO: Implement descriptor set binding
}

void VulkanCommandList::bindTexture(uint32_t set, uint32_t binding, IRHITexture* texture,
                                    IRHISampler* sampler) {
    // TODO: Implement descriptor set binding
}

void VulkanCommandList::bindStorageTexture(uint32_t set, uint32_t binding, IRHITexture* texture,
                                           uint32_t mipLevel) {
    // TODO: Implement descriptor set binding
}

void VulkanCommandList::bindSampler(uint32_t set, uint32_t binding, IRHISampler* sampler) {
    // TODO: Implement descriptor set binding
}

void VulkanCommandList::bindAccelerationStructure(uint32_t set, uint32_t binding,
                                                  IRHIAccelerationStructure* as) {
    // TODO: Implement descriptor set binding
}

// ============================================================================
// Draw Commands
// ============================================================================
void VulkanCommandList::draw(uint32_t vertexCount, uint32_t instanceCount,
                             uint32_t firstVertex, uint32_t firstInstance) {
    vkCmdDraw(m_commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
}

void VulkanCommandList::drawIndexed(uint32_t indexCount, uint32_t instanceCount,
                                    uint32_t firstIndex, int32_t vertexOffset,
                                    uint32_t firstInstance) {
    vkCmdDrawIndexed(m_commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void VulkanCommandList::drawIndirect(IRHIBuffer* buffer, uint64_t offset,
                                     uint32_t drawCount, uint32_t stride) {
    auto vkBuffer = static_cast<VulkanBuffer*>(buffer);
    vkCmdDrawIndirect(m_commandBuffer, vkBuffer->getBuffer(), offset, drawCount, stride);
}

void VulkanCommandList::drawIndexedIndirect(IRHIBuffer* buffer, uint64_t offset,
                                            uint32_t drawCount, uint32_t stride) {
    auto vkBuffer = static_cast<VulkanBuffer*>(buffer);
    vkCmdDrawIndexedIndirect(m_commandBuffer, vkBuffer->getBuffer(), offset, drawCount, stride);
}

void VulkanCommandList::drawIndirectCount(IRHIBuffer* argBuffer, uint64_t argOffset,
                                          IRHIBuffer* countBuffer, uint64_t countOffset,
                                          uint32_t maxDrawCount, uint32_t stride) {
    auto vkArgBuffer = static_cast<VulkanBuffer*>(argBuffer);
    auto vkCountBuffer = static_cast<VulkanBuffer*>(countBuffer);
    vkCmdDrawIndirectCount(m_commandBuffer, vkArgBuffer->getBuffer(), argOffset,
                           vkCountBuffer->getBuffer(), countOffset, maxDrawCount, stride);
}

void VulkanCommandList::drawIndexedIndirectCount(IRHIBuffer* argBuffer, uint64_t argOffset,
                                                 IRHIBuffer* countBuffer, uint64_t countOffset,
                                                 uint32_t maxDrawCount, uint32_t stride) {
    auto vkArgBuffer = static_cast<VulkanBuffer*>(argBuffer);
    auto vkCountBuffer = static_cast<VulkanBuffer*>(countBuffer);
    vkCmdDrawIndexedIndirectCount(m_commandBuffer, vkArgBuffer->getBuffer(), argOffset,
                                  vkCountBuffer->getBuffer(), countOffset, maxDrawCount, stride);
}

// ============================================================================
// Mesh Shader Commands
// ============================================================================
void VulkanCommandList::dispatchMesh(uint32_t groupCountX, uint32_t groupCountY,
                                     uint32_t groupCountZ) {
    auto func = (PFN_vkCmdDrawMeshTasksEXT)vkGetDeviceProcAddr(
        m_rhi->getDevice(), "vkCmdDrawMeshTasksEXT");
    if (func) {
        func(m_commandBuffer, groupCountX, groupCountY, groupCountZ);
    }
}

void VulkanCommandList::dispatchMeshIndirect(IRHIBuffer* buffer, uint64_t offset) {
    auto vkBuffer = static_cast<VulkanBuffer*>(buffer);
    auto func = (PFN_vkCmdDrawMeshTasksIndirectEXT)vkGetDeviceProcAddr(
        m_rhi->getDevice(), "vkCmdDrawMeshTasksIndirectEXT");
    if (func) {
        func(m_commandBuffer, vkBuffer->getBuffer(), offset, 1, 0);
    }
}

void VulkanCommandList::dispatchMeshIndirectCount(IRHIBuffer* argBuffer, uint64_t argOffset,
                                                  IRHIBuffer* countBuffer, uint64_t countOffset,
                                                  uint32_t maxDispatchCount, uint32_t stride) {
    auto vkArgBuffer = static_cast<VulkanBuffer*>(argBuffer);
    auto vkCountBuffer = static_cast<VulkanBuffer*>(countBuffer);
    auto func = (PFN_vkCmdDrawMeshTasksIndirectCountEXT)vkGetDeviceProcAddr(
        m_rhi->getDevice(), "vkCmdDrawMeshTasksIndirectCountEXT");
    if (func) {
        func(m_commandBuffer, vkArgBuffer->getBuffer(), argOffset,
             vkCountBuffer->getBuffer(), countOffset, maxDispatchCount, stride);
    }
}

// ============================================================================
// Compute Commands
// ============================================================================
void VulkanCommandList::dispatch(uint32_t groupCountX, uint32_t groupCountY,
                                 uint32_t groupCountZ) {
    vkCmdDispatch(m_commandBuffer, groupCountX, groupCountY, groupCountZ);
}

void VulkanCommandList::dispatchIndirect(IRHIBuffer* buffer, uint64_t offset) {
    auto vkBuffer = static_cast<VulkanBuffer*>(buffer);
    vkCmdDispatchIndirect(m_commandBuffer, vkBuffer->getBuffer(), offset);
}

// ============================================================================
// Ray Tracing Commands
// ============================================================================
void VulkanCommandList::dispatchRays(const RHIDispatchRaysDesc& desc) {
    VkStridedDeviceAddressRegionKHR raygenRegion{};
    VkStridedDeviceAddressRegionKHR missRegion{};
    VkStridedDeviceAddressRegionKHR hitRegion{};
    VkStridedDeviceAddressRegionKHR callableRegion{};
    
    if (desc.rayGenShaderTable.buffer) {
        auto buf = static_cast<VulkanBuffer*>(desc.rayGenShaderTable.buffer);
        raygenRegion.deviceAddress = buf->getGPUAddress() + desc.rayGenShaderTable.offset;
        raygenRegion.size = desc.rayGenShaderTable.size;
        raygenRegion.stride = desc.rayGenShaderTable.stride;
    }
    
    if (desc.missShaderTable.buffer) {
        auto buf = static_cast<VulkanBuffer*>(desc.missShaderTable.buffer);
        missRegion.deviceAddress = buf->getGPUAddress() + desc.missShaderTable.offset;
        missRegion.size = desc.missShaderTable.size;
        missRegion.stride = desc.missShaderTable.stride;
    }
    
    if (desc.hitGroupTable.buffer) {
        auto buf = static_cast<VulkanBuffer*>(desc.hitGroupTable.buffer);
        hitRegion.deviceAddress = buf->getGPUAddress() + desc.hitGroupTable.offset;
        hitRegion.size = desc.hitGroupTable.size;
        hitRegion.stride = desc.hitGroupTable.stride;
    }
    
    if (desc.callableShaderTable.buffer) {
        auto buf = static_cast<VulkanBuffer*>(desc.callableShaderTable.buffer);
        callableRegion.deviceAddress = buf->getGPUAddress() + desc.callableShaderTable.offset;
        callableRegion.size = desc.callableShaderTable.size;
        callableRegion.stride = desc.callableShaderTable.stride;
    }
    
    auto func = (PFN_vkCmdTraceRaysKHR)vkGetDeviceProcAddr(m_rhi->getDevice(), "vkCmdTraceRaysKHR");
    if (func) {
        func(m_commandBuffer, &raygenRegion, &missRegion, &hitRegion, &callableRegion,
             desc.width, desc.height, desc.depth);
    }
}

void VulkanCommandList::buildAccelerationStructure(const RHIAccelerationStructureBuildInfo& info) {
    // TODO: Implement acceleration structure building
}

void VulkanCommandList::copyAccelerationStructure(IRHIAccelerationStructure* dst,
                                                  IRHIAccelerationStructure* src, bool compact) {
    auto vkDst = static_cast<VulkanAccelerationStructure*>(dst);
    auto vkSrc = static_cast<VulkanAccelerationStructure*>(src);
    
    VkCopyAccelerationStructureInfoKHR copyInfo{};
    copyInfo.sType = VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR;
    copyInfo.src = vkSrc->getHandle();
    copyInfo.dst = vkDst->getHandle();
    copyInfo.mode = compact ? VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR :
                              VK_COPY_ACCELERATION_STRUCTURE_MODE_CLONE_KHR;
    
    auto func = (PFN_vkCmdCopyAccelerationStructureKHR)vkGetDeviceProcAddr(
        m_rhi->getDevice(), "vkCmdCopyAccelerationStructureKHR");
    if (func) {
        func(m_commandBuffer, &copyInfo);
    }
}

// ============================================================================
// Copy Commands
// ============================================================================
void VulkanCommandList::copyBuffer(IRHIBuffer* src, IRHIBuffer* dst,
                                   const RHIBufferCopy* regions, uint32_t regionCount) {
    auto vkSrc = static_cast<VulkanBuffer*>(src);
    auto vkDst = static_cast<VulkanBuffer*>(dst);
    
    std::vector<VkBufferCopy> vkRegions(regionCount);
    for (uint32_t i = 0; i < regionCount; i++) {
        vkRegions[i].srcOffset = regions[i].srcOffset;
        vkRegions[i].dstOffset = regions[i].dstOffset;
        vkRegions[i].size = regions[i].size;
    }
    
    vkCmdCopyBuffer(m_commandBuffer, vkSrc->getBuffer(), vkDst->getBuffer(),
                    regionCount, vkRegions.data());
}

void VulkanCommandList::copyTexture(IRHITexture* src, IRHITexture* dst,
                                    const RHITextureCopy* regions, uint32_t regionCount) {
    auto vkSrc = static_cast<VulkanTexture*>(src);
    auto vkDst = static_cast<VulkanTexture*>(dst);
    
    std::vector<VkImageCopy> vkRegions(regionCount);
    for (uint32_t i = 0; i < regionCount; i++) {
        vkRegions[i].srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vkRegions[i].srcSubresource.mipLevel = regions[i].srcMipLevel;
        vkRegions[i].srcSubresource.baseArrayLayer = regions[i].srcArrayLayer;
        vkRegions[i].srcSubresource.layerCount = 1;
        vkRegions[i].srcOffset = { regions[i].srcOffsetX, regions[i].srcOffsetY, regions[i].srcOffsetZ };
        
        vkRegions[i].dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vkRegions[i].dstSubresource.mipLevel = regions[i].dstMipLevel;
        vkRegions[i].dstSubresource.baseArrayLayer = regions[i].dstArrayLayer;
        vkRegions[i].dstSubresource.layerCount = 1;
        vkRegions[i].dstOffset = { regions[i].dstOffsetX, regions[i].dstOffsetY, regions[i].dstOffsetZ };
        
        vkRegions[i].extent = { regions[i].width, regions[i].height, regions[i].depth };
    }
    
    vkCmdCopyImage(m_commandBuffer, vkSrc->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   vkDst->getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   regionCount, vkRegions.data());
}

void VulkanCommandList::copyBufferToTexture(IRHIBuffer* src, IRHITexture* dst,
                                            const RHIBufferTextureCopy* regions,
                                            uint32_t regionCount) {
    auto vkSrc = static_cast<VulkanBuffer*>(src);
    auto vkDst = static_cast<VulkanTexture*>(dst);
    
    std::vector<VkBufferImageCopy> vkRegions(regionCount);
    for (uint32_t i = 0; i < regionCount; i++) {
        vkRegions[i].bufferOffset = regions[i].bufferOffset;
        vkRegions[i].bufferRowLength = regions[i].bufferRowLength;
        vkRegions[i].bufferImageHeight = regions[i].bufferImageHeight;
        vkRegions[i].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vkRegions[i].imageSubresource.mipLevel = regions[i].textureMipLevel;
        vkRegions[i].imageSubresource.baseArrayLayer = regions[i].textureArrayLayer;
        vkRegions[i].imageSubresource.layerCount = regions[i].textureArrayLayerCount;
        vkRegions[i].imageOffset = { regions[i].textureOffsetX, regions[i].textureOffsetY, regions[i].textureOffsetZ };
        vkRegions[i].imageExtent = { regions[i].width, regions[i].height, regions[i].depth };
    }
    
    vkCmdCopyBufferToImage(m_commandBuffer, vkSrc->getBuffer(), vkDst->getImage(),
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, regionCount, vkRegions.data());
}

void VulkanCommandList::copyTextureToBuffer(IRHITexture* src, IRHIBuffer* dst,
                                            const RHIBufferTextureCopy* regions,
                                            uint32_t regionCount) {
    auto vkSrc = static_cast<VulkanTexture*>(src);
    auto vkDst = static_cast<VulkanBuffer*>(dst);
    
    std::vector<VkBufferImageCopy> vkRegions(regionCount);
    for (uint32_t i = 0; i < regionCount; i++) {
        vkRegions[i].bufferOffset = regions[i].bufferOffset;
        vkRegions[i].bufferRowLength = regions[i].bufferRowLength;
        vkRegions[i].bufferImageHeight = regions[i].bufferImageHeight;
        vkRegions[i].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vkRegions[i].imageSubresource.mipLevel = regions[i].textureMipLevel;
        vkRegions[i].imageSubresource.baseArrayLayer = regions[i].textureArrayLayer;
        vkRegions[i].imageSubresource.layerCount = regions[i].textureArrayLayerCount;
        vkRegions[i].imageOffset = { regions[i].textureOffsetX, regions[i].textureOffsetY, regions[i].textureOffsetZ };
        vkRegions[i].imageExtent = { regions[i].width, regions[i].height, regions[i].depth };
    }
    
    vkCmdCopyImageToBuffer(m_commandBuffer, vkSrc->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           vkDst->getBuffer(), regionCount, vkRegions.data());
}

// ============================================================================
// Clear Commands
// ============================================================================
void VulkanCommandList::clearBuffer(IRHIBuffer* buffer, uint32_t value,
                                    uint64_t offset, uint64_t size) {
    auto vkBuffer = static_cast<VulkanBuffer*>(buffer);
    vkCmdFillBuffer(m_commandBuffer, vkBuffer->getBuffer(), offset,
                    size == ~0ull ? VK_WHOLE_SIZE : size, value);
}

void VulkanCommandList::clearTexture(IRHITexture* texture, const float color[4],
                                     uint32_t baseMip, uint32_t mipCount,
                                     uint32_t baseLayer, uint32_t layerCount) {
    auto vkTexture = static_cast<VulkanTexture*>(texture);
    
    VkClearColorValue clearColor;
    clearColor.float32[0] = color[0];
    clearColor.float32[1] = color[1];
    clearColor.float32[2] = color[2];
    clearColor.float32[3] = color[3];
    
    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseMipLevel = baseMip;
    range.levelCount = mipCount == ~0u ? VK_REMAINING_MIP_LEVELS : mipCount;
    range.baseArrayLayer = baseLayer;
    range.layerCount = layerCount == ~0u ? VK_REMAINING_ARRAY_LAYERS : layerCount;
    
    vkCmdClearColorImage(m_commandBuffer, vkTexture->getImage(),
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &range);
}

void VulkanCommandList::clearDepthStencil(IRHITexture* texture, float depth, uint8_t stencil,
                                          bool clearDepth, bool clearStencil) {
    auto vkTexture = static_cast<VulkanTexture*>(texture);
    
    VkClearDepthStencilValue clearValue;
    clearValue.depth = depth;
    clearValue.stencil = stencil;
    
    VkImageSubresourceRange range{};
    if (clearDepth) range.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
    if (clearStencil) range.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    range.baseMipLevel = 0;
    range.levelCount = 1;
    range.baseArrayLayer = 0;
    range.layerCount = 1;
    
    vkCmdClearDepthStencilImage(m_commandBuffer, vkTexture->getImage(),
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, 1, &range);
}

// ============================================================================
// Query Commands
// ============================================================================
void VulkanCommandList::beginQuery(IRHIQueryPool* pool, uint32_t index) {
    auto vkPool = static_cast<VulkanQueryPool*>(pool);
    vkCmdBeginQuery(m_commandBuffer, vkPool->getPool(), index, 0);
}

void VulkanCommandList::endQuery(IRHIQueryPool* pool, uint32_t index) {
    auto vkPool = static_cast<VulkanQueryPool*>(pool);
    vkCmdEndQuery(m_commandBuffer, vkPool->getPool(), index);
}

void VulkanCommandList::resetQueryPool(IRHIQueryPool* pool, uint32_t firstQuery, uint32_t count) {
    auto vkPool = static_cast<VulkanQueryPool*>(pool);
    vkCmdResetQueryPool(m_commandBuffer, vkPool->getPool(), firstQuery, count);
}

void VulkanCommandList::writeTimestamp(IRHIQueryPool* pool, uint32_t index) {
    auto vkPool = static_cast<VulkanQueryPool*>(pool);
    vkCmdWriteTimestamp(m_commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, vkPool->getPool(), index);
}

void VulkanCommandList::resolveQueryData(IRHIQueryPool* pool, uint32_t firstQuery, uint32_t count,
                                         IRHIBuffer* destination, uint64_t offset) {
    auto vkPool = static_cast<VulkanQueryPool*>(pool);
    auto vkDst = static_cast<VulkanBuffer*>(destination);
    
    vkCmdCopyQueryPoolResults(m_commandBuffer, vkPool->getPool(), firstQuery, count,
                              vkDst->getBuffer(), offset, sizeof(uint64_t),
                              VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
}

// ============================================================================
// Debug Markers
// ============================================================================
void VulkanCommandList::beginDebugLabel(const char* name, const glm::vec4& color) {
    VkDebugUtilsLabelEXT label{};
    label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
    label.pLabelName = name;
    label.color[0] = color.r;
    label.color[1] = color.g;
    label.color[2] = color.b;
    label.color[3] = color.a;
    
    auto func = (PFN_vkCmdBeginDebugUtilsLabelEXT)vkGetInstanceProcAddr(
        m_rhi->getInstance(), "vkCmdBeginDebugUtilsLabelEXT");
    if (func) {
        func(m_commandBuffer, &label);
    }
}

void VulkanCommandList::endDebugLabel() {
    auto func = (PFN_vkCmdEndDebugUtilsLabelEXT)vkGetInstanceProcAddr(
        m_rhi->getInstance(), "vkCmdEndDebugUtilsLabelEXT");
    if (func) {
        func(m_commandBuffer);
    }
}

void VulkanCommandList::insertDebugLabel(const char* name, const glm::vec4& color) {
    VkDebugUtilsLabelEXT label{};
    label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
    label.pLabelName = name;
    label.color[0] = color.r;
    label.color[1] = color.g;
    label.color[2] = color.b;
    label.color[3] = color.a;
    
    auto func = (PFN_vkCmdInsertDebugUtilsLabelEXT)vkGetInstanceProcAddr(
        m_rhi->getInstance(), "vkCmdInsertDebugUtilsLabelEXT");
    if (func) {
        func(m_commandBuffer, &label);
    }
}

// ============================================================================
// Miscellaneous
// ============================================================================
void VulkanCommandList::fillBuffer(IRHIBuffer* buffer, uint64_t offset,
                                   uint64_t size, uint32_t data) {
    clearBuffer(buffer, data, offset, size);
}

void VulkanCommandList::updateBuffer(IRHIBuffer* buffer, uint64_t offset,
                                     uint64_t size, const void* data) {
    auto vkBuffer = static_cast<VulkanBuffer*>(buffer);
    vkCmdUpdateBuffer(m_commandBuffer, vkBuffer->getBuffer(), offset, size, data);
}

void VulkanCommandList::generateMipmaps(IRHITexture* texture) {
    auto vkTexture = static_cast<VulkanTexture*>(texture);
    
    int32_t mipWidth = vkTexture->getWidth();
    int32_t mipHeight = vkTexture->getHeight();
    
    for (uint32_t i = 1; i < vkTexture->getMipLevels(); i++) {
        VkImageBlit blit{};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = vkTexture->getArrayLayers();
        blit.srcOffsets[0] = { 0, 0, 0 };
        blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
        
        mipWidth = std::max(1, mipWidth / 2);
        mipHeight = std::max(1, mipHeight / 2);
        
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = vkTexture->getArrayLayers();
        blit.dstOffsets[0] = { 0, 0, 0 };
        blit.dstOffsets[1] = { mipWidth, mipHeight, 1 };
        
        vkCmdBlitImage(m_commandBuffer, vkTexture->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       vkTexture->getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &blit, VK_FILTER_LINEAR);
    }
}

void VulkanCommandList::resolveTexture(IRHITexture* src, IRHITexture* dst,
                                       uint32_t srcMip, uint32_t srcLayer,
                                       uint32_t dstMip, uint32_t dstLayer) {
    auto vkSrc = static_cast<VulkanTexture*>(src);
    auto vkDst = static_cast<VulkanTexture*>(dst);
    
    VkImageResolve region{};
    region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.srcSubresource.mipLevel = srcMip;
    region.srcSubresource.baseArrayLayer = srcLayer;
    region.srcSubresource.layerCount = 1;
    region.srcOffset = { 0, 0, 0 };
    region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.dstSubresource.mipLevel = dstMip;
    region.dstSubresource.baseArrayLayer = dstLayer;
    region.dstSubresource.layerCount = 1;
    region.dstOffset = { 0, 0, 0 };
    region.extent = { vkSrc->getWidth(), vkSrc->getHeight(), 1 };
    
    vkCmdResolveImage(m_commandBuffer, vkSrc->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      vkDst->getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

} // namespace Sanic

#endif // SANIC_ENABLE_VULKAN
