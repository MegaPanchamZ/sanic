#include "D3D12RHI.h"

#ifdef SANIC_ENABLE_D3D12

#include "../../core/Log.h"
#include "../../core/Window.h"

#include <stdexcept>
#include <algorithm>

// D3D12MA implementation
#define D3D12MA_IMPLEMENTATION
#include <D3D12MemAlloc.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

namespace Sanic {

// ============================================================================
// Helper Macros
// ============================================================================

#define D3D12_CHECK(x) \
    do { \
        HRESULT hr = x; \
        if (FAILED(hr)) { \
            LOG_ERROR("D3D12 error: HRESULT = 0x{:08X}", static_cast<uint32_t>(hr)); \
            return false; \
        } \
    } while (0)

#define D3D12_CHECK_VOID(x) \
    do { \
        HRESULT hr = x; \
        if (FAILED(hr)) { \
            LOG_ERROR("D3D12 error: HRESULT = 0x{:08X}", static_cast<uint32_t>(hr)); \
        } \
    } while (0)

// ============================================================================
// D3D12DescriptorHeap Implementation
// ============================================================================

D3D12DescriptorHeap::D3D12DescriptorHeap(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type,
                                         uint32_t numDescriptors, bool shaderVisible) 
    : m_numDescriptors(numDescriptors)
    , m_allocated(numDescriptors, false) {
    
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = type;
    desc.NumDescriptors = numDescriptors;
    desc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    desc.NodeMask = 0;
    
    HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_heap));
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create D3D12 descriptor heap");
        return;
    }
    
    m_cpuStart = m_heap->GetCPUDescriptorHandleForHeapStart();
    if (shaderVisible) {
        m_gpuStart = m_heap->GetGPUDescriptorHandleForHeapStart();
    }
    m_descriptorSize = device->GetDescriptorHandleIncrementSize(type);
}

uint32_t D3D12DescriptorHeap::allocate(uint32_t count) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Simple linear search for contiguous block
    for (uint32_t i = m_searchStart; i + count <= m_numDescriptors; i++) {
        bool found = true;
        for (uint32_t j = 0; j < count; j++) {
            if (m_allocated[i + j]) {
                found = false;
                break;
            }
        }
        if (found) {
            for (uint32_t j = 0; j < count; j++) {
                m_allocated[i + j] = true;
            }
            m_searchStart = i + count;
            return i;
        }
    }
    
    // Wrap around search
    for (uint32_t i = 0; i < m_searchStart && i + count <= m_numDescriptors; i++) {
        bool found = true;
        for (uint32_t j = 0; j < count; j++) {
            if (m_allocated[i + j]) {
                found = false;
                break;
            }
        }
        if (found) {
            for (uint32_t j = 0; j < count; j++) {
                m_allocated[i + j] = true;
            }
            m_searchStart = i + count;
            return i;
        }
    }
    
    LOG_ERROR("D3D12 descriptor heap exhausted");
    return UINT32_MAX;
}

void D3D12DescriptorHeap::free(uint32_t index, uint32_t count) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (uint32_t i = 0; i < count && (index + i) < m_numDescriptors; i++) {
        m_allocated[index + i] = false;
    }
    if (index < m_searchStart) {
        m_searchStart = index;
    }
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12DescriptorHeap::getCPUHandle(uint32_t index) const {
    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_cpuStart;
    handle.ptr += static_cast<SIZE_T>(index) * m_descriptorSize;
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE D3D12DescriptorHeap::getGPUHandle(uint32_t index) const {
    D3D12_GPU_DESCRIPTOR_HANDLE handle = m_gpuStart;
    handle.ptr += static_cast<UINT64>(index) * m_descriptorSize;
    return handle;
}

// ============================================================================
// D3D12RHI Implementation
// ============================================================================

D3D12RHI::D3D12RHI() = default;

D3D12RHI::~D3D12RHI() {
    shutdown();
}

bool D3D12RHI::initialize(Window& window, const RHIConfig& config) {
    m_config = config;
    m_window = &window;
    
    if (!createDevice(config)) return false;
    if (!createQueues()) return false;
    if (!createDescriptorHeaps()) return false;
    if (!createSwapchain(window)) return false;
    
    // Create frame fence
    HRESULT hr = m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_frameFence));
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create frame fence");
        return false;
    }
    
    m_frameFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_frameFenceEvent) {
        LOG_ERROR("Failed to create fence event");
        return false;
    }
    
    queryCapabilities();
    
    LOG_INFO("D3D12 RHI initialized successfully");
    return true;
}

