#pragma once

#ifdef SANIC_ENABLE_D3D12

#include "../RHI.h"
#include "../RHIResources.h"
#include "../RHICommandList.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <D3D12MemAlloc.h>

// Detect if newer D3D12 interfaces are available
// MinGW may not have the newer interfaces
#ifndef __ID3D12Device5_INTERFACE_DEFINED__
#define SANIC_D3D12_NO_DXR
using ID3D12Device5 = ID3D12Device;
#endif

#ifndef __ID3D12GraphicsCommandList6_INTERFACE_DEFINED__
#define SANIC_D3D12_NO_MESH_SHADERS
using ID3D12GraphicsCommandList6 = ID3D12GraphicsCommandList4;
#endif

#ifndef __ID3D12GraphicsCommandList4_INTERFACE_DEFINED__
using ID3D12GraphicsCommandList4 = ID3D12GraphicsCommandList;
using ID3D12GraphicsCommandList6 = ID3D12GraphicsCommandList;
#endif

#ifndef __ID3D12Debug3_INTERFACE_DEFINED__
using ID3D12Debug3 = ID3D12Debug;
#endif

#ifndef __ID3D12StateObject_INTERFACE_DEFINED__
#define SANIC_D3D12_NO_STATE_OBJECTS
struct ID3D12StateObject : ID3D12Pageable {};
#endif

#ifndef __ID3D12InfoQueue_INTERFACE_DEFINED__
struct ID3D12InfoQueue : IUnknown {};
#endif

#include <wrl/client.h>
#include <vector>
#include <array>
#include <memory>
#include <mutex>

using Microsoft::WRL::ComPtr;

namespace Sanic {

// Forward declarations
class D3D12Buffer;
class D3D12Texture;
class D3D12TextureView;
class D3D12Sampler;
class D3D12Pipeline;
class D3D12CommandList;
class D3D12Fence;
class D3D12Semaphore;
class D3D12QueryPool;
class D3D12AccelerationStructure;
class Window;

// ============================================================================
// D3D12 Conversion Functions
// ============================================================================
DXGI_FORMAT ToDXGIFormat(RHIFormat format);
RHIFormat FromDXGIFormat(DXGI_FORMAT format);
D3D12_RESOURCE_STATES ToD3D12ResourceState(RHIResourceState state);
D3D12_HEAP_TYPE ToD3D12HeapType(RHIMemoryType type);
D3D12_SHADER_VISIBILITY ToD3D12ShaderVisibility(RHIShaderStage stage);
D3D12_DESCRIPTOR_RANGE_TYPE ToD3D12DescriptorRangeType(RHIDescriptorType type);
D3D12_FILTER ToD3D12Filter(RHIFilter min, RHIFilter mag, RHIMipmapMode mip, bool aniso, bool compare);
D3D12_TEXTURE_ADDRESS_MODE ToD3D12AddressMode(RHIAddressMode mode);
D3D12_COMPARISON_FUNC ToD3D12CompareFunc(RHICompareOp op);
D3D12_BLEND ToD3D12Blend(RHIBlendFactor factor);
D3D12_BLEND_OP ToD3D12BlendOp(RHIBlendOp op);
D3D12_STENCIL_OP ToD3D12StencilOp(RHIStencilOp op);
D3D12_CULL_MODE ToD3D12CullMode(RHICullMode mode);
D3D12_FILL_MODE ToD3D12FillMode(RHIFillMode mode);
D3D12_PRIMITIVE_TOPOLOGY_TYPE ToD3D12TopologyType(RHIPrimitiveTopology topology);
D3D12_PRIMITIVE_TOPOLOGY ToD3D12Topology(RHIPrimitiveTopology topology);
DXGI_FORMAT ToDXGIIndexFormat(RHIIndexType type);
D3D12_RESOURCE_DIMENSION ToD3D12ResourceDimension(RHITextureDimension dim);

// ============================================================================
// D3D12 Descriptor Heap Manager
// ============================================================================
class D3D12DescriptorHeap {
public:
    D3D12DescriptorHeap(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type,
                        uint32_t numDescriptors, bool shaderVisible);
    ~D3D12DescriptorHeap() = default;
    
    uint32_t allocate(uint32_t count = 1);
    void free(uint32_t index, uint32_t count = 1);
    
