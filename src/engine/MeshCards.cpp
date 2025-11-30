/**
 * MeshCards.cpp
 * 
 * Implementation of Lumen-style mesh card system.
 */

#include "MeshCards.h"
#include "VulkanContext.h"
#include <algorithm>
#include <cstring>

MeshCards::~MeshCards() {
    cleanup();
}

bool MeshCards::initialize(VulkanContext* context, const MeshCardsConfig& config) {
    if (initialized_) return true;
    
    context_ = context;
    config_ = config;
    
    VkDevice device = context_->getDevice();
    
    // Card buffer
    VkDeviceSize cardBufferSize = sizeof(GPULumenCard) * config_.maxCards;
    
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = cardBufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | 
                       VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                       VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &cardBuffer_) != VK_SUCCESS) {
        return false;
    }
    
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, cardBuffer_, &memReqs);
    
    VkMemoryAllocateFlagsInfo flagsInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
    flagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    
    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.pNext = &flagsInfo;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = context_->findMemoryType(memReqs.memoryTypeBits, 
                                                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &cardMemory_) != VK_SUCCESS) {
        cleanup();
        return false;
    }
    
    vkBindBufferMemory(device, cardBuffer_, cardMemory_, 0);
    
    VkBufferDeviceAddressInfo addrInfo{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    addrInfo.buffer = cardBuffer_;
    cardBufferAddr_ = vkGetBufferDeviceAddress(device, &addrInfo);
    
    // Mesh cards buffer
    VkDeviceSize meshCardsBufferSize = sizeof(GPUMeshCardsData) * config_.maxMeshCards;
    bufferInfo.size = meshCardsBufferSize;
    
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &meshCardsBuffer_) != VK_SUCCESS) {
        cleanup();
        return false;
    }
    
    vkGetBufferMemoryRequirements(device, meshCardsBuffer_, &memReqs);
    allocInfo.allocationSize = memReqs.size;
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &meshCardsMemory_) != VK_SUCCESS) {
        cleanup();
        return false;
    }
    
    vkBindBufferMemory(device, meshCardsBuffer_, meshCardsMemory_, 0);
    
    // Staging buffer
    VkDeviceSize stagingSize = std::max(cardBufferSize, meshCardsBufferSize);
    bufferInfo.size = stagingSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &stagingBuffer_) != VK_SUCCESS) {
        cleanup();
        return false;
    }
    
    vkGetBufferMemoryRequirements(device, stagingBuffer_, &memReqs);
    
    VkMemoryAllocateInfo stagingAllocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    stagingAllocInfo.allocationSize = memReqs.size;
    stagingAllocInfo.memoryTypeIndex = context_->findMemoryType(memReqs.memoryTypeBits,
                                                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    if (vkAllocateMemory(device, &stagingAllocInfo, nullptr, &stagingMemory_) != VK_SUCCESS) {
        cleanup();
        return false;
    }
    
    vkBindBufferMemory(device, stagingBuffer_, stagingMemory_, 0);
    vkMapMemory(device, stagingMemory_, 0, stagingSize, 0, &stagingMapped_);
    
    // Reserve space
    cards_.reserve(config_.maxCards);
    meshCards_.reserve(config_.maxMeshCards);
    
    initialized_ = true;
    return true;
}

void MeshCards::cleanup() {
    if (!context_) return;
    VkDevice device = context_->getDevice();
    
    if (stagingMapped_) {
        vkUnmapMemory(device, stagingMemory_);
        stagingMapped_ = nullptr;
    }
    
    if (stagingBuffer_) vkDestroyBuffer(device, stagingBuffer_, nullptr);
    if (stagingMemory_) vkFreeMemory(device, stagingMemory_, nullptr);
    
    if (cardBuffer_) vkDestroyBuffer(device, cardBuffer_, nullptr);
    if (cardMemory_) vkFreeMemory(device, cardMemory_, nullptr);
    
    if (meshCardsBuffer_) vkDestroyBuffer(device, meshCardsBuffer_, nullptr);
    if (meshCardsMemory_) vkFreeMemory(device, meshCardsMemory_, nullptr);
    
    if (pageTableBuffer_) vkDestroyBuffer(device, pageTableBuffer_, nullptr);
    if (pageTableMemory_) vkFreeMemory(device, pageTableMemory_, nullptr);
    
    cards_.clear();
    meshCards_.clear();
    meshIdToMeshCards_.clear();
    freeCardSlots_.clear();
    freeMeshCardsSlots_.clear();
    pages_.clear();
    dirtyCards_.clear();
    
    initialized_ = false;
}