void D3D12RHI::shutdown() {
    waitIdle();
    
    if (m_frameFenceEvent) {
        CloseHandle(m_frameFenceEvent);
        m_frameFenceEvent = nullptr;
    }
    
    m_backBuffers.clear();
    m_swapchain.Reset();
    
    m_cbvSrvUavHeap.reset();
    m_samplerHeap.reset();
    m_rtvHeap.reset();
    m_dsvHeap.reset();
    
    if (m_allocator) {
        m_allocator.Reset();
    }
    
    m_copyQueue.Reset();
    m_computeQueue.Reset();
    m_graphicsQueue.Reset();
    
    m_device5.Reset();
    m_device.Reset();
    m_adapter.Reset();
    m_factory.Reset();
    
    m_infoQueue.Reset();
    m_debugInterface.Reset();
    
    LOG_INFO("D3D12 RHI shutdown complete");
}

bool D3D12RHI::createDevice(const RHIConfig& config) {
    UINT dxgiFactoryFlags = 0;
    
    // Enable debug layer
    if (config.enableValidation) {
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&m_debugInterface)))) {
            m_debugInterface->EnableDebugLayer();
            m_debugInterface->SetEnableGPUBasedValidation(TRUE);
            dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
            LOG_INFO("D3D12 debug layer enabled");
        }
    }
    
    // Create factory
    D3D12_CHECK(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&m_factory)));
    
    // Find adapter
    ComPtr<IDXGIAdapter1> adapter1;
    for (UINT i = 0; m_factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                           IID_PPV_ARGS(&adapter1)) != DXGI_ERROR_NOT_FOUND; i++) {
        DXGI_ADAPTER_DESC1 desc;
        adapter1->GetDesc1(&desc);
        
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        
        if (SUCCEEDED(D3D12CreateDevice(adapter1.Get(), D3D_FEATURE_LEVEL_12_0,
                                        __uuidof(ID3D12Device), nullptr))) {
            adapter1.As(&m_adapter);
            
            char adapterName[256];
            WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, adapterName, sizeof(adapterName), nullptr, nullptr);
            LOG_INFO("Selected GPU: {}", adapterName);
            break;
        }
    }
    
    if (!m_adapter) {
        LOG_ERROR("No suitable D3D12 adapter found");
        return false;
    }
    
    // Create device
    D3D12_CHECK(D3D12CreateDevice(m_adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_device)));
    
    // Try to get Device5 for ray tracing
    m_device.As(&m_device5);
    
    // Configure info queue
    if (config.enableValidation) {
        if (SUCCEEDED(m_device.As(&m_infoQueue))) {
            m_infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
            m_infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
            
            D3D12_MESSAGE_ID suppressIds[] = {
                D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
                D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE,
            };
            
            D3D12_INFO_QUEUE_FILTER filter = {};
            filter.DenyList.NumIDs = _countof(suppressIds);
            filter.DenyList.pIDList = suppressIds;
            m_infoQueue->PushStorageFilter(&filter);
        }
    }
    
    // Create allocator
    D3D12MA::ALLOCATOR_DESC allocDesc = {};
    allocDesc.pDevice = m_device.Get();
    allocDesc.pAdapter = m_adapter.Get();
    allocDesc.Flags = D3D12MA::ALLOCATOR_FLAG_NONE;
    
    D3D12_CHECK(D3D12MA::CreateAllocator(&allocDesc, &m_allocator));
    
    return true;
}

bool D3D12RHI::createQueues() {
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.NodeMask = 0;
    
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    D3D12_CHECK(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_graphicsQueue)));
    
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    D3D12_CHECK(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_computeQueue)));
    
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
    D3D12_CHECK(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_copyQueue)));
    
    return true;
}

