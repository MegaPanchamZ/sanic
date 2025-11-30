#include "D3D12RHI.h"

#ifdef SANIC_ENABLE_D3D12

#include "../../core/Log.h"
#include <algorithm>

namespace Sanic {

// ============================================================================
// D3D12CommandList Implementation
// ============================================================================

D3D12CommandList::D3D12CommandList(D3D12RHI* rhi, RHIQueueType queueType)
    : m_rhi(rhi)
    , m_queueType(queueType) {
    
    switch (queueType) {
        case RHIQueueType::Graphics:
            m_listType = D3D12_COMMAND_LIST_TYPE_DIRECT;
            break;
        case RHIQueueType::Compute:
            m_listType = D3D12_COMMAND_LIST_TYPE_COMPUTE;
            break;
        case RHIQueueType::Transfer:
            m_listType = D3D12_COMMAND_LIST_TYPE_COPY;
            break;
    }
    
    HRESULT hr = m_rhi->getDevice()->CreateCommandAllocator(
        m_listType, IID_PPV_ARGS(&m_allocator));
    
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create D3D12 command allocator");
        return;
    }
    
    hr = m_rhi->getDevice()->CreateCommandList(
        0, m_listType, m_allocator.Get(), nullptr,
        IID_PPV_ARGS(&m_commandList));
    
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create D3D12 command list");
        return;
    }
    
    // Close immediately - will be reset when begin() is called
    m_commandList->Close();
}

D3D12CommandList::~D3D12CommandList() {
    // ComPtrs handle cleanup
}

void D3D12CommandList::begin() {
    m_allocator->Reset();
    m_commandList->Reset(m_allocator.Get(), nullptr);
    
    // Set descriptor heaps
    ID3D12DescriptorHeap* heaps[] = {
        m_rhi->getCBVSRVUAVHeap()->getHeap(),
        m_rhi->getSamplerHeap()->getHeap()
    };
    m_commandList->SetDescriptorHeaps(2, heaps);
}

void D3D12CommandList::end() {
    m_commandList->Close();
}

void D3D12CommandList::reset() {
    m_allocator->Reset();
    m_commandList->Reset(m_allocator.Get(), nullptr);
    m_currentPipeline = nullptr;
    m_insideRenderPass = false;
}

// ============================================================================
// Barriers
// ============================================================================

void D3D12CommandList::barrier(const RHIBarrier* barriers, uint32_t count) {
    std::vector<D3D12_RESOURCE_BARRIER> d3dBarriers;
    d3dBarriers.reserve(count);
    
    for (uint32_t i = 0; i < count; i++) {
        const auto& barrier = barriers[i];
        D3D12_RESOURCE_BARRIER d3dBarrier = {};
        
        if (barrier.type == RHIBarrierType::Global) {
            d3dBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            d3dBarrier.UAV.pResource = nullptr;
        }
        else if (barrier.type == RHIBarrierType::Buffer) {
            auto* d3dBuffer = static_cast<D3D12Buffer*>(barrier.buffer);
            
            d3dBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            d3dBarrier.Transition.pResource = d3dBuffer->getResource();
            d3dBarrier.Transition.StateBefore = ToD3D12ResourceState(barrier.stateBefore);
            d3dBarrier.Transition.StateAfter = ToD3D12ResourceState(barrier.stateAfter);
            d3dBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        }
        else if (barrier.type == RHIBarrierType::Texture) {
            auto* d3dTexture = static_cast<D3D12Texture*>(barrier.texture);
            
            d3dBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            d3dBarrier.Transition.pResource = d3dTexture->getResource();
            d3dBarrier.Transition.StateBefore = ToD3D12ResourceState(barrier.stateBefore);
            d3dBarrier.Transition.StateAfter = ToD3D12ResourceState(barrier.stateAfter);
            
            if (barrier.subresource.mipCount == 0 && barrier.subresource.layerCount == 0) {
                d3dBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            } else {
                d3dBarrier.Transition.Subresource = 
                    barrier.subresource.baseMipLevel +
                    barrier.subresource.baseArrayLayer * d3dTexture->getMipLevels();
            }
        }
        
        d3dBarriers.push_back(d3dBarrier);
    }
    
    if (!d3dBarriers.empty()) {
        m_commandList->ResourceBarrier(static_cast<UINT>(d3dBarriers.size()), d3dBarriers.data());
    }
}