    D3D12_CPU_DESCRIPTOR_HANDLE getCPUHandle(uint32_t index) const;
    D3D12_GPU_DESCRIPTOR_HANDLE getGPUHandle(uint32_t index) const;
    
    ID3D12DescriptorHeap* getHeap() const { return m_heap.Get(); }
    uint32_t getDescriptorSize() const { return m_descriptorSize; }
    
private:
    ComPtr<ID3D12DescriptorHeap> m_heap;
    D3D12_CPU_DESCRIPTOR_HANDLE m_cpuStart;
    D3D12_GPU_DESCRIPTOR_HANDLE m_gpuStart;
    uint32_t m_descriptorSize;
    uint32_t m_numDescriptors;
    
    std::vector<bool> m_allocated;
    uint32_t m_searchStart = 0;
    std::mutex m_mutex;
};

// ============================================================================
// D3D12RHI - Main D3D12 Backend Implementation
// ============================================================================
class D3D12RHI : public IRHI {
public:
    D3D12RHI();
    ~D3D12RHI() override;
    
    // IRHI Interface Implementation
    bool initialize(Window& window, const RHIConfig& config) override;
    void shutdown() override;
    
    // Capabilities
    const RHICapabilities& getCapabilities() const override { return m_capabilities; }
    RHIBackend getBackend() const override { return RHIBackend::D3D12; }
    
    // Resource Creation
    std::unique_ptr<IRHIBuffer> createBuffer(const RHIBufferDesc& desc) override;
    std::unique_ptr<IRHITexture> createTexture(const RHITextureDesc& desc) override;
    std::unique_ptr<IRHITextureView> createTextureView(
        IRHITexture* texture, RHIFormat format,
        uint32_t baseMip, uint32_t mipCount,
        uint32_t baseLayer, uint32_t layerCount) override;
    std::unique_ptr<IRHISampler> createSampler(const RHISamplerDesc& desc) override;
    std::unique_ptr<IRHIPipeline> createGraphicsPipeline(
        const RHIGraphicsPipelineDesc& desc) override;
    std::unique_ptr<IRHIPipeline> createComputePipeline(
        const RHIComputePipelineDesc& desc) override;
    std::unique_ptr<IRHIPipeline> createRayTracingPipeline(
        const RHIRayTracingPipelineDesc& desc) override;
    std::unique_ptr<IRHIFence> createFence(bool signaled) override;
    std::unique_ptr<IRHISemaphore> createSemaphore() override;
    std::unique_ptr<IRHIQueryPool> createQueryPool(QueryType type, uint32_t count) override;
    std::unique_ptr<IRHIAccelerationStructure> createAccelerationStructure(
        bool isTopLevel, uint64_t size) override;
    AccelerationStructureSizes getAccelerationStructureSizes(
        const RHIAccelerationStructureBuildInfo& info) override;
    
    // Command Lists
    std::unique_ptr<IRHICommandList> createCommandList(RHIQueueType queue) override;
    
    // Submission
    void submit(IRHICommandList* cmdList, IRHIFence* signalFence) override;
    void submitAsync(IRHICommandList* cmdList, RHIQueueType queue,
                    IRHIFence* signalFence) override;
    void submit(const SubmitInfo& info, RHIQueueType queue) override;
    
    // Swapchain
    IRHITexture* getBackBuffer() override;
    uint32_t getBackBufferIndex() const override { return m_currentBackBufferIndex; }
    uint32_t getBackBufferCount() const override { return static_cast<uint32_t>(m_backBuffers.size()); }
    RHIFormat getBackBufferFormat() const override { return m_backBufferFormat; }
    void present() override;
    void resize(uint32_t width, uint32_t height) override;
    uint32_t getSwapchainWidth() const override { return m_swapchainWidth; }
    uint32_t getSwapchainHeight() const override { return m_swapchainHeight; }
    
    // Frame Management
    void beginFrame() override;
    void endFrame() override;
    uint32_t getFrameIndex() const override { return m_frameIndex; }
    uint64_t getFrameCount() const override { return m_frameCount; }
    
    // Synchronization
    void waitIdle() override;
    void waitQueueIdle(RHIQueueType queue) override;
    
    // Memory
    RHIMemoryStats getMemoryStats() const override;
    
    // Debug
    void setDebugName(IRHIResource* resource, const char* name) override;
    void beginCapture() override;
    void endCapture() override;
    double getTimestampFrequency() const override;
    