uint32_t MeshCards::registerMesh(uint32_t meshId,
                                  const glm::vec3& boundsMin,
                                  const glm::vec3& boundsMax,
                                  const glm::mat4& transform,
                                  const std::vector<OBB>* cardOBBs) {
    // Check if already registered
    auto it = meshIdToMeshCards_.find(meshId);
    if (it != meshIdToMeshCards_.end()) {
        return it->second;
    }
    
    // Allocate mesh cards slot
    uint32_t meshCardsIndex;
    if (!freeMeshCardsSlots_.empty()) {
        meshCardsIndex = freeMeshCardsSlots_.back();
        freeMeshCardsSlots_.pop_back();
    } else {
        meshCardsIndex = static_cast<uint32_t>(meshCards_.size());
        meshCards_.emplace_back();
    }
    
    LumenMeshCards& mc = meshCards_[meshCardsIndex];
    mc.meshId = meshId;
    mc.localToWorld = transform;
    mc.worldToLocal = glm::inverse(transform);
    mc.boundsMin = boundsMin;
    mc.boundsMax = boundsMax;
    mc.firstCardIndex = static_cast<uint32_t>(cards_.size());
    mc.cardCount = 0;
    mc.directionMask.reset();
    mc.hasDistanceField = false;
    mc.affectsIndirectLighting = true;
    mc.affectsSkyLighting = true;
    mc.isVisible = true;
    mc.needsUpdate = true;
    mc.screenSize = 0.0f;
    mc.currentLOD = 0;
    
    // Generate cards
    if (cardOBBs && !cardOBBs->empty()) {
        generateCardsFromOBBs(meshCardsIndex, *cardOBBs);
    } else {
        generateCardsFromBounds(meshCardsIndex, boundsMin, boundsMax);
    }
    
    mc.cardCount = static_cast<uint32_t>(cards_.size()) - mc.firstCardIndex;
    
    meshIdToMeshCards_[meshId] = meshCardsIndex;
    buffersNeedRebuild_ = true;
    
    return meshCardsIndex;
}

void MeshCards::unregisterMesh(uint32_t meshCardsIndex) {
    if (meshCardsIndex >= meshCards_.size()) return;
    
    LumenMeshCards& mc = meshCards_[meshCardsIndex];
    
    // Free all cards
    for (uint32_t i = 0; i < mc.cardCount; i++) {
        uint32_t cardIndex = mc.firstCardIndex + i;
        freeCardFromAtlas(cards_[cardIndex]);
        freeCardSlots_.push_back(cardIndex);
    }
    
    // Remove from lookup
    meshIdToMeshCards_.erase(mc.meshId);
    
    // Add to free list
    freeMeshCardsSlots_.push_back(meshCardsIndex);
    mc.cardCount = 0;
    mc.meshId = UINT32_MAX;
    
    buffersNeedRebuild_ = true;
}

