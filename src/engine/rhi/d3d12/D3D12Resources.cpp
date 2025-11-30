#include "D3D12RHI.h"

#ifdef SANIC_ENABLE_D3D12

#include "../../core/Log.h"

namespace Sanic {

// ============================================================================
// D3D12Buffer Implementation
// ============================================================================

D3D12Buffer::D3D12Buffer(D3D12RHI* rhi, const RHIBufferDesc& desc)
    : m_rhi(rhi)
    , m_desc(desc) {
    
    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Alignment = 0;
    resourceDesc.Width = desc.size;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.SampleDesc.Quality = 0;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    
    if (hasFlag(desc.usage, RHIBufferUsage::Storage)) {
        resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }
    
    D3D12MA::ALLOCATION_DESC allocDesc = {};
    allocDesc.HeapType = ToD3D12HeapType(desc.memoryType);
    
    D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;
    if (desc.memoryType == RHIMemoryType::Upload) {
        initialState = D3D12_RESOURCE_STATE_GENERIC_READ;
    } else if (desc.memoryType == RHIMemoryType::Readback) {
        initialState = D3D12_RESOURCE_STATE_COPY_DEST;
    }
    
    HRESULT hr = m_rhi->getAllocator()->CreateResource(
        &allocDesc,
        &resourceDesc,
        initialState,
        nullptr,
        &m_allocation,
        IID_PPV_ARGS(&m_resource));
    
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create D3D12 buffer");
        return;
    }
    
    // Persistently map upload/readback buffers
    if (desc.memoryType == RHIMemoryType::Upload || desc.memoryType == RHIMemoryType::Readback) {
        D3D12_RANGE readRange = {};
        if (desc.memoryType == RHIMemoryType::Upload) {
            readRange.Begin = 0;
            readRange.End = 0;
        }
        m_resource->Map(0, &readRange, &m_mappedPtr);
    }
    
    if (!desc.name.empty()) {
        std::wstring wname(desc.name.begin(), desc.name.end());
        m_resource->SetName(wname.c_str());
    }
}

D3D12Buffer::~D3D12Buffer() {
    if (m_mappedPtr && m_resource) {
        m_resource->Unmap(0, nullptr);
        m_mappedPtr = nullptr;
    }
}

void* D3D12Buffer::map() {
    if (m_mappedPtr) {
        return m_mappedPtr;
    }
    
    D3D12_RANGE readRange = { 0, 0 };
    HRESULT hr = m_resource->Map(0, &readRange, &m_mappedPtr);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to map D3D12 buffer");
        return nullptr;
    }
    return m_mappedPtr;
}

void D3D12Buffer::unmap() {
    if (m_mappedPtr && m_desc.memoryType != RHIMemoryType::Upload &&
        m_desc.memoryType != RHIMemoryType::Readback) {
        m_resource->Unmap(0, nullptr);
        m_mappedPtr = nullptr;
    }
}

uint64_t D3D12Buffer::getGPUAddress() const {
    return m_resource ? m_resource->GetGPUVirtualAddress() : 0;
}

// ============================================================================
// D3D12Texture Implementation
// ============================================================================