bool D3D12RHI::createSwapchain(Window& window) {
    HWND hwnd = static_cast<HWND>(window.getNativeHandle());
    m_swapchainWidth = window.getWidth();
    m_swapchainHeight = window.getHeight();
    
    DXGI_SWAP_CHAIN_DESC1 swapchainDesc = {};
    swapchainDesc.Width = m_swapchainWidth;
    swapchainDesc.Height = m_swapchainHeight;
    swapchainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapchainDesc.Stereo = FALSE;
    swapchainDesc.SampleDesc.Count = 1;
    swapchainDesc.SampleDesc.Quality = 0;
    swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapchainDesc.BufferCount = MAX_FRAMES_IN_FLIGHT;
    swapchainDesc.Scaling = DXGI_SCALING_STRETCH;
    swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapchainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    swapchainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    
    ComPtr<IDXGISwapChain1> swapchain1;
    D3D12_CHECK(m_factory->CreateSwapChainForHwnd(
        m_graphicsQueue.Get(), hwnd, &swapchainDesc,
        nullptr, nullptr, &swapchain1));
    
    m_factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    swapchain1.As(&m_swapchain);
    
    m_backBufferFormat = RHIFormat::R8G8B8A8_UNORM;
    
    // Get back buffers
    m_backBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    for (UINT i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        ComPtr<ID3D12Resource> resource;
        D3D12_CHECK(m_swapchain->GetBuffer(i, IID_PPV_ARGS(&resource)));
        
        RHITextureDesc desc{};
        desc.width = m_swapchainWidth;
        desc.height = m_swapchainHeight;
        desc.depth = 1;
        desc.mipLevels = 1;
        desc.arrayLayers = 1;
        desc.format = m_backBufferFormat;
        desc.usage = RHITextureUsage::RenderTarget;
        desc.dimension = RHITextureDimension::Texture2D;
        desc.sampleCount = RHISampleCount::Count1;
        
        m_backBuffers[i] = std::make_unique<D3D12Texture>(this, resource.Get(), desc);
    }
    
    m_currentBackBufferIndex = m_swapchain->GetCurrentBackBufferIndex();
    
    return true;
}

bool D3D12RHI::createDescriptorHeaps() {
    m_cbvSrvUavHeap = std::make_unique<D3D12DescriptorHeap>(
        m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1000000, true);
    
    m_samplerHeap = std::make_unique<D3D12DescriptorHeap>(
        m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 2048, true);
    
    m_rtvHeap = std::make_unique<D3D12DescriptorHeap>(
        m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1024, false);
    
    m_dsvHeap = std::make_unique<D3D12DescriptorHeap>(
        m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 256, false);
    
    return true;
}

void D3D12RHI::queryCapabilities() {
    DXGI_ADAPTER_DESC1 adapterDesc;
    m_adapter->GetDesc1(&adapterDesc);
    
    D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
    m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
    
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
    m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5));
    
    D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7 = {};
    m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7));
    
    m_capabilities.maxTextureSize = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
    m_capabilities.maxCubeMapSize = D3D12_REQ_TEXTURECUBE_DIMENSION;
    m_capabilities.maxRenderTargets = 8;
    m_capabilities.maxComputeWorkGroupSize[0] = D3D12_CS_THREAD_GROUP_MAX_X;
    m_capabilities.maxComputeWorkGroupSize[1] = D3D12_CS_THREAD_GROUP_MAX_Y;
    m_capabilities.maxComputeWorkGroupSize[2] = D3D12_CS_THREAD_GROUP_MAX_Z;
    m_capabilities.maxAnisotropy = 16.0f;
    
    m_capabilities.rayTracing = options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_0;
    m_capabilities.meshShaders = options7.MeshShaderTier >= D3D12_MESH_SHADER_TIER_1;
    m_capabilities.variableRateShading = options7.VariableShadingRateTier >= D3D12_VARIABLE_SHADING_RATE_TIER_1;
    m_capabilities.bindlessResources = true;
    m_capabilities.conservativeRasterization = options.ConservativeRasterizationTier >= D3D12_CONSERVATIVE_RASTERIZATION_TIER_1;
    
    LOG_INFO("D3D12 Capabilities: Ray Tracing={}, Mesh Shaders={}, VRS={}, Bindless={}",
             m_capabilities.rayTracing, m_capabilities.meshShaders,
             m_capabilities.variableRateShading, m_capabilities.bindlessResources);
}

