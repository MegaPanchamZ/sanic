#pragma once
#include "RHICommandList.h"
#include <memory>
#include <functional>
#include <string>

namespace Sanic {

// Forward declarations
class Window;

//=============================================================================
// RHI Backend Selection
//=============================================================================

enum class RHIBackend {
    Vulkan,
    D3D12,
    // Metal,  // Future
};

//=============================================================================
// RHI Configuration
//=============================================================================

struct RHIConfig {
    RHIBackend backend = RHIBackend::Vulkan;
    bool enableValidation = true;           // Enable debug/validation layers
    bool enableGPUValidation = false;       // Enable GPU-based validation (slow)
    bool enableRayTracing = true;           // Enable ray tracing extensions
    bool enableMeshShaders = true;          // Enable mesh shader extensions
    bool enableVariableRateShading = false; // Enable VRS
    uint32_t frameBufferCount = 2;          // Double/triple buffering
    bool vsync = true;                      // Enable vsync
    bool hdr = false;                       // Enable HDR output
    const char* applicationName = "Sanic Engine";
    uint32_t applicationVersion = 1;
};

//=============================================================================
// RHI Capabilities Query
//=============================================================================

struct RHICapabilities {
    // Feature support
    bool supportsRayTracing = false;
    bool supportsMeshShaders = false;
    bool supportsVariableRateShading = false;
    bool supports64BitAtomics = false;
    bool supportsInt16 = false;
    bool supportsFloat16 = false;
    bool supportsBufferDeviceAddress = true;
    bool supportsBindless = true;
    bool supportsConservativeRasterization = false;
    bool supportsMultiDrawIndirectCount = false;
    bool supportsTimestampQueries = true;
    
    // Limits
    uint32_t maxBoundDescriptorSets = 8;
    uint32_t maxPushConstantSize = 128;
    uint32_t maxUniformBufferSize = 65536;
    uint64_t maxStorageBufferSize = 0;
    uint64_t maxBufferSize = 0;
    uint32_t maxTexture2DSize = 16384;
    uint32_t maxTexture3DSize = 2048;
    uint32_t maxTextureCubeSize = 16384;
    uint32_t maxTextureArrayLayers = 2048;
    uint32_t maxColorAttachments = 8;
    uint32_t maxComputeWorkGroupSize[3] = {1024, 1024, 64};
    uint32_t maxComputeWorkGroupCount[3] = {65535, 65535, 65535};
    uint32_t maxMeshOutputVertices = 256;
    uint32_t maxMeshOutputPrimitives = 256;
    uint32_t maxTaskWorkGroupSize = 128;
    float timestampPeriod = 1.0f;  // Nanoseconds per timestamp tick
    
    // Ray tracing limits
    uint32_t maxRayRecursionDepth = 1;
    uint32_t maxRayDispatchInvocationCount = 0;
    uint32_t shaderGroupHandleSize = 32;
    uint32_t shaderGroupBaseAlignment = 64;
    
    // Memory
    uint64_t dedicatedVideoMemory = 0;      // GPU VRAM
    uint64_t sharedSystemMemory = 0;        // Shared with CPU
    
    // Device info
    std::string deviceName;
    std::string driverVersion;
    std::string apiVersion;
    uint32_t vendorID = 0;
    uint32_t deviceID = 0;
    
    // Vendor detection helpers
    bool isNvidia() const { return vendorID == 0x10DE; }
    bool isAMD() const { return vendorID == 0x1002; }
    bool isIntel() const { return vendorID == 0x8086; }
    bool isQualcomm() const { return vendorID == 0x5143; }
    bool isApple() const { return vendorID == 0x106B; }
};

//=============================================================================
// Memory Statistics
//=============================================================================

struct RHIMemoryStats {
    uint64_t usedDeviceMemory = 0;
    uint64_t totalDeviceMemory = 0;
    uint64_t usedHostMemory = 0;
    uint64_t totalHostMemory = 0;
    uint64_t allocationCount = 0;
    
    // Per-heap breakdown (if available)
    struct HeapInfo {
        uint64_t used = 0;
        uint64_t size = 0;
        bool isDeviceLocal = false;
        bool isHostVisible = false;
    };
    std::vector<HeapInfo> heaps;
};

//=============================================================================
// Frame Resources
//=============================================================================

// Per-frame resources that need to be cycled
struct RHIFrameResources {
    std::unique_ptr<IRHICommandList> commandList;
    std::unique_ptr<IRHIFence> fence;
    // Add more per-frame resources as needed
};

//=============================================================================
// Main RHI Interface
//=============================================================================

class IRHI {
public:
    virtual ~IRHI() = default;
    
    //-------------------------------------------------------------------------
    // Initialization & Shutdown
    //-------------------------------------------------------------------------
    
    // Initialize the RHI with given window and configuration
    virtual bool initialize(Window& window, const RHIConfig& config) = 0;
    
    // Shutdown and release all resources
    virtual void shutdown() = 0;
    