void MeshCards::updateTransform(uint32_t meshCardsIndex, const glm::mat4& transform) {
    if (meshCardsIndex >= meshCards_.size()) return;
    
    LumenMeshCards& mc = meshCards_[meshCardsIndex];
    mc.localToWorld = transform;
    mc.worldToLocal = glm::inverse(transform);
    
    // Update world OBBs for all cards
    for (uint32_t i = 0; i < mc.cardCount; i++) {
        uint32_t cardIndex = mc.firstCardIndex + i;
        LumenCard& card = cards_[cardIndex];
        
        // Transform local OBB to world
        card.worldOBB.center = glm::vec3(transform * glm::vec4(card.localOBB.center, 1.0f));
        card.worldOBB.orientation = glm::mat3(transform) * card.localOBB.orientation;
        
        // Scale extents by transform scale
        glm::vec3 scale(
            glm::length(glm::vec3(transform[0])),
            glm::length(glm::vec3(transform[1])),
            glm::length(glm::vec3(transform[2]))
        );
        card.worldOBB.extents = card.localOBB.extents * scale;
        
        card.needsCapture = true;
        dirtyCards_.push_back(cardIndex);
    }
    
    mc.needsUpdate = true;
    buffersNeedRebuild_ = true;
}

void MeshCards::generateCardsFromBounds(uint32_t meshCardsIndex,
                                         const glm::vec3& boundsMin,
                                         const glm::vec3& boundsMax) {
    LumenMeshCards& mc = meshCards_[meshCardsIndex];
    
    glm::vec3 center = (boundsMin + boundsMax) * 0.5f;
    glm::vec3 extents = (boundsMax - boundsMin) * 0.5f;
    
    // 6 axis-aligned card normals
    const glm::vec3 normals[6] = {
        { 1, 0, 0}, {-1, 0, 0},  // ±X
        { 0, 1, 0}, { 0,-1, 0},  // ±Y
        { 0, 0, 1}, { 0, 0,-1}   // ±Z
    };
    
    // Card tangent/bitangent for each direction
    const glm::vec3 tangents[6] = {
        { 0, 0,-1}, { 0, 0, 1},  // ±X
        { 1, 0, 0}, { 1, 0, 0},  // ±Y
        { 1, 0, 0}, {-1, 0, 0}   // ±Z
    };
    
    for (int i = 0; i < 6; i++) {
        // Check minimum surface area
        glm::vec3 cardExtent = extents;
        cardExtent[i / 2] = 0.0f;  // Zero out the normal axis
        float surfaceArea = 4.0f * cardExtent.x * cardExtent.y + 
                           4.0f * cardExtent.y * cardExtent.z + 
                           4.0f * cardExtent.z * cardExtent.x;
        
        if (surfaceArea < config_.minCardSurfaceArea) {
            continue;  // Skip small cards
        }
        
        // Allocate card slot
        uint32_t cardIndex;
        if (!freeCardSlots_.empty()) {
            cardIndex = freeCardSlots_.back();
            freeCardSlots_.pop_back();
        } else {
            cardIndex = static_cast<uint32_t>(cards_.size());
            cards_.emplace_back();
        }
        
        LumenCard& card = cards_[cardIndex];
        
        // Build local OBB
        card.localOBB.center = center + normals[i] * extents;
        card.localOBB.extents = cardExtent;
        
        // Orientation: columns are tangent, bitangent, normal
        glm::vec3 normal = normals[i];
        glm::vec3 tangent = tangents[i];
        glm::vec3 bitangent = glm::cross(normal, tangent);
        card.localOBB.orientation = glm::mat3(tangent, bitangent, normal);
        
        // Transform to world
        card.worldOBB.center = glm::vec3(mc.localToWorld * glm::vec4(card.localOBB.center, 1.0f));
        card.worldOBB.orientation = glm::mat3(mc.localToWorld) * card.localOBB.orientation;
        
        glm::vec3 scale(
            glm::length(glm::vec3(mc.localToWorld[0])),
            glm::length(glm::vec3(mc.localToWorld[1])),
            glm::length(glm::vec3(mc.localToWorld[2]))
        );
        card.worldOBB.extents = card.localOBB.extents * scale;
        
        // Properties
        card.direction = static_cast<CardDirection>(i);
        card.meshCardsIndex = meshCardsIndex;
        card.cardIndex = i;
        card.initialAspectRatio = card.worldOBB.extents.x / 
                                  std::max(card.worldOBB.extents.y, 0.001f);
        card.priority = 0.0f;
        card.needsCapture = true;
        card.isVisible = true;
        card.lastCaptureFrame = 0;
        card.lastAccessFrame = 0;
        card.gpuIndex = cardIndex;
        
        // Allocate atlas space
        allocateCardInAtlas(card);
        
        // Update direction mask
        mc.directionMask.set(i);
        
        dirtyCards_.push_back(cardIndex);
    }
}