void D3D12RHI::waitForGPU() {
    m_fenceValue++;
    m_graphicsQueue->Signal(m_frameFence.Get(), m_fenceValue);
    
    if (m_frameFence->GetCompletedValue() < m_fenceValue) {
        m_frameFence->SetEventOnCompletion(m_fenceValue, m_frameFenceEvent);
        WaitForSingleObject(m_frameFenceEvent, INFINITE);
    }
}

ID3D12CommandQueue* D3D12RHI::getQueue(RHIQueueType type) const {
    switch (type) {
        case RHIQueueType::Graphics: return m_graphicsQueue.Get();
        case RHIQueueType::Compute: return m_computeQueue.Get();
        case RHIQueueType::Transfer: return m_copyQueue.Get();
        default: return m_graphicsQueue.Get();
    }
}

// ============================================================================
// Resource Creation
// ============================================================================

std::unique_ptr<IRHIBuffer> D3D12RHI::createBuffer(const RHIBufferDesc& desc) {
    return std::make_unique<D3D12Buffer>(this, desc);
}

std::unique_ptr<IRHITexture> D3D12RHI::createTexture(const RHITextureDesc& desc) {
    return std::make_unique<D3D12Texture>(this, desc);
}

std::unique_ptr<IRHITextureView> D3D12RHI::createTextureView(
    IRHITexture* texture, RHIFormat format,
    uint32_t baseMip, uint32_t mipCount,
    uint32_t baseLayer, uint32_t layerCount) {
    return std::make_unique<D3D12TextureView>(
        this, static_cast<D3D12Texture*>(texture),
        format, baseMip, mipCount, baseLayer, layerCount);
}

std::unique_ptr<IRHISampler> D3D12RHI::createSampler(const RHISamplerDesc& desc) {
    return std::make_unique<D3D12Sampler>(this, desc);
}