void D3D12CommandList::uavBarrier(IRHIBuffer* buffer) {
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = buffer ? static_cast<D3D12Buffer*>(buffer)->getResource() : nullptr;
    m_commandList->ResourceBarrier(1, &barrier);
}

void D3D12CommandList::uavBarrier(IRHITexture* texture) {
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = texture ? static_cast<D3D12Texture*>(texture)->getResource() : nullptr;
    m_commandList->ResourceBarrier(1, &barrier);
}

// ============================================================================
// Render Pass
// ============================================================================

void D3D12CommandList::beginRenderPass(const RHIRenderPassBeginInfo& info) {
    m_insideRenderPass = true;
    
    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvHandles;
    rtvHandles.reserve(info.colorAttachmentCount);
    
    for (uint32_t i = 0; i < info.colorAttachmentCount; i++) {
        const auto& attachment = info.colorAttachments[i];
        if (attachment.texture) {
            auto* d3dTexture = static_cast<D3D12Texture*>(attachment.texture);
            rtvHandles.push_back(d3dTexture->getRTV());
            
            if (attachment.loadOp == RHILoadOp::Clear) {
                m_commandList->ClearRenderTargetView(
                    rtvHandles.back(),
                    attachment.clearColor,
                    0, nullptr);
            }
        }
    }
    
    D3D12_CPU_DESCRIPTOR_HANDLE* pDsv = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = {};
    
    if (info.depthAttachment && info.depthAttachment->texture) {
        auto* d3dTexture = static_cast<D3D12Texture*>(info.depthAttachment->texture);
        dsvHandle = d3dTexture->getDSV();
        pDsv = &dsvHandle;
        
        if (info.depthAttachment->loadOp == RHILoadOp::Clear) {
            D3D12_CLEAR_FLAGS flags = D3D12_CLEAR_FLAG_DEPTH;
            if (info.stencilAttachment && info.stencilAttachment->loadOp == RHILoadOp::Clear) {
                flags |= D3D12_CLEAR_FLAG_STENCIL;
            }
            m_commandList->ClearDepthStencilView(
                dsvHandle,
                flags,
                info.depthAttachment->clearDepth,
                info.stencilAttachment ? info.stencilAttachment->clearStencil : 0,
                0, nullptr);
        }
    }
    
    m_commandList->OMSetRenderTargets(
        static_cast<UINT>(rtvHandles.size()),
        rtvHandles.empty() ? nullptr : rtvHandles.data(),
        FALSE,
        pDsv);
}

void D3D12CommandList::endRenderPass() {
    m_insideRenderPass = false;
}

// ============================================================================
// Pipeline State
// ============================================================================

void D3D12CommandList::setPipeline(IRHIPipeline* pipeline) {
    auto* d3dPipeline = static_cast<D3D12Pipeline*>(pipeline);
    
    if (d3dPipeline->getPSO()) {
        m_commandList->SetPipelineState(d3dPipeline->getPSO());
    }
    
    if (d3dPipeline->getType() == RHIPipelineType::Compute) {
        m_commandList->SetComputeRootSignature(d3dPipeline->getRootSignature());
    } else {
        m_commandList->SetGraphicsRootSignature(d3dPipeline->getRootSignature());
        m_commandList->IASetPrimitiveTopology(d3dPipeline->getTopology());
    }
    
    m_currentPipeline = d3dPipeline;
}

void D3D12CommandList::setViewport(const RHIViewport& viewport) {
    D3D12_VIEWPORT vp = {};
    vp.TopLeftX = viewport.x;
    vp.TopLeftY = viewport.y;
    vp.Width = viewport.width;
    vp.Height = viewport.height;
    vp.MinDepth = viewport.minDepth;
    vp.MaxDepth = viewport.maxDepth;
    m_commandList->RSSetViewports(1, &vp);
}

void D3D12CommandList::setViewports(const RHIViewport* viewports, uint32_t count) {
    std::vector<D3D12_VIEWPORT> d3dViewports(count);
    for (uint32_t i = 0; i < count; i++) {
        d3dViewports[i].TopLeftX = viewports[i].x;
        d3dViewports[i].TopLeftY = viewports[i].y;
        d3dViewports[i].Width = viewports[i].width;
        d3dViewports[i].Height = viewports[i].height;
        d3dViewports[i].MinDepth = viewports[i].minDepth;
        d3dViewports[i].MaxDepth = viewports[i].maxDepth;
    }
    m_commandList->RSSetViewports(static_cast<UINT>(count), d3dViewports.data());
}

