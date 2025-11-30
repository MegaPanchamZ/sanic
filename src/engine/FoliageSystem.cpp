/**
 * FoliageSystem.cpp
 * 
 * Implementation of GPU-driven foliage instancing system.
 */

#include "FoliageSystem.h"
#include "VulkanContext.h"
#include "LandscapeSystem.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Sanic {

// Cluster size for instance grouping
constexpr uint32_t kClusterSize = 128;

// Hash function for sector grid
inline uint64_t sectorHash(int32_t x, int32_t y) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) | 
            static_cast<uint32_t>(y);
}

FoliageSystem::FoliageSystem() = default;

FoliageSystem::~FoliageSystem() {
    shutdown();
}

bool FoliageSystem::initialize(VulkanContext* context, const FoliageConfig& config) {
    if (initialized_) return true;
    
    context_ = context;
    config_ = config;
    
    if (!createBuffers()) {
        return false;
    }
    
    if (!createCullPipeline()) {
        return false;
    }
    
    initialized_ = true;
    return true;
}

void FoliageSystem::shutdown() {
    if (!initialized_) return;
    
    VkDevice device = context_->getDevice();
    
    // Cleanup buffers
    auto destroyBuffer = [device](VkBuffer& buffer, VkDeviceMemory& memory) {
        if (buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, buffer, nullptr);
            buffer = VK_NULL_HANDLE;
        }
        if (memory != VK_NULL_HANDLE) {
            vkFreeMemory(device, memory, nullptr);
            memory = VK_NULL_HANDLE;
        }
    };
    
    destroyBuffer(instanceBuffer_, instanceMemory_);
    destroyBuffer(visibleInstanceBuffer_, visibleMemory_);
    destroyBuffer(clusterBuffer_, clusterMemory_);
    destroyBuffer(indirectBuffer_, indirectMemory_);
    destroyBuffer(cullDataBuffer_, cullDataMemory_);
    destroyBuffer(counterBuffer_, counterMemory_);
    
    // Cleanup pipeline
    if (clusterCullPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, clusterCullPipeline_, nullptr);
    }
    if (instanceCullPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, instanceCullPipeline_, nullptr);
    }
    if (cullPipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, cullPipelineLayout_, nullptr);
    }
    if (cullDescSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, cullDescSetLayout_, nullptr);
    }
    if (cullDescPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, cullDescPool_, nullptr);
    }
    
    instances_.clear();
    clusters_.clear();
    sectors_.clear();
    types_.clear();
    
    initialized_ = false;
}