std::unique_ptr<IRHIPipeline> D3D12RHI::createGraphicsPipeline(const RHIGraphicsPipelineDesc& desc) {
    auto pipeline = std::make_unique<D3D12Pipeline>(this, RHIPipelineType::Graphics);
    
    // Build PSO description
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    
    // Shaders
    if (!desc.vs.code.empty()) {
        psoDesc.VS = { desc.vs.code.data(), desc.vs.code.size() };
    }
    if (!desc.ps.code.empty()) {
        psoDesc.PS = { desc.ps.code.data(), desc.ps.code.size() };
    }
    if (!desc.gs.code.empty()) {
        psoDesc.GS = { desc.gs.code.data(), desc.gs.code.size() };
    }
    if (!desc.hs.code.empty()) {
        psoDesc.HS = { desc.hs.code.data(), desc.hs.code.size() };
    }
    if (!desc.ds.code.empty()) {
        psoDesc.DS = { desc.ds.code.data(), desc.ds.code.size() };
    }
    
    // Input layout
    std::vector<D3D12_INPUT_ELEMENT_DESC> inputElements;
    for (const auto& attr : desc.vertexLayout.attributes) {
        D3D12_INPUT_ELEMENT_DESC element = {};
        element.SemanticName = attr.semantic.c_str();
        element.SemanticIndex = attr.semanticIndex;
        element.Format = ToDXGIFormat(attr.format);
        element.InputSlot = attr.binding;
        element.AlignedByteOffset = attr.offset;
        element.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
        element.InstanceDataStepRate = 0;
        
        if (attr.binding < desc.vertexLayout.bindings.size()) {
            if (desc.vertexLayout.bindings[attr.binding].inputRate == RHIInputRate::Instance) {
                element.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
                element.InstanceDataStepRate = 1;
            }
        }
        
        inputElements.push_back(element);
    }
    
    psoDesc.InputLayout = { inputElements.data(), static_cast<UINT>(inputElements.size()) };
    
    // Rasterizer state
    psoDesc.RasterizerState.FillMode = ToD3D12FillMode(desc.rasterState.fillMode);
    psoDesc.RasterizerState.CullMode = ToD3D12CullMode(desc.rasterState.cullMode);
    psoDesc.RasterizerState.FrontCounterClockwise = desc.rasterState.frontFace == RHIFrontFace::CounterClockwise;
    psoDesc.RasterizerState.DepthBias = static_cast<INT>(desc.rasterState.depthBias);
    psoDesc.RasterizerState.DepthBiasClamp = desc.rasterState.depthBiasClamp;
    psoDesc.RasterizerState.SlopeScaledDepthBias = desc.rasterState.depthBiasSlope;
    psoDesc.RasterizerState.DepthClipEnable = !desc.rasterState.depthClampEnable;
    psoDesc.RasterizerState.MultisampleEnable = FALSE;
    psoDesc.RasterizerState.AntialiasedLineEnable = FALSE;
    psoDesc.RasterizerState.ForcedSampleCount = 0;
    psoDesc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    
    // Blend state
    psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
    psoDesc.BlendState.IndependentBlendEnable = TRUE;
    
    for (size_t i = 0; i < desc.blendState.renderTargets.size() && i < 8; i++) {
        const auto& target = desc.blendState.renderTargets[i];
        auto& rtBlend = psoDesc.BlendState.RenderTarget[i];
        
        rtBlend.BlendEnable = target.blendEnable;
        rtBlend.LogicOpEnable = FALSE;
        rtBlend.SrcBlend = ToD3D12Blend(target.srcBlend);
        rtBlend.DestBlend = ToD3D12Blend(target.dstBlend);
        rtBlend.BlendOp = ToD3D12BlendOp(target.blendOp);
        rtBlend.SrcBlendAlpha = ToD3D12Blend(target.srcBlendAlpha);
        rtBlend.DestBlendAlpha = ToD3D12Blend(target.dstBlendAlpha);
        rtBlend.BlendOpAlpha = ToD3D12BlendOp(target.blendOpAlpha);
        rtBlend.RenderTargetWriteMask = static_cast<UINT8>(target.writeMask);
    }
    
    // Depth stencil state
    psoDesc.DepthStencilState.DepthEnable = desc.depthStencilState.depthTestEnable;
    psoDesc.DepthStencilState.DepthWriteMask = desc.depthStencilState.depthWriteEnable ? 
        D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.DepthStencilState.DepthFunc = ToD3D12CompareFunc(desc.depthStencilState.depthCompareOp);
    psoDesc.DepthStencilState.StencilEnable = desc.depthStencilState.stencilEnable;
    psoDesc.DepthStencilState.StencilReadMask = desc.depthStencilState.stencilReadMask;
    psoDesc.DepthStencilState.StencilWriteMask = desc.depthStencilState.stencilWriteMask;
    
    // Sample desc
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;
    
    // Primitive topology
    psoDesc.PrimitiveTopologyType = ToD3D12TopologyType(desc.primitiveTopology);
    
    // Render target formats
    psoDesc.NumRenderTargets = static_cast<UINT>(desc.renderTargetFormats.size());
    for (size_t i = 0; i < desc.renderTargetFormats.size() && i < 8; i++) {
        psoDesc.RTVFormats[i] = ToDXGIFormat(desc.renderTargetFormats[i]);
    }
    
    psoDesc.DSVFormat = ToDXGIFormat(desc.depthStencilFormat);
    
    // Create root signature (bindless style)
    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                        D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
                        D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;
    
    // Add push constants support
    D3D12_ROOT_PARAMETER rootParams[1] = {};
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[0].Constants.ShaderRegister = 0;
    rootParams[0].Constants.RegisterSpace = 0;
    rootParams[0].Constants.Num32BitValues = 32; // 128 bytes of push constants
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    
    rootSigDesc.NumParameters = 1;
    rootSigDesc.pParameters = rootParams;
    
    ComPtr<ID3DBlob> serializedRootSig;
    ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        &serializedRootSig, &errorBlob);
    
    if (FAILED(hr)) {
        if (errorBlob) {
            LOG_ERROR("Root signature error: {}", static_cast<char*>(errorBlob->GetBufferPointer()));
        }
        return nullptr;
    }
    
    ComPtr<ID3D12RootSignature> rootSig;
    hr = m_device->CreateRootSignature(0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(&rootSig));
    
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create root signature");
        return nullptr;
    }
    
    pipeline->setRootSignature(rootSig);
    psoDesc.pRootSignature = rootSig.Get();
    
    // Create PSO
    ComPtr<ID3D12PipelineState> pso;
    hr = m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso));
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create graphics pipeline state");
        return nullptr;
    }
    
    pipeline->setPSO(pso);
    pipeline->setTopology(ToD3D12Topology(desc.primitiveTopology));
    
    return pipeline;
}