void D3D12CommandList::setScissor(const RHIScissor& scissor) {
    D3D12_RECT rect = {};
    rect.left = static_cast<LONG>(scissor.x);
    rect.top = static_cast<LONG>(scissor.y);
    rect.right = static_cast<LONG>(scissor.x + scissor.width);
    rect.bottom = static_cast<LONG>(scissor.y + scissor.height);
    m_commandList->RSSetScissorRects(1, &rect);
}

void D3D12CommandList::setScissors(const RHIScissor* scissors, uint32_t count) {
    std::vector<D3D12_RECT> rects(count);
    for (uint32_t i = 0; i < count; i++) {
        rects[i].left = static_cast<LONG>(scissors[i].x);
        rects[i].top = static_cast<LONG>(scissors[i].y);
        rects[i].right = static_cast<LONG>(scissors[i].x + scissors[i].width);
        rects[i].bottom = static_cast<LONG>(scissors[i].y + scissors[i].height);
    }
    m_commandList->RSSetScissorRects(static_cast<UINT>(count), rects.data());
}

void D3D12CommandList::setBlendConstants(const float constants[4]) {
    m_commandList->OMSetBlendFactor(constants);
}

void D3D12CommandList::setStencilReference(uint32_t reference) {
    m_commandList->OMSetStencilRef(reference);
}

void D3D12CommandList::setDepthBias(float constantFactor, float clamp, float slopeFactor) {
    // D3D12 doesn't support dynamic depth bias - it's part of PSO
    // Would need to use different PSOs for different bias values
}

void D3D12CommandList::setLineWidth(float width) {
    // D3D12 doesn't support dynamic line width
}

// ============================================================================
// Resource Binding
// ============================================================================

void D3D12CommandList::setVertexBuffer(uint32_t slot, IRHIBuffer* buffer, uint64_t offset) {
    auto* d3dBuffer = static_cast<D3D12Buffer*>(buffer);
    
    D3D12_VERTEX_BUFFER_VIEW vbv = {};
    vbv.BufferLocation = d3dBuffer->getGPUAddress() + offset;
    vbv.SizeInBytes = static_cast<UINT>(d3dBuffer->getSize() - offset);
    vbv.StrideInBytes = 0; // Would need to get from somewhere
    
    m_commandList->IASetVertexBuffers(slot, 1, &vbv);
}

void D3D12CommandList::setVertexBuffers(uint32_t firstSlot, IRHIBuffer** buffers,
                                        const uint64_t* offsets, uint32_t count) {
    std::vector<D3D12_VERTEX_BUFFER_VIEW> vbvs(count);
    for (uint32_t i = 0; i < count; i++) {
        auto* d3dBuffer = static_cast<D3D12Buffer*>(buffers[i]);
        uint64_t offset = offsets ? offsets[i] : 0;
        
        vbvs[i].BufferLocation = d3dBuffer->getGPUAddress() + offset;
        vbvs[i].SizeInBytes = static_cast<UINT>(d3dBuffer->getSize() - offset);
        vbvs[i].StrideInBytes = 0;
    }
    m_commandList->IASetVertexBuffers(firstSlot, static_cast<UINT>(count), vbvs.data());
}

void D3D12CommandList::setIndexBuffer(IRHIBuffer* buffer, uint64_t offset, RHIIndexType indexType) {
    auto* d3dBuffer = static_cast<D3D12Buffer*>(buffer);
    
    D3D12_INDEX_BUFFER_VIEW ibv = {};
    ibv.BufferLocation = d3dBuffer->getGPUAddress() + offset;
    ibv.SizeInBytes = static_cast<UINT>(d3dBuffer->getSize() - offset);
    ibv.Format = ToDXGIIndexFormat(indexType);
    
    m_commandList->IASetIndexBuffer(&ibv);
}

