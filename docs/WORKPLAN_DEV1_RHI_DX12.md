# Developer 1: RHI Abstraction & DirectX 12 Backend

## Assigned Tasks
- **Task 13**: Graphics API (DirectX Support) - RHI Abstraction Layer
- **Task 14 (Partial)**: Shader System Foundation

## Overview
Your responsibility is to create a Render Hardware Interface (RHI) abstraction layer that allows the engine to run on both Vulkan and DirectX 12. This is foundational work that other developers depend on.

---

## Task 13: RHI Abstraction Layer

### Objective
Abstract all Vulkan-specific code behind a platform-agnostic interface, then implement a DirectX 12 backend.

### Architecture

```
src/engine/rhi/
├── RHI.h                    # Main interface definitions
├── RHITypes.h               # Platform-agnostic type definitions
├── RHIResources.h           # Buffer, Texture, Pipeline abstractions
├── RHICommandList.h         # Command recording interface
├── RHIContext.h             # Device/queue management
├── vulkan/
│   ├── VulkanRHI.cpp
│   ├── VulkanResources.cpp
│   ├── VulkanCommandList.cpp
│   └── VulkanContext.cpp
└── d3d12/
    ├── D3D12RHI.cpp
    ├── D3D12Resources.cpp
    ├── D3D12CommandList.cpp
    └── D3D12Context.cpp
```

### Step 1: Define Core RHI Types

Create `src/engine/rhi/RHITypes.h`:

```cpp
#pragma once
#include <cstdint>
#include <string>

namespace Sanic {

// Forward declarations
class IRHIBuffer;
class IRHITexture;
class IRHIPipeline;
class IRHICommandList;
class IRHIFence;

// Handle types (opaque)
using RHIBufferHandle = IRHIBuffer*;
using RHITextureHandle = IRHITexture*;
using RHIPipelineHandle = IRHIPipeline*;

// Enums matching Vulkan/DX12 concepts
enum class RHIFormat : uint32_t {
    Unknown = 0,
    R8_UNORM,
    R8G8B8A8_UNORM,
    R8G8B8A8_SRGB,
    R16G16B16A16_FLOAT,
    R32G32B32A32_FLOAT,
    R32_FLOAT,
    R32_UINT,
    R32G32_FLOAT,
    R32G32B32_FLOAT,
    D32_FLOAT,
    D24_UNORM_S8_UINT,
    D32_FLOAT_S8_UINT,
    BC1_UNORM,
    BC3_UNORM,
    BC5_UNORM,
    BC7_UNORM,
    // Add more as needed
};

enum class RHIBufferUsage : uint32_t {
    None = 0,
    VertexBuffer = 1 << 0,
    IndexBuffer = 1 << 1,
    UniformBuffer = 1 << 2,
    StorageBuffer = 1 << 3,
    IndirectBuffer = 1 << 4,
    TransferSrc = 1 << 5,
    TransferDst = 1 << 6,
    AccelerationStructure = 1 << 7,
    ShaderBindingTable = 1 << 8,
};

enum class RHITextureUsage : uint32_t {
    None = 0,
    Sampled = 1 << 0,
    Storage = 1 << 1,
    RenderTarget = 1 << 2,
    DepthStencil = 1 << 3,
    TransferSrc = 1 << 4,
    TransferDst = 1 << 5,
};

enum class RHIResourceState : uint32_t {
    Undefined,
    Common,
    VertexBuffer,
    IndexBuffer,
    UniformBuffer,
    ShaderResource,
    UnorderedAccess,
    RenderTarget,
    DepthWrite,
    DepthRead,
    CopySrc,
    CopyDst,
    Present,
};

enum class RHIPipelineType : uint32_t {
    Graphics,
    Compute,
    RayTracing,
    MeshShader,
};

enum class RHIShaderStage : uint32_t {
    Vertex = 1 << 0,
    Fragment = 1 << 1,
    Compute = 1 << 2,
    Task = 1 << 3,
    Mesh = 1 << 4,
    RayGen = 1 << 5,
    Miss = 1 << 6,
    ClosestHit = 1 << 7,
    AnyHit = 1 << 8,
    Intersection = 1 << 9,
};

enum class RHIQueueType : uint32_t {
    Graphics,
    Compute,
    Transfer,
};

// Descriptor types
enum class RHIDescriptorType : uint32_t {
    Sampler,
    SampledImage,
    StorageImage,
    UniformBuffer,
    StorageBuffer,
    UniformBufferDynamic,
    StorageBufferDynamic,
    AccelerationStructure,
};

} // namespace Sanic
```