bool FoliageSystem::createBuffers() {
    VkDevice device = context_->getDevice();
    
    // Helper to create buffer
    auto createBuffer = [this, device](VkBuffer& buffer, VkDeviceMemory& memory,
                                        VkDeviceSize size, VkBufferUsageFlags usage,
                                        bool hostVisible = false) -> bool {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if (!hostVisible) {
            bufferInfo.usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        }
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
            return false;
        }
        
        VkMemoryRequirements memReqs;
        vkGetBufferMemoryRequirements(device, buffer, &memReqs);
        
        VkMemoryAllocateFlagsInfo allocFlags{};
        allocFlags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        allocFlags.flags = hostVisible ? 0 : VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.pNext = hostVisible ? nullptr : &allocFlags;
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = context_->findMemoryType(
            memReqs.memoryTypeBits,
            hostVisible ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
                        : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );
        
        if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
            return false;
        }
        
        vkBindBufferMemory(device, buffer, memory, 0);
        return true;
    };
    
    // Instance buffer (all instances)
    if (!createBuffer(instanceBuffer_, instanceMemory_,
                      config_.maxTotalInstances * sizeof(FoliageInstance),
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) {
        return false;
    }
    
    // Get device address
    VkBufferDeviceAddressInfo addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addressInfo.buffer = instanceBuffer_;
    instanceBufferAddress_ = vkGetBufferDeviceAddress(device, &addressInfo);
    
    // Visible instance buffer (compacted)
    if (!createBuffer(visibleInstanceBuffer_, visibleMemory_,
                      config_.maxVisibleInstances * sizeof(FoliageInstance),
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) {
        return false;
    }
    
    // Cluster buffer
    struct GPUCluster {
        glm::vec4 boundsSphere;     // xyz: center, w: radius
        glm::vec4 boundsMin;
        glm::vec4 boundsMax;
        uint32_t instanceOffset;
        uint32_t instanceCount;
        uint32_t typeId;
        uint32_t flags;
    };
    
    if (!createBuffer(clusterBuffer_, clusterMemory_,
                      config_.maxClusters * sizeof(GPUCluster),
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) {
        return false;
    }
    
    // Indirect draw buffer
    if (!createBuffer(indirectBuffer_, indirectMemory_,
                      sizeof(VkDrawIndirectCommand),
                      VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) {
        return false;
    }
    
    // Cull data buffer
    if (!createBuffer(cullDataBuffer_, cullDataMemory_,
                      sizeof(FoliageCullData),
                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true)) {
        return false;
    }
    
    // Counter buffer
    if (!createBuffer(counterBuffer_, counterMemory_,
                      sizeof(uint32_t) * 4,
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)) {
        return false;
    }
    
    return true;
}

bool FoliageSystem::createCullPipeline() {
    VkDevice device = context_->getDevice();
    
    // Descriptor set layout
    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    };
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &cullDescSetLayout_) != VK_SUCCESS) {
        return false;
    }
    
    // Pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &cullDescSetLayout_;
    
    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &cullPipelineLayout_) != VK_SUCCESS) {
        return false;
    }
    
    // Descriptor pool
    std::vector<VkDescriptorPoolSize> poolSizes = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5},
    };
    
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &cullDescPool_) != VK_SUCCESS) {
        return false;
    }
    
    // Allocate descriptor set
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = cullDescPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &cullDescSetLayout_;
    
    if (vkAllocateDescriptorSets(device, &allocInfo, &cullDescSet_) != VK_SUCCESS) {
        return false;
    }
    
    // Update descriptor set
    std::vector<VkDescriptorBufferInfo> bufferInfos = {
        {cullDataBuffer_, 0, sizeof(FoliageCullData)},
        {clusterBuffer_, 0, VK_WHOLE_SIZE},
        {instanceBuffer_, 0, VK_WHOLE_SIZE},
        {visibleInstanceBuffer_, 0, VK_WHOLE_SIZE},
        {indirectBuffer_, 0, VK_WHOLE_SIZE},
        {counterBuffer_, 0, VK_WHOLE_SIZE},
    };
    
    std::vector<VkWriteDescriptorSet> writes(bindings.size());
    for (size_t i = 0; i < bindings.size(); ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = cullDescSet_;
        writes[i].dstBinding = static_cast<uint32_t>(i);
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = bindings[i].descriptorType;
        writes[i].pBufferInfo = &bufferInfos[i];
    }
    
    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    
    // Note: Compute shader would be loaded here
    // For now, pipeline creation is deferred until shader is available
    
    return true;
}

uint32_t FoliageSystem::registerType(const FoliageType& type) {
    uint32_t id = nextTypeId_++;
    types_[id] = type;
    types_[id].id = id;
    return id;
}

void FoliageSystem::unregisterType(uint32_t typeId) {
    types_.erase(typeId);
    
    // Remove instances of this type
    instances_.erase(
        std::remove_if(instances_.begin(), instances_.end(),
                       [typeId](const FoliageInstance& inst) { return inst.typeId == typeId; }),
        instances_.end()
    );
    
    buffersDirty_ = true;
}

void FoliageSystem::addInstances(uint32_t typeId, const std::vector<FoliageInstance>& newInstances) {
    if (types_.find(typeId) == types_.end()) return;
    
    for (const auto& inst : newInstances) {
        FoliageInstance copy = inst;
        copy.typeId = typeId;
        instances_.push_back(copy);
    }
    
    buffersDirty_ = true;
}

void FoliageSystem::removeInstances(const glm::vec3& center, float radius, uint32_t typeId) {
    float radiusSq = radius * radius;
    
    instances_.erase(
        std::remove_if(instances_.begin(), instances_.end(),
                       [&](const FoliageInstance& inst) {
                           if (typeId != 0 && inst.typeId != typeId) return false;
                           glm::vec3 pos(inst.positionScale);
                           return glm::dot(pos - center, pos - center) <= radiusSq;
                       }),
        instances_.end()
    );
    
    buffersDirty_ = true;
}

