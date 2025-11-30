/**
 * RenderGraph.cpp
 * 
 * Implementation of Unreal Engine RDG-style Render Dependency Graph.
 */

#include "RenderGraph.h"
#include "VulkanContext.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <stack>
#include <cassert>

// ============================================================================
// PSO CACHE KEY
// ============================================================================

bool PSOCacheKey::operator==(const PSOCacheKey& other) const {
    return bindPoint == other.bindPoint &&
           colorFormats == other.colorFormats &&
           depthFormat == other.depthFormat &&
           stencilFormat == other.stencilFormat &&
           samples == other.samples &&
           vertexShaderHash == other.vertexShaderHash &&
           fragmentShaderHash == other.fragmentShaderHash &&
           computeShaderHash == other.computeShaderHash &&
           meshShaderHash == other.meshShaderHash &&
           taskShaderHash == other.taskShaderHash &&
           vertexInputHash == other.vertexInputHash &&
           rasterStateHash == other.rasterStateHash &&
           depthStencilHash == other.depthStencilHash &&
           blendStateHash == other.blendStateHash;
}

size_t PSOCacheKeyHash::operator()(const PSOCacheKey& key) const {
    size_t hash = std::hash<int>()(static_cast<int>(key.bindPoint));
    hash ^= std::hash<int>()(static_cast<int>(key.depthFormat)) << 1;
    hash ^= std::hash<int>()(static_cast<int>(key.samples)) << 2;
    hash ^= std::hash<uint64_t>()(key.vertexShaderHash) << 3;
    hash ^= std::hash<uint64_t>()(key.fragmentShaderHash) << 4;
    hash ^= std::hash<uint64_t>()(key.computeShaderHash) << 5;
    hash ^= std::hash<uint64_t>()(key.rasterStateHash) << 6;
    for (auto fmt : key.colorFormats) {
        hash ^= std::hash<int>()(static_cast<int>(fmt));
    }
    return hash;
}

// ============================================================================
// PSO CACHE
// ============================================================================