### Step 2: Define Resource Interfaces

Create `src/engine/rhi/RHIResources.h`:

```cpp
#pragma once
#include "RHITypes.h"
#include <vector>
#include <memory>

namespace Sanic {

// Buffer creation descriptor
struct RHIBufferDesc {
    uint64_t size = 0;
    RHIBufferUsage usage = RHIBufferUsage::None;
    bool hostVisible = false;           // CPU accessible
    bool persistentlyMapped = false;    // Keep mapped
    const char* debugName = nullptr;
};

// Texture creation descriptor
struct RHITextureDesc {
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t depth = 1;
    uint32_t mipLevels = 1;
    uint32_t arrayLayers = 1;
    RHIFormat format = RHIFormat::R8G8B8A8_UNORM;
    RHITextureUsage usage = RHITextureUsage::Sampled;
    bool isCube = false;
    const char* debugName = nullptr;
};

// Sampler descriptor
struct RHISamplerDesc {
    enum class Filter { Nearest, Linear };
    enum class AddressMode { Repeat, Clamp, Mirror, Border };
    
    Filter minFilter = Filter::Linear;
    Filter magFilter = Filter::Linear;
    Filter mipFilter = Filter::Linear;
    AddressMode addressU = AddressMode::Repeat;
    AddressMode addressV = AddressMode::Repeat;
    AddressMode addressW = AddressMode::Repeat;
    float maxAnisotropy = 16.0f;
    bool compareEnable = false;
    float minLod = 0.0f;
    float maxLod = 1000.0f;
};

// Pipeline descriptor
struct RHIGraphicsPipelineDesc {
    std::vector<uint32_t> vertexShaderSpirv;
    std::vector<uint32_t> fragmentShaderSpirv;
    
    // Vertex input
    struct VertexAttribute {
        uint32_t location;
        uint32_t binding;
        RHIFormat format;
        uint32_t offset;
    };
    std::vector<VertexAttribute> vertexAttributes;
    
    struct VertexBinding {
        uint32_t binding;
        uint32_t stride;
        bool perInstance;
    };
    std::vector<VertexBinding> vertexBindings;
    
    // Rasterization
    enum class CullMode { None, Front, Back };
    enum class FillMode { Solid, Wireframe };
    CullMode cullMode = CullMode::Back;
    FillMode fillMode = FillMode::Solid;
    bool depthClampEnable = false;
    
    // Depth/stencil
    bool depthTestEnable = true;
    bool depthWriteEnable = true;
    enum class CompareOp { Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always };
    CompareOp depthCompareOp = CompareOp::Less;
    
    // Blend state
    struct BlendState {
        bool enable = false;
        // ... blend factors
    };
    std::vector<BlendState> blendStates;
    
    // Render targets
    std::vector<RHIFormat> colorFormats;
    RHIFormat depthFormat = RHIFormat::D32_FLOAT;
    
    const char* debugName = nullptr;
};

struct RHIComputePipelineDesc {
    std::vector<uint32_t> computeShaderSpirv;
    const char* debugName = nullptr;
};

// Resource interfaces
class IRHIBuffer {
public:
    virtual ~IRHIBuffer() = default;
    virtual uint64_t getSize() const = 0;
    virtual void* map() = 0;
    virtual void unmap() = 0;
    virtual uint64_t getGPUAddress() const = 0;  // For buffer device address
};

class IRHITexture {
public:
    virtual ~IRHITexture() = default;
    virtual uint32_t getWidth() const = 0;
    virtual uint32_t getHeight() const = 0;
    virtual uint32_t getDepth() const = 0;
    virtual RHIFormat getFormat() const = 0;
};

class IRHISampler {
public:
    virtual ~IRHISampler() = default;
};

class IRHIPipeline {
public:
    virtual ~IRHIPipeline() = default;
    virtual RHIPipelineType getType() const = 0;
};

class IRHIFence {
public:
    virtual ~IRHIFence() = default;
    virtual void wait(uint64_t timeout = UINT64_MAX) = 0;
    virtual void reset() = 0;
    virtual bool isSignaled() const = 0;
};

} // namespace Sanic
```