void D3D12CommandList::pushConstants(RHIShaderStage stages, uint32_t offset,
                                     uint32_t size, const void* data) {
    UINT num32BitValues = (size + 3) / 4;
    
    if (m_currentPipeline && m_currentPipeline->getType() == RHIPipelineType::Compute) {
        m_commandList->SetComputeRoot32BitConstants(0, num32BitValues, data, offset / 4);
    } else {
        m_commandList->SetGraphicsRoot32BitConstants(0, num32BitValues, data, offset / 4);
    }
}

void D3D12CommandList::bindBuffer(uint32_t set, uint32_t binding, IRHIBuffer* buffer,
                                  uint64_t offset, uint64_t range) {
    // With bindless, resources are accessed via descriptor indices
    // The actual binding is done by setting the descriptor index in push constants
}

void D3D12CommandList::bindTexture(uint32_t set, uint32_t binding, IRHITexture* texture,
                                   IRHISampler* sampler) {
    // With bindless, resources are accessed via descriptor indices
}

void D3D12CommandList::bindStorageTexture(uint32_t set, uint32_t binding, IRHITexture* texture,
                                          uint32_t mipLevel) {
    // With bindless, resources are accessed via descriptor indices
}

void D3D12CommandList::bindSampler(uint32_t set, uint32_t binding, IRHISampler* sampler) {
    // With bindless, samplers are accessed via descriptor indices
}

void D3D12CommandList::bindAccelerationStructure(uint32_t set, uint32_t binding,
                                                 IRHIAccelerationStructure* as) {
    // With bindless, AS is accessed via descriptor indices or SRV
}

// ============================================================================
// Draw Commands
// ============================================================================

void D3D12CommandList::draw(uint32_t vertexCount, uint32_t instanceCount,
                            uint32_t firstVertex, uint32_t firstInstance) {
    m_commandList->DrawInstanced(vertexCount, instanceCount, firstVertex, firstInstance);
}

