#pragma once

#ifdef SANIC_ENABLE_VULKAN

#include "../RHI.h"
#include "../RHIResources.h"
#include "../RHICommandList.h"

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <vector>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <mutex>
#include <unordered_map>

namespace Sanic {

// Forward declarations
class VulkanBuffer;
class VulkanTexture;
class VulkanTextureView;
class VulkanSampler;
class VulkanPipeline;
class VulkanCommandList;
class VulkanFence;
class VulkanSemaphore;
class VulkanQueryPool;
class VulkanAccelerationStructure;
class Window;

// ============================================================================
// Vulkan Conversion Functions
// ============================================================================
VkFormat ToVkFormat(RHIFormat format);
RHIFormat FromVkFormat(VkFormat format);
VkImageLayout ToVkImageLayout(RHIResourceState state);
VkAccessFlags ToVkAccessFlags(RHIResourceState state);
VkPipelineStageFlags ToVkPipelineStage(RHIResourceState state);
VkBufferUsageFlags ToVkBufferUsage(RHIBufferUsage usage);
VkImageUsageFlags ToVkImageUsage(RHITextureUsage usage);
VkFilter ToVkFilter(RHIFilter filter);
VkSamplerAddressMode ToVkAddressMode(RHIAddressMode mode);
VkCompareOp ToVkCompareOp(RHICompareOp op);
VkPrimitiveTopology ToVkPrimitiveTopology(RHIPrimitiveTopology topology);
VkPolygonMode ToVkPolygonMode(RHIFillMode mode);
VkCullModeFlags ToVkCullMode(RHICullMode mode);
VkFrontFace ToVkFrontFace(RHIFrontFace face);
VkBlendFactor ToVkBlendFactor(RHIBlendFactor factor);
VkBlendOp ToVkBlendOp(RHIBlendOp op);
VkStencilOp ToVkStencilOp(RHIStencilOp op);
VkDescriptorType ToVkDescriptorType(RHIDescriptorType type);
VkShaderStageFlags ToVkShaderStage(RHIShaderStage stage);
VkImageType ToVkImageType(RHITextureDimension dim);
VkImageViewType ToVkImageViewType(RHITextureDimension dim, bool isArray);
VkSampleCountFlagBits ToVkSampleCount(RHISampleCount count);
VkBorderColor ToVkBorderColor(RHIBorderColor color);
VkIndexType ToVkIndexType(RHIIndexType type);

// ============================================================================
// Vulkan Queue Family Indices
// ============================================================================
struct VulkanQueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> computeFamily;
    std::optional<uint32_t> transferFamily;
    std::optional<uint32_t> presentFamily;
    