D3D12Texture::D3D12Texture(D3D12RHI* rhi, const RHITextureDesc& desc)
    : m_rhi(rhi)
    , m_desc(desc)
    , m_ownsResource(true) {
    
    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = ToD3D12ResourceDimension(desc.dimension);
    resourceDesc.Alignment = 0;
    resourceDesc.Width = desc.width;
    resourceDesc.Height = desc.height;
    resourceDesc.DepthOrArraySize = desc.dimension == RHITextureDimension::Texture3D ?
        desc.depth : desc.arrayLayers;
    resourceDesc.MipLevels = desc.mipLevels;
    resourceDesc.Format = ToDXGIFormat(desc.format);
    resourceDesc.SampleDesc.Count = static_cast<UINT>(desc.sampleCount);
    resourceDesc.SampleDesc.Quality = 0;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    
    if (hasFlag(desc.usage, RHITextureUsage::RenderTarget)) {
        resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    }
    if (hasFlag(desc.usage, RHITextureUsage::DepthStencil)) {
        resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    }
    if (hasFlag(desc.usage, RHITextureUsage::Storage)) {
        resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }
    
    D3D12MA::ALLOCATION_DESC allocDesc = {};
    allocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
    
    D3D12_CLEAR_VALUE clearValue = {};
    D3D12_CLEAR_VALUE* pClearValue = nullptr;
    
    if (hasFlag(desc.usage, RHITextureUsage::RenderTarget)) {
        clearValue.Format = resourceDesc.Format;
        clearValue.Color[0] = 0.0f;
        clearValue.Color[1] = 0.0f;
        clearValue.Color[2] = 0.0f;
        clearValue.Color[3] = 1.0f;
        pClearValue = &clearValue;
    } else if (hasFlag(desc.usage, RHITextureUsage::DepthStencil)) {
        clearValue.Format = resourceDesc.Format;
        clearValue.DepthStencil.Depth = 1.0f;
        clearValue.DepthStencil.Stencil = 0;
        pClearValue = &clearValue;
    }
    
    HRESULT hr = m_rhi->getAllocator()->CreateResource(
        &allocDesc,
        &resourceDesc,
        D3D12_RESOURCE_STATE_COMMON,
        pClearValue,
        &m_allocation,
        IID_PPV_ARGS(&m_resource));
    
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create D3D12 texture");
        return;
    }
    
    createViews();
    
    if (!desc.name.empty()) {
        std::wstring wname(desc.name.begin(), desc.name.end());
        m_resource->SetName(wname.c_str());
    }
}

D3D12Texture::D3D12Texture(D3D12RHI* rhi, ID3D12Resource* resource, const RHITextureDesc& desc)
    : m_rhi(rhi)
    , m_desc(desc)
    , m_ownsResource(false) {
    m_resource = resource;
    createViews();
}

D3D12Texture::~D3D12Texture() {
    // Free descriptor heap indices
    if (m_srvIndex != UINT32_MAX) {
        m_rhi->getCBVSRVUAVHeap()->free(m_srvIndex);
    }
    if (m_uavIndex != UINT32_MAX) {
        m_rhi->getCBVSRVUAVHeap()->free(m_uavIndex);
    }
    if (m_rtvIndex != UINT32_MAX) {
        m_rhi->getRTVHeap()->free(m_rtvIndex);
    }
    if (m_dsvIndex != UINT32_MAX) {
        m_rhi->getDSVHeap()->free(m_dsvIndex);
    }
    
    // Only release allocation if we own the resource
    if (m_ownsResource) {
        m_allocation.Reset();
    } else {
        m_resource.Detach(); // Don't release swapchain resources
    }
}