    // Ray Tracing
    ShaderBindingTableInfo getShaderBindingTableInfo() const override;
    bool getShaderGroupHandles(IRHIPipeline* pipeline,
                              uint32_t firstGroup, uint32_t groupCount,
                              void* data, size_t dataSize) override;
    
    // D3D12-Specific Getters
    ID3D12Device* getDevice() const { return m_device.Get(); }
    ID3D12Device5* getDevice5() const { return m_device5.Get(); }
    D3D12MA::Allocator* getAllocator() const { return m_allocator.Get(); }
    ID3D12CommandQueue* getGraphicsQueue() const { return m_graphicsQueue.Get(); }
    ID3D12CommandQueue* getComputeQueue() const { return m_computeQueue.Get(); }
    ID3D12CommandQueue* getCopyQueue() const { return m_copyQueue.Get(); }
    
    D3D12DescriptorHeap* getCBVSRVUAVHeap() const { return m_cbvSrvUavHeap.get(); }
    D3D12DescriptorHeap* getSamplerHeap() const { return m_samplerHeap.get(); }
    D3D12DescriptorHeap* getRTVHeap() const { return m_rtvHeap.get(); }
    D3D12DescriptorHeap* getDSVHeap() const { return m_dsvHeap.get(); }
    
private:
    bool createDevice(const RHIConfig& config);
    bool createQueues();
    bool createSwapchain(Window& window);
    bool createDescriptorHeaps();
    void queryCapabilities();
    void waitForGPU();
    ID3D12CommandQueue* getQueue(RHIQueueType type) const;
    
private:
    // Factory and Adapter
    ComPtr<IDXGIFactory6> m_factory;
    ComPtr<IDXGIAdapter4> m_adapter;
    
    // Device
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12Device5> m_device5;  // For ray tracing
    
    // Memory Allocator
    ComPtr<D3D12MA::Allocator> m_allocator;
    
    // Command Queues
    ComPtr<ID3D12CommandQueue> m_graphicsQueue;
    ComPtr<ID3D12CommandQueue> m_computeQueue;
    ComPtr<ID3D12CommandQueue> m_copyQueue;
    
    // Swapchain
    ComPtr<IDXGISwapChain4> m_swapchain;
    std::vector<std::unique_ptr<D3D12Texture>> m_backBuffers;
    uint32_t m_currentBackBufferIndex = 0;
    RHIFormat m_backBufferFormat = RHIFormat::R8G8B8A8_UNORM;
    uint32_t m_swapchainWidth = 0;
    uint32_t m_swapchainHeight = 0;
    
    // Descriptor Heaps
    std::unique_ptr<D3D12DescriptorHeap> m_cbvSrvUavHeap;
    std::unique_ptr<D3D12DescriptorHeap> m_samplerHeap;
    std::unique_ptr<D3D12DescriptorHeap> m_rtvHeap;
    std::unique_ptr<D3D12DescriptorHeap> m_dsvHeap;
    
    // Frame Synchronization
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;
    ComPtr<ID3D12Fence> m_frameFence;
    HANDLE m_frameFenceEvent = nullptr;
    std::array<uint64_t, MAX_FRAMES_IN_FLIGHT> m_frameFenceValues{};
    uint32_t m_frameIndex = 0;
    uint64_t m_frameCount = 0;
    uint64_t m_fenceValue = 0;
    
    // Capabilities
    RHICapabilities m_capabilities{};
    
    // Config
    RHIConfig m_config;
    Window* m_window = nullptr;
    
    // Debug
    ComPtr<ID3D12Debug3> m_debugInterface;
    ComPtr<ID3D12InfoQueue> m_infoQueue;
};

// ============================================================================
// D3D12Buffer
// ============================================================================
class D3D12Buffer : public IRHIBuffer {
public:
    D3D12Buffer(D3D12RHI* rhi, const RHIBufferDesc& desc);
    ~D3D12Buffer() override;
    
    // IRHIBuffer Interface
    uint64_t getSize() const override { return m_desc.size; }
    RHIBufferUsage getUsage() const override { return m_desc.usage; }
    RHIMemoryType getMemoryType() const override { return m_desc.memoryType; }
    void* map() override;
    void unmap() override;
    void* getMappedPointer() const override { return m_mappedPtr; }
    uint64_t getGPUAddress() const override;
    