    bool isComplete() const {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

// ============================================================================
// Vulkan Swapchain Support Details
// ============================================================================
struct VulkanSwapchainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

// ============================================================================
// Vulkan Per-Frame Resources
// ============================================================================
struct VulkanFrameResources {
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
    VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
    VkFence inFlightFence = VK_NULL_HANDLE;
    
    // Descriptor pool per frame for dynamic allocation
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
};

// ============================================================================
// VulkanRHI - Main Vulkan Backend Implementation
// ============================================================================
class VulkanRHI : public IRHI {
public:
    VulkanRHI();
    ~VulkanRHI() override;
    
    // IRHI Interface Implementation
    bool initialize(Window& window, const RHIConfig& config) override;
    void shutdown() override;
    
    // Capabilities
    const RHICapabilities& getCapabilities() const override { return m_capabilities; }
    RHIBackend getBackend() const override { return RHIBackend::Vulkan; }
    
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
    uint32_t getBackBufferIndex() const override { return m_currentImageIndex; }
    uint32_t getBackBufferCount() const override { return static_cast<uint32_t>(m_swapchainTextures.size()); }
    RHIFormat getBackBufferFormat() const override { return m_swapchainFormat; }
    void present() override;
    void resize(uint32_t width, uint32_t height) override;
    uint32_t getSwapchainWidth() const override { return m_swapchainExtent.width; }
    uint32_t getSwapchainHeight() const override { return m_swapchainExtent.height; }
    
    // Frame Management
    void beginFrame() override;
    void endFrame() override;
    uint32_t getFrameIndex() const override { return m_currentFrame; }
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
    
    // ========================================================================
    // Vulkan-Specific Getters
    // ========================================================================
    VkInstance getInstance() const { return m_instance; }
    VkPhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }
    VkDevice getDevice() const { return m_device; }
    VmaAllocator getAllocator() const { return m_allocator; }
    VkQueue getGraphicsQueue() const { return m_graphicsQueue; }
    VkQueue getComputeQueue() const { return m_computeQueue; }
    VkQueue getTransferQueue() const { return m_transferQueue; }
    uint32_t getGraphicsQueueFamily() const { return m_queueFamilies.graphicsFamily.value_or(0); }
    uint32_t getComputeQueueFamily() const { return m_queueFamilies.computeFamily.value_or(0); }
    uint32_t getTransferQueueFamily() const { return m_queueFamilies.transferFamily.value_or(0); }
    
    VkCommandPool getCommandPool(uint32_t frameIndex) const { return m_frameResources[frameIndex].commandPool; }
    VkDescriptorPool getDescriptorPool(uint32_t frameIndex) const { return m_frameResources[frameIndex].descriptorPool; }
    
    // Helper for creating one-time command buffers
    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer commandBuffer);
    
private:
    bool createInstance(const RHIConfig& config);
    bool selectPhysicalDevice();
    bool createLogicalDevice(const RHIConfig& config);
    bool createAllocator();
    bool createSwapchain(uint32_t width, uint32_t height);
    bool createCommandPools();
    bool createSyncObjects();
    bool createDescriptorPools();
    
    void destroySwapchain();
    bool recreateSwapchain(uint32_t width, uint32_t height);
    
    VulkanQueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
    VulkanSwapchainSupportDetails querySwapchainSupport(VkPhysicalDevice device);
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, uint32_t width, uint32_t height);
    
    void queryCapabilities();
    VkQueue getQueue(RHIQueueType type) const;
    uint32_t getQueueFamilyIndex(RHIQueueType type) const;
    
private:
    // Vulkan Core Objects
    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    
    // Memory Allocator
    VmaAllocator m_allocator = VK_NULL_HANDLE;
    
    // Queues
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_computeQueue = VK_NULL_HANDLE;
    VkQueue m_transferQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;
    VulkanQueueFamilyIndices m_queueFamilies;
    
    // Swapchain
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> m_swapchainImages;
    std::vector<VkImageView> m_swapchainImageViews;
    std::vector<std::unique_ptr<VulkanTexture>> m_swapchainTextures;
    VkFormat m_swapchainImageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D m_swapchainExtent{};
    RHIFormat m_swapchainFormat = RHIFormat::Unknown;
    
    // Per-Frame Resources
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;
    std::array<VulkanFrameResources, MAX_FRAMES_IN_FLIGHT> m_frameResources;
    uint32_t m_currentFrame = 0;
    uint32_t m_currentImageIndex = 0;
    uint64_t m_frameCount = 0;
    
    // Device Capabilities
    RHICapabilities m_capabilities{};
    VkPhysicalDeviceProperties m_deviceProperties{};
    VkPhysicalDeviceFeatures m_deviceFeatures{};
    VkPhysicalDeviceVulkan12Features m_vulkan12Features{};
    VkPhysicalDeviceVulkan13Features m_vulkan13Features{};
    VkPhysicalDeviceMeshShaderFeaturesEXT m_meshShaderFeatures{};
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR m_rayTracingFeatures{};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR m_accelStructFeatures{};
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR m_rayTracingProperties{};
    
    // Configuration
    RHIConfig m_config;
    Window* m_window = nullptr;
    bool m_validationEnabled = false;
    bool m_frameStarted = false;
};

// ============================================================================
// VulkanBuffer
// ============================================================================
class VulkanBuffer : public IRHIBuffer {
public:
    VulkanBuffer(VulkanRHI* rhi, const RHIBufferDesc& desc);
    ~VulkanBuffer() override;
    