### Step 3: Define Command List Interface

Create `src/engine/rhi/RHICommandList.h`:

```cpp
#pragma once
#include "RHIResources.h"
#include <glm/glm.hpp>

namespace Sanic {

struct RHIViewport {
    float x, y, width, height;
    float minDepth, maxDepth;
};

struct RHIScissor {
    int32_t x, y;
    uint32_t width, height;
};

struct RHIBufferCopy {
    uint64_t srcOffset;
    uint64_t dstOffset;
    uint64_t size;
};

struct RHITextureCopy {
    uint32_t srcMip, srcLayer;
    uint32_t dstMip, dstLayer;
    uint32_t width, height, depth;
};

// Resource barrier
struct RHIBarrier {
    enum class Type { Buffer, Texture, Global };
    Type type;
    
    union {
        struct {
            IRHIBuffer* buffer;
            RHIResourceState before;
            RHIResourceState after;
        } bufferBarrier;
        
        struct {
            IRHITexture* texture;
            RHIResourceState before;
            RHIResourceState after;
            uint32_t baseMip;
            uint32_t mipCount;
            uint32_t baseLayer;
            uint32_t layerCount;
        } textureBarrier;
    };
    
    static RHIBarrier Buffer(IRHIBuffer* buf, RHIResourceState before, RHIResourceState after) {
        RHIBarrier b;
        b.type = Type::Buffer;
        b.bufferBarrier = {buf, before, after};
        return b;
    }
    
    static RHIBarrier Texture(IRHITexture* tex, RHIResourceState before, RHIResourceState after,
                              uint32_t baseMip = 0, uint32_t mipCount = 1,
                              uint32_t baseLayer = 0, uint32_t layerCount = 1) {
        RHIBarrier b;
        b.type = Type::Texture;
        b.textureBarrier = {tex, before, after, baseMip, mipCount, baseLayer, layerCount};
        return b;
    }
};

class IRHICommandList {
public:
    virtual ~IRHICommandList() = default;
    
    // Lifecycle
    virtual void begin() = 0;
    virtual void end() = 0;
    virtual void reset() = 0;
    
    // Resource barriers
    virtual void barrier(const RHIBarrier* barriers, uint32_t count) = 0;
    
    // Render pass
    virtual void beginRenderPass(IRHITexture** colorTargets, uint32_t colorCount,
                                 IRHITexture* depthTarget,
                                 const glm::vec4* clearColors,
                                 float clearDepth, uint8_t clearStencil) = 0;
    virtual void endRenderPass() = 0;
    
    // Pipeline binding
    virtual void setPipeline(IRHIPipeline* pipeline) = 0;
    virtual void setViewport(const RHIViewport& viewport) = 0;
    virtual void setScissor(const RHIScissor& scissor) = 0;
    
    // Resource binding
    virtual void setVertexBuffer(uint32_t slot, IRHIBuffer* buffer, uint64_t offset = 0) = 0;
    virtual void setIndexBuffer(IRHIBuffer* buffer, uint64_t offset = 0, bool use32Bit = true) = 0;
    virtual void pushConstants(RHIShaderStage stages, uint32_t offset, uint32_t size, const void* data) = 0;
    
    // Descriptor binding (simplified - expand for full bindless)
    virtual void bindBuffer(uint32_t set, uint32_t binding, IRHIBuffer* buffer) = 0;
    virtual void bindTexture(uint32_t set, uint32_t binding, IRHITexture* texture, IRHISampler* sampler) = 0;
    virtual void bindStorageTexture(uint32_t set, uint32_t binding, IRHITexture* texture) = 0;
    
    // Draw commands
    virtual void draw(uint32_t vertexCount, uint32_t instanceCount = 1,
                     uint32_t firstVertex = 0, uint32_t firstInstance = 0) = 0;
    virtual void drawIndexed(uint32_t indexCount, uint32_t instanceCount = 1,
                            uint32_t firstIndex = 0, int32_t vertexOffset = 0,
                            uint32_t firstInstance = 0) = 0;
    virtual void drawIndirect(IRHIBuffer* buffer, uint64_t offset, uint32_t drawCount, uint32_t stride) = 0;
    virtual void drawIndexedIndirect(IRHIBuffer* buffer, uint64_t offset, uint32_t drawCount, uint32_t stride) = 0;
    
    // Mesh shaders
    virtual void dispatchMesh(uint32_t groupCountX, uint32_t groupCountY = 1, uint32_t groupCountZ = 1) = 0;
    virtual void dispatchMeshIndirect(IRHIBuffer* buffer, uint64_t offset) = 0;
    
    // Compute
    virtual void dispatch(uint32_t groupCountX, uint32_t groupCountY = 1, uint32_t groupCountZ = 1) = 0;
    virtual void dispatchIndirect(IRHIBuffer* buffer, uint64_t offset) = 0;
    
    // Copy operations
    virtual void copyBuffer(IRHIBuffer* src, IRHIBuffer* dst, const RHIBufferCopy* regions, uint32_t count) = 0;
    virtual void copyTexture(IRHITexture* src, IRHITexture* dst, const RHITextureCopy* regions, uint32_t count) = 0;
    virtual void copyBufferToTexture(IRHIBuffer* src, IRHITexture* dst,
                                     uint64_t bufferOffset, uint32_t mip, uint32_t layer) = 0;
    
    // Debug
    virtual void beginDebugLabel(const char* name, const glm::vec4& color = glm::vec4(1)) = 0;
    virtual void endDebugLabel() = 0;
};

} // namespace Sanic
```