    // D3D12-Specific
    ID3D12Resource* getResource() const { return m_resource.Get(); }
    D3D12MA::Allocation* getAllocation() const { return m_allocation.Get(); }
    
private:
    D3D12RHI* m_rhi = nullptr;
    RHIBufferDesc m_desc{};
    ComPtr<ID3D12Resource> m_resource;
    ComPtr<D3D12MA::Allocation> m_allocation;
    void* m_mappedPtr = nullptr;
};

// ============================================================================
// D3D12Texture
// ============================================================================
class D3D12Texture : public IRHITexture {
public:
    D3D12Texture(D3D12RHI* rhi, const RHITextureDesc& desc);
    // Constructor for swapchain textures (external ownership)
    D3D12Texture(D3D12RHI* rhi, ID3D12Resource* resource, const RHITextureDesc& desc);
    ~D3D12Texture() override;
    
    // IRHITexture Interface
    uint32_t getWidth() const override { return m_desc.width; }
    uint32_t getHeight() const override { return m_desc.height; }
    uint32_t getDepth() const override { return m_desc.depth; }
    uint32_t getMipLevels() const override { return m_desc.mipLevels; }
    uint32_t getArrayLayers() const override { return m_desc.arrayLayers; }
    RHIFormat getFormat() const override { return m_desc.format; }
    RHITextureUsage getUsage() const override { return m_desc.usage; }
    RHITextureDimension getDimension() const override { return m_desc.dimension; }
    RHISampleCount getSampleCount() const override { return m_desc.sampleCount; }
    
    // D3D12-Specific
    ID3D12Resource* getResource() const { return m_resource.Get(); }
    D3D12_CPU_DESCRIPTOR_HANDLE getSRV() const { return m_srvHandle; }
    D3D12_CPU_DESCRIPTOR_HANDLE getUAV() const { return m_uavHandle; }
    D3D12_CPU_DESCRIPTOR_HANDLE getRTV() const { return m_rtvHandle; }
    D3D12_CPU_DESCRIPTOR_HANDLE getDSV() const { return m_dsvHandle; }
    
private:
    void createViews();
    
    D3D12RHI* m_rhi = nullptr;
    RHITextureDesc m_desc{};
    ComPtr<ID3D12Resource> m_resource;
    ComPtr<D3D12MA::Allocation> m_allocation;
    bool m_ownsResource = true;
    
    D3D12_CPU_DESCRIPTOR_HANDLE m_srvHandle{};
    D3D12_CPU_DESCRIPTOR_HANDLE m_uavHandle{};
    D3D12_CPU_DESCRIPTOR_HANDLE m_rtvHandle{};
    D3D12_CPU_DESCRIPTOR_HANDLE m_dsvHandle{};
    uint32_t m_srvIndex = UINT32_MAX;
    uint32_t m_uavIndex = UINT32_MAX;
    uint32_t m_rtvIndex = UINT32_MAX;
    uint32_t m_dsvIndex = UINT32_MAX;
};

// ============================================================================
// D3D12TextureView
// ============================================================================
class D3D12TextureView : public IRHITextureView {
public:
    D3D12TextureView(D3D12RHI* rhi, D3D12Texture* texture, RHIFormat format,
                     uint32_t baseMip, uint32_t mipCount,
                     uint32_t baseLayer, uint32_t layerCount);
    ~D3D12TextureView() override;
    
    // IRHITextureView Interface
    IRHITexture* getTexture() const override { return m_texture; }
    RHIFormat getFormat() const override { return m_format; }
    uint32_t getBaseMipLevel() const override { return m_baseMip; }
    uint32_t getMipLevelCount() const override { return m_mipCount; }
    uint32_t getBaseArrayLayer() const override { return m_baseLayer; }
    uint32_t getArrayLayerCount() const override { return m_layerCount; }
    
    // D3D12-Specific
    D3D12_CPU_DESCRIPTOR_HANDLE getSRV() const { return m_srvHandle; }
    
private:
    D3D12RHI* m_rhi = nullptr;
    D3D12Texture* m_texture = nullptr;
    RHIFormat m_format;
    uint32_t m_baseMip;
    uint32_t m_mipCount;
    uint32_t m_baseLayer;
    uint32_t m_layerCount;
    D3D12_CPU_DESCRIPTOR_HANDLE m_srvHandle{};
    uint32_t m_srvIndex = UINT32_MAX;
};

// ============================================================================
// D3D12Sampler
// ============================================================================
class D3D12Sampler : public IRHISampler {
public:
    D3D12Sampler(D3D12RHI* rhi, const RHISamplerDesc& desc);
    ~D3D12Sampler() override;
    