    // IRHIBuffer Interface
    uint64_t getSize() const override { return m_desc.size; }
    RHIBufferUsage getUsage() const override { return m_desc.usage; }
    RHIMemoryType getMemoryType() const override { return m_desc.memoryType; }
    void* map() override;
    void unmap() override;
    void* getMappedPointer() const override { return m_mappedPtr; }
    uint64_t getGPUAddress() const override;
    
    // Vulkan-Specific
    VkBuffer getBuffer() const { return m_buffer; }
    VmaAllocation getAllocation() const { return m_allocation; }
    
private:
    VulkanRHI* m_rhi = nullptr;
    RHIBufferDesc m_desc{};
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VkDeviceAddress m_gpuAddress = 0;
    void* m_mappedPtr = nullptr;
};

// ============================================================================
// VulkanTexture
// ============================================================================
class VulkanTexture : public IRHITexture {
public:
    VulkanTexture(VulkanRHI* rhi, const RHITextureDesc& desc);
    // Constructor for swapchain images (external ownership)
    VulkanTexture(VulkanRHI* rhi, VkImage image, VkImageView view, const RHITextureDesc& desc);
    ~VulkanTexture() override;
    
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
    
    // Vulkan-Specific
    VkImage getImage() const { return m_image; }
    VkImageView getDefaultView() const { return m_defaultView; }
    VmaAllocation getAllocation() const { return m_allocation; }
    bool ownsImage() const { return m_ownsImage; }
    
    void createDefaultView();
    
private:
    VulkanRHI* m_rhi = nullptr;
    RHITextureDesc m_desc{};
    VkImage m_image = VK_NULL_HANDLE;
    VkImageView m_defaultView = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    bool m_ownsImage = true;
};

// ============================================================================
// VulkanTextureView
// ============================================================================
class VulkanTextureView : public IRHITextureView {
public:
    VulkanTextureView(VulkanRHI* rhi, VulkanTexture* texture, RHIFormat format,
                      uint32_t baseMip, uint32_t mipCount,
                      uint32_t baseLayer, uint32_t layerCount);
    ~VulkanTextureView() override;
    
    // IRHITextureView Interface
    IRHITexture* getTexture() const override { return m_texture; }
    RHIFormat getFormat() const override { return m_format; }
    uint32_t getBaseMipLevel() const override { return m_baseMip; }
    uint32_t getMipLevelCount() const override { return m_mipCount; }
    uint32_t getBaseArrayLayer() const override { return m_baseLayer; }
    uint32_t getArrayLayerCount() const override { return m_layerCount; }
    
    // Vulkan-Specific
    VkImageView getView() const { return m_view; }
    
private:
    VulkanRHI* m_rhi = nullptr;
    VulkanTexture* m_texture = nullptr;
    VkImageView m_view = VK_NULL_HANDLE;
    RHIFormat m_format;
    uint32_t m_baseMip;
    uint32_t m_mipCount;
    uint32_t m_baseLayer;
    uint32_t m_layerCount;
};

// ============================================================================
// VulkanSampler
// ============================================================================
class VulkanSampler : public IRHISampler {
public:
    VulkanSampler(VulkanRHI* rhi, const RHISamplerDesc& desc);
    ~VulkanSampler() override;
    
    // Vulkan-Specific
    VkSampler getSampler() const { return m_sampler; }
    
private:
    VulkanRHI* m_rhi = nullptr;
    VkSampler m_sampler = VK_NULL_HANDLE;
};

// ============================================================================
// VulkanPipeline
// ============================================================================
class VulkanPipeline : public IRHIPipeline {
public:
    VulkanPipeline(VulkanRHI* rhi, RHIPipelineType type);
    ~VulkanPipeline() override;
    
    RHIPipelineType getType() const override { return m_type; }
    
    // Vulkan-Specific
    VkPipeline getPipeline() const { return m_pipeline; }
    VkPipelineLayout getLayout() const { return m_pipelineLayout; }
    VkPipelineBindPoint getBindPoint() const;
    void setPipeline(VkPipeline pipeline) { m_pipeline = pipeline; }
    void setLayout(VkPipelineLayout layout) { m_pipelineLayout = layout; }
    