### Step 4: Define RHI Device Interface

Create `src/engine/rhi/RHI.h`:

```cpp
#pragma once
#include "RHICommandList.h"
#include <memory>
#include <functional>

namespace Sanic {

class Window;

enum class RHIBackend {
    Vulkan,
    D3D12,
};

struct RHIConfig {
    RHIBackend backend = RHIBackend::Vulkan;
    bool enableValidation = true;
    bool enableRayTracing = true;
    bool enableMeshShaders = true;
    uint32_t frameBufferCount = 2;
};

struct RHICapabilities {
    bool supportsRayTracing = false;
    bool supportsMeshShaders = false;
    bool supportsVariableRateShading = false;
    bool supports64BitAtomics = false;
    bool supportsBufferDeviceAddress = true;
    uint32_t maxBoundDescriptorSets = 8;
    uint32_t maxPushConstantSize = 128;
    uint64_t maxBufferSize = 0;
    uint64_t maxStorageBufferSize = 0;
    std::string deviceName;
    std::string apiVersion;
};

class IRHI {
public:
    virtual ~IRHI() = default;
    
    // Initialization
    virtual bool initialize(Window& window, const RHIConfig& config) = 0;
    virtual void shutdown() = 0;
    
    // Capabilities
    virtual const RHICapabilities& getCapabilities() const = 0;
    virtual RHIBackend getBackend() const = 0;
    
    // Resource creation
    virtual std::unique_ptr<IRHIBuffer> createBuffer(const RHIBufferDesc& desc) = 0;
    virtual std::unique_ptr<IRHITexture> createTexture(const RHITextureDesc& desc) = 0;
    virtual std::unique_ptr<IRHISampler> createSampler(const RHISamplerDesc& desc) = 0;
    virtual std::unique_ptr<IRHIPipeline> createGraphicsPipeline(const RHIGraphicsPipelineDesc& desc) = 0;
    virtual std::unique_ptr<IRHIPipeline> createComputePipeline(const RHIComputePipelineDesc& desc) = 0;
    virtual std::unique_ptr<IRHIFence> createFence(bool signaled = false) = 0;
    
    // Command lists
    virtual std::unique_ptr<IRHICommandList> createCommandList(RHIQueueType queue = RHIQueueType::Graphics) = 0;
    
    // Submission
    virtual void submit(IRHICommandList* cmdList, IRHIFence* fence = nullptr) = 0;
    virtual void submitAsync(IRHICommandList* cmdList, RHIQueueType queue, IRHIFence* fence = nullptr) = 0;
    
    // Swapchain
    virtual IRHITexture* getBackBuffer() = 0;
    virtual uint32_t getBackBufferIndex() = 0;
    virtual void present() = 0;
    virtual void resize(uint32_t width, uint32_t height) = 0;
    
    // Sync
    virtual void waitIdle() = 0;
    virtual void beginFrame() = 0;
    virtual void endFrame() = 0;
    
    // Memory stats
    virtual uint64_t getUsedVideoMemory() const = 0;
    virtual uint64_t getTotalVideoMemory() const = 0;
};

// Factory function
std::unique_ptr<IRHI> CreateRHI(RHIBackend backend);

} // namespace Sanic
```