    // D3D12-Specific
    D3D12_CPU_DESCRIPTOR_HANDLE getHandle() const { return m_handle; }
    uint32_t getIndex() const { return m_index; }
    
private:
    D3D12RHI* m_rhi = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE m_handle{};
    uint32_t m_index = UINT32_MAX;
};

// ============================================================================
// D3D12Pipeline
// ============================================================================
class D3D12Pipeline : public IRHIPipeline {
public:
    D3D12Pipeline(D3D12RHI* rhi, RHIPipelineType type);
    ~D3D12Pipeline() override;
    
    RHIPipelineType getType() const override { return m_type; }
    
    // D3D12-Specific
    ID3D12PipelineState* getPSO() const { return m_pso.Get(); }
    ID3D12StateObject* getStateObject() const { return m_stateObject.Get(); }
    ID3D12RootSignature* getRootSignature() const { return m_rootSignature.Get(); }
    D3D12_PRIMITIVE_TOPOLOGY getTopology() const { return m_topology; }
    
    void setPSO(ComPtr<ID3D12PipelineState> pso) { m_pso = pso; }
    void setStateObject(ComPtr<ID3D12StateObject> so) { m_stateObject = so; }
    void setRootSignature(ComPtr<ID3D12RootSignature> rs) { m_rootSignature = rs; }
    void setTopology(D3D12_PRIMITIVE_TOPOLOGY topology) { m_topology = topology; }
    
private:
    D3D12RHI* m_rhi = nullptr;
    RHIPipelineType m_type;
    ComPtr<ID3D12PipelineState> m_pso;
    ComPtr<ID3D12StateObject> m_stateObject;  // For ray tracing
    ComPtr<ID3D12RootSignature> m_rootSignature;
    D3D12_PRIMITIVE_TOPOLOGY m_topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
};

// ============================================================================
// D3D12Fence
// ============================================================================
class D3D12Fence : public IRHIFence {
public:
    D3D12Fence(D3D12RHI* rhi, bool signaled);
    ~D3D12Fence() override;
    
    void wait(uint64_t timeout) override;
    void reset() override;
    bool isSignaled() const override;
    uint64_t getValue() const override { return m_value; }
    void signal(uint64_t value) override;
    
    // D3D12-Specific
    ID3D12Fence* getFence() const { return m_fence.Get(); }
    uint64_t getCompletedValue() const;
    void setEventOnCompletion(uint64_t value);
    
private:
    D3D12RHI* m_rhi = nullptr;
    ComPtr<ID3D12Fence> m_fence;
    HANDLE m_event = nullptr;
    uint64_t m_value = 0;
};

// ============================================================================
// D3D12Semaphore (D3D12 uses fences for GPU-GPU sync)
// ============================================================================
class D3D12Semaphore : public IRHISemaphore {
public:
    D3D12Semaphore(D3D12RHI* rhi);
    ~D3D12Semaphore() override;
    
    // D3D12-Specific
    ID3D12Fence* getFence() const { return m_fence.Get(); }
    uint64_t getValue() const { return m_value; }
    void increment() { m_value++; }
    
private:
    D3D12RHI* m_rhi = nullptr;
    ComPtr<ID3D12Fence> m_fence;
    uint64_t m_value = 0;
};

// ============================================================================
// D3D12QueryPool
// ============================================================================
class D3D12QueryPool : public IRHIQueryPool {
public:
    D3D12QueryPool(D3D12RHI* rhi, D3D12_QUERY_TYPE type, uint32_t count);
    ~D3D12QueryPool() override;
    
    uint32_t getQueryCount() const override { return m_count; }
    bool getResults(uint32_t firstQuery, uint32_t queryCount,
                   void* data, size_t dataSize,
                   size_t stride, bool wait) override;
    