void MeshCards::generateCardsFromOBBs(uint32_t meshCardsIndex,
                                       const std::vector<OBB>& obbs) {
    LumenMeshCards& mc = meshCards_[meshCardsIndex];
    
    for (size_t i = 0; i < obbs.size() && i < 6; i++) {
        const OBB& obb = obbs[i];
        
        // Check minimum surface area
        if (obb.getSurfaceArea() < config_.minCardSurfaceArea) {
            continue;
        }
        
        uint32_t cardIndex;
        if (!freeCardSlots_.empty()) {
            cardIndex = freeCardSlots_.back();
            freeCardSlots_.pop_back();
        } else {
            cardIndex = static_cast<uint32_t>(cards_.size());
            cards_.emplace_back();
        }
        
        LumenCard& card = cards_[cardIndex];
        card.localOBB = obb;
        
        // Transform to world
        card.worldOBB.center = glm::vec3(mc.localToWorld * glm::vec4(obb.center, 1.0f));
        card.worldOBB.orientation = glm::mat3(mc.localToWorld) * obb.orientation;
        
        glm::vec3 scale(
            glm::length(glm::vec3(mc.localToWorld[0])),
            glm::length(glm::vec3(mc.localToWorld[1])),
            glm::length(glm::vec3(mc.localToWorld[2]))
        );
        card.worldOBB.extents = obb.extents * scale;
        
        // Determine direction from normal (third column of orientation)
        glm::vec3 normal = card.worldOBB.orientation[2];
        int dirIndex = 0;
        float maxDot = -1.0f;
        const glm::vec3 axes[6] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
        for (int d = 0; d < 6; d++) {
            float dot = glm::dot(normal, axes[d]);
            if (dot > maxDot) {
                maxDot = dot;
                dirIndex = d;
            }
        }
        
        card.direction = static_cast<CardDirection>(dirIndex);
        card.meshCardsIndex = meshCardsIndex;
        card.cardIndex = static_cast<uint32_t>(i);
        card.initialAspectRatio = card.worldOBB.extents.x / 
                                  std::max(card.worldOBB.extents.y, 0.001f);
        card.priority = 0.0f;
        card.needsCapture = true;
        card.isVisible = true;
        card.lastCaptureFrame = 0;
        card.lastAccessFrame = 0;
        card.gpuIndex = cardIndex;
        
        allocateCardInAtlas(card);
        mc.directionMask.set(dirIndex);
        dirtyCards_.push_back(cardIndex);
    }
}

bool MeshCards::allocateCardInAtlas(LumenCard& card) {
    // Simple allocation - in production, use proper page allocator
    static uint32_t nextX = 0;
    static uint32_t nextY = 0;
    static uint32_t rowHeight = 0;
    
    uint32_t resolution = config_.minCardResolution;  // Start with minimum
    
    if (nextX + resolution > config_.physicalPageSize * 32) {
        nextX = 0;
        nextY += rowHeight;
        rowHeight = 0;
    }
    
    if (nextY + resolution > config_.physicalPageSize * 32) {
        return false;  // Atlas full
    }
    
    card.atlasAlloc.offsetX = static_cast<uint16_t>(nextX);
    card.atlasAlloc.offsetY = static_cast<uint16_t>(nextY);
    card.atlasAlloc.sizeX = static_cast<uint16_t>(resolution);
    card.atlasAlloc.sizeY = static_cast<uint16_t>(resolution);
    card.atlasAlloc.mipLevel = 3;  // Minimum mip (8x8)
    card.atlasAlloc.valid = true;
    
    nextX += resolution;
    rowHeight = std::max(rowHeight, resolution);
    
    return true;
}