    //-------------------------------------------------------------------------
    // Capabilities Query
    //-------------------------------------------------------------------------
    
    virtual const RHICapabilities& getCapabilities() const = 0;
    virtual RHIBackend getBackend() const = 0;
    
    //-------------------------------------------------------------------------
    // Resource Creation
    //-------------------------------------------------------------------------
    
    // Buffers
    virtual std::unique_ptr<IRHIBuffer> createBuffer(const RHIBufferDesc& desc) = 0;
    
    // Textures
    virtual std::unique_ptr<IRHITexture> createTexture(const RHITextureDesc& desc) = 0;
    
    // Texture views (optional - texture may provide default views)
    virtual std::unique_ptr<IRHITextureView> createTextureView(
        IRHITexture* texture, RHIFormat format = RHIFormat::Unknown,
        uint32_t baseMip = 0, uint32_t mipCount = ~0u,
        uint32_t baseLayer = 0, uint32_t layerCount = ~0u) = 0;
    
    // Samplers
    virtual std::unique_ptr<IRHISampler> createSampler(const RHISamplerDesc& desc) = 0;
    
    // Pipelines
    virtual std::unique_ptr<IRHIPipeline> createGraphicsPipeline(
        const RHIGraphicsPipelineDesc& desc) = 0;
    virtual std::unique_ptr<IRHIPipeline> createComputePipeline(
        const RHIComputePipelineDesc& desc) = 0;
    virtual std::unique_ptr<IRHIPipeline> createRayTracingPipeline(
        const RHIRayTracingPipelineDesc& desc) = 0;
    
    // Synchronization
    virtual std::unique_ptr<IRHIFence> createFence(bool signaled = false) = 0;
    virtual std::unique_ptr<IRHISemaphore> createSemaphore() = 0;
    
    // Queries
    enum class QueryType {
        Occlusion,
        Timestamp,
        PipelineStatistics,
    };
    virtual std::unique_ptr<IRHIQueryPool> createQueryPool(
        QueryType type, uint32_t count) = 0;
    
    // Acceleration structures
    virtual std::unique_ptr<IRHIAccelerationStructure> createAccelerationStructure(
        bool isTopLevel, uint64_t size) = 0;
    
    // Get acceleration structure build sizes
    struct AccelerationStructureSizes {
        uint64_t accelerationStructureSize = 0;
        uint64_t buildScratchSize = 0;
        uint64_t updateScratchSize = 0;
    };
    virtual AccelerationStructureSizes getAccelerationStructureSizes(
        const RHIAccelerationStructureBuildInfo& info) = 0;
    
    //-------------------------------------------------------------------------
    // Command Lists
    //-------------------------------------------------------------------------
    
    // Create a command list for the specified queue type
    virtual std::unique_ptr<IRHICommandList> createCommandList(
        RHIQueueType queue = RHIQueueType::Graphics) = 0;
    
    //-------------------------------------------------------------------------
    // Command Submission
    //-------------------------------------------------------------------------
    
    // Submit command list to graphics queue
    virtual void submit(IRHICommandList* cmdList, IRHIFence* signalFence = nullptr) = 0;
    
    // Submit to specific queue
    virtual void submitAsync(IRHICommandList* cmdList, RHIQueueType queue,
                            IRHIFence* signalFence = nullptr) = 0;
    
    // Submit with wait/signal semaphores
    struct SubmitInfo {
        IRHICommandList** commandLists = nullptr;
        uint32_t commandListCount = 0;
        IRHISemaphore** waitSemaphores = nullptr;
        uint32_t waitSemaphoreCount = 0;
        IRHISemaphore** signalSemaphores = nullptr;
        uint32_t signalSemaphoreCount = 0;
        IRHIFence* signalFence = nullptr;
    };
    virtual void submit(const SubmitInfo& info, RHIQueueType queue = RHIQueueType::Graphics) = 0;
    
    //-------------------------------------------------------------------------
    // Swapchain Management
    //-------------------------------------------------------------------------
    
    // Get the current back buffer texture
    virtual IRHITexture* getBackBuffer() = 0;
    
    // Get the current back buffer index
    virtual uint32_t getBackBufferIndex() const = 0;
    
    // Get number of back buffers
    virtual uint32_t getBackBufferCount() const = 0;
    
    // Get back buffer format
    virtual RHIFormat getBackBufferFormat() const = 0;
    
    // Present the current back buffer
    virtual void present() = 0;
    
    // Handle window resize
    virtual void resize(uint32_t width, uint32_t height) = 0;
    
    // Get swapchain dimensions
    virtual uint32_t getSwapchainWidth() const = 0;
    virtual uint32_t getSwapchainHeight() const = 0;
    
    //-------------------------------------------------------------------------
    // Frame Management
    //-------------------------------------------------------------------------
    
    // Begin a new frame (waits for GPU if needed, acquires back buffer)
    virtual void beginFrame() = 0;
    