    // D3D12-Specific
    ID3D12QueryHeap* getHeap() const { return m_heap.Get(); }
    ID3D12Resource* getResultBuffer() const { return m_resultBuffer.Get(); }
    D3D12_QUERY_TYPE getType() const { return m_type; }
    
private:
    D3D12RHI* m_rhi = nullptr;
    ComPtr<ID3D12QueryHeap> m_heap;
    ComPtr<ID3D12Resource> m_resultBuffer;
    D3D12_QUERY_TYPE m_type;
    uint32_t m_count = 0;
};

// ============================================================================
// D3D12AccelerationStructure
// ============================================================================
class D3D12AccelerationStructure : public IRHIAccelerationStructure {
public:
    D3D12AccelerationStructure(D3D12RHI* rhi, bool isTopLevel, uint64_t size);
    ~D3D12AccelerationStructure() override;
    
    uint64_t getGPUAddress() const override;
    bool isTopLevel() const override { return m_isTopLevel; }
    
    // D3D12-Specific
    ID3D12Resource* getResource() const { return m_resource.Get(); }
    
private:
    D3D12RHI* m_rhi = nullptr;
    ComPtr<ID3D12Resource> m_resource;
    ComPtr<D3D12MA::Allocation> m_allocation;
    bool m_isTopLevel = false;
    uint64_t m_size = 0;
};

// ============================================================================
// D3D12CommandList
// ============================================================================
class D3D12CommandList : public IRHICommandList {
public:
    D3D12CommandList(D3D12RHI* rhi, RHIQueueType queueType);
    ~D3D12CommandList() override;
    
    // Lifecycle
    void begin() override;
    void end() override;
    void reset() override;
    
    // Barriers
    void barrier(const RHIBarrier* barriers, uint32_t count) override;
    void uavBarrier(IRHIBuffer* buffer) override;
    void uavBarrier(IRHITexture* texture) override;
    
    // Render Pass
    void beginRenderPass(const RHIRenderPassBeginInfo& info) override;
    void endRenderPass() override;
    
    // Pipeline State
    void setPipeline(IRHIPipeline* pipeline) override;
    void setViewport(const RHIViewport& viewport) override;
    void setViewports(const RHIViewport* viewports, uint32_t count) override;
    void setScissor(const RHIScissor& scissor) override;
    void setScissors(const RHIScissor* scissors, uint32_t count) override;
    void setBlendConstants(const float constants[4]) override;
    void setStencilReference(uint32_t reference) override;
    void setDepthBias(float constantFactor, float clamp, float slopeFactor) override;
    void setLineWidth(float width) override;
    
    // Resource Binding
    void setVertexBuffer(uint32_t slot, IRHIBuffer* buffer, uint64_t offset) override;
    void setVertexBuffers(uint32_t firstSlot, IRHIBuffer** buffers,
                         const uint64_t* offsets, uint32_t count) override;
    void setIndexBuffer(IRHIBuffer* buffer, uint64_t offset, RHIIndexType indexType) override;
    void pushConstants(RHIShaderStage stages, uint32_t offset,
                      uint32_t size, const void* data) override;
    void bindBuffer(uint32_t set, uint32_t binding, IRHIBuffer* buffer,
                   uint64_t offset, uint64_t range) override;
    void bindTexture(uint32_t set, uint32_t binding, IRHITexture* texture,
                    IRHISampler* sampler) override;
    void bindStorageTexture(uint32_t set, uint32_t binding, IRHITexture* texture,
                           uint32_t mipLevel) override;
    void bindSampler(uint32_t set, uint32_t binding, IRHISampler* sampler) override;
    void bindAccelerationStructure(uint32_t set, uint32_t binding,
                                  IRHIAccelerationStructure* as) override;
    
    // Draw Commands
    void draw(uint32_t vertexCount, uint32_t instanceCount,
             uint32_t firstVertex, uint32_t firstInstance) override;
    void drawIndexed(uint32_t indexCount, uint32_t instanceCount,
                    uint32_t firstIndex, int32_t vertexOffset,
                    uint32_t firstInstance) override;
    void drawIndirect(IRHIBuffer* buffer, uint64_t offset,
                     uint32_t drawCount, uint32_t stride) override;
    void drawIndexedIndirect(IRHIBuffer* buffer, uint64_t offset,
                            uint32_t drawCount, uint32_t stride) override;
    void drawIndirectCount(IRHIBuffer* argBuffer, uint64_t argOffset,
                          IRHIBuffer* countBuffer, uint64_t countOffset,
                          uint32_t maxDrawCount, uint32_t stride) override;
    void drawIndexedIndirectCount(IRHIBuffer* argBuffer, uint64_t argOffset,
                                 IRHIBuffer* countBuffer, uint64_t countOffset,
                                 uint32_t maxDrawCount, uint32_t stride) override;
    