---

## Unreal Engine Reference Files

Study these Unreal source files for implementation guidance:

### Core RHI Architecture
```
Engine/Source/Runtime/RHI/
├── Public/
│   ├── RHI.h                           # Main RHI interface
│   ├── RHIResources.h                  # Resource types
│   ├── RHICommandList.h                # Command recording
│   ├── RHIDefinitions.h                # Enums and flags
│   └── RHIContext.h                    # Execution context
└── Private/
    └── RHI.cpp                         # Core implementation
```

**Key file to study**: `Engine/Source/Runtime/RHI/Public/RHI.h`
- Look at `FRHIResource` base class
- Study `FRHICommandList` interface
- Examine resource creation patterns

### D3D12 Backend
```
Engine/Source/Runtime/D3D12RHI/
├── Private/
│   ├── D3D12RHI.cpp                    # Main implementation
│   ├── D3D12Device.cpp                 # Device management
│   ├── D3D12Commands.cpp               # Command list impl
│   ├── D3D12Resources.cpp              # Buffer/texture
│   ├── D3D12Shader.cpp                 # Shader handling
│   ├── D3D12StateCache.cpp             # PSO caching
│   └── D3D12Descriptors.cpp            # Descriptor heaps
└── Public/
    └── D3D12RHI.h
```

**Key patterns to study**:

1. **Root Signature Management** (`D3D12RootSignature.cpp`)
   - DX12 uses root signatures instead of descriptor set layouts
   - Map Vulkan descriptor sets → root parameters

2. **Descriptor Heap Management** (`D3D12Descriptors.cpp`)
   - DX12 has limited descriptor heaps
   - Unreal uses ring buffer allocation

3. **Resource State Tracking** (`D3D12Resources.cpp`)
   - DX12 requires explicit state transitions
   - Study `FD3D12Resource::TransitionResource()`

4. **Command Allocator Pooling** (`D3D12CommandAllocator.cpp`)
   - DX12 command allocators can't be reused until GPU completes
   - Unreal pools allocators per-frame

### Vulkan Backend (for reference)
```
Engine/Source/Runtime/VulkanRHI/
├── Private/
│   ├── VulkanRHI.cpp
│   ├── VulkanDevice.cpp
│   ├── VulkanCommands.cpp
│   ├── VulkanResources.cpp
│   ├── VulkanPipeline.cpp
│   └── VulkanDescriptors.cpp
└── Public/
    └── VulkanRHI.h
```

---

## Task 14 (Partial): Shader Compilation Integration

Your shader work focuses on the **RHI integration** - making shaders work across both APIs.

### Shader Bytecode Abstraction

```cpp
// src/engine/rhi/RHIShader.h
#pragma once
#include "RHITypes.h"
#include <vector>
#include <string>

namespace Sanic {

struct RHIShaderBytecode {
    std::vector<uint8_t> spirv;          // Always present (source of truth)
    std::vector<uint8_t> dxil;           // Generated for D3D12
    RHIShaderStage stage;
    std::string entryPoint = "main";
};

class RHIShaderCompiler {
public:
    // Compile GLSL to SPIR-V (existing)
    static bool CompileGLSLToSPIRV(const std::string& source, 
                                    RHIShaderStage stage,
                                    std::vector<uint32_t>& outSpirv,
                                    std::string& outErrors);
    
    // Cross-compile SPIR-V to DXIL (for D3D12)
    // Uses SPIRV-Cross + DXC
    static bool CrossCompileToDXIL(const std::vector<uint32_t>& spirv,
                                   RHIShaderStage stage,
                                   std::vector<uint8_t>& outDxil,
                                   std::string& outErrors);
    
    // Reflect shader bindings
    struct ShaderReflection {
        struct Binding {
            uint32_t set;
            uint32_t binding;
            RHIDescriptorType type;
            uint32_t count;
            std::string name;
        };
        std::vector<Binding> bindings;
        
        struct PushConstant {
            uint32_t offset;
            uint32_t size;
            RHIShaderStage stages;
        };
        std::vector<PushConstant> pushConstants;
    };
    
    static bool ReflectSPIRV(const std::vector<uint32_t>& spirv,
                             ShaderReflection& outReflection);
};

} // namespace Sanic
```