void FoliageSystem::scatterOnLandscape(LandscapeSystem* landscape, uint32_t landscapeId,
                                        uint32_t typeId,
                                        const glm::vec3& regionMin, const glm::vec3& regionMax,
                                        float densityScale, uint32_t seed) {
    auto typeIt = types_.find(typeId);
    if (typeIt == types_.end()) return;
    
    const FoliageType& type = typeIt->second;
    float density = type.density * densityScale;
    
    // Calculate area and number of instances
    float areaWidth = regionMax.x - regionMin.x;
    float areaDepth = regionMax.z - regionMin.z;
    float area = areaWidth * areaDepth;
    uint32_t targetCount = static_cast<uint32_t>(area * density);
    
    if (targetCount == 0) return;
    
    // Random placement
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> distX(regionMin.x, regionMax.x);
    std::uniform_real_distribution<float> distZ(regionMin.z, regionMax.z);
    std::uniform_real_distribution<float> distScale(type.minScale, type.maxScale);
    std::uniform_real_distribution<float> distRotation(0.0f, glm::radians(type.randomRotation));
    
    std::vector<FoliageInstance> newInstances;
    newInstances.reserve(targetCount);
    
    for (uint32_t i = 0; i < targetCount; ++i) {
        float x = distX(rng);
        float z = distZ(rng);
        
        // Get height from landscape
        float y = landscape->getHeightAt(landscapeId, x, z);
        glm::vec3 normal = landscape->getNormalAt(landscapeId, x, z);
        
        // Skip if too steep
        if (normal.y < 0.5f) continue;
        
        FoliageInstance inst;
        inst.positionScale = glm::vec4(x, y, z, distScale(rng));
        
        // Rotation
        float yaw = distRotation(rng);
        glm::vec3 up = glm::mix(glm::vec3(0, 1, 0), normal, type.normalAlignStrength);
        up = glm::normalize(up);
        
        // Simple rotation storage (will be converted to matrix in shader)
        inst.rotationLOD = glm::vec4(yaw, 0, 0, 0);
        
        inst.typeId = typeId;
        inst.clusterIndex = 0;  // Will be assigned during rebuild
        inst.flags = 0;
        inst.padding = 0;
        
        newInstances.push_back(inst);
    }
    
    addInstances(typeId, newInstances);
}

void FoliageSystem::scatterWithDensityMap(LandscapeSystem* landscape, uint32_t landscapeId,
                                           uint32_t typeId,
                                           const std::vector<float>& densityMap,
                                           uint32_t mapWidth, uint32_t mapHeight,
                                           const glm::vec3& regionMin, const glm::vec3& regionMax) {
    auto typeIt = types_.find(typeId);
    if (typeIt == types_.end()) return;
    
    const FoliageType& type = typeIt->second;
    
    std::mt19937 rng(12345);
    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
    std::uniform_real_distribution<float> distScale(type.minScale, type.maxScale);
    std::uniform_real_distribution<float> distRotation(0.0f, glm::radians(type.randomRotation));
    
    float cellWidth = (regionMax.x - regionMin.x) / mapWidth;
    float cellDepth = (regionMax.z - regionMin.z) / mapHeight;
    float cellArea = cellWidth * cellDepth;
    
    std::vector<FoliageInstance> newInstances;
    
    for (uint32_t y = 0; y < mapHeight; ++y) {
        for (uint32_t x = 0; x < mapWidth; ++x) {
            float density = densityMap[y * mapWidth + x] * type.density;
            uint32_t instancesInCell = static_cast<uint32_t>(cellArea * density);
            
            // Probabilistic rounding
            float frac = cellArea * density - instancesInCell;
            if (dist01(rng) < frac) instancesInCell++;
            
            for (uint32_t i = 0; i < instancesInCell; ++i) {
                float px = regionMin.x + (x + dist01(rng)) * cellWidth;
                float pz = regionMin.z + (y + dist01(rng)) * cellDepth;
                float py = landscape->getHeightAt(landscapeId, px, pz);
                
                glm::vec3 normal = landscape->getNormalAt(landscapeId, px, pz);
                if (normal.y < 0.5f) continue;
                
                FoliageInstance inst;
                inst.positionScale = glm::vec4(px, py, pz, distScale(rng));
                inst.rotationLOD = glm::vec4(distRotation(rng), 0, 0, 0);
                inst.typeId = typeId;
                inst.clusterIndex = 0;
                inst.flags = 0;
                inst.padding = 0;
                
                newInstances.push_back(inst);
            }
        }
    }
    
    addInstances(typeId, newInstances);
}