void MeshCards::freeCardFromAtlas(LumenCard& card) {
    card.atlasAlloc.valid = false;
    // In production, actually free the space
}

void MeshCards::buildGPUBuffers(VkCommandBuffer cmd) {
    if (!buffersNeedRebuild_ && dirtyCards_.empty()) return;
    
    // Build card buffer
    std::vector<GPULumenCard> gpuCards(cards_.size());
    
    for (size_t i = 0; i < cards_.size(); i++) {
        const LumenCard& card = cards_[i];
        GPULumenCard& gpu = gpuCards[i];
        
        gpu.worldCenter = glm::vec4(card.worldOBB.center, card.worldOBB.getSurfaceArea());
        gpu.worldExtents = glm::vec4(card.worldOBB.extents, card.priority);
        
        // Convert orientation matrix to quaternion
        glm::quat q = glm::quat_cast(card.worldOBB.orientation);
        gpu.orientation = glm::vec4(q.x, q.y, q.z, q.w);
        
        gpu.atlasRect = glm::ivec4(
            card.atlasAlloc.offsetX,
            card.atlasAlloc.offsetY,
            card.atlasAlloc.sizeX,
            card.atlasAlloc.sizeY
        );
        
        glm::vec3 normal = card.worldOBB.orientation[2];
        gpu.normalDirection = glm::vec4(normal, float(card.atlasAlloc.mipLevel));
        
        uint32_t flags = (card.needsCapture ? 1 : 0) | (card.isVisible ? 2 : 0);
        gpu.indices = glm::ivec4(card.meshCardsIndex, card.cardIndex, flags, 0);
    }
    
    // Copy to staging and upload
    memcpy(stagingMapped_, gpuCards.data(), sizeof(GPULumenCard) * gpuCards.size());
    
    VkBufferCopy copyRegion{};
    copyRegion.size = sizeof(GPULumenCard) * gpuCards.size();
    vkCmdCopyBuffer(cmd, stagingBuffer_, cardBuffer_, 1, &copyRegion);
    
    // Build mesh cards buffer
    std::vector<GPUMeshCardsData> gpuMeshCards(meshCards_.size());
    
    for (size_t i = 0; i < meshCards_.size(); i++) {
        const LumenMeshCards& mc = meshCards_[i];
        GPUMeshCardsData& gpu = gpuMeshCards[i];
        
        gpu.localToWorld = mc.localToWorld;
        gpu.boundsMin = glm::vec4(mc.boundsMin, static_cast<float>(mc.firstCardIndex));
        gpu.boundsMax = glm::vec4(mc.boundsMax, static_cast<float>(mc.cardCount));
        gpu.flags = glm::ivec4(
            static_cast<int>(mc.directionMask.to_ulong()),
            mc.meshId,
            mc.currentLOD,
            0
        );
    }
    
    memcpy(stagingMapped_, gpuMeshCards.data(), sizeof(GPUMeshCardsData) * gpuMeshCards.size());
    
    VkBufferCopy meshCopyRegion{};
    meshCopyRegion.size = sizeof(GPUMeshCardsData) * gpuMeshCards.size();
    vkCmdCopyBuffer(cmd, stagingBuffer_, meshCardsBuffer_, 1, &meshCopyRegion);
    
    // Memory barrier
    VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.buffer = cardBuffer_;
    barrier.size = VK_WHOLE_SIZE;
    
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, 
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 1, &barrier, 0, nullptr);
    
    barrier.buffer = meshCardsBuffer_;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 1, &barrier, 0, nullptr);
    
    buffersNeedRebuild_ = false;
    dirtyCards_.clear();
}