void D3D12Texture::createViews() {
    auto* device = m_rhi->getDevice();
    
    // SRV
    if (hasFlag(m_desc.usage, RHITextureUsage::Sampled)) {
        m_srvIndex = m_rhi->getCBVSRVUAVHeap()->allocate();
        if (m_srvIndex != UINT32_MAX) {
            m_srvHandle = m_rhi->getCBVSRVUAVHeap()->getCPUHandle(m_srvIndex);
            
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = ToDXGIFormat(m_desc.format);
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            
            switch (m_desc.dimension) {
                case RHITextureDimension::Texture1D:
                    if (m_desc.arrayLayers > 1) {
                        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
                        srvDesc.Texture1DArray.MipLevels = m_desc.mipLevels;
                        srvDesc.Texture1DArray.ArraySize = m_desc.arrayLayers;
                    } else {
                        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
                        srvDesc.Texture1D.MipLevels = m_desc.mipLevels;
                    }
                    break;
                    
                case RHITextureDimension::Texture2D:
                    if (m_desc.arrayLayers > 1) {
                        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
                        srvDesc.Texture2DArray.MipLevels = m_desc.mipLevels;
                        srvDesc.Texture2DArray.ArraySize = m_desc.arrayLayers;
                    } else {
                        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                        srvDesc.Texture2D.MipLevels = m_desc.mipLevels;
                    }
                    break;
                    
                case RHITextureDimension::Texture3D:
                    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
                    srvDesc.Texture3D.MipLevels = m_desc.mipLevels;
                    break;
                    
                case RHITextureDimension::TextureCube:
                    if (m_desc.arrayLayers > 6) {
                        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
                        srvDesc.TextureCubeArray.MipLevels = m_desc.mipLevels;
                        srvDesc.TextureCubeArray.NumCubes = m_desc.arrayLayers / 6;
                    } else {
                        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
                        srvDesc.TextureCube.MipLevels = m_desc.mipLevels;
                    }
                    break;
            }
            
            device->CreateShaderResourceView(m_resource.Get(), &srvDesc, m_srvHandle);
        }
    }
    
    // UAV
    if (hasFlag(m_desc.usage, RHITextureUsage::Storage)) {
        m_uavIndex = m_rhi->getCBVSRVUAVHeap()->allocate();
        if (m_uavIndex != UINT32_MAX) {
            m_uavHandle = m_rhi->getCBVSRVUAVHeap()->getCPUHandle(m_uavIndex);
            
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
            uavDesc.Format = ToDXGIFormat(m_desc.format);
            
            switch (m_desc.dimension) {
                case RHITextureDimension::Texture1D:
                    if (m_desc.arrayLayers > 1) {
                        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
                        uavDesc.Texture1DArray.ArraySize = m_desc.arrayLayers;
                    } else {
                        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
                    }
                    break;
                    
                case RHITextureDimension::Texture2D:
                case RHITextureDimension::TextureCube:
                    if (m_desc.arrayLayers > 1) {
                        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
                        uavDesc.Texture2DArray.ArraySize = m_desc.arrayLayers;
                    } else {
                        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                    }
                    break;
                    
                case RHITextureDimension::Texture3D:
                    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
                    uavDesc.Texture3D.WSize = m_desc.depth;
                    break;
            }
            
            device->CreateUnorderedAccessView(m_resource.Get(), nullptr, &uavDesc, m_uavHandle);
        }
    }
    
    // RTV
    if (hasFlag(m_desc.usage, RHITextureUsage::RenderTarget)) {
        m_rtvIndex = m_rhi->getRTVHeap()->allocate();
        if (m_rtvIndex != UINT32_MAX) {
            m_rtvHandle = m_rhi->getRTVHeap()->getCPUHandle(m_rtvIndex);
            device->CreateRenderTargetView(m_resource.Get(), nullptr, m_rtvHandle);
        }
    }
    
    // DSV
    if (hasFlag(m_desc.usage, RHITextureUsage::DepthStencil)) {
        m_dsvIndex = m_rhi->getDSVHeap()->allocate();
        if (m_dsvIndex != UINT32_MAX) {
            m_dsvHandle = m_rhi->getDSVHeap()->getCPUHandle(m_dsvIndex);
            device->CreateDepthStencilView(m_resource.Get(), nullptr, m_dsvHandle);
        }
    }
}

// ============================================================================
// D3D12TextureView Implementation
// ============================================================================

D3D12TextureView::D3D12TextureView(D3D12RHI* rhi, D3D12Texture* texture, RHIFormat format,
                                   uint32_t baseMip, uint32_t mipCount,
                                   uint32_t baseLayer, uint32_t layerCount)
    : m_rhi(rhi)
    , m_texture(texture)
    , m_format(format)
    , m_baseMip(baseMip)
    , m_mipCount(mipCount)
    , m_baseLayer(baseLayer)
    , m_layerCount(layerCount) {
    
    m_srvIndex = m_rhi->getCBVSRVUAVHeap()->allocate();
    if (m_srvIndex != UINT32_MAX) {
        m_srvHandle = m_rhi->getCBVSRVUAVHeap()->getCPUHandle(m_srvIndex);
        
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = ToDXGIFormat(format);
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        
        auto dim = texture->getDimension();
        switch (dim) {
            case RHITextureDimension::Texture1D:
                if (layerCount > 1) {
                    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
                    srvDesc.Texture1DArray.MostDetailedMip = baseMip;
                    srvDesc.Texture1DArray.MipLevels = mipCount;
                    srvDesc.Texture1DArray.FirstArraySlice = baseLayer;
                    srvDesc.Texture1DArray.ArraySize = layerCount;
                } else {
                    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
                    srvDesc.Texture1D.MostDetailedMip = baseMip;
                    srvDesc.Texture1D.MipLevels = mipCount;
                }
                break;
                
            case RHITextureDimension::Texture2D:
                if (layerCount > 1) {
                    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
                    srvDesc.Texture2DArray.MostDetailedMip = baseMip;
                    srvDesc.Texture2DArray.MipLevels = mipCount;
                    srvDesc.Texture2DArray.FirstArraySlice = baseLayer;
                    srvDesc.Texture2DArray.ArraySize = layerCount;
                } else {
                    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                    srvDesc.Texture2D.MostDetailedMip = baseMip;
                    srvDesc.Texture2D.MipLevels = mipCount;
                }
                break;
                
            case RHITextureDimension::Texture3D:
                srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
                srvDesc.Texture3D.MostDetailedMip = baseMip;
                srvDesc.Texture3D.MipLevels = mipCount;
                break;
                
            case RHITextureDimension::TextureCube:
                if (layerCount > 6) {
                    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
                    srvDesc.TextureCubeArray.MostDetailedMip = baseMip;
                    srvDesc.TextureCubeArray.MipLevels = mipCount;
                    srvDesc.TextureCubeArray.First2DArrayFace = baseLayer;
                    srvDesc.TextureCubeArray.NumCubes = layerCount / 6;
                } else {
                    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
                    srvDesc.TextureCube.MostDetailedMip = baseMip;
                    srvDesc.TextureCube.MipLevels = mipCount;
                }
                break;
        }
        
        m_rhi->getDevice()->CreateShaderResourceView(
            texture->getResource(), &srvDesc, m_srvHandle);
    }
}