    // End the current frame (submits final commands)
    virtual void endFrame() = 0;
    
    // Get current frame index (for double/triple buffering)
    virtual uint32_t getFrameIndex() const = 0;
    
    // Get total frame count since initialization
    virtual uint64_t getFrameCount() const = 0;
    
    //-------------------------------------------------------------------------
    // Synchronization
    //-------------------------------------------------------------------------
    
    // Wait for all GPU work to complete
    virtual void waitIdle() = 0;
    
    // Wait for specific queue to be idle
    virtual void waitQueueIdle(RHIQueueType queue) = 0;
    
    //-------------------------------------------------------------------------
    // Memory Management
    //-------------------------------------------------------------------------
    
    // Get memory statistics
    virtual RHIMemoryStats getMemoryStats() const = 0;
    
    // Legacy compatibility (maps to getMemoryStats)
    uint64_t getUsedVideoMemory() const { return getMemoryStats().usedDeviceMemory; }
    uint64_t getTotalVideoMemory() const { return getMemoryStats().totalDeviceMemory; }
    
    //-------------------------------------------------------------------------
    // Debug & Profiling
    //-------------------------------------------------------------------------
    
    // Set object debug name
    virtual void setDebugName(IRHIResource* resource, const char* name) = 0;
    
    // Begin/end GPU capture (for tools like RenderDoc, PIX)
    virtual void beginCapture() = 0;
    virtual void endCapture() = 0;
    
    // Get GPU timestamp frequency (for converting query results to time)
    virtual double getTimestampFrequency() const = 0;
    
    //-------------------------------------------------------------------------
    // Shader Binding Table (for Ray Tracing)
    //-------------------------------------------------------------------------
    
    struct ShaderBindingTableInfo {
        uint32_t handleSize = 0;
        uint32_t handleAlignment = 0;
        uint32_t baseAlignment = 0;
    };
    virtual ShaderBindingTableInfo getShaderBindingTableInfo() const = 0;
    
    // Get shader group handles from ray tracing pipeline
    virtual bool getShaderGroupHandles(IRHIPipeline* pipeline,
                                       uint32_t firstGroup, uint32_t groupCount,
                                       void* data, size_t dataSize) = 0;
    
    //-------------------------------------------------------------------------
    // Immediate Mode Helpers
    //-------------------------------------------------------------------------
    
    // Execute commands immediately (blocking)
    template<typename Func>
    void executeImmediate(Func&& func) {
        auto cmd = createCommandList();
        cmd->begin();
        func(cmd.get());
        cmd->end();
        
        auto fence = createFence();
        submit(cmd.get(), fence.get());
        fence->wait();
    }
    
    // Upload data to buffer (staging)
    void uploadBuffer(IRHIBuffer* dst, const void* data, uint64_t size,
                     uint64_t dstOffset = 0) {
        auto staging = createBuffer(RHIBufferDesc::Staging(size));
        void* mapped = staging->map();
        memcpy(mapped, data, size);
        staging->unmap();
        
        executeImmediate([&](IRHICommandList* cmd) {
            cmd->barrier(RHIBarrier::Buffer(dst, RHIResourceState::Undefined, 
                                           RHIResourceState::CopyDst));
            RHIBufferCopy region;
            region.srcOffset = 0;
            region.dstOffset = dstOffset;
            region.size = size;
            cmd->copyBuffer(staging.get(), dst, &region, 1);
            cmd->barrier(RHIBarrier::Buffer(dst, RHIResourceState::CopyDst,
                                           RHIResourceState::Common));
        });
    }
    
    // Upload data to texture
    void uploadTexture(IRHITexture* dst, const void* data, uint64_t size,
                      uint32_t mipLevel = 0, uint32_t arrayLayer = 0) {
        auto staging = createBuffer(RHIBufferDesc::Staging(size));
        void* mapped = staging->map();
        memcpy(mapped, data, size);
        staging->unmap();
        
        executeImmediate([&](IRHICommandList* cmd) {
            cmd->barrier(RHIBarrier::Texture(dst, RHIResourceState::Undefined,
                                            RHIResourceState::CopyDst));
            cmd->copyBufferToTexture(staging.get(), dst, mipLevel, arrayLayer);
            cmd->barrier(RHIBarrier::Texture(dst, RHIResourceState::CopyDst,
                                            RHIResourceState::ShaderResource));
        });
    }
};

//=============================================================================
// RHI Factory
//=============================================================================

// Create RHI instance for the specified backend
std::unique_ptr<IRHI> CreateRHI(RHIBackend backend);

// Get default backend for the current platform
inline RHIBackend GetDefaultRHIBackend() {
#ifdef _WIN32
    return RHIBackend::D3D12;  // Prefer D3D12 on Windows
#else
    return RHIBackend::Vulkan;  // Vulkan elsewhere
#endif
}

// Check if a backend is available on the current platform
bool IsRHIBackendAvailable(RHIBackend backend);

} // namespace Sanic