    // Mesh Shader Commands
    void dispatchMesh(uint32_t groupCountX, uint32_t groupCountY,
                     uint32_t groupCountZ) override;
    void dispatchMeshIndirect(IRHIBuffer* buffer, uint64_t offset) override;
    void dispatchMeshIndirectCount(IRHIBuffer* argBuffer, uint64_t argOffset,
                                  IRHIBuffer* countBuffer, uint64_t countOffset,
                                  uint32_t maxDispatchCount, uint32_t stride) override;
    
    // Compute Commands
    void dispatch(uint32_t groupCountX, uint32_t groupCountY,
                 uint32_t groupCountZ) override;
    void dispatchIndirect(IRHIBuffer* buffer, uint64_t offset) override;
    
    // Ray Tracing Commands
    void dispatchRays(const RHIDispatchRaysDesc& desc) override;
    void buildAccelerationStructure(const RHIAccelerationStructureBuildInfo& info) override;
    void copyAccelerationStructure(IRHIAccelerationStructure* dst,
                                  IRHIAccelerationStructure* src, bool compact) override;
    
    // Copy Commands
    void copyBuffer(IRHIBuffer* src, IRHIBuffer* dst,
                   const RHIBufferCopy* regions, uint32_t regionCount) override;
    void copyTexture(IRHITexture* src, IRHITexture* dst,
                    const RHITextureCopy* regions, uint32_t regionCount) override;
    void copyBufferToTexture(IRHIBuffer* src, IRHITexture* dst,
                            const RHIBufferTextureCopy* regions,
                            uint32_t regionCount) override;
    void copyTextureToBuffer(IRHITexture* src, IRHIBuffer* dst,
                            const RHIBufferTextureCopy* regions,
                            uint32_t regionCount) override;
    
    // Clear Commands
    void clearBuffer(IRHIBuffer* buffer, uint32_t value,
                    uint64_t offset, uint64_t size) override;
    void clearTexture(IRHITexture* texture, const float color[4],
                     uint32_t baseMip, uint32_t mipCount,
                     uint32_t baseLayer, uint32_t layerCount) override;
    void clearDepthStencil(IRHITexture* texture, float depth, uint8_t stencil,
                          bool clearDepth, bool clearStencil) override;
    
    // Query Commands
    void beginQuery(IRHIQueryPool* pool, uint32_t index) override;
    void endQuery(IRHIQueryPool* pool, uint32_t index) override;
    void resetQueryPool(IRHIQueryPool* pool, uint32_t firstQuery, uint32_t count) override;
    void writeTimestamp(IRHIQueryPool* pool, uint32_t index) override;
    void resolveQueryData(IRHIQueryPool* pool, uint32_t firstQuery, uint32_t count,
                         IRHIBuffer* destination, uint64_t offset) override;
    
    // Debug Markers
    void beginDebugLabel(const char* name, const glm::vec4& color) override;
    void endDebugLabel() override;
    void insertDebugLabel(const char* name, const glm::vec4& color) override;
    
    // Miscellaneous
    void fillBuffer(IRHIBuffer* buffer, uint64_t offset,
                   uint64_t size, uint32_t data) override;
    void updateBuffer(IRHIBuffer* buffer, uint64_t offset,
                     uint64_t size, const void* data) override;
    void generateMipmaps(IRHITexture* texture) override;
    void resolveTexture(IRHITexture* src, IRHITexture* dst,
                       uint32_t srcMip, uint32_t srcLayer,
                       uint32_t dstMip, uint32_t dstLayer) override;
    
    // D3D12-Specific
    ID3D12GraphicsCommandList6* getCommandList() const { return m_commandList.Get(); }
    ID3D12CommandAllocator* getAllocator() const { return m_allocator.Get(); }
    RHIQueueType getQueueType() const { return m_queueType; }
    
private:
    D3D12RHI* m_rhi = nullptr;
    RHIQueueType m_queueType;
    D3D12_COMMAND_LIST_TYPE m_listType;
    ComPtr<ID3D12CommandAllocator> m_allocator;
    ComPtr<ID3D12GraphicsCommandList6> m_commandList;
    
    // Current state tracking
    D3D12Pipeline* m_currentPipeline = nullptr;
    bool m_insideRenderPass = false;
};

} // namespace Sanic

#endif // SANIC_ENABLE_D3D12