    void addDescriptorSetLayout(VkDescriptorSetLayout layout) { m_descriptorSetLayouts.push_back(layout); }
    const std::vector<VkDescriptorSetLayout>& getDescriptorSetLayouts() const { return m_descriptorSetLayouts; }
    
    void setPushConstantRanges(const std::vector<VkPushConstantRange>& ranges) { m_pushConstantRanges = ranges; }
    const std::vector<VkPushConstantRange>& getPushConstantRanges() const { return m_pushConstantRanges; }
    
private:
    VulkanRHI* m_rhi = nullptr;
    RHIPipelineType m_type;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSetLayout> m_descriptorSetLayouts;
    std::vector<VkPushConstantRange> m_pushConstantRanges;
};

// ============================================================================
// VulkanFence
// ============================================================================
class VulkanFence : public IRHIFence {
public:
    VulkanFence(VulkanRHI* rhi, bool signaled);
    ~VulkanFence() override;
    
    void wait(uint64_t timeout) override;
    void reset() override;
    bool isSignaled() const override;
    uint64_t getValue() const override { return m_value; }
    void signal(uint64_t value) override;
    
    // Vulkan-Specific
    VkFence getFence() const { return m_fence; }
    
private:
    VulkanRHI* m_rhi = nullptr;
    VkFence m_fence = VK_NULL_HANDLE;
    uint64_t m_value = 0;
};

// ============================================================================
// VulkanSemaphore
// ============================================================================
class VulkanSemaphore : public IRHISemaphore {
public:
    VulkanSemaphore(VulkanRHI* rhi);
    ~VulkanSemaphore() override;
    
    // Vulkan-Specific
    VkSemaphore getSemaphore() const { return m_semaphore; }
    
private:
    VulkanRHI* m_rhi = nullptr;
    VkSemaphore m_semaphore = VK_NULL_HANDLE;
};

// ============================================================================
// VulkanQueryPool
// ============================================================================
class VulkanQueryPool : public IRHIQueryPool {
public:
    VulkanQueryPool(VulkanRHI* rhi, VkQueryType type, uint32_t count);
    ~VulkanQueryPool() override;
    
    uint32_t getQueryCount() const override { return m_count; }
    bool getResults(uint32_t firstQuery, uint32_t queryCount,
                   void* data, size_t dataSize,
                   size_t stride, bool wait) override;
    
    // Vulkan-Specific
    VkQueryPool getPool() const { return m_pool; }
    
private:
    VulkanRHI* m_rhi = nullptr;
    VkQueryPool m_pool = VK_NULL_HANDLE;
    uint32_t m_count = 0;
};

// ============================================================================
// VulkanAccelerationStructure
// ============================================================================
class VulkanAccelerationStructure : public IRHIAccelerationStructure {
public:
    VulkanAccelerationStructure(VulkanRHI* rhi, bool isTopLevel, uint64_t size);
    ~VulkanAccelerationStructure() override;
    
    uint64_t getGPUAddress() const override;
    bool isTopLevel() const override { return m_isTopLevel; }
    
    // Vulkan-Specific
    VkAccelerationStructureKHR getHandle() const { return m_handle; }
    VkBuffer getBuffer() const { return m_buffer; }
    
private:
    VulkanRHI* m_rhi = nullptr;
    VkAccelerationStructureKHR m_handle = VK_NULL_HANDLE;
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    bool m_isTopLevel = false;
    uint64_t m_size = 0;
};

// ============================================================================
// VulkanCommandList
// ============================================================================
class VulkanCommandList : public IRHICommandList {
public:
    VulkanCommandList(VulkanRHI* rhi, RHIQueueType queueType);
    ~VulkanCommandList() override;
    
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
    
    // Vulkan-Specific
    VkCommandBuffer getCommandBuffer() const { return m_commandBuffer; }
    RHIQueueType getQueueType() const { return m_queueType; }
    
private:
    VulkanRHI* m_rhi = nullptr;
    RHIQueueType m_queueType;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
    
    // Current state tracking
    VulkanPipeline* m_currentPipeline = nullptr;
    bool m_insideRenderPass = false;
};

} // namespace Sanic

#endif // SANIC_ENABLE_VULKAN