### Cross-Compilation Pipeline

Use these tools:
1. **SPIRV-Cross**: Convert SPIR-V to HLSL
2. **DXC (DirectX Shader Compiler)**: Compile HLSL to DXIL

```cpp
// Pseudo-implementation
bool RHIShaderCompiler::CrossCompileToDXIL(
    const std::vector<uint32_t>& spirv,
    RHIShaderStage stage,
    std::vector<uint8_t>& outDxil,
    std::string& outErrors) 
{
    // Step 1: SPIRV-Cross to HLSL
    spirv_cross::CompilerHLSL hlslCompiler(spirv);
    spirv_cross::CompilerHLSL::Options opts;
    opts.shader_model = 66;  // SM 6.6
    hlslCompiler.set_hlsl_options(opts);
    std::string hlsl = hlslCompiler.compile();
    
    // Step 2: DXC to DXIL
    // Use IDxcCompiler3 interface
    // Target: lib_6_6 for ray tracing, vs_6_6/ps_6_6 for raster, cs_6_6 for compute
    
    return true;
}
```

---

## Implementation Checklist

### Week 1-2: Core Types & Vulkan Refactor
- [ ] Create RHI type definitions
- [ ] Create resource interfaces
- [ ] Create command list interface
- [ ] Refactor existing VulkanContext to implement IRHI
- [ ] Ensure engine still works with VulkanRHI

### Week 3-4: D3D12 Device & Resources
- [ ] D3D12 device initialization (IDXGIFactory, ID3D12Device)
- [ ] Command queue and allocator management
- [ ] Descriptor heap management
- [ ] Buffer creation (committed resources)
- [ ] Texture creation (placed resources)
- [ ] Swapchain (IDXGISwapChain4)

### Week 5-6: D3D12 Commands & Pipelines
- [ ] Command list implementation
- [ ] Root signature generation from reflection
- [ ] PSO creation and caching
- [ ] Resource barriers
- [ ] Draw/dispatch commands

### Week 7-8: Shader Cross-Compilation
- [ ] Integrate SPIRV-Cross
- [ ] Integrate DXC
- [ ] Shader reflection
- [ ] Root signature auto-generation
- [ ] Test all existing shaders on D3D12

---

## Dependencies

### Required Libraries
```cmake
# D3D12
find_package(directx-headers CONFIG REQUIRED)
find_package(directx-agility-sdk CONFIG REQUIRED)  # Latest D3D12

# Shader compilation
find_package(spirv_cross_core CONFIG REQUIRED)
find_package(spirv_cross_hlsl CONFIG REQUIRED)
# DXC comes with Windows SDK or download from GitHub
```

### Windows SDK
- Minimum: Windows 10 SDK 10.0.19041.0
- Recommended: Windows 11 SDK for mesh shaders

---

## Testing Strategy

### Unit Tests
1. Create buffer → verify size, map/unmap
2. Create texture → verify dimensions, format
3. Create pipeline → verify compilation
4. Record commands → verify no crashes
5. Execute commands → verify output

### Integration Tests
1. Render triangle on D3D12
2. Compute shader execution
3. Multi-frame rendering
4. Window resize handling
5. Run existing Nanite pipeline on D3D12

### Validation
- Enable D3D12 Debug Layer
- Use PIX for GPU capture
- Compare frame output between Vulkan and D3D12

---

## Coordination Notes

- **Developer 2** (Shader System) depends on your RHIShaderBytecode structure
- **Developer 3** (Editor/ImGui) will use your RHI for rendering
- Keep the existing VulkanContext working while building RHI
- Use `#ifdef` or runtime backend selection for testing both

---

## Resources

### Documentation
- [D3D12 Programming Guide](https://docs.microsoft.com/en-us/windows/win32/direct3d12/)
- [D3D12 Memory Management](https://docs.microsoft.com/en-us/windows/win32/direct3d12/memory-management)
- [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross)
- [DXC](https://github.com/microsoft/DirectXShaderCompiler)

### Sample Code
- [D3D12 Hello World](https://github.com/microsoft/DirectX-Graphics-Samples)
- [D3D12MA (AMD Memory Allocator)](https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator)