PSOCache::PSOCache(VulkanContext& context) : context(context) {
    // Create Vulkan pipeline cache
    VkPipelineCacheCreateInfo cacheInfo = {VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    vkCreatePipelineCache(context.getDevice(), &cacheInfo, nullptr, &vulkanPipelineCache);
}

PSOCache::~PSOCache() {
    for (auto& [key, pso] : psoCache) {
        if (pso.pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(context.getDevice(), pso.pipeline, nullptr);
        }
    }
    if (vulkanPipelineCache != VK_NULL_HANDLE) {
        vkDestroyPipelineCache(context.getDevice(), vulkanPipelineCache, nullptr);
    }
}

VkPipeline PSOCache::getOrCreateGraphicsPipeline(
    const PSOCacheKey& key,
    const VkGraphicsPipelineCreateInfo& createInfo
) {
    auto it = psoCache.find(key);
    if (it != psoCache.end()) {
        it->second.useCount++;
        cacheHits++;
        return it->second.pipeline;
    }
    
    cacheMisses++;
    
    VkPipeline pipeline;
    VkResult result = vkCreateGraphicsPipelines(
        context.getDevice(),
        vulkanPipelineCache,
        1, &createInfo,
        nullptr,
        &pipeline
    );
    
    if (result != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    
    CachedPSO cached;
    cached.pipeline = pipeline;
    cached.layout = createInfo.layout;
    cached.useCount = 1;
    psoCache[key] = cached;
    
    return pipeline;
}

VkPipeline PSOCache::getOrCreateComputePipeline(
    const PSOCacheKey& key,
    const VkComputePipelineCreateInfo& createInfo
) {
    auto it = psoCache.find(key);
    if (it != psoCache.end()) {
        it->second.useCount++;
        cacheHits++;
        return it->second.pipeline;
    }
    
    cacheMisses++;
    
    VkPipeline pipeline;
    VkResult result = vkCreateComputePipelines(
        context.getDevice(),
        vulkanPipelineCache,
        1, &createInfo,
        nullptr,
        &pipeline
    );
    
    if (result != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    
    CachedPSO cached;
    cached.pipeline = pipeline;
    cached.layout = createInfo.layout;
    cached.useCount = 1;
    psoCache[key] = cached;
    
    return pipeline;
}

void PSOCache::evictUnused(uint64_t currentFrame, uint64_t frameThreshold) {
    for (auto it = psoCache.begin(); it != psoCache.end();) {
        if (currentFrame - it->second.lastUsedFrame > frameThreshold) {
            vkDestroyPipeline(context.getDevice(), it->second.pipeline, nullptr);
            it = psoCache.erase(it);
        } else {
            ++it;
        }
    }
}

void PSOCache::saveToFile(const std::string& path) {
    size_t dataSize = 0;
    vkGetPipelineCacheData(context.getDevice(), vulkanPipelineCache, &dataSize, nullptr);
    
    std::vector<char> data(dataSize);
    vkGetPipelineCacheData(context.getDevice(), vulkanPipelineCache, &dataSize, data.data());
    
    std::ofstream file(path, std::ios::binary);
    file.write(data.data(), dataSize);
}

void PSOCache::loadFromFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return;
    
    size_t dataSize = file.tellg();
    file.seekg(0);
    
    std::vector<char> data(dataSize);
    file.read(data.data(), dataSize);
    
    // Destroy old cache and create new one with loaded data
    vkDestroyPipelineCache(context.getDevice(), vulkanPipelineCache, nullptr);
    
    VkPipelineCacheCreateInfo cacheInfo = {VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    cacheInfo.initialDataSize = dataSize;
    cacheInfo.pInitialData = data.data();
    vkCreatePipelineCache(context.getDevice(), &cacheInfo, nullptr, &vulkanPipelineCache);
}

// ============================================================================
// RDG RESOURCE POOL
// ============================================================================

RDGResourcePool::RDGResourcePool(VulkanContext& context) : context(context) {}

RDGResourcePool::~RDGResourcePool() {
    for (auto& tex : texturePool) {
        destroyTexture(tex.get());
    }
    for (auto& buf : bufferPool) {
        destroyBuffer(buf.get());
    }
}

bool RDGResourcePool::isCompatible(const RDGTextureDesc& a, const RDGTextureDesc& b) const {
    // For pooling, we allow some flexibility - same size and compatible usage
    return a.width == b.width && a.height == b.height &&
           a.depth == b.depth && a.format == b.format &&
           a.mipLevels == b.mipLevels && a.arrayLayers == b.arrayLayers &&
           (a.usage & b.usage) == b.usage;  // Pool texture must support required usage
}

bool RDGResourcePool::isCompatible(const RDGBufferDesc& a, const RDGBufferDesc& b) const {
    return a.size >= b.size && (a.usage & b.usage) == b.usage;
}

PooledTexture* RDGResourcePool::acquireTexture(const RDGTextureDesc& desc) {
    // Find compatible free texture
    for (auto it = freeTextures.begin(); it != freeTextures.end(); ++it) {
        if (isCompatible((*it)->desc, desc)) {
            PooledTexture* tex = *it;
            freeTextures.erase(it);
            return tex;
        }
    }
    
    // Create new texture
    return createTexture(desc);
}

void RDGResourcePool::releaseTexture(PooledTexture* texture, uint64_t frame) {
    texture->lastUsedFrame = frame;
    freeTextures.push_back(texture);
}

PooledBuffer* RDGResourcePool::acquireBuffer(const RDGBufferDesc& desc) {
    // Find compatible free buffer
    for (auto it = freeBuffers.begin(); it != freeBuffers.end(); ++it) {
        if (isCompatible((*it)->desc, desc)) {
            PooledBuffer* buf = *it;
            freeBuffers.erase(it);
            return buf;
        }
    }
    
    // Create new buffer
    return createBuffer(desc);
}

void RDGResourcePool::releaseBuffer(PooledBuffer* buffer, uint64_t frame) {
    buffer->lastUsedFrame = frame;
    freeBuffers.push_back(buffer);
}

PooledTexture* RDGResourcePool::createTexture(const RDGTextureDesc& desc) {
    auto pooled = std::make_unique<PooledTexture>();
    pooled->desc = desc;
    
    // Create image
    VkImageCreateInfo imageInfo = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = desc.imageType;
    imageInfo.extent.width = desc.width;
    imageInfo.extent.height = desc.height;
    imageInfo.extent.depth = desc.depth;
    imageInfo.mipLevels = desc.mipLevels;
    imageInfo.arrayLayers = desc.arrayLayers;
    imageInfo.format = desc.format;
    imageInfo.tiling = desc.tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = desc.usage;
    imageInfo.samples = desc.samples;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    vkCreateImage(context.getDevice(), &imageInfo, nullptr, &pooled->image);
    
    // Allocate memory
    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(context.getDevice(), pooled->image, &memReqs);
    
    VkMemoryAllocateInfo allocInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = context.findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    vkAllocateMemory(context.getDevice(), &allocInfo, nullptr, &pooled->memory);
    vkBindImageMemory(context.getDevice(), pooled->image, pooled->memory, 0);
    
    totalMemoryUsed += memReqs.size;
    
    // Create view
    VkImageViewCreateInfo viewInfo = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = pooled->image;
    viewInfo.viewType = (desc.arrayLayers > 1) ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : 
                        (desc.imageType == VK_IMAGE_TYPE_3D) ? VK_IMAGE_VIEW_TYPE_3D : VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = desc.format;
    viewInfo.subresourceRange.aspectMask = 
        (desc.format == VK_FORMAT_D32_SFLOAT || desc.format == VK_FORMAT_D24_UNORM_S8_UINT) ?
        VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = desc.mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = desc.arrayLayers;
    
    vkCreateImageView(context.getDevice(), &viewInfo, nullptr, &pooled->view);
    
    PooledTexture* result = pooled.get();
    texturePool.push_back(std::move(pooled));
    return result;
}

PooledBuffer* RDGResourcePool::createBuffer(const RDGBufferDesc& desc) {
    auto pooled = std::make_unique<PooledBuffer>();
    pooled->desc = desc;
    
    VkBufferCreateInfo bufferInfo = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = desc.size;
    bufferInfo.usage = desc.usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    vkCreateBuffer(context.getDevice(), &bufferInfo, nullptr, &pooled->buffer);
    
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(context.getDevice(), pooled->buffer, &memReqs);
    
    VkMemoryPropertyFlags memProps = desc.hostVisible ?
        (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) :
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    
    VkMemoryAllocateFlagsInfo flagsInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
    flagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    
    VkMemoryAllocateInfo allocInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.pNext = &flagsInfo;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = context.findMemoryType(memReqs.memoryTypeBits, memProps);
    
    vkAllocateMemory(context.getDevice(), &allocInfo, nullptr, &pooled->memory);
    vkBindBufferMemory(context.getDevice(), pooled->buffer, pooled->memory, 0);
    
    totalMemoryUsed += memReqs.size;
    
    // Get device address
    VkBufferDeviceAddressInfo addrInfo = {VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    addrInfo.buffer = pooled->buffer;
    pooled->deviceAddress = vkGetBufferDeviceAddress(context.getDevice(), &addrInfo);
    
    PooledBuffer* result = pooled.get();
    bufferPool.push_back(std::move(pooled));
    return result;
}

void RDGResourcePool::destroyTexture(PooledTexture* texture) {
    if (texture->view != VK_NULL_HANDLE) {
        vkDestroyImageView(context.getDevice(), texture->view, nullptr);
    }
    if (texture->image != VK_NULL_HANDLE) {
        vkDestroyImage(context.getDevice(), texture->image, nullptr);
    }
    if (texture->memory != VK_NULL_HANDLE) {
        vkFreeMemory(context.getDevice(), texture->memory, nullptr);
    }
}

void RDGResourcePool::destroyBuffer(PooledBuffer* buffer) {
    if (buffer->buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(context.getDevice(), buffer->buffer, nullptr);
    }
    if (buffer->memory != VK_NULL_HANDLE) {
        vkFreeMemory(context.getDevice(), buffer->memory, nullptr);
    }
}

void RDGResourcePool::evictUnused(uint64_t currentFrame, uint64_t frameThreshold) {
    // Evict unused textures
    freeTextures.erase(
        std::remove_if(freeTextures.begin(), freeTextures.end(),
            [&](PooledTexture* tex) {
                if (currentFrame - tex->lastUsedFrame > frameThreshold) {
                    destroyTexture(tex);
                    return true;
                }
                return false;
            }),
        freeTextures.end()
    );
    
    // Evict unused buffers
    freeBuffers.erase(
        std::remove_if(freeBuffers.begin(), freeBuffers.end(),
            [&](PooledBuffer* buf) {
                if (currentFrame - buf->lastUsedFrame > frameThreshold) {
                    destroyBuffer(buf);
                    return true;
                }
                return false;
            }),
        freeBuffers.end()
    );
}

void RDGResourcePool::trimPool(size_t maxTextures, size_t maxBuffers) {
    while (freeTextures.size() > maxTextures) {
        destroyTexture(freeTextures.back());
        freeTextures.pop_back();
    }
    while (freeBuffers.size() > maxBuffers) {
        destroyBuffer(freeBuffers.back());
        freeBuffers.pop_back();
    }
}

// ============================================================================
// RENDER GRAPH - PASS BUILDER
// ============================================================================

RenderGraph::PassBuilder::PassBuilder(RenderGraph& graph, RDGPassHandle passHandle)
    : graph(graph), passHandle(passHandle) {}

RenderGraph::PassBuilder& RenderGraph::PassBuilder::readTexture(
    RDGTextureHandle texture, RDGAccessType access,
    uint32_t mip, uint32_t mipCount,
    uint32_t layer, uint32_t layerCount
) {
    RDGPass* pass = graph.getPass(passHandle);
    if (pass) {
        RDGResourceAccess ra;
        ra.type = RDGResourceAccess::Type::Texture;
        ra.texture = texture;
        ra.access = access | RDGAccessType::Read;
        ra.mipLevel = mip;
        ra.mipCount = mipCount;
        ra.arrayLayer = layer;
        ra.layerCount = layerCount;
        pass->textureAccesses.push_back(ra);
    }
    return *this;
}

RenderGraph::PassBuilder& RenderGraph::PassBuilder::writeTexture(
    RDGTextureHandle texture, RDGAccessType access,
    uint32_t mip, uint32_t mipCount,
    uint32_t layer, uint32_t layerCount
) {
    RDGPass* pass = graph.getPass(passHandle);
    if (pass) {
        RDGResourceAccess ra;
        ra.type = RDGResourceAccess::Type::Texture;
        ra.texture = texture;
        ra.access = access | RDGAccessType::Write;
        ra.mipLevel = mip;
        ra.mipCount = mipCount;
        ra.arrayLayer = layer;
        ra.layerCount = layerCount;
        pass->textureAccesses.push_back(ra);
    }
    return *this;
}

RenderGraph::PassBuilder& RenderGraph::PassBuilder::readBuffer(
    RDGBufferHandle buffer, RDGAccessType access
) {
    RDGPass* pass = graph.getPass(passHandle);
    if (pass) {
        RDGResourceAccess ra;
        ra.type = RDGResourceAccess::Type::Buffer;
        ra.buffer = buffer;
        ra.access = access | RDGAccessType::Read;
        pass->bufferAccesses.push_back(ra);
    }
    return *this;
}

RenderGraph::PassBuilder& RenderGraph::PassBuilder::writeBuffer(
    RDGBufferHandle buffer, RDGAccessType access
) {
    RDGPass* pass = graph.getPass(passHandle);
    if (pass) {
        RDGResourceAccess ra;
        ra.type = RDGResourceAccess::Type::Buffer;
        ra.buffer = buffer;
        ra.access = access | RDGAccessType::Write;
        pass->bufferAccesses.push_back(ra);
    }
    return *this;
}

RenderGraph::PassBuilder& RenderGraph::PassBuilder::setRenderTarget(
    RDGTextureHandle texture, VkAttachmentLoadOp loadOp,
    VkAttachmentStoreOp storeOp, VkClearColorValue clearValue
) {
    RDGPass* pass = graph.getPass(passHandle);
    RDGTexture* tex = graph.getTexture(texture);
    if (pass && tex) {
        VkRenderingAttachmentInfo attachment = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        attachment.imageView = tex->view;
        attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachment.loadOp = loadOp;
        attachment.storeOp = storeOp;
        attachment.clearValue.color = clearValue;
        pass->colorAttachments.push_back(attachment);
        
        // Also track as write access
        writeTexture(texture, RDGAccessType::RTV);
    }
    return *this;
}

RenderGraph::PassBuilder& RenderGraph::PassBuilder::setDepthStencil(
    RDGTextureHandle texture, VkAttachmentLoadOp loadOp,
    VkAttachmentStoreOp storeOp, VkClearDepthStencilValue clearValue
) {
    RDGPass* pass = graph.getPass(passHandle);
    RDGTexture* tex = graph.getTexture(texture);
    if (pass && tex) {
        pass->depthAttachment = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        pass->depthAttachment.imageView = tex->view;
        pass->depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        pass->depthAttachment.loadOp = loadOp;
        pass->depthAttachment.storeOp = storeOp;
        pass->depthAttachment.clearValue.depthStencil = clearValue;
        pass->hasDepth = true;
        
        writeTexture(texture, RDGAccessType::DSV);
    }
    return *this;
}

RenderGraph::PassBuilder& RenderGraph::PassBuilder::setRenderExtent(uint32_t width, uint32_t height) {
    RDGPass* pass = graph.getPass(passHandle);
    if (pass) {
        pass->renderExtent = {width, height};
    }
    return *this;
}

// ============================================================================
// RENDER GRAPH - MAIN IMPLEMENTATION
// ============================================================================

RenderGraph::RenderGraph(VulkanContext& context) : context(context) {
    psoCache = std::make_unique<PSOCache>(context);
    resourcePool = std::make_unique<RDGResourcePool>(context);
}

RenderGraph::~RenderGraph() {
    reset();
}

RDGTextureHandle RenderGraph::createTexture(const std::string& name, const RDGTextureDesc& desc) {
    auto texture = std::make_unique<RDGTexture>();
    texture->handle = static_cast<RDGTextureHandle>(textures.size());
    texture->name = name;
    texture->desc = desc;
    texture->subresourceStates.resize(texture->getSubresourceCount());
    
    textureNameMap[name] = texture->handle;
    RDGTextureHandle handle = texture->handle;
    textures.push_back(std::move(texture));
    return handle;
}

RDGBufferHandle RenderGraph::createBuffer(const std::string& name, const RDGBufferDesc& desc) {
    auto buffer = std::make_unique<RDGBuffer>();
    buffer->handle = static_cast<RDGBufferHandle>(buffers.size());
    buffer->name = name;
    buffer->desc = desc;
    
    bufferNameMap[name] = buffer->handle;
    RDGBufferHandle handle = buffer->handle;
    buffers.push_back(std::move(buffer));
    return handle;
}

RDGTextureHandle RenderGraph::registerExternalTexture(
    const std::string& name,
    VkImage image,
    VkImageView view,
    const RDGTextureDesc& desc,
    VkImageLayout currentLayout
) {
    auto texture = std::make_unique<RDGTexture>();
    texture->handle = static_cast<RDGTextureHandle>(textures.size());
    texture->name = name;
    texture->desc = desc;
    texture->image = image;
    texture->view = view;
    texture->isExternal = true;
    texture->subresourceStates.resize(texture->getSubresourceCount());
    
    // Set initial layout for all subresources
    for (auto& state : texture->subresourceStates) {
        state.layout = currentLayout;
    }
    
    textureNameMap[name] = texture->handle;
    RDGTextureHandle handle = texture->handle;
    textures.push_back(std::move(texture));
    return handle;
}

RDGBufferHandle RenderGraph::registerExternalBuffer(
    const std::string& name,
    VkBuffer buffer,
    const RDGBufferDesc& desc
) {
    auto buf = std::make_unique<RDGBuffer>();
    buf->handle = static_cast<RDGBufferHandle>(buffers.size());
    buf->name = name;
    buf->desc = desc;
    buf->buffer = buffer;
    buf->isExternal = true;
    
    bufferNameMap[name] = buf->handle;
    RDGBufferHandle handle = buf->handle;
    buffers.push_back(std::move(buf));
    return handle;
}

RenderGraph::PassBuilder RenderGraph::addPass(
    const std::string& name,
    RDGPassFlags flags,
    RDGPass::ExecuteFunction executeFunc
) {
    auto pass = std::make_unique<RDGPass>();
    pass->handle = static_cast<RDGPassHandle>(passes.size());
    pass->name = name;
    pass->flags = flags;
    pass->executeFunc = std::move(executeFunc);
    
    RDGPassHandle handle = pass->handle;
    passes.push_back(std::move(pass));
    return PassBuilder(*this, handle);
}

RDGTexture* RenderGraph::getTexture(RDGTextureHandle handle) {
    if (handle < textures.size()) {
        return textures[handle].get();
    }
    return nullptr;
}

const RDGTexture* RenderGraph::getTexture(RDGTextureHandle handle) const {
    if (handle < textures.size()) {
        return textures[handle].get();
    }
    return nullptr;
}

RDGBuffer* RenderGraph::getBuffer(RDGBufferHandle handle) {
    if (handle < buffers.size()) {
        return buffers[handle].get();
    }
    return nullptr;
}

const RDGBuffer* RenderGraph::getBuffer(RDGBufferHandle handle) const {
    if (handle < buffers.size()) {
        return buffers[handle].get();
    }
    return nullptr;
}

RDGPass* RenderGraph::getPass(RDGPassHandle handle) {
    if (handle < passes.size()) {
        return passes[handle].get();
    }
    return nullptr;
}

VkImage RenderGraph::getTextureImage(RDGTextureHandle handle) const {
    const RDGTexture* tex = getTexture(handle);
    return tex ? tex->image : VK_NULL_HANDLE;
}

VkImageView RenderGraph::getTextureView(RDGTextureHandle handle) const {
    const RDGTexture* tex = getTexture(handle);
    return tex ? tex->view : VK_NULL_HANDLE;
}

VkBuffer RenderGraph::getBufferVk(RDGBufferHandle handle) const {
    const RDGBuffer* buf = getBuffer(handle);
    return buf ? buf->buffer : VK_NULL_HANDLE;
}

VkDeviceAddress RenderGraph::getBufferAddress(RDGBufferHandle handle) const {
    const RDGBuffer* buf = getBuffer(handle);
    return buf ? buf->deviceAddress : 0;
}

// ============================================================================
// COMPILATION
// ============================================================================

void RenderGraph::compile() {
    if (isCompiled) return;
    
    buildDependencies();
    cullUnusedPasses();
    topologicalSort();
    allocateResources();
    planBarriers();
    mergeRenderPasses();
    
    isCompiled = true;
}

void RenderGraph::buildDependencies() {
    // Track producers for each resource subresource
    std::unordered_map<uint64_t, RDGPassHandle> textureProducers;  // key = texHandle << 32 | subresource
    std::unordered_map<RDGBufferHandle, RDGPassHandle> bufferProducers;
    
    for (auto& pass : passes) {
        // Process texture accesses
        for (const auto& access : pass->textureAccesses) {
            RDGTexture* tex = getTexture(access.texture);
            if (!tex) continue;
            
            // Update first/last pass
            if (tex->firstPass == RDG_INVALID_PASS) {
                tex->firstPass = pass->handle;
            }
            tex->lastPass = pass->handle;
            
            // Handle dependencies
            for (uint32_t mip = access.mipLevel; mip < access.mipLevel + access.mipCount; ++mip) {
                for (uint32_t layer = access.arrayLayer; layer < access.arrayLayer + access.layerCount; ++layer) {
                    uint32_t subresource = tex->getSubresourceIndex(mip, layer);
                    uint64_t key = (static_cast<uint64_t>(access.texture) << 32) | subresource;
                    
                    // If reading, add dependency on producer
                    if (HasFlag(access.access, RDGAccessType::Read)) {
                        auto it = textureProducers.find(key);
                        if (it != textureProducers.end() && it->second != pass->handle) {
                            pass->producers.push_back(it->second);
                            passes[it->second]->consumers.push_back(pass->handle);
                        }
                    }
                    
                    // If writing, become the new producer
                    if (HasFlag(access.access, RDGAccessType::Write)) {
                        textureProducers[key] = pass->handle;
                    }
                }
            }
        }
        
        // Process buffer accesses
        for (const auto& access : pass->bufferAccesses) {
            RDGBuffer* buf = getBuffer(access.buffer);
            if (!buf) continue;
            
            if (buf->firstPass == RDG_INVALID_PASS) {
                buf->firstPass = pass->handle;
            }
            buf->lastPass = pass->handle;
            
            if (HasFlag(access.access, RDGAccessType::Read)) {
                auto it = bufferProducers.find(access.buffer);
                if (it != bufferProducers.end() && it->second != pass->handle) {
                    pass->producers.push_back(it->second);
                    passes[it->second]->consumers.push_back(pass->handle);
                }
            }
            
            if (HasFlag(access.access, RDGAccessType::Write)) {
                bufferProducers[access.buffer] = pass->handle;
            }
        }
    }
    
    // Remove duplicate dependencies
    for (auto& pass : passes) {
        std::sort(pass->producers.begin(), pass->producers.end());
        pass->producers.erase(std::unique(pass->producers.begin(), pass->producers.end()), pass->producers.end());
        
        std::sort(pass->consumers.begin(), pass->consumers.end());
        pass->consumers.erase(std::unique(pass->consumers.begin(), pass->consumers.end()), pass->consumers.end());
    }
}

void RenderGraph::cullUnusedPasses() {
    // Mark passes that have outputs (write to resources that are read later or are external)
    std::vector<bool> hasOutput(passes.size(), false);
    
    for (size_t i = 0; i < passes.size(); ++i) {
        // NeverCull passes are always kept
        if (HasFlag(passes[i]->flags, RDGPassFlags::NeverCull)) {
            hasOutput[i] = true;
            continue;
        }
        
        // Check if any written resource is used by another pass or is external
        for (const auto& access : passes[i]->textureAccesses) {
            if (HasFlag(access.access, RDGAccessType::Write)) {
                RDGTexture* tex = getTexture(access.texture);
                if (tex && (tex->isExternal || tex->lastPass != passes[i]->handle)) {
                    hasOutput[i] = true;
                    break;
                }
            }
        }
        
        if (!hasOutput[i]) {
            for (const auto& access : passes[i]->bufferAccesses) {
                if (HasFlag(access.access, RDGAccessType::Write)) {
                    RDGBuffer* buf = getBuffer(access.buffer);
                    if (buf && (buf->isExternal || buf->lastPass != passes[i]->handle)) {
                        hasOutput[i] = true;
                        break;
                    }
                }
            }
        }
    }
    
    // Propagate: if a pass is needed, all its producers are needed
    std::vector<bool> visited(passes.size(), false);
    std::stack<RDGPassHandle> stack;
    
    for (size_t i = 0; i < passes.size(); ++i) {
        if (hasOutput[i]) {
            stack.push(static_cast<RDGPassHandle>(i));
        }
    }
    
    while (!stack.empty()) {
        RDGPassHandle current = stack.top();
        stack.pop();
        
        if (visited[current]) continue;
        visited[current] = true;
        
        for (RDGPassHandle producer : passes[current]->producers) {
            if (!visited[producer]) {
                stack.push(producer);
            }
        }
    }
    
    // Cull passes that weren't visited
    for (size_t i = 0; i < passes.size(); ++i) {
        passes[i]->isCulled = !visited[i];
    }
}

void RenderGraph::topologicalSort() {
    executionOrder.clear();
    
    // Kahn's algorithm
    std::vector<int32_t> inDegree(passes.size(), 0);
    
    for (const auto& pass : passes) {
        if (pass->isCulled) continue;
        for (RDGPassHandle producer : pass->producers) {
            if (!passes[producer]->isCulled) {
                inDegree[pass->handle]++;
            }
        }
    }
    
    std::queue<RDGPassHandle> queue;
    for (size_t i = 0; i < passes.size(); ++i) {
        if (!passes[i]->isCulled && inDegree[i] == 0) {
            queue.push(static_cast<RDGPassHandle>(i));
        }
    }
    
    while (!queue.empty()) {
        RDGPassHandle current = queue.front();
        queue.pop();
        executionOrder.push_back(current);
        
        for (RDGPassHandle consumer : passes[current]->consumers) {
            if (!passes[consumer]->isCulled) {
                inDegree[consumer]--;
                if (inDegree[consumer] == 0) {
                    queue.push(consumer);
                }
            }
        }
    }
}

void RenderGraph::allocateResources() {
    // Allocate transient textures from pool
    for (auto& tex : textures) {
        if (tex->isExternal || tex->isCulled()) continue;
        
        PooledTexture* pooled = resourcePool->acquireTexture(tex->desc);
        tex->image = pooled->image;
        tex->view = pooled->view;
        tex->memory = pooled->memory;
    }
    
    // Allocate transient buffers from pool
    for (auto& buf : buffers) {
        if (buf->isExternal) continue;
        
        PooledBuffer* pooled = resourcePool->acquireBuffer(buf->desc);
        buf->buffer = pooled->buffer;
        buf->memory = pooled->memory;
        buf->deviceAddress = pooled->deviceAddress;
    }
}

void RenderGraph::planBarriers() {
    passBarriers.resize(executionOrder.size());
    passEpilogueBarriers.resize(executionOrder.size());
    
    for (size_t i = 0; i < executionOrder.size(); ++i) {
        passBarriers[i] = computeBarriers(executionOrder[i]);
    }
}

RDGBarrierBatch RenderGraph::computeBarriers(RDGPassHandle passHandle) {
    RDGBarrierBatch batch;
    RDGPass* pass = getPass(passHandle);
    if (!pass) return batch;
    
    // Process texture barriers
    for (const auto& access : pass->textureAccesses) {
        RDGTexture* tex = getTexture(access.texture);
        if (!tex) continue;
        
        for (uint32_t mip = access.mipLevel; mip < access.mipLevel + access.mipCount; ++mip) {
            for (uint32_t layer = access.arrayLayer; layer < access.arrayLayer + access.layerCount; ++layer) {
                uint32_t subresource = tex->getSubresourceIndex(mip, layer);
                RDGSubresourceState& state = tex->subresourceStates[subresource];
                
                RDGSubresourceState newState;
                newState.access = access.access;
                newState.layout = getOptimalLayout(access.access, tex->desc.format);
                newState.stages = getStageFlags(access.access, pass->flags);
                newState.accessMask = getAccessFlags(access.access);
                newState.producerPassIndex = HasFlag(access.access, RDGAccessType::Write) ? passHandle : state.producerPassIndex;
                
                // Check if barrier is needed
                if (state.layout != newState.layout || 
                    (state.accessMask & VK_ACCESS_2_MEMORY_WRITE_BIT) ||
                    (newState.accessMask & VK_ACCESS_2_MEMORY_WRITE_BIT)) {
                    
                    batch.imageBarriers.push_back(createImageBarrier(tex, subresource, state, newState));
                    batch.srcStageMask |= state.stages;
                    batch.dstStageMask |= newState.stages;
                }
                
                state = newState;
            }
        }
    }
    
    // Process buffer barriers
    for (const auto& access : pass->bufferAccesses) {
        RDGBuffer* buf = getBuffer(access.buffer);
        if (!buf) continue;
        
        RDGSubresourceState newState;
        newState.access = access.access;
        newState.stages = getStageFlags(access.access, pass->flags);
        newState.accessMask = getAccessFlags(access.access);
        newState.producerPassIndex = HasFlag(access.access, RDGAccessType::Write) ? passHandle : buf->state.producerPassIndex;
        
        if ((buf->state.accessMask & VK_ACCESS_2_MEMORY_WRITE_BIT) ||
            (newState.accessMask & VK_ACCESS_2_MEMORY_WRITE_BIT)) {
            
            batch.bufferBarriers.push_back(createBufferBarrier(buf, buf->state, newState));
            batch.srcStageMask |= buf->state.stages;
            batch.dstStageMask |= newState.stages;
        }
        
        buf->state = newState;
    }
    
    return batch;
}

VkImageMemoryBarrier2 RenderGraph::createImageBarrier(
    RDGTexture* texture,
    uint32_t subresource,
    const RDGSubresourceState& oldState,
    const RDGSubresourceState& newState
) {
    uint32_t mip = subresource % texture->desc.mipLevels;
    uint32_t layer = subresource / texture->desc.mipLevels;
    
    VkImageMemoryBarrier2 barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = oldState.stages != VK_PIPELINE_STAGE_2_NONE ? oldState.stages : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.srcAccessMask = oldState.accessMask;
    barrier.dstStageMask = newState.stages;
    barrier.dstAccessMask = newState.accessMask;
    barrier.oldLayout = oldState.layout;
    barrier.newLayout = newState.layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = texture->image;
    barrier.subresourceRange.aspectMask = 
        (texture->desc.format == VK_FORMAT_D32_SFLOAT || texture->desc.format == VK_FORMAT_D24_UNORM_S8_UINT) ?
        VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = mip;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = layer;
    barrier.subresourceRange.layerCount = 1;
    
    return barrier;
}

VkBufferMemoryBarrier2 RenderGraph::createBufferBarrier(
    RDGBuffer* buffer,
    const RDGSubresourceState& oldState,
    const RDGSubresourceState& newState
) {
    VkBufferMemoryBarrier2 barrier = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
    barrier.srcStageMask = oldState.stages != VK_PIPELINE_STAGE_2_NONE ? oldState.stages : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.srcAccessMask = oldState.accessMask;
    barrier.dstStageMask = newState.stages;
    barrier.dstAccessMask = newState.accessMask;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = buffer->buffer;
    barrier.offset = 0;
    barrier.size = VK_WHOLE_SIZE;
    
    return barrier;
}

VkImageLayout RenderGraph::getOptimalLayout(RDGAccessType access, VkFormat format) {
    bool isDepth = format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_D24_UNORM_S8_UINT ||
                   format == VK_FORMAT_D16_UNORM || format == VK_FORMAT_D32_SFLOAT_S8_UINT;
    
    if (HasFlag(access, RDGAccessType::RTV)) {
        return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    if (HasFlag(access, RDGAccessType::DSV)) {
        return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }
    if (HasFlag(access, RDGAccessType::UAVCompute) || HasFlag(access, RDGAccessType::UAVGraphics)) {
        return VK_IMAGE_LAYOUT_GENERAL;
    }
    if (HasFlag(access, RDGAccessType::SRVCompute) || HasFlag(access, RDGAccessType::SRVGraphics)) {
        return isDepth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    if (HasFlag(access, RDGAccessType::CopySrc)) {
        return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    }
    if (HasFlag(access, RDGAccessType::CopyDst)) {
        return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    }
    if (HasFlag(access, RDGAccessType::Present)) {
        return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }
    
    return VK_IMAGE_LAYOUT_GENERAL;
}

VkPipelineStageFlags2 RenderGraph::getStageFlags(RDGAccessType access, RDGPassFlags passFlags) {
    VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_NONE;
    
    bool isCompute = HasFlag(passFlags, RDGPassFlags::Compute);
    
    if (HasFlag(access, RDGAccessType::SRVCompute) || HasFlag(access, RDGAccessType::UAVCompute)) {
        stages |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    }
    if (HasFlag(access, RDGAccessType::SRVGraphics)) {
        stages |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
    }
    if (HasFlag(access, RDGAccessType::UAVGraphics)) {
        stages |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    }
    if (HasFlag(access, RDGAccessType::RTV)) {
        stages |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    if (HasFlag(access, RDGAccessType::DSV)) {
        stages |= VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    }
    if (HasFlag(access, RDGAccessType::CopySrc) || HasFlag(access, RDGAccessType::CopyDst)) {
        stages |= VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    }
    if (HasFlag(access, RDGAccessType::IndirectBuffer)) {
        stages |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
    }
    if (HasFlag(access, RDGAccessType::VertexBuffer) || HasFlag(access, RDGAccessType::IndexBuffer)) {
        stages |= VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
    }
    
    return stages != VK_PIPELINE_STAGE_2_NONE ? stages : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
}

VkAccessFlags2 RenderGraph::getAccessFlags(RDGAccessType access) {
    VkAccessFlags2 flags = VK_ACCESS_2_NONE;
    
    if (HasFlag(access, RDGAccessType::SRVCompute) || HasFlag(access, RDGAccessType::SRVGraphics)) {
        flags |= VK_ACCESS_2_SHADER_READ_BIT;
    }
    if (HasFlag(access, RDGAccessType::UAVCompute) || HasFlag(access, RDGAccessType::UAVGraphics)) {
        flags |= VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
    }
    if (HasFlag(access, RDGAccessType::RTV)) {
        flags |= VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    }
    if (HasFlag(access, RDGAccessType::DSV)) {
        flags |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    }
    if (HasFlag(access, RDGAccessType::CopySrc)) {
        flags |= VK_ACCESS_2_TRANSFER_READ_BIT;
    }
    if (HasFlag(access, RDGAccessType::CopyDst)) {
        flags |= VK_ACCESS_2_TRANSFER_WRITE_BIT;
    }
    if (HasFlag(access, RDGAccessType::IndirectBuffer)) {
        flags |= VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
    }
    if (HasFlag(access, RDGAccessType::VertexBuffer)) {
        flags |= VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
    }
    if (HasFlag(access, RDGAccessType::IndexBuffer)) {
        flags |= VK_ACCESS_2_INDEX_READ_BIT;
    }
    
    return flags;
}

void RenderGraph::mergeRenderPasses() {
    // TODO: Implement render pass merging for tiled GPUs
    // This would merge consecutive raster passes with compatible render targets
}

// ============================================================================
// EXECUTION
// ============================================================================

void RenderGraph::execute(VkCommandBuffer cmd) {
    if (!isCompiled) {
        compile();
    }
    
    for (size_t i = 0; i < executionOrder.size(); ++i) {
        RDGPassHandle passHandle = executionOrder[i];
        RDGPass* pass = getPass(passHandle);
        
        if (!pass || pass->isCulled) continue;
        
        // Submit prologue barriers
        if (!passBarriers[i].empty()) {
            passBarriers[i].submit(cmd);
        }
        
        // Execute the pass
        executePass(cmd, pass);
        
        // Submit epilogue barriers (for split barriers)
        if (!passEpilogueBarriers[i].empty()) {
            passEpilogueBarriers[i].submit(cmd);
        }
    }
    
    currentFrame++;
}

void RenderGraph::executePass(VkCommandBuffer cmd, RDGPass* pass) {
    if (debugOutput) {
        // Insert debug label
        VkDebugUtilsLabelEXT label = {VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
        label.pLabelName = pass->name.c_str();
        label.color[0] = 0.0f;
        label.color[1] = 1.0f;
        label.color[2] = 0.0f;
        label.color[3] = 1.0f;
        // vkCmdBeginDebugUtilsLabelEXT(cmd, &label);  // Requires extension
    }
    
    bool isRaster = HasFlag(pass->flags, RDGPassFlags::Raster);
    
    if (isRaster && !HasFlag(pass->flags, RDGPassFlags::SkipRenderPass)) {
        beginRenderPass(cmd, pass);
    }
    
    // Execute user function
    if (pass->executeFunc) {
        pass->executeFunc(cmd, *this);
    }
    
    if (isRaster && !HasFlag(pass->flags, RDGPassFlags::SkipRenderPass)) {
        endRenderPass(cmd, pass);
    }
    
    pass->isExecuted = true;
}

void RenderGraph::beginRenderPass(VkCommandBuffer cmd, RDGPass* pass) {
    // Update attachment image views from current texture state
    for (auto& attachment : pass->colorAttachments) {
        // Views should already be set from setRenderTarget
    }
    
    VkRenderingInfo renderingInfo = {VK_STRUCTURE_TYPE_RENDERING_INFO};
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = pass->renderExtent;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = static_cast<uint32_t>(pass->colorAttachments.size());
    renderingInfo.pColorAttachments = pass->colorAttachments.data();
    
    if (pass->hasDepth) {
        renderingInfo.pDepthAttachment = &pass->depthAttachment;
    }
    if (pass->hasStencil) {
        renderingInfo.pStencilAttachment = &pass->stencilAttachment;
    }
    
    vkCmdBeginRendering(cmd, &renderingInfo);
    
    // Set viewport and scissor
    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(pass->renderExtent.width);
    viewport.height = static_cast<float>(pass->renderExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    
    VkRect2D scissor = {};
    scissor.offset = {0, 0};
    scissor.extent = pass->renderExtent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

void RenderGraph::endRenderPass(VkCommandBuffer cmd, RDGPass* pass) {
    vkCmdEndRendering(cmd);
}

void RenderGraph::reset() {
    // Release pooled resources
    for (auto& tex : textures) {
        if (!tex->isExternal && tex->image != VK_NULL_HANDLE) {
            // Find and release back to pool
            // (In practice, the pool manages lifetime)
        }
    }
    
    textures.clear();
    buffers.clear();
    passes.clear();
    textureNameMap.clear();
    bufferNameMap.clear();
    executionOrder.clear();
    passBarriers.clear();
    passEpilogueBarriers.clear();
    isCompiled = false;
    
    // Periodic cleanup
    if (currentFrame % 60 == 0) {
        resourcePool->evictUnused(currentFrame);
        psoCache->evictUnused(currentFrame);
    }
}

void RenderGraph::dumpGraph(const std::string& filename) {
    std::ofstream file(filename);
    file << "digraph RenderGraph {\n";
    file << "  rankdir=TB;\n";
    file << "  node [shape=box];\n\n";
    
    // Passes
    for (const auto& pass : passes) {
        std::string color = pass->isCulled ? "gray" : 
                           HasFlag(pass->flags, RDGPassFlags::Compute) ? "lightblue" : "lightgreen";
        file << "  pass_" << pass->handle << " [label=\"" << pass->name 
             << "\" style=filled fillcolor=" << color << "];\n";
    }
    
    file << "\n";
    
    // Dependencies
    for (const auto& pass : passes) {
        for (RDGPassHandle producer : pass->producers) {
            file << "  pass_" << producer << " -> pass_" << pass->handle << ";\n";
        }
    }
    
    file << "}\n";
}

// ============================================================================
// GPU EVENT SCOPE
// ============================================================================

RDGEventScope::RDGEventScope(VkCommandBuffer cmd, const char* name) : cmd(cmd) {
    VkDebugUtilsLabelEXT label = {VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
    label.pLabelName = name;
    // vkCmdBeginDebugUtilsLabelEXT(cmd, &label);
}

RDGEventScope::~RDGEventScope() {
    // vkCmdEndDebugUtilsLabelEXT(cmd);
}