D3D12TextureView::~D3D12TextureView() {
    if (m_srvIndex != UINT32_MAX) {
        m_rhi->getCBVSRVUAVHeap()->free(m_srvIndex);
    }
}

// ============================================================================
// D3D12Sampler Implementation
// ============================================================================

D3D12Sampler::D3D12Sampler(D3D12RHI* rhi, const RHISamplerDesc& desc)
    : m_rhi(rhi) {
    
    m_index = m_rhi->getSamplerHeap()->allocate();
    if (m_index != UINT32_MAX) {
        m_handle = m_rhi->getSamplerHeap()->getCPUHandle(m_index);
        
        D3D12_SAMPLER_DESC samplerDesc = {};
        samplerDesc.Filter = ToD3D12Filter(desc.minFilter, desc.magFilter, desc.mipFilter,
            desc.compareEnable, desc.maxAnisotropy > 1.0f);
        samplerDesc.AddressU = ToD3D12AddressMode(desc.addressU);
        samplerDesc.AddressV = ToD3D12AddressMode(desc.addressV);
        samplerDesc.AddressW = ToD3D12AddressMode(desc.addressW);
        samplerDesc.MipLODBias = desc.mipLodBias;
        samplerDesc.MaxAnisotropy = static_cast<UINT>(desc.maxAnisotropy);
        samplerDesc.ComparisonFunc = ToD3D12CompareFunc(desc.compareOp);
        samplerDesc.BorderColor[0] = 0.0f;
        samplerDesc.BorderColor[1] = 0.0f;
        samplerDesc.BorderColor[2] = 0.0f;
        samplerDesc.BorderColor[3] = 1.0f;
        samplerDesc.MinLOD = desc.minLod;
        samplerDesc.MaxLOD = desc.maxLod;
        
        m_rhi->getDevice()->CreateSampler(&samplerDesc, m_handle);
    }
}

D3D12Sampler::~D3D12Sampler() {
    if (m_index != UINT32_MAX) {
        m_rhi->getSamplerHeap()->free(m_index);
    }
}

// ============================================================================
// D3D12Pipeline Implementation
// ============================================================================

D3D12Pipeline::D3D12Pipeline(D3D12RHI* rhi, RHIPipelineType type)
    : m_rhi(rhi)
    , m_type(type) {
}

D3D12Pipeline::~D3D12Pipeline() {
    // ComPtr handles cleanup
}

// ============================================================================
// D3D12Fence Implementation
// ============================================================================