void FoliageSystem::update(float deltaTime) {
    currentTime_ += deltaTime;
    
    if (buffersDirty_) {
        rebuildClusters();
        rebuildSectors();
        uploadInstances();
        buffersDirty_ = false;
    }
}

void FoliageSystem::rebuildClusters() {
    clusters_.clear();
    
    if (instances_.empty()) return;
    
    // Sort instances by type and position for better clustering
    std::vector<size_t> indices(instances_.size());
    std::iota(indices.begin(), indices.end(), 0);
    
    std::sort(indices.begin(), indices.end(), [this](size_t a, size_t b) {
        if (instances_[a].typeId != instances_[b].typeId) {
            return instances_[a].typeId < instances_[b].typeId;
        }
        // Sort by position within type
        glm::vec3 posA(instances_[a].positionScale);
        glm::vec3 posB(instances_[b].positionScale);
        
        int gridXA = static_cast<int>(posA.x / config_.sectorSize);
        int gridXB = static_cast<int>(posB.x / config_.sectorSize);
        if (gridXA != gridXB) return gridXA < gridXB;
        
        int gridZA = static_cast<int>(posA.z / config_.sectorSize);
        int gridZB = static_cast<int>(posB.z / config_.sectorSize);
        return gridZA < gridZB;
    });
    
    // Reorder instances
    std::vector<FoliageInstance> sortedInstances;
    sortedInstances.reserve(instances_.size());
    for (size_t idx : indices) {
        sortedInstances.push_back(instances_[idx]);
    }
    instances_ = std::move(sortedInstances);
    
    // Build clusters
    uint32_t currentType = UINT32_MAX;
    uint32_t clusterStart = 0;
    
    for (size_t i = 0; i < instances_.size(); ++i) {
        bool newCluster = (instances_[i].typeId != currentType) ||
                          (i - clusterStart >= kClusterSize);
        
        if (newCluster && clusterStart < i) {
            // Finish current cluster
            FoliageCluster cluster;
            cluster.id = static_cast<uint32_t>(clusters_.size());
            cluster.typeId = currentType;
            cluster.instanceOffset = clusterStart;
            cluster.instanceCount = static_cast<uint32_t>(i - clusterStart);
            
            // Compute bounds
            cluster.boundsMin = glm::vec3(std::numeric_limits<float>::max());
            cluster.boundsMax = glm::vec3(std::numeric_limits<float>::lowest());
            
            for (uint32_t j = clusterStart; j < i; ++j) {
                glm::vec3 pos(instances_[j].positionScale);
                float scale = instances_[j].positionScale.w;
                
                cluster.boundsMin = glm::min(cluster.boundsMin, pos - glm::vec3(scale));
                cluster.boundsMax = glm::max(cluster.boundsMax, pos + glm::vec3(scale));
                
                instances_[j].clusterIndex = cluster.id;
            }
            
            cluster.center = (cluster.boundsMin + cluster.boundsMax) * 0.5f;
            cluster.radius = glm::length(cluster.boundsMax - cluster.center);
            
            clusters_.push_back(cluster);
        }
        
        if (newCluster) {
            currentType = instances_[i].typeId;
            clusterStart = static_cast<uint32_t>(i);
        }
    }
    
    // Last cluster
    if (clusterStart < instances_.size()) {
        FoliageCluster cluster;
        cluster.id = static_cast<uint32_t>(clusters_.size());
        cluster.typeId = currentType;
        cluster.instanceOffset = clusterStart;
        cluster.instanceCount = static_cast<uint32_t>(instances_.size() - clusterStart);
        
        cluster.boundsMin = glm::vec3(std::numeric_limits<float>::max());
        cluster.boundsMax = glm::vec3(std::numeric_limits<float>::lowest());
        
        for (uint32_t j = clusterStart; j < instances_.size(); ++j) {
            glm::vec3 pos(instances_[j].positionScale);
            float scale = instances_[j].positionScale.w;
            
            cluster.boundsMin = glm::min(cluster.boundsMin, pos - glm::vec3(scale));
            cluster.boundsMax = glm::max(cluster.boundsMax, pos + glm::vec3(scale));
            
            instances_[j].clusterIndex = cluster.id;
        }
        
        cluster.center = (cluster.boundsMin + cluster.boundsMax) * 0.5f;
        cluster.radius = glm::length(cluster.boundsMax - cluster.center);
        
        clusters_.push_back(cluster);
    }
}

