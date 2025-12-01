#include "VirtualTextureSystem.h"
#include "VulkanRenderer.h"
#include "Pipeline.h"
#include "Descriptor.h"
#include "Buffer.h"
#include "Image.h"

#include <fstream>
#include <algorithm>
#include <cstring>

namespace Kinetic {

//------------------------------------------------------------------------------
// FileVTPageProvider
//------------------------------------------------------------------------------

FileVTPageProvider::FileVTPageProvider(const std::string& basePath, const VirtualTextureConfig& config)
    : m_basePath(basePath), m_config(config) {}

bool FileVTPageProvider::loadPage(const VTPageId& pageId, void* outData, size_t dataSize) {
    // Construct file path: basePath/vtX/mipY/pageX_pageY.bin
    char path[512];
    snprintf(path, sizeof(path), "%s/vt%u/mip%u/page_%u_%u.bin",
             m_basePath.c_str(), pageId.vtIndex, pageId.mipLevel, pageId.pageX, pageId.pageY);
    
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    file.read(static_cast<char*>(outData), dataSize);
    return file.good();
}

size_t FileVTPageProvider::getPageDataSize() const {
    uint32_t pageWithPadding = m_config.pageSize + m_config.pagePadding * 2;
    return pageWithPadding * pageWithPadding * 4;  // RGBA8
}

bool FileVTPageProvider::pageExists(const VTPageId& pageId) const {
    char path[512];
    snprintf(path, sizeof(path), "%s/vt%u/mip%u/page_%u_%u.bin",
             m_basePath.c_str(), pageId.vtIndex, pageId.mipLevel, pageId.pageX, pageId.pageY);
    
    std::ifstream file(path);
    return file.good();
}

//------------------------------------------------------------------------------
// ProceduralVTPageProvider
//------------------------------------------------------------------------------

ProceduralVTPageProvider::ProceduralVTPageProvider(GeneratorFunc generator, const VirtualTextureConfig& config)
    : m_generator(std::move(generator)), m_config(config) {}

bool ProceduralVTPageProvider::loadPage(const VTPageId& pageId, void* outData, size_t dataSize) {
    if (m_generator) {
        m_generator(pageId, outData, dataSize);
        return true;
    }
    return false;
}

size_t ProceduralVTPageProvider::getPageDataSize() const {
    uint32_t pageWithPadding = m_config.pageSize + m_config.pagePadding * 2;
    return pageWithPadding * pageWithPadding * 4;
}

bool ProceduralVTPageProvider::pageExists(const VTPageId& pageId) const {
    // Procedural pages always exist within bounds
    uint32_t mipScale = 1u << pageId.mipLevel;
    uint32_t maxPages = m_config.virtualWidth / (m_config.pageSize * mipScale);
    return pageId.pageX < maxPages && pageId.pageY < maxPages;
}

//------------------------------------------------------------------------------
// VirtualTextureSystem
//------------------------------------------------------------------------------

VirtualTextureSystem::VirtualTextureSystem() {}

VirtualTextureSystem::~VirtualTextureSystem() {
    shutdown();
}

bool VirtualTextureSystem::initialize(VulkanRenderer* renderer, const VirtualTextureConfig& defaultConfig) {
    m_renderer = renderer;
    m_defaultConfig = defaultConfig;
    
    if (!renderer) {
        return false;
    }
    
    // Create physical cache
    createPhysicalCache();
    
    // Create page table
    createPageTable();
    
    // Create feedback buffer
    createFeedbackBuffer();
    
    // Create pipelines
    createPipelines();
    
    // Create descriptor sets
    createDescriptorSets();
    
    // Create cache sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxAnisotropy = 16.0f;
    samplerInfo.anisotropyEnable = VK_TRUE;
    vkCreateSampler(m_renderer->getDevice(), &samplerInfo, nullptr, &m_cacheSampler);
    
    // Create staging buffer for page uploads
    size_t pageDataSize = (m_defaultConfig.pageSize + m_defaultConfig.pagePadding * 2);
    pageDataSize *= pageDataSize * 4;  // RGBA8
    m_uploadStaging = std::make_unique<Buffer>();
    m_uploadStaging->create(m_renderer, pageDataSize * 16,  // Buffer for 16 pages
                            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                            VMA_MEMORY_USAGE_CPU_TO_GPU);
    
    // Start streaming thread
    m_streamingThreadRunning = true;
    m_streamingThread = std::thread(&VirtualTextureSystem::streamingThreadFunc, this);
    
    return true;
}

void VirtualTextureSystem::shutdown() {
    // Stop streaming thread
    m_streamingThreadRunning = false;
    if (m_streamingThread.joinable()) {
        m_streamingThread.join();
    }
    
    if (m_renderer) {
        vkDeviceWaitIdle(m_renderer->getDevice());
        
        if (m_cacheSampler != VK_NULL_HANDLE) {
            vkDestroySampler(m_renderer->getDevice(), m_cacheSampler, nullptr);
            m_cacheSampler = VK_NULL_HANDLE;
        }
    }
    
    m_virtualTextures.clear();
    m_physicalCache.reset();
    m_pageTable.reset();
    m_pageTableStaging.reset();
    m_feedbackBuffer.reset();
    m_feedbackReadback.reset();
    m_uploadStaging.reset();
    
    m_feedbackPipeline.reset();
    m_pageTableUpdatePipeline.reset();
    m_feedbackDescSet.reset();
    m_vtSampleDescSet.reset();
    
    m_pageMapping.clear();
    m_physicalPages.clear();
    m_freePages.clear();
    
    m_renderer = nullptr;
}

void VirtualTextureSystem::createPhysicalCache() {
    // Physical cache is a large texture atlas
    m_physicalCache = std::make_unique<Image>();
    m_physicalCache->create2D(m_renderer,
                               m_defaultConfig.physicalCacheWidth,
                               m_defaultConfig.physicalCacheHeight,
                               m_defaultConfig.format,
                               VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    
    // Calculate number of physical pages
    uint32_t pageWithPadding = m_defaultConfig.pageSize + m_defaultConfig.pagePadding * 2;
    m_physicalPagesX = m_defaultConfig.physicalCacheWidth / pageWithPadding;
    m_physicalPagesY = m_defaultConfig.physicalCacheHeight / pageWithPadding;
    
    // Initialize physical pages
    m_physicalPages.resize(m_physicalPagesX * m_physicalPagesY);
    for (uint32_t y = 0; y < m_physicalPagesY; y++) {
        for (uint32_t x = 0; x < m_physicalPagesX; x++) {
            uint32_t idx = y * m_physicalPagesX + x;
            m_physicalPages[idx].physicalX = x;
            m_physicalPages[idx].physicalY = y;
            m_physicalPages[idx].valid = false;
            m_freePages.push_back(idx);
        }
    }
}

void VirtualTextureSystem::createPageTable() {
    // Page table maps virtual pages to physical pages
    // Format: R16G16B16A16_UINT (physX, physY, mipLevel, flags)
    uint32_t maxMips = m_defaultConfig.maxMipLevels;
    uint32_t tableSize = m_defaultConfig.virtualWidth / m_defaultConfig.pageSize;
    
    m_pageTable = std::make_unique<Image>();
    m_pageTable->create2D(m_renderer, tableSize, tableSize,
                          VK_FORMAT_R16G16B16A16_UINT,
                          VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    
    // Staging buffer for CPU updates
    m_pageTableStaging = std::make_unique<Buffer>();
    m_pageTableStaging->create(m_renderer, tableSize * tableSize * 8,  // 4 * uint16
                                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                VMA_MEMORY_USAGE_CPU_TO_GPU);
}

void VirtualTextureSystem::createFeedbackBuffer() {
    // Feedback buffer records which pages are needed
    // Format: R16G16B16A16_UINT (pageX, pageY, mipLevel, vtIndex)
    m_feedbackBuffer = std::make_unique<Image>();
    m_feedbackBuffer->create2D(m_renderer,
                                m_defaultConfig.feedbackWidth,
                                m_defaultConfig.feedbackHeight,
                                VK_FORMAT_R16G16B16A16_UINT,
                                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    
    // Readback buffer
    size_t readbackSize = m_defaultConfig.feedbackWidth * m_defaultConfig.feedbackHeight * 8;
    m_feedbackReadback = std::make_unique<Buffer>();
    m_feedbackReadback->create(m_renderer, readbackSize,
                                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                VMA_MEMORY_USAGE_GPU_TO_CPU);
}

void VirtualTextureSystem::createPipelines() {
    // Feedback pass pipeline
    m_feedbackPipeline = std::make_unique<GraphicsPipeline>();
    m_feedbackPipeline->createFromShaders(m_renderer,
                                           "shaders/vt_feedback.vert.spv",
                                           "shaders/vt_feedback.frag.spv");
    
    // Page table update compute pipeline
    m_pageTableUpdatePipeline = std::make_unique<ComputePipeline>();
    m_pageTableUpdatePipeline->create(m_renderer, "shaders/vt_page_table.comp.spv");
}

void VirtualTextureSystem::createDescriptorSets() {
    // Feedback descriptor set
    m_feedbackDescSet = std::make_unique<DescriptorSet>();
    m_feedbackDescSet->addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
    m_feedbackDescSet->addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);  // Page table
    m_feedbackDescSet->create(m_renderer);
    
    // VT sampling descriptor set
    m_vtSampleDescSet = std::make_unique<DescriptorSet>();
    m_vtSampleDescSet->addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);  // Physical cache
    m_vtSampleDescSet->addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);  // Page table
    m_vtSampleDescSet->addBinding(2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT);         // VT params
    m_vtSampleDescSet->create(m_renderer);
}

uint32_t VirtualTextureSystem::createVirtualTexture(const VirtualTextureConfig& config,
                                                     std::unique_ptr<IVTPageProvider> pageProvider) {
    auto vt = std::make_unique<VirtualTexture>();
    vt->id = m_nextVTId++;
    vt->config = config;
    vt->pageProvider = std::move(pageProvider);
    
    uint32_t id = vt->id;
    m_virtualTextures.push_back(std::move(vt));
    
    return id;
}

void VirtualTextureSystem::destroyVirtualTexture(uint32_t id) {
    auto it = std::find_if(m_virtualTextures.begin(), m_virtualTextures.end(),
                           [id](const auto& vt) { return vt->id == id; });
    if (it != m_virtualTextures.end()) {
        // Evict all pages belonging to this VT
        for (auto& page : m_physicalPages) {
            if (page.valid && page.virtualPage.vtIndex == id) {
                page.valid = false;
                m_freePages.push_back(&page - m_physicalPages.data());
            }
        }
        
        // Remove from mapping
        for (auto it2 = m_pageMapping.begin(); it2 != m_pageMapping.end();) {
            if (it2->first.vtIndex == id) {
                it2 = m_pageMapping.erase(it2);
            } else {
                ++it2;
            }
        }
        
        m_virtualTextures.erase(it);
    }
}

VirtualTexture* VirtualTextureSystem::getVirtualTexture(uint32_t id) {
    for (auto& vt : m_virtualTextures) {
        if (vt->id == id) {
            return vt.get();
        }
    }
    return nullptr;
}

void VirtualTextureSystem::setWorldMapping(uint32_t vtId, const glm::vec2& origin, const glm::vec2& size) {
    if (auto* vt = getVirtualTexture(vtId)) {
        vt->worldOrigin = origin;
        vt->worldSize = size;
    }
}

void VirtualTextureSystem::beginFrame(uint64_t frameNumber) {
    m_currentFrame = frameNumber;
    m_stats = Stats{};
}

void VirtualTextureSystem::processRequests(VkCommandBuffer cmd) {
    // Read feedback from previous frame
    readFeedbackBuffer();
    
    // Process pending requests
    processPageRequests();
    
    // Upload loaded pages to GPU
    uploadPendingPages(cmd);
    
    // Update page table
    updatePageTable(cmd);
}

void VirtualTextureSystem::endFrame() {
    // Calculate cache utilization
    uint32_t usedPages = 0;
    for (const auto& page : m_physicalPages) {
        if (page.valid) usedPages++;
    }
    m_stats.cacheUtilization = static_cast<float>(usedPages) / m_physicalPages.size();
}

void VirtualTextureSystem::readFeedbackBuffer() {
    // Read feedback data from GPU
    const uint16_t* data = static_cast<const uint16_t*>(m_feedbackReadback->map());
    if (!data) return;
    
    std::unordered_map<VTPageId, uint32_t, VTPageIdHash> requestCounts;
    
    uint32_t numPixels = m_defaultConfig.feedbackWidth * m_defaultConfig.feedbackHeight;
    for (uint32_t i = 0; i < numPixels; i++) {
        uint16_t pageX = data[i * 4 + 0];
        uint16_t pageY = data[i * 4 + 1];
        uint16_t mipLevel = data[i * 4 + 2];
        uint16_t vtIndex = data[i * 4 + 3];
        
        if (vtIndex == 0 || vtIndex == 0xFFFF) continue;  // Invalid
        
        VTPageId pageId{vtIndex, mipLevel, pageX, pageY};
        requestCounts[pageId]++;
    }
    
    m_feedbackReadback->unmap();
    
    // Convert to priority queue
    std::lock_guard<std::mutex> lock(m_requestMutex);
    for (const auto& [pageId, count] : requestCounts) {
        // Check if already loaded
        if (m_pageMapping.find(pageId) != m_pageMapping.end()) {
            m_stats.cacheHits++;
            continue;
        }
        
        m_stats.cacheMisses++;
        m_pendingRequests.push({pageId, count});
        m_stats.requestedPages++;
    }
}

void VirtualTextureSystem::processPageRequests() {
    // Handled by streaming thread
}

void VirtualTextureSystem::uploadPendingPages(VkCommandBuffer cmd) {
    std::lock_guard<std::mutex> lock(m_loadedMutex);
    
    uint32_t pagesUploaded = 0;
    const uint32_t maxUploadsPerFrame = 16;
    
    uint32_t pageWithPadding = m_defaultConfig.pageSize + m_defaultConfig.pagePadding * 2;
    size_t pageDataSize = pageWithPadding * pageWithPadding * 4;
    
    while (!m_loadedPages.empty() && pagesUploaded < maxUploadsPerFrame) {
        LoadedPage& loaded = m_loadedPages.front();
        
        // Allocate physical page
        PhysicalPage* physPage = allocatePage();
        if (!physPage) {
            // Cache is full, try to evict
            evictLRUPage();
            physPage = allocatePage();
            if (!physPage) break;  // Still no space
        }
        
        // Copy to staging buffer
        void* staging = m_uploadStaging->map();
        memcpy(static_cast<uint8_t*>(staging) + pagesUploaded * pageDataSize,
               loaded.data.data(), loaded.data.size());
        m_uploadStaging->unmap();
        
        // Copy from staging to physical cache
        VkBufferImageCopy region{};
        region.bufferOffset = pagesUploaded * pageDataSize;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageOffset = {
            static_cast<int32_t>(physPage->physicalX * pageWithPadding),
            static_cast<int32_t>(physPage->physicalY * pageWithPadding),
            0
        };
        region.imageExtent = {pageWithPadding, pageWithPadding, 1};
        
        vkCmdCopyBufferToImage(cmd,
                               m_uploadStaging->getBuffer(),
                               m_physicalCache->getImage(),
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               1, &region);
        
        // Update mapping
        physPage->virtualPage = loaded.pageId;
        physPage->lastUsedFrame = m_currentFrame;
        physPage->valid = true;
        m_pageMapping[loaded.pageId] = physPage;
        
        m_loadedPages.pop();
        pagesUploaded++;
        m_stats.uploadedPages++;
    }
}

void VirtualTextureSystem::updatePageTable(VkCommandBuffer cmd) {
    // Update page table based on current mappings
    uint32_t tableSize = m_defaultConfig.virtualWidth / m_defaultConfig.pageSize;
    
    uint16_t* tableData = static_cast<uint16_t*>(m_pageTableStaging->map());
    
    for (const auto& [pageId, physPage] : m_pageMapping) {
        if (pageId.mipLevel > 0) continue;  // Only update mip 0 for now
        
        uint32_t idx = (pageId.pageY * tableSize + pageId.pageX) * 4;
        tableData[idx + 0] = static_cast<uint16_t>(physPage->physicalX);
        tableData[idx + 1] = static_cast<uint16_t>(physPage->physicalY);
        tableData[idx + 2] = static_cast<uint16_t>(pageId.mipLevel);
        tableData[idx + 3] = 1;  // Valid flag
    }
    
    m_pageTableStaging->unmap();
    
    // Copy to page table image
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {tableSize, tableSize, 1};
    
    m_pageTable->transitionLayout(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    vkCmdCopyBufferToImage(cmd,
                           m_pageTableStaging->getBuffer(),
                           m_pageTable->getImage(),
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &region);
    m_pageTable->transitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

PhysicalPage* VirtualTextureSystem::allocatePage() {
    if (m_freePages.empty()) {
        return nullptr;
    }
    
    uint32_t idx = m_freePages.back();
    m_freePages.pop_back();
    return &m_physicalPages[idx];
}

void VirtualTextureSystem::evictLRUPage() {
    // Find least recently used page
    PhysicalPage* lruPage = nullptr;
    uint64_t oldestFrame = UINT64_MAX;
    
    for (auto& page : m_physicalPages) {
        if (page.valid && page.lastUsedFrame < oldestFrame) {
            oldestFrame = page.lastUsedFrame;
            lruPage = &page;
        }
    }
    
    if (lruPage) {
        // Remove from mapping
        m_pageMapping.erase(lruPage->virtualPage);
        
        // Mark as free
        lruPage->valid = false;
        m_freePages.push_back(lruPage - m_physicalPages.data());
        
        m_stats.evictedPages++;
    }
}

void VirtualTextureSystem::streamingThreadFunc() {
    while (m_streamingThreadRunning) {
        PageRequest request;
        bool hasRequest = false;
        
        {
            std::lock_guard<std::mutex> lock(m_requestMutex);
            if (!m_pendingRequests.empty()) {
                request = m_pendingRequests.front();
                m_pendingRequests.pop();
                hasRequest = true;
            }
        }
        
        if (hasRequest) {
            // Find the virtual texture
            VirtualTexture* vt = nullptr;
            for (auto& v : m_virtualTextures) {
                if (v->id == request.pageId.vtIndex) {
                    vt = v.get();
                    break;
                }
            }
            
            if (vt && vt->pageProvider) {
                size_t dataSize = vt->pageProvider->getPageDataSize();
                LoadedPage loaded;
                loaded.pageId = request.pageId;
                loaded.data.resize(dataSize);
                
                if (vt->pageProvider->loadPage(request.pageId, loaded.data.data(), dataSize)) {
                    std::lock_guard<std::mutex> lock(m_loadedMutex);
                    m_loadedPages.push(std::move(loaded));
                }
            }
        } else {
            // No work, sleep a bit
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

void VirtualTextureSystem::renderFeedback(VkCommandBuffer cmd,
                                           VkRenderPass renderPass,
                                           const glm::mat4& viewProjection,
                                           const glm::vec3& cameraPos) {
    // Render scene to feedback buffer at lower resolution
    // This records which virtual texture pages are visible
    
    m_feedbackPipeline->bind(cmd);
    m_feedbackDescSet->bind(cmd, m_feedbackPipeline->getLayout());
    
    // Draw scene geometry (would be provided externally)
}

VkImageView VirtualTextureSystem::getPhysicalCacheView() const {
    return m_physicalCache ? m_physicalCache->getView() : VK_NULL_HANDLE;
}

VkImageView VirtualTextureSystem::getPageTableView() const {
    return m_pageTable ? m_pageTable->getView() : VK_NULL_HANDLE;
}

VkSampler VirtualTextureSystem::getPhysicalCacheSampler() const {
    return m_cacheSampler;
}

VirtualTextureSystem::VTShaderParams VirtualTextureSystem::getShaderParams(uint32_t vtId) const {
    VTShaderParams params{};
    
    if (auto* vt = const_cast<VirtualTextureSystem*>(this)->getVirtualTexture(vtId)) {
        params.virtualSize = glm::vec2(vt->config.virtualWidth, vt->config.virtualHeight);
        params.physicalPageSize = glm::vec2(vt->config.pageSize);
        params.tilePadding = glm::vec2(vt->config.pagePadding);
        params.maxMipLevel = static_cast<float>(vt->config.maxMipLevels - 1);
        params.mipBias = 0.0f;
        params.vtIndex = vtId;
        params.worldOrigin = vt->worldOrigin;
        params.worldSize = vt->worldSize;
    }
    
    return params;
}

void VirtualTextureSystem::drawDebugUI() {
    // ImGui debug interface
}

void VirtualTextureSystem::visualizePageTable(VkCommandBuffer cmd, VkImageView output) {
    // Visualization of page table for debugging
}

//------------------------------------------------------------------------------
// LandscapeVirtualTexture
//------------------------------------------------------------------------------

LandscapeVirtualTexture::LandscapeVirtualTexture() {}

bool LandscapeVirtualTexture::initialize(VirtualTextureSystem* vtSystem,
                                          const glm::vec2& worldOrigin,
                                          const glm::vec2& worldSize,
                                          uint32_t resolution) {
    m_vtSystem = vtSystem;
    
    VirtualTextureConfig config;
    config.virtualWidth = resolution;
    config.virtualHeight = resolution;
    
    // Create procedural page provider for landscape compositing
    auto generator = [this](const VTPageId& pageId, void* data, size_t size) {
        // This would composite heightmap, weightmap, and material layers
        // into the virtual texture page
        memset(data, 128, size);  // Placeholder
    };
    
    auto provider = std::make_unique<ProceduralVTPageProvider>(generator, config);
    m_vtId = vtSystem->createVirtualTexture(config, std::move(provider));
    vtSystem->setWorldMapping(m_vtId, worldOrigin, worldSize);
    
    return m_vtId != 0;
}

void LandscapeVirtualTexture::shutdown() {
    if (m_vtSystem && m_vtId != 0) {
        m_vtSystem->destroyVirtualTexture(m_vtId);
        m_vtId = 0;
    }
}

void LandscapeVirtualTexture::setHeightmap(Image* heightmap) {
    m_heightmap = heightmap;
}

void LandscapeVirtualTexture::setWeightmap(Image* weightmap) {
    m_weightmap = weightmap;
}

void LandscapeVirtualTexture::addMaterialLayer(uint32_t index, Image* baseColor, Image* normal, Image* orm) {
    if (index >= m_materialLayers.size()) {
        m_materialLayers.resize(index + 1);
    }
    m_materialLayers[index] = {baseColor, normal, orm};
}

void LandscapeVirtualTexture::invalidateRegion(const glm::vec2& min, const glm::vec2& max) {
    // Mark pages in region as needing regeneration
}

} // namespace Kinetic