D3D12Fence::D3D12Fence(D3D12RHI* rhi, bool signaled)
    : m_rhi(rhi)
    , m_value(signaled ? 1 : 0) {
    
    HRESULT hr = m_rhi->getDevice()->CreateFence(
        m_value, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
    
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create D3D12 fence");
        return;
    }
    
    m_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

D3D12Fence::~D3D12Fence() {
    if (m_event) {
        CloseHandle(m_event);
        m_event = nullptr;
    }
}

void D3D12Fence::wait(uint64_t timeout) {
    if (m_fence->GetCompletedValue() < m_value) {
        m_fence->SetEventOnCompletion(m_value, m_event);
        WaitForSingleObject(m_event, timeout == UINT64_MAX ? INFINITE : 
            static_cast<DWORD>(timeout / 1000000));
    }
}

void D3D12Fence::reset() {
    m_value = 0;
}

bool D3D12Fence::isSignaled() const {
    return m_fence->GetCompletedValue() >= m_value;
}

void D3D12Fence::signal(uint64_t value) {
    m_value = value;
}

uint64_t D3D12Fence::getCompletedValue() const {
    return m_fence->GetCompletedValue();
}

void D3D12Fence::setEventOnCompletion(uint64_t value) {
    m_fence->SetEventOnCompletion(value, m_event);
}

// ============================================================================
// D3D12Semaphore Implementation
// ============================================================================

D3D12Semaphore::D3D12Semaphore(D3D12RHI* rhi)
    : m_rhi(rhi)
    , m_value(0) {
    
    HRESULT hr = m_rhi->getDevice()->CreateFence(
        0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
    
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create D3D12 semaphore fence");
    }
}

D3D12Semaphore::~D3D12Semaphore() {
    // ComPtr handles cleanup
}

// ============================================================================
// D3D12QueryPool Implementation
// ============================================================================

D3D12QueryPool::D3D12QueryPool(D3D12RHI* rhi, D3D12_QUERY_TYPE type, uint32_t count)
    : m_rhi(rhi)
    , m_type(type)
    , m_count(count) {
    
    D3D12_QUERY_HEAP_DESC heapDesc = {};
    heapDesc.Type = (type == D3D12_QUERY_TYPE_TIMESTAMP) ? 
        D3D12_QUERY_HEAP_TYPE_TIMESTAMP :
        (type == D3D12_QUERY_TYPE_OCCLUSION) ?
            D3D12_QUERY_HEAP_TYPE_OCCLUSION :
            D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS;
    heapDesc.Count = count;
    heapDesc.NodeMask = 0;
    
    HRESULT hr = m_rhi->getDevice()->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(&m_heap));
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create D3D12 query heap");
        return;
    }
    
    // Create result buffer
    uint64_t querySize = (type == D3D12_QUERY_TYPE_PIPELINE_STATISTICS) ?
        sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS) : sizeof(uint64_t);
    
    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = count * querySize;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_READBACK;
    
    hr = m_rhi->getDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&m_resultBuffer));
    
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create D3D12 query result buffer");
    }
}

D3D12QueryPool::~D3D12QueryPool() {
    // ComPtrs handle cleanup
}

bool D3D12QueryPool::getResults(uint32_t firstQuery, uint32_t queryCount,
                                void* data, size_t dataSize,
                                size_t stride, bool wait) {
    if (!m_resultBuffer) return false;
    
    // Map and read results
    void* mappedData = nullptr;
    D3D12_RANGE readRange = { 0, dataSize };
    
    HRESULT hr = m_resultBuffer->Map(0, &readRange, &mappedData);
    if (FAILED(hr)) {
        return false;
    }
    
    memcpy(data, mappedData, dataSize);
    
    D3D12_RANGE writeRange = { 0, 0 };
    m_resultBuffer->Unmap(0, &writeRange);
    
    return true;
}

// ============================================================================
// D3D12AccelerationStructure Implementation
// ============================================================================

D3D12AccelerationStructure::D3D12AccelerationStructure(D3D12RHI* rhi, bool isTopLevel, uint64_t size)
    : m_rhi(rhi)
    , m_isTopLevel(isTopLevel)
    , m_size(size) {
    
    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Width = size;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    
    D3D12MA::ALLOCATION_DESC allocDesc = {};
    allocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
    
    HRESULT hr = m_rhi->getAllocator()->CreateResource(
        &allocDesc,
        &resourceDesc,
        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
        nullptr,
        &m_allocation,
        IID_PPV_ARGS(&m_resource));
    
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create D3D12 acceleration structure");
    }
}

D3D12AccelerationStructure::~D3D12AccelerationStructure() {
    // ComPtrs handle cleanup
}

uint64_t D3D12AccelerationStructure::getGPUAddress() const {
    return m_resource ? m_resource->GetGPUVirtualAddress() : 0;
}

} // namespace Sanic

#endif // SANIC_ENABLE_D3D12