void FoliageSystem::rebuildSectors() {
    sectors_.clear();
    sectorGrid_.clear();
    
    for (auto& cluster : clusters_) {
        int gridX = static_cast<int>(std::floor(cluster.center.x / config_.sectorSize));
        int gridZ = static_cast<int>(std::floor(cluster.center.z / config_.sectorSize));
        
        uint64_t hash = sectorHash(gridX, gridZ);
        
        auto it = sectorGrid_.find(hash);
        if (it == sectorGrid_.end()) {
            FoliageSector sector;
            sector.id = static_cast<uint32_t>(sectors_.size());
            sector.gridCoord = glm::ivec2(gridX, gridZ);
            sector.boundsMin = glm::vec3(gridX * config_.sectorSize, -1e6f, gridZ * config_.sectorSize);
            sector.boundsMax = glm::vec3((gridX + 1) * config_.sectorSize, 1e6f, (gridZ + 1) * config_.sectorSize);
            sector.isLoaded = true;
            
            sectorGrid_[hash] = sector.id;
            sectors_.push_back(sector);
            it = sectorGrid_.find(hash);
        }
        
        sectors_[it->second].clusterIds.push_back(cluster.id);
        
        // Expand sector bounds to include cluster
        sectors_[it->second].boundsMin.y = std::min(sectors_[it->second].boundsMin.y, cluster.boundsMin.y);
        sectors_[it->second].boundsMax.y = std::max(sectors_[it->second].boundsMax.y, cluster.boundsMax.y);
    }
}

void FoliageSystem::uploadInstances() {
    if (instances_.empty()) return;
    
    // Upload via staging buffer
    VkDevice device = context_->getDevice();
    size_t dataSize = instances_.size() * sizeof(FoliageInstance);
    
    // Create staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = dataSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    vkCreateBuffer(device, &bufferInfo, nullptr, &stagingBuffer);
    
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, stagingBuffer, &memReqs);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = context_->findMemoryType(
        memReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    
    vkAllocateMemory(device, &allocInfo, nullptr, &stagingMemory);
    vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0);
    
    // Copy data
    void* mapped;
    vkMapMemory(device, stagingMemory, 0, dataSize, 0, &mapped);
    memcpy(mapped, instances_.data(), dataSize);
    vkUnmapMemory(device, stagingMemory);
    
    // Submit copy command
    VkCommandBuffer cmd = context_->beginSingleTimeCommands();
    
    VkBufferCopy copyRegion{};
    copyRegion.size = dataSize;
    vkCmdCopyBuffer(cmd, stagingBuffer, instanceBuffer_, 1, &copyRegion);
    
    context_->endSingleTimeCommands(cmd);
    
    // Cleanup staging
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);
    
    // Also upload cluster data
    // (Similar process for cluster buffer)
}