std::unique_ptr<IRHIPipeline> D3D12RHI::createComputePipeline(const RHIComputePipelineDesc& desc) {
    auto pipeline = std::make_unique<D3D12Pipeline>(this, RHIPipelineType::Compute);
    
    // Create root signature
    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
                        D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;
    
    D3D12_ROOT_PARAMETER rootParams[1] = {};
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[0].Constants.ShaderRegister = 0;
    rootParams[0].Constants.RegisterSpace = 0;
    rootParams[0].Constants.Num32BitValues = 32;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    
    rootSigDesc.NumParameters = 1;
    rootSigDesc.pParameters = rootParams;
    
    ComPtr<ID3DBlob> serializedRootSig;
    ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        &serializedRootSig, &errorBlob);
    
    if (FAILED(hr)) {
        return nullptr;
    }
    
    ComPtr<ID3D12RootSignature> rootSig;
    hr = m_device->CreateRootSignature(0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(&rootSig));
    
    if (FAILED(hr)) {
        return nullptr;
    }
    
    pipeline->setRootSignature(rootSig);
    
    // Create compute PSO
    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSig.Get();
    psoDesc.CS = { desc.cs.code.data(), desc.cs.code.size() };
    
    ComPtr<ID3D12PipelineState> pso;
    hr = m_device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pso));
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create compute pipeline state");
        return nullptr;
    }
    
    pipeline->setPSO(pso);
    
    return pipeline;
}

std::unique_ptr<IRHIPipeline> D3D12RHI::createRayTracingPipeline(const RHIRayTracingPipelineDesc& desc) {
    if (!m_device5) {
        LOG_ERROR("Ray tracing not supported - Device5 not available");
        return nullptr;
    }
    
    auto pipeline = std::make_unique<D3D12Pipeline>(this, RHIPipelineType::RayTracing);
    
    // TODO: Implement ray tracing pipeline creation
    LOG_WARN("Ray tracing pipeline creation not yet implemented");
    
    return nullptr;
}

std::unique_ptr<IRHIFence> D3D12RHI::createFence(bool signaled) {
    return std::make_unique<D3D12Fence>(this, signaled);
}

std::unique_ptr<IRHISemaphore> D3D12RHI::createSemaphore() {
    return std::make_unique<D3D12Semaphore>(this);
}

std::unique_ptr<IRHIQueryPool> D3D12RHI::createQueryPool(QueryType type, uint32_t count) {
    D3D12_QUERY_TYPE d3dType;
    switch (type) {
        case QueryType::Timestamp: d3dType = D3D12_QUERY_TYPE_TIMESTAMP; break;
        case QueryType::Occlusion: d3dType = D3D12_QUERY_TYPE_OCCLUSION; break;
        case QueryType::PipelineStatistics: d3dType = D3D12_QUERY_TYPE_PIPELINE_STATISTICS; break;
        default: d3dType = D3D12_QUERY_TYPE_TIMESTAMP; break;
    }
    return std::make_unique<D3D12QueryPool>(this, d3dType, count);
}

std::unique_ptr<IRHIAccelerationStructure> D3D12RHI::createAccelerationStructure(bool isTopLevel, uint64_t size) {
    return std::make_unique<D3D12AccelerationStructure>(this, isTopLevel, size);
}

AccelerationStructureSizes D3D12RHI::getAccelerationStructureSizes(const RHIAccelerationStructureBuildInfo& info) {
    AccelerationStructureSizes sizes = {};
    
    if (!m_device5) {
        return sizes;
    }
    
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.Type = info.isTopLevel ? 
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL :
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    inputs.NumDescs = info.geometryCount;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo = {};
    m_device5->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuildInfo);
    
    sizes.accelerationStructureSize = prebuildInfo.ResultDataMaxSizeInBytes;
    sizes.buildScratchSize = prebuildInfo.ScratchDataSizeInBytes;
    sizes.updateScratchSize = prebuildInfo.UpdateScratchDataSizeInBytes;
    
    return sizes;
}

// ============================================================================
// Command Lists
// ============================================================================

std::unique_ptr<IRHICommandList> D3D12RHI::createCommandList(RHIQueueType queue) {
    return std::make_unique<D3D12CommandList>(this, queue);
}

// ============================================================================
// Submission
// ============================================================================