std::vector<uint32_t> MeshCards::getCardsToCapture(uint32_t maxCards, const glm::vec3& cameraPos) {
    updateCardPriorities(cameraPos);
    
    std::vector<uint32_t> result;
    result.reserve(maxCards);
    
    // Collect cards that need capture
    std::vector<std::pair<float, uint32_t>> priorityList;
    for (size_t i = 0; i < cards_.size(); i++) {
        if (cards_[i].needsCapture && cards_[i].isVisible) {
            priorityList.push_back({cards_[i].priority, static_cast<uint32_t>(i)});
        }
    }
    
    // Sort by priority (highest first)
    std::sort(priorityList.begin(), priorityList.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    
    for (size_t i = 0; i < std::min(priorityList.size(), size_t(maxCards)); i++) {
        result.push_back(priorityList[i].second);
    }
    
    return result;
}

void MeshCards::markCaptured(uint32_t cardIndex, uint32_t frame) {
    if (cardIndex < cards_.size()) {
        cards_[cardIndex].needsCapture = false;
        cards_[cardIndex].lastCaptureFrame = frame;
    }
}

void MeshCards::cullCards(const glm::mat4& viewProj, const glm::vec3& cameraPos) {
    // Extract frustum planes
    glm::vec4 planes[6];
    planes[0] = viewProj[3] + viewProj[0];  // Left
    planes[1] = viewProj[3] - viewProj[0];  // Right
    planes[2] = viewProj[3] + viewProj[1];  // Bottom
    planes[3] = viewProj[3] - viewProj[1];  // Top
    planes[4] = viewProj[3] + viewProj[2];  // Near
    planes[5] = viewProj[3] - viewProj[2];  // Far
    
    for (auto& plane : planes) {
        plane /= glm::length(glm::vec3(plane));
    }
    
    for (auto& card : cards_) {
        // OBB frustum test
        const OBB& obb = card.worldOBB;
        bool visible = true;
        
        for (int i = 0; i < 6 && visible; i++) {
            glm::vec3 planeNormal(planes[i]);
            float planeDist = planes[i].w;
            
            // Project OBB onto plane normal
            float r = glm::abs(glm::dot(obb.orientation[0], planeNormal)) * obb.extents.x +
                     glm::abs(glm::dot(obb.orientation[1], planeNormal)) * obb.extents.y +
                     glm::abs(glm::dot(obb.orientation[2], planeNormal)) * obb.extents.z;
            
            float s = glm::dot(planeNormal, obb.center) + planeDist;
            
            if (s < -r) {
                visible = false;
            }
        }
        
        card.isVisible = visible;
        card.lastAccessFrame = visible ? currentFrame_ : card.lastAccessFrame;
    }
    
    currentFrame_++;
}

void MeshCards::updateCardPriorities(const glm::vec3& cameraPos) {
    for (auto& card : cards_) {
        if (!card.isVisible) {
            card.priority = 0.0f;
            continue;
        }
        
        float distance = glm::length(card.worldOBB.center - cameraPos);
        float surfaceArea = card.worldOBB.getSurfaceArea();
        
        // Priority = surface area / distance² (screen-space importance)
        card.priority = surfaceArea / (distance * distance + 1.0f);
        
        // Boost priority for cards that haven't been captured recently
        uint32_t framesSinceCapture = currentFrame_ - card.lastCaptureFrame;
        if (card.needsCapture || framesSinceCapture > 60) {
            card.priority *= 2.0f;
        }
    }
}

MeshCards::Stats MeshCards::getStats() const {
    Stats stats{};
    stats.totalCards = static_cast<uint32_t>(cards_.size());
    stats.totalMeshCards = static_cast<uint32_t>(meshCards_.size());
    
    for (const auto& card : cards_) {
        if (card.isVisible) stats.visibleCards++;
        if (card.needsCapture) stats.pendingCaptures++;
    }
    
    stats.residentPages = static_cast<uint32_t>(pages_.size());
    stats.atlasUtilization = float(stats.residentPages) / float(config_.maxPages);
    
    return stats;
}