void FoliageSystem::cullInstances(VkCommandBuffer cmd, const glm::mat4& viewProj,
                                   const glm::vec3& cameraPos) {
    if (instances_.empty()) return;
    
    VkDevice device = context_->getDevice();
    
    // Update cull data
    FoliageCullData cullData;
    cullData.viewProj = viewProj;
    cullData.cameraPosition = cameraPos;
    cullData.time = currentTime_;
    cullData.lodBias = config_.lodBias;
    cullData.totalInstances = static_cast<uint32_t>(instances_.size());
    
    // Extract frustum planes
    glm::mat4 m = glm::transpose(viewProj);
    cullData.frustumPlanes[0] = m[3] + m[0];  // Left
    cullData.frustumPlanes[1] = m[3] - m[0];  // Right
    cullData.frustumPlanes[2] = m[3] + m[1];  // Bottom
    cullData.frustumPlanes[3] = m[3] - m[1];  // Top
    cullData.frustumPlanes[4] = m[3] + m[2];  // Near
    cullData.frustumPlanes[5] = m[3] - m[2];  // Far
    
    // Normalize planes
    for (int i = 0; i < 6; ++i) {
        float len = glm::length(glm::vec3(cullData.frustumPlanes[i]));
        cullData.frustumPlanes[i] /= len;
    }
    
    // Upload cull data
    void* mapped;
    vkMapMemory(device, cullDataMemory_, 0, sizeof(FoliageCullData), 0, &mapped);
    memcpy(mapped, &cullData, sizeof(FoliageCullData));
    vkUnmapMemory(device, cullDataMemory_);
    
    // Reset counter
    vkCmdFillBuffer(cmd, counterBuffer_, 0, sizeof(uint32_t), 0);
    
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
    
    // CPU frustum cull sectors and clusters (GPU cull would be done via compute)
    frustumCullClusters(viewProj);
    
    // Dispatch cluster culling compute
    if (clusterCullPipeline_ != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, clusterCullPipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cullPipelineLayout_,
                                 0, 1, &cullDescSet_, 0, nullptr);
        
        uint32_t groupCount = (static_cast<uint32_t>(clusters_.size()) + 63) / 64;
        vkCmdDispatch(cmd, groupCount, 1, 1);
    }
    
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
    
    // Dispatch instance culling compute
    if (instanceCullPipeline_ != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, instanceCullPipeline_);
        
        uint32_t groupCount = (static_cast<uint32_t>(instances_.size()) + 63) / 64;
        vkCmdDispatch(cmd, groupCount, 1, 1);
    }
    
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
        VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void FoliageSystem::frustumCullClusters(const glm::mat4& viewProj) {
    // Extract frustum planes
    glm::mat4 m = glm::transpose(viewProj);
    glm::vec4 planes[6];
    planes[0] = m[3] + m[0];  // Left
    planes[1] = m[3] - m[0];  // Right
    planes[2] = m[3] + m[1];  // Bottom
    planes[3] = m[3] - m[1];  // Top
    planes[4] = m[3] + m[2];  // Near
    planes[5] = m[3] - m[2];  // Far
    
    for (int i = 0; i < 6; ++i) {
        float len = glm::length(glm::vec3(planes[i]));
        planes[i] /= len;
    }
    
    visibleCount_ = 0;
    
    // First cull sectors
    for (auto& sector : sectors_) {
        sector.isVisible = true;
        
        for (int i = 0; i < 6; ++i) {
            glm::vec3 positive = sector.boundsMin;
            if (planes[i].x >= 0) positive.x = sector.boundsMax.x;
            if (planes[i].y >= 0) positive.y = sector.boundsMax.y;
            if (planes[i].z >= 0) positive.z = sector.boundsMax.z;
            
            if (glm::dot(glm::vec3(planes[i]), positive) + planes[i].w < 0) {
                sector.isVisible = false;
                break;
            }
        }
    }
    
    // Cull clusters in visible sectors
    for (const auto& sector : sectors_) {
        if (!sector.isVisible) continue;
        
        for (uint32_t clusterId : sector.clusterIds) {
            auto& cluster = clusters_[clusterId];
            cluster.isVisible = true;
            
            for (int i = 0; i < 6; ++i) {
                float dist = glm::dot(glm::vec3(planes[i]), cluster.center) + planes[i].w;
                if (dist < -cluster.radius) {
                    cluster.isVisible = false;
                    break;
                }
            }
            
            if (cluster.isVisible) {
                visibleCount_ += cluster.instanceCount;
            }
        }
    }
}

FoliageSystem::Statistics FoliageSystem::getStatistics() const {
    Statistics stats = {};
    stats.totalInstances = static_cast<uint32_t>(instances_.size());
    stats.visibleInstances = visibleCount_;
    stats.totalClusters = static_cast<uint32_t>(clusters_.size());
    stats.sectorsLoaded = static_cast<uint32_t>(sectors_.size());
    
    for (const auto& cluster : clusters_) {
        if (cluster.isVisible) stats.visibleClusters++;
    }
    
    return stats;
}

} // namespace Sanic