void D3D12CommandList::drawIndexed(uint32_t indexCount, uint32_t instanceCount,
                                   uint32_t firstIndex, int32_t vertexOffset,
                                   uint32_t firstInstance) {
    m_commandList->DrawIndexedInstanced(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void D3D12CommandList::drawIndirect(IRHIBuffer* buffer, uint64_t offset,
                                    uint32_t drawCount, uint32_t stride) {
    // TODO: Need command signature
    auto* d3dBuffer = static_cast<D3D12Buffer*>(buffer);
    (void)d3dBuffer;
    (void)offset;
    (void)drawCount;
    (void)stride;
}

void D3D12CommandList::drawIndexedIndirect(IRHIBuffer* buffer, uint64_t offset,
                                           uint32_t drawCount, uint32_t stride) {
    // TODO: Need command signature
}

void D3D12CommandList::drawIndirectCount(IRHIBuffer* argBuffer, uint64_t argOffset,
                                         IRHIBuffer* countBuffer, uint64_t countOffset,
                                         uint32_t maxDrawCount, uint32_t stride) {
    // TODO: Need command signature
}

void D3D12CommandList::drawIndexedIndirectCount(IRHIBuffer* argBuffer, uint64_t argOffset,
                                                IRHIBuffer* countBuffer, uint64_t countOffset,
                                                uint32_t maxDrawCount, uint32_t stride) {
    // TODO: Need command signature
}

// ============================================================================
// Mesh Shader Commands
// ============================================================================

void D3D12CommandList::dispatchMesh(uint32_t groupCountX, uint32_t groupCountY,
                                    uint32_t groupCountZ) {
    m_commandList->DispatchMesh(groupCountX, groupCountY, groupCountZ);
}

void D3D12CommandList::dispatchMeshIndirect(IRHIBuffer* buffer, uint64_t offset) {
    // TODO: Need command signature
}

void D3D12CommandList::dispatchMeshIndirectCount(IRHIBuffer* argBuffer, uint64_t argOffset,
                                                 IRHIBuffer* countBuffer, uint64_t countOffset,
                                                 uint32_t maxDispatchCount, uint32_t stride) {
    // TODO: Need command signature
}

// ============================================================================
// Compute Commands
// ============================================================================

void D3D12CommandList::dispatch(uint32_t groupCountX, uint32_t groupCountY,
                                uint32_t groupCountZ) {
    m_commandList->Dispatch(groupCountX, groupCountY, groupCountZ);
}

void D3D12CommandList::dispatchIndirect(IRHIBuffer* buffer, uint64_t offset) {
    // TODO: Need command signature
}

// ============================================================================
// Ray Tracing Commands
// ============================================================================

void D3D12CommandList::dispatchRays(const RHIDispatchRaysDesc& desc) {
    D3D12_DISPATCH_RAYS_DESC d3dDesc = {};
    
    d3dDesc.RayGenerationShaderRecord.StartAddress = desc.rayGenShaderTable.deviceAddress;
    d3dDesc.RayGenerationShaderRecord.SizeInBytes = desc.rayGenShaderTable.size;
    
    d3dDesc.MissShaderTable.StartAddress = desc.missShaderTable.deviceAddress;
    d3dDesc.MissShaderTable.SizeInBytes = desc.missShaderTable.size;
    d3dDesc.MissShaderTable.StrideInBytes = desc.missShaderTable.stride;
    
    d3dDesc.HitGroupTable.StartAddress = desc.hitShaderTable.deviceAddress;
    d3dDesc.HitGroupTable.SizeInBytes = desc.hitShaderTable.size;
    d3dDesc.HitGroupTable.StrideInBytes = desc.hitShaderTable.stride;
    
    d3dDesc.CallableShaderTable.StartAddress = desc.callableShaderTable.deviceAddress;
    d3dDesc.CallableShaderTable.SizeInBytes = desc.callableShaderTable.size;
    d3dDesc.CallableShaderTable.StrideInBytes = desc.callableShaderTable.stride;
    
    d3dDesc.Width = desc.width;
    d3dDesc.Height = desc.height;
    d3dDesc.Depth = desc.depth;
    
    m_commandList->DispatchRays(&d3dDesc);
}

void D3D12CommandList::buildAccelerationStructure(const RHIAccelerationStructureBuildInfo& info) {
    auto* dstAS = static_cast<D3D12AccelerationStructure*>(info.dstAccelerationStructure);
    auto* scratchBuffer = static_cast<D3D12Buffer*>(info.scratchBuffer);
    
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
    buildDesc.DestAccelerationStructureData = dstAS->getGPUAddress();
    buildDesc.ScratchAccelerationStructureData = scratchBuffer->getGPUAddress() + info.scratchBufferOffset;
    
    buildDesc.Inputs.Type = info.isTopLevel ?
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL :
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    buildDesc.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    buildDesc.Inputs.NumDescs = info.geometryCount;
    buildDesc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    
    // TODO: Build geometry descriptions
    
    m_commandList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);
}

void D3D12CommandList::copyAccelerationStructure(IRHIAccelerationStructure* dst,
                                                 IRHIAccelerationStructure* src, bool compact) {
    auto* dstAS = static_cast<D3D12AccelerationStructure*>(dst);
    auto* srcAS = static_cast<D3D12AccelerationStructure*>(src);
    
    m_commandList->CopyRaytracingAccelerationStructure(
        dstAS->getGPUAddress(),
        srcAS->getGPUAddress(),
        compact ? D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_COMPACT :
                  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_CLONE);
}

// ============================================================================
// Copy Commands
// ============================================================================

void D3D12CommandList::copyBuffer(IRHIBuffer* src, IRHIBuffer* dst,
                                  const RHIBufferCopy* regions, uint32_t regionCount) {
    auto* d3dSrc = static_cast<D3D12Buffer*>(src);
    auto* d3dDst = static_cast<D3D12Buffer*>(dst);
    
    for (uint32_t i = 0; i < regionCount; i++) {
        m_commandList->CopyBufferRegion(
            d3dDst->getResource(), regions[i].dstOffset,
            d3dSrc->getResource(), regions[i].srcOffset,
            regions[i].size);
    }
}

void D3D12CommandList::copyTexture(IRHITexture* src, IRHITexture* dst,
                                   const RHITextureCopy* regions, uint32_t regionCount) {
    auto* d3dSrc = static_cast<D3D12Texture*>(src);
    auto* d3dDst = static_cast<D3D12Texture*>(dst);
    
    for (uint32_t i = 0; i < regionCount; i++) {
        D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
        srcLoc.pResource = d3dSrc->getResource();
        srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcLoc.SubresourceIndex = regions[i].srcMipLevel +
            regions[i].srcArrayLayer * d3dSrc->getMipLevels();
        
        D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
        dstLoc.pResource = d3dDst->getResource();
        dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLoc.SubresourceIndex = regions[i].dstMipLevel +
            regions[i].dstArrayLayer * d3dDst->getMipLevels();
        
        D3D12_BOX box = {};
        box.left = regions[i].srcOffset.x;
        box.top = regions[i].srcOffset.y;
        box.front = regions[i].srcOffset.z;
        box.right = regions[i].srcOffset.x + regions[i].extent.width;
        box.bottom = regions[i].srcOffset.y + regions[i].extent.height;
        box.back = regions[i].srcOffset.z + regions[i].extent.depth;
        
        m_commandList->CopyTextureRegion(
            &dstLoc, regions[i].dstOffset.x, regions[i].dstOffset.y, regions[i].dstOffset.z,
            &srcLoc, &box);
    }
}

void D3D12CommandList::copyBufferToTexture(IRHIBuffer* src, IRHITexture* dst,
                                           const RHIBufferTextureCopy* regions,
                                           uint32_t regionCount) {
    auto* d3dSrc = static_cast<D3D12Buffer*>(src);
    auto* d3dDst = static_cast<D3D12Texture*>(dst);
    
    for (uint32_t i = 0; i < regionCount; i++) {
        D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
        dstLoc.pResource = d3dDst->getResource();
        dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLoc.SubresourceIndex = regions[i].mipLevel +
            regions[i].arrayLayer * d3dDst->getMipLevels();
        
        D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
        srcLoc.pResource = d3dSrc->getResource();
        srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        
        D3D12_RESOURCE_DESC resDesc = d3dDst->getResource()->GetDesc();
        m_rhi->getDevice()->GetCopyableFootprints(
            &resDesc, dstLoc.SubresourceIndex, 1, regions[i].bufferOffset,
            &srcLoc.PlacedFootprint, nullptr, nullptr, nullptr);
        
        m_commandList->CopyTextureRegion(
            &dstLoc, regions[i].textureOffset.x, regions[i].textureOffset.y, regions[i].textureOffset.z,
            &srcLoc, nullptr);
    }
}

void D3D12CommandList::copyTextureToBuffer(IRHITexture* src, IRHIBuffer* dst,
                                           const RHIBufferTextureCopy* regions,
                                           uint32_t regionCount) {
    auto* d3dSrc = static_cast<D3D12Texture*>(src);
    auto* d3dDst = static_cast<D3D12Buffer*>(dst);
    
    for (uint32_t i = 0; i < regionCount; i++) {
        D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
        srcLoc.pResource = d3dSrc->getResource();
        srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcLoc.SubresourceIndex = regions[i].mipLevel +
            regions[i].arrayLayer * d3dSrc->getMipLevels();
        
        D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
        dstLoc.pResource = d3dDst->getResource();
        dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        
        D3D12_RESOURCE_DESC resDesc = d3dSrc->getResource()->GetDesc();
        m_rhi->getDevice()->GetCopyableFootprints(
            &resDesc, srcLoc.SubresourceIndex, 1, regions[i].bufferOffset,
            &dstLoc.PlacedFootprint, nullptr, nullptr, nullptr);
        
        m_commandList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
    }
}

// ============================================================================
// Clear Commands
// ============================================================================

void D3D12CommandList::clearBuffer(IRHIBuffer* buffer, uint32_t value,
                                   uint64_t offset, uint64_t size) {
    // D3D12 doesn't have a direct buffer fill command
    // Would need to use a compute shader
}

void D3D12CommandList::clearTexture(IRHITexture* texture, const float color[4],
                                    uint32_t baseMip, uint32_t mipCount,
                                    uint32_t baseLayer, uint32_t layerCount) {
    auto* d3dTexture = static_cast<D3D12Texture*>(texture);
    if (d3dTexture->getRTV().ptr != 0) {
        m_commandList->ClearRenderTargetView(d3dTexture->getRTV(), color, 0, nullptr);
    }
}

void D3D12CommandList::clearDepthStencil(IRHITexture* texture, float depth, uint8_t stencil,
                                         bool clearDepth, bool clearStencil) {
    auto* d3dTexture = static_cast<D3D12Texture*>(texture);
    
    D3D12_CLEAR_FLAGS flags = {};
    if (clearDepth) flags |= D3D12_CLEAR_FLAG_DEPTH;
    if (clearStencil) flags |= D3D12_CLEAR_FLAG_STENCIL;
    
    if (d3dTexture->getDSV().ptr != 0) {
        m_commandList->ClearDepthStencilView(d3dTexture->getDSV(), flags, depth, stencil, 0, nullptr);
    }
}

// ============================================================================
// Query Commands
// ============================================================================

void D3D12CommandList::beginQuery(IRHIQueryPool* pool, uint32_t index) {
    auto* d3dPool = static_cast<D3D12QueryPool*>(pool);
    m_commandList->BeginQuery(d3dPool->getHeap(), d3dPool->getType(), index);
}

void D3D12CommandList::endQuery(IRHIQueryPool* pool, uint32_t index) {
    auto* d3dPool = static_cast<D3D12QueryPool*>(pool);
    m_commandList->EndQuery(d3dPool->getHeap(), d3dPool->getType(), index);
}

void D3D12CommandList::resetQueryPool(IRHIQueryPool* pool, uint32_t firstQuery, uint32_t count) {
    // D3D12 queries don't need explicit reset
}

void D3D12CommandList::writeTimestamp(IRHIQueryPool* pool, uint32_t index) {
    auto* d3dPool = static_cast<D3D12QueryPool*>(pool);
    m_commandList->EndQuery(d3dPool->getHeap(), D3D12_QUERY_TYPE_TIMESTAMP, index);
}

void D3D12CommandList::resolveQueryData(IRHIQueryPool* pool, uint32_t firstQuery, uint32_t count,
                                        IRHIBuffer* destination, uint64_t offset) {
    auto* d3dPool = static_cast<D3D12QueryPool*>(pool);
    auto* d3dDest = static_cast<D3D12Buffer*>(destination);
    
    m_commandList->ResolveQueryData(
        d3dPool->getHeap(),
        d3dPool->getType(),
        firstQuery,
        count,
        d3dDest->getResource(),
        offset);
}

// ============================================================================
// Debug Markers
// ============================================================================

void D3D12CommandList::beginDebugLabel(const char* name, const glm::vec4& color) {
#ifdef USE_PIX
    PIXBeginEvent(m_commandList.Get(), PIX_COLOR(
        static_cast<BYTE>(color.r * 255),
        static_cast<BYTE>(color.g * 255),
        static_cast<BYTE>(color.b * 255)), name);
#else
    (void)name;
    (void)color;
#endif
}

void D3D12CommandList::endDebugLabel() {
#ifdef USE_PIX
    PIXEndEvent(m_commandList.Get());
#endif
}

void D3D12CommandList::insertDebugLabel(const char* name, const glm::vec4& color) {
#ifdef USE_PIX
    PIXSetMarker(m_commandList.Get(), PIX_COLOR(
        static_cast<BYTE>(color.r * 255),
        static_cast<BYTE>(color.g * 255),
        static_cast<BYTE>(color.b * 255)), name);
#else
    (void)name;
    (void)color;
#endif
}

// ============================================================================
// Miscellaneous
// ============================================================================

void D3D12CommandList::fillBuffer(IRHIBuffer* buffer, uint64_t offset,
                                  uint64_t size, uint32_t data) {
    // Would need compute shader
}

void D3D12CommandList::updateBuffer(IRHIBuffer* buffer, uint64_t offset,
                                    uint64_t size, const void* data) {
    // Would need staging buffer
}

void D3D12CommandList::generateMipmaps(IRHITexture* texture) {
    // Would need compute shader
}

void D3D12CommandList::resolveTexture(IRHITexture* src, IRHITexture* dst,
                                      uint32_t srcMip, uint32_t srcLayer,
                                      uint32_t dstMip, uint32_t dstLayer) {
    auto* d3dSrc = static_cast<D3D12Texture*>(src);
    auto* d3dDst = static_cast<D3D12Texture*>(dst);
    
    uint32_t srcSubresource = srcMip + srcLayer * d3dSrc->getMipLevels();
    uint32_t dstSubresource = dstMip + dstLayer * d3dDst->getMipLevels();
    
    m_commandList->ResolveSubresource(
        d3dDst->getResource(), dstSubresource,
        d3dSrc->getResource(), srcSubresource,
        ToDXGIFormat(d3dDst->getFormat()));
}

} // namespace Sanic

#endif // SANIC_ENABLE_D3D12