void D3D12RHI::submit(IRHICommandList* cmdList, IRHIFence* signalFence) {
    auto* d3dCmdList = static_cast<D3D12CommandList*>(cmdList);
    ID3D12CommandList* lists[] = { d3dCmdList->getCommandList() };
    
    auto* queue = getQueue(d3dCmdList->getQueueType());
    queue->ExecuteCommandLists(1, lists);
    
    if (signalFence) {
        auto* d3dFence = static_cast<D3D12Fence*>(signalFence);
        d3dFence->signal(d3dFence->getValue() + 1);
        queue->Signal(d3dFence->getFence(), d3dFence->getValue());
    }
}

void D3D12RHI::submitAsync(IRHICommandList* cmdList, RHIQueueType queue, IRHIFence* signalFence) {
    auto* d3dCmdList = static_cast<D3D12CommandList*>(cmdList);
    ID3D12CommandList* lists[] = { d3dCmdList->getCommandList() };
    
    auto* targetQueue = getQueue(queue);
    targetQueue->ExecuteCommandLists(1, lists);
    
    if (signalFence) {
        auto* d3dFence = static_cast<D3D12Fence*>(signalFence);
        d3dFence->signal(d3dFence->getValue() + 1);
        targetQueue->Signal(d3dFence->getFence(), d3dFence->getValue());
    }
}

void D3D12RHI::submit(const SubmitInfo& info, RHIQueueType queue) {
    auto* targetQueue = getQueue(queue);
    
    // Wait for wait semaphores
    for (auto* sem : info.waitSemaphores) {
        auto* d3dSem = static_cast<D3D12Semaphore*>(sem);
        targetQueue->Wait(d3dSem->getFence(), d3dSem->getValue());
    }
    
    // Execute command lists
    std::vector<ID3D12CommandList*> lists;
    for (auto* cmdList : info.commandLists) {
        auto* d3dCmdList = static_cast<D3D12CommandList*>(cmdList);
        lists.push_back(d3dCmdList->getCommandList());
    }
    
    if (!lists.empty()) {
        targetQueue->ExecuteCommandLists(static_cast<UINT>(lists.size()), lists.data());
    }
    
    // Signal semaphores
    for (auto* sem : info.signalSemaphores) {
        auto* d3dSem = static_cast<D3D12Semaphore*>(sem);
        d3dSem->increment();
        targetQueue->Signal(d3dSem->getFence(), d3dSem->getValue());
    }
    
    // Signal fence
    if (info.signalFence) {
        auto* d3dFence = static_cast<D3D12Fence*>(info.signalFence);
        d3dFence->signal(d3dFence->getValue() + 1);
        targetQueue->Signal(d3dFence->getFence(), d3dFence->getValue());
    }
}

// ============================================================================
// Swapchain
// ============================================================================

IRHITexture* D3D12RHI::getBackBuffer() {
    return m_backBuffers[m_currentBackBufferIndex].get();
}

void D3D12RHI::present() {
    if (m_swapchain) {
        m_swapchain->Present(0, DXGI_PRESENT_ALLOW_TEARING);
        m_currentBackBufferIndex = m_swapchain->GetCurrentBackBufferIndex();
    }
}

void D3D12RHI::resize(uint32_t width, uint32_t height) {
    if (!m_swapchain || (width == m_swapchainWidth && height == m_swapchainHeight)) {
        return;
    }
    
    waitIdle();
    
    m_backBuffers.clear();
    
    HRESULT hr = m_swapchain->ResizeBuffers(MAX_FRAMES_IN_FLIGHT, width, height,
        DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING);
    
    if (FAILED(hr)) {
        LOG_ERROR("Failed to resize swapchain");
        return;
    }
    
    m_swapchainWidth = width;
    m_swapchainHeight = height;
    
    // Recreate back buffers
    m_backBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    for (UINT i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        ComPtr<ID3D12Resource> resource;
        hr = m_swapchain->GetBuffer(i, IID_PPV_ARGS(&resource));
        if (FAILED(hr)) continue;
        
        RHITextureDesc desc{};
        desc.width = m_swapchainWidth;
        desc.height = m_swapchainHeight;
        desc.depth = 1;
        desc.mipLevels = 1;
        desc.arrayLayers = 1;
        desc.format = m_backBufferFormat;
        desc.usage = RHITextureUsage::RenderTarget;
        desc.dimension = RHITextureDimension::Texture2D;
        desc.sampleCount = RHISampleCount::Count1;
        
        m_backBuffers[i] = std::make_unique<D3D12Texture>(this, resource.Get(), desc);
    }
    
    m_currentBackBufferIndex = m_swapchain->GetCurrentBackBufferIndex();
}

// ============================================================================
// Frame Management
// ============================================================================

void D3D12RHI::beginFrame() {
    // Wait for the frame we're about to render
    uint64_t completedValue = m_frameFence->GetCompletedValue();
    if (m_frameFenceValues[m_frameIndex] != 0 && completedValue < m_frameFenceValues[m_frameIndex]) {
        m_frameFence->SetEventOnCompletion(m_frameFenceValues[m_frameIndex], m_frameFenceEvent);
        WaitForSingleObject(m_frameFenceEvent, INFINITE);
    }
}

void D3D12RHI::endFrame() {
    // Signal fence for this frame
    m_fenceValue++;
    m_frameFenceValues[m_frameIndex] = m_fenceValue;
    m_graphicsQueue->Signal(m_frameFence.Get(), m_fenceValue);
    
    m_frameIndex = (m_frameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
    m_frameCount++;
}

// ============================================================================
// Synchronization
// ============================================================================

void D3D12RHI::waitIdle() {
    waitForGPU();
}

void D3D12RHI::waitQueueIdle(RHIQueueType queue) {
    ComPtr<ID3D12Fence> fence;
    HRESULT hr = m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    if (FAILED(hr)) return;
    
    HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!event) return;
    
    auto* targetQueue = getQueue(queue);
    targetQueue->Signal(fence.Get(), 1);
    
    if (fence->GetCompletedValue() < 1) {
        fence->SetEventOnCompletion(1, event);
        WaitForSingleObject(event, INFINITE);
    }
    
    CloseHandle(event);
}

// ============================================================================
// Memory
// ============================================================================

RHIMemoryStats D3D12RHI::getMemoryStats() const {
    RHIMemoryStats stats = {};
    
    if (m_allocator) {
        D3D12MA::TotalStatistics totalStats;
        m_allocator->CalculateStatistics(&totalStats);
        
        stats.totalAllocated = totalStats.Total.Stats.BlockBytes;
        stats.totalUsed = totalStats.Total.Stats.AllocationBytes;
    }
    
    return stats;
}

// ============================================================================
// Debug
// ============================================================================

void D3D12RHI::setDebugName(IRHIResource* resource, const char* name) {
    // Would need to track resources and their D3D12 objects
    // For now, this is a stub
}

void D3D12RHI::beginCapture() {
#ifdef USE_PIX
    PIXBeginCapture(PIX_CAPTURE_GPU, nullptr);
#endif
}

void D3D12RHI::endCapture() {
#ifdef USE_PIX
    PIXEndCapture(FALSE);
#endif
}

double D3D12RHI::getTimestampFrequency() const {
    UINT64 frequency = 0;
    if (m_graphicsQueue) {
        m_graphicsQueue->GetTimestampFrequency(&frequency);
    }
    return static_cast<double>(frequency);
}

// ============================================================================
// Ray Tracing
// ============================================================================

ShaderBindingTableInfo D3D12RHI::getShaderBindingTableInfo() const {
    ShaderBindingTableInfo info = {};
    
    // D3D12 shader record alignment
    info.handleSize = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
    info.handleSizeAligned = D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT;
    info.baseAlignment = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
    
    return info;
}

bool D3D12RHI::getShaderGroupHandles(IRHIPipeline* pipeline,
                                     uint32_t firstGroup, uint32_t groupCount,
                                     void* data, size_t dataSize) {
    auto* d3dPipeline = static_cast<D3D12Pipeline*>(pipeline);
    if (!d3dPipeline->getStateObject()) {
        return false;
    }
    
    ComPtr<ID3D12StateObjectProperties> props;
    if (FAILED(d3dPipeline->getStateObject()->QueryInterface(IID_PPV_ARGS(&props)))) {
        return false;
    }
    
    // TODO: Get shader identifiers from state object
    
    return true;
}

} // namespace Sanic

#endif // SANIC_ENABLE_D3D12
