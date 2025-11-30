/**
 * LevelStreaming.cpp
 * 
 * Implementation of World Partition style level streaming.
 */

#include "LevelStreaming.h"
#include "VulkanContext.h"
#include "AsyncPhysics.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <chrono>

namespace Sanic {

LevelStreaming::LevelStreaming() = default;

LevelStreaming::~LevelStreaming() {
    shutdown();
}

bool LevelStreaming::initialize(VulkanContext* context, AsyncPhysics* physics,
                                 const LevelStreamingConfig& config) {
    if (initialized_) return true;
    
    context_ = context;
    physics_ = physics;
    config_ = config;
    
    // Start streaming threads
    if (config_.useAsyncLoading) {
        shutdownRequested_ = false;
        for (uint32_t i = 0; i < config_.streamingThreads; ++i) {
            streamingThreads_.emplace_back(&LevelStreaming::streamingThreadFunc, this);
        }
    }
    
    initialized_ = true;
    return true;
}

void LevelStreaming::shutdown() {
    if (!initialized_) return;
    
    // Signal shutdown to streaming threads
    {
        std::lock_guard<std::mutex> lock(streamingMutex_);
        shutdownRequested_ = true;
    }
    streamingCondition_.notify_all();
    
    // Wait for threads to finish
    for (auto& thread : streamingThreads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    streamingThreads_.clear();
    
    // Unload all cells
    for (auto& [hash, cell] : cells_) {
        if (cell.state == WorldCell::State::Loaded) {
            unloadCellActors(cell);
        }
    }
    
    cells_.clear();
    sources_.clear();
    dataLayers_.clear();
    streamingVolumes_.clear();
    
    initialized_ = false;
}

glm::ivec2 LevelStreaming::worldToCell(const glm::vec3& worldPos) const {
    return glm::ivec2(
        static_cast<int>(std::floor(worldPos.x / config_.cellSize)),
        static_cast<int>(std::floor(worldPos.z / config_.cellSize))
    );
}

glm::vec3 LevelStreaming::cellToWorld(glm::ivec2 cellCoord) const {
    return glm::vec3(
        cellCoord.x * config_.cellSize + config_.cellSize * 0.5f,
        0.0f,
        cellCoord.y * config_.cellSize + config_.cellSize * 0.5f
    );
}

uint64_t LevelStreaming::cellHash(glm::ivec2 coord) const {
    return (static_cast<uint64_t>(static_cast<uint32_t>(coord.x)) << 32) |
            static_cast<uint32_t>(coord.y);
}

WorldCell& LevelStreaming::getOrCreateCell(glm::ivec2 coord) {
    uint64_t hash = cellHash(coord);
    
    auto it = cells_.find(hash);
    if (it == cells_.end()) {
        WorldCell cell;
        cell.id = static_cast<uint32_t>(cells_.size());
        cell.gridCoord = coord;
        cell.boundsMin = glm::vec3(coord.x * config_.cellSize, -1e6f, coord.y * config_.cellSize);
        cell.boundsMax = glm::vec3((coord.x + 1) * config_.cellSize, 1e6f, (coord.y + 1) * config_.cellSize);
        cell.state = WorldCell::State::Unloaded;
        
        cells_[hash] = cell;
        it = cells_.find(hash);
    }
    
    return it->second;
}

uint32_t LevelStreaming::addStreamingSource(const StreamingSource& source) {
    uint32_t id = nextSourceId_++;
    sources_[id] = source;
    sources_[id].id = id;
    return id;
}

void LevelStreaming::updateStreamingSource(uint32_t sourceId, const glm::vec3& position,
                                            const glm::vec3& velocity) {
    auto it = sources_.find(sourceId);
    if (it != sources_.end()) {
        it->second.position = position;
        it->second.velocity = velocity;
    }
}

void LevelStreaming::removeStreamingSource(uint32_t sourceId) {
    sources_.erase(sourceId);
}

uint32_t LevelStreaming::addDataLayer(const DataLayer& layer) {
    uint32_t id = nextLayerId_++;
    dataLayers_[id] = layer;
    dataLayers_[id].id = id;
    
    if (layer.isRuntime) {
        enabledLayers_.insert(id);
    }
    
    return id;
}

void LevelStreaming::setDataLayerEnabled(uint32_t layerId, bool enabled) {
    if (enabled) {
        enabledLayers_.insert(layerId);
    } else {
        enabledLayers_.erase(layerId);
    }
}

uint32_t LevelStreaming::addStreamingVolume(const StreamingVolume& volume) {
    uint32_t id = nextVolumeId_++;
    streamingVolumes_[id] = volume;
    streamingVolumes_[id].id = id;
    
    // Find affected cells
    glm::ivec2 minCell = worldToCell(volume.boundsMin);
    glm::ivec2 maxCell = worldToCell(volume.boundsMax);
    
    for (int y = minCell.y; y <= maxCell.y; ++y) {
        for (int x = minCell.x; x <= maxCell.x; ++x) {
            WorldCell& cell = getOrCreateCell({x, y});
            streamingVolumes_[id].affectedCells.push_back(cell.id);
        }
    }
    
    return id;
}

void LevelStreaming::addActorToCell(glm::ivec2 cellCoord, const CellActor& actor) {
    WorldCell& cell = getOrCreateCell(cellCoord);
    cell.actors.push_back(actor);
    
    // Update cell bounds
    cell.boundsMin.y = std::min(cell.boundsMin.y, actor.boundsMin.y);
    cell.boundsMax.y = std::max(cell.boundsMax.y, actor.boundsMax.y);
}

void LevelStreaming::update(float deltaTime, uint64_t frameNumber) {
    currentFrame_ = frameNumber;
    
    // Update streaming priorities
    updateStreamingPriorities();
    
    // Process streaming queue (synchronous loads if async disabled)
    if (!config_.useAsyncLoading) {
        processStreamingQueue();
    } else {
        // Wake up streaming threads
        streamingCondition_.notify_all();
    }
    
    // Update HLOD visibility
    updateHLODVisibility();
}

void LevelStreaming::updateStreamingPriorities() {
    // Calculate min distance to any streaming source for each cell
    for (auto& [hash, cell] : cells_) {
        cell.distanceToSource = std::numeric_limits<float>::max();
        cell.loadPriority = 0.0f;
        
        for (const auto& [id, source] : sources_) {
            if (!source.isActive) continue;
            
            glm::vec3 cellCenter = cellToWorld(cell.gridCoord);
            
            // Predict future position if using velocity
            glm::vec3 sourcePos = source.position;
            if (source.useVelocityPrediction) {
                sourcePos += source.velocity * 1.0f;  // 1 second prediction
            }
            
            float dist = glm::distance(glm::vec2(cellCenter.x, cellCenter.z),
                                        glm::vec2(sourcePos.x, sourcePos.z));
            
            if (dist < cell.distanceToSource) {
                cell.distanceToSource = dist;
            }
            
            // Calculate priority (closer = higher priority)
            float priority = source.priority * 1000.0f + (1000.0f - dist);
            cell.loadPriority = std::max(cell.loadPriority, priority);
        }
    }
    
    // Check streaming volumes
    for (const auto& [id, volume] : streamingVolumes_) {
        if (!volume.isEnabled) continue;
        
        for (uint32_t cellId : volume.affectedCells) {
            // Find cell by ID (inefficient - in production use ID lookup map)
            for (auto& [hash, cell] : cells_) {
                if (cell.id != cellId) continue;
                
                switch (volume.mode) {
                    case StreamingVolume::Mode::ForceLoad:
                        cell.loadPriority += 10000.0f;
                        break;
                    case StreamingVolume::Mode::BlockLoad:
                        cell.loadPriority = -10000.0f;
                        break;
                    case StreamingVolume::Mode::OverrideDistance:
                        cell.distanceToSource = std::min(cell.distanceToSource, volume.overrideDistance);
                        break;
                    case StreamingVolume::Mode::ForceUnload:
                        cell.loadPriority = -20000.0f;
                        break;
                }
                break;
            }
        }
    }
    
    // Queue loads/unloads
    std::lock_guard<std::mutex> lock(queueMutex_);
    
    for (auto& [hash, cell] : cells_) {
        bool shouldLoad = cell.distanceToSource < config_.streamingDistance &&
                          cell.loadPriority > 0;
        bool shouldUnload = cell.distanceToSource > config_.unloadDistance ||
                            cell.loadPriority < 0;
        
        if (shouldLoad && cell.state == WorldCell::State::Unloaded) {
            // Check if not already in queue
            bool alreadyQueued = false;
            {
                std::lock_guard<std::mutex> activeLock(activeLoadsMutex_);
                alreadyQueued = activeLoads_.count(cell.id) > 0;
            }
            
            if (!alreadyQueued) {
                StreamingRequest req;
                req.cellId = cell.id;
                req.priority = cell.loadPriority;
                req.isLoad = true;
                loadQueue_.push(req);
            }
        } else if (shouldUnload && cell.state == WorldCell::State::Loaded) {
            StreamingRequest req;
            req.cellId = cell.id;
            req.priority = -cell.loadPriority;  // Invert so low priority unloads first
            req.isLoad = false;
            unloadQueue_.push(req);
        }
    }
}

void LevelStreaming::processStreamingQueue() {
    uint32_t loadsThisFrame = 0;
    
    std::lock_guard<std::mutex> lock(queueMutex_);
    
    // Process loads
    while (!loadQueue_.empty() && loadsThisFrame < config_.maxLoadsPerFrame) {
        StreamingRequest req = loadQueue_.top();
        loadQueue_.pop();
        
        // Find cell
        for (auto& [hash, cell] : cells_) {
            if (cell.id == req.cellId && cell.state == WorldCell::State::Unloaded) {
                if (loadCell(cell)) {
                    loadsThisFrame++;
                }
                break;
            }
        }
    }
    
    // Process unloads
    while (!unloadQueue_.empty()) {
        StreamingRequest req = unloadQueue_.top();
        unloadQueue_.pop();
        
        // Find cell
        for (auto& [hash, cell] : cells_) {
            if (cell.id == req.cellId && cell.state == WorldCell::State::Loaded) {
                unloadCell(cell);
                break;
            }
        }
    }
}

void LevelStreaming::streamingThreadFunc() {
    while (!shutdownRequested_) {
        StreamingRequest req;
        bool hasRequest = false;
        
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            
            if (!loadQueue_.empty()) {
                req = loadQueue_.top();
                loadQueue_.pop();
                hasRequest = true;
            }
        }
        
        if (!hasRequest) {
            std::unique_lock<std::mutex> lock(streamingMutex_);
            streamingCondition_.wait_for(lock, std::chrono::milliseconds(100));
            continue;
        }
        
        // Mark as actively loading
        {
            std::lock_guard<std::mutex> lock(activeLoadsMutex_);
            if (activeLoads_.count(req.cellId) > 0) {
                continue;  // Already being loaded
            }
            activeLoads_.insert(req.cellId);
        }
        
        // Find and load cell
        auto startTime = std::chrono::high_resolution_clock::now();
        
        for (auto& [hash, cell] : cells_) {
            if (cell.id == req.cellId) {
                if (req.isLoad && cell.state == WorldCell::State::Unloaded) {
                    loadCell(cell);
                } else if (!req.isLoad && cell.state == WorldCell::State::Loaded) {
                    unloadCell(cell);
                }
                break;
            }
        }
        
        auto endTime = std::chrono::high_resolution_clock::now();
        float loadTime = std::chrono::duration<float, std::milli>(endTime - startTime).count();
        
        // Update statistics
        uint32_t count = loadCount_.fetch_add(1) + 1;
        float avg = averageLoadTime_.load();
        averageLoadTime_.store((avg * (count - 1) + loadTime) / count);
        
        // Remove from active loads
        {
            std::lock_guard<std::mutex> lock(activeLoadsMutex_);
            activeLoads_.erase(req.cellId);
        }
    }
}

bool LevelStreaming::loadCell(WorldCell& cell) {
    if (cell.state != WorldCell::State::Unloaded) {
        return false;
    }
    
    cell.state = WorldCell::State::Loading;
    
    // Load actors
    loadCellActors(cell);
    
    cell.state = WorldCell::State::Loaded;
    cell.lastAccessFrame = currentFrame_;
    
    // Fire callback
    if (onCellLoaded_) {
        onCellLoaded_(cell.id);
    }
    
    return true;
}

bool LevelStreaming::unloadCell(WorldCell& cell) {
    if (cell.state != WorldCell::State::Loaded) {
        return false;
    }
    
    cell.state = WorldCell::State::Unloading;
    
    // Unload actors
    unloadCellActors(cell);
    
    cell.state = WorldCell::State::Unloaded;
    
    // Fire callback
    if (onCellUnloaded_) {
        onCellUnloaded_(cell.id);
    }
    
    return true;
}

void LevelStreaming::loadCellActors(WorldCell& cell) {
    for (auto& actor : cell.actors) {
        if (actor.isLoaded) continue;
        
        // Load mesh
        // In production, this would load from asset system
        // actor.meshId = assetSystem->loadMesh(actor.typeName);
        
        // Create physics body if needed
        if (physics_ && actor.meshId != 0) {
            // Create static collision
            // actor.physicsBodyId = physics_->createStaticBody(...);
        }
        
        actor.isLoaded = true;
    }
}

void LevelStreaming::unloadCellActors(WorldCell& cell) {
    for (auto& actor : cell.actors) {
        if (!actor.isLoaded) continue;
        
        // Destroy physics body
        if (physics_ && actor.physicsBodyId != 0) {
            // physics_->destroyBody(actor.physicsBodyId);
            actor.physicsBodyId = 0;
        }
        
        // Unload mesh
        // assetSystem->unloadMesh(actor.meshId);
        actor.meshId = 0;
        
        actor.isLoaded = false;
    }
}

void LevelStreaming::updateHLODVisibility() {
    for (auto& [hash, cell] : cells_) {
        // Determine which HLOD level to show
        uint32_t targetHLOD = 0;
        
        for (size_t i = 0; i < config_.hlodLevels.size(); ++i) {
            if (cell.distanceToSource >= config_.hlodLevels[i].distance) {
                targetHLOD = static_cast<uint32_t>(i + 1);
            }
        }
        
        if (targetHLOD != cell.hlodLevel) {
            // Switch HLOD level
            // In production, this would show/hide appropriate actors
            cell.hlodLevel = targetHLOD;
        }
    }
}

void LevelStreaming::generateHLOD(const glm::ivec2& regionMin, const glm::ivec2& regionMax) {
    // Generate HLOD meshes for cells in region
    // This is typically done offline during cooking, not at runtime
    
    for (int y = regionMin.y; y <= regionMax.y; ++y) {
        for (int x = regionMin.x; x <= regionMax.x; ++x) {
            uint64_t hash = cellHash({x, y});
            auto it = cells_.find(hash);
            if (it == cells_.end()) continue;
            
            WorldCell& cell = it->second;
            
            // For each HLOD level, generate simplified mesh
            for (const auto& hlodLevel : config_.hlodLevels) {
                // Merge all actors in cell
                // Simplify to target triangle count
                // Generate texture atlas
                // Store as HLOD actor
            }
        }
    }
}

void LevelStreaming::forceLoadRadius(const glm::vec3& position, float radius, bool waitForComplete) {
    glm::ivec2 centerCell = worldToCell(position);
    int cellRadius = static_cast<int>(std::ceil(radius / config_.cellSize));
    
    std::vector<uint32_t> cellsToLoad;
    
    for (int y = -cellRadius; y <= cellRadius; ++y) {
        for (int x = -cellRadius; x <= cellRadius; ++x) {
            glm::ivec2 coord = centerCell + glm::ivec2(x, y);
            glm::vec3 cellCenter = cellToWorld(coord);
            
            if (glm::distance(glm::vec2(position.x, position.z),
                              glm::vec2(cellCenter.x, cellCenter.z)) > radius) {
                continue;
            }
            
            WorldCell& cell = getOrCreateCell(coord);
            if (cell.state == WorldCell::State::Unloaded) {
                cellsToLoad.push_back(cell.id);
            }
        }
    }
    
    if (waitForComplete) {
        // Synchronous load
        for (uint32_t cellId : cellsToLoad) {
            for (auto& [hash, cell] : cells_) {
                if (cell.id == cellId) {
                    loadCell(cell);
                    break;
                }
            }
        }
    } else {
        // Queue loads
        std::lock_guard<std::mutex> lock(queueMutex_);
        
        for (uint32_t cellId : cellsToLoad) {
            StreamingRequest req;
            req.cellId = cellId;
            req.priority = 10000.0f;  // High priority
            req.isLoad = true;
            loadQueue_.push(req);
        }
        
        streamingCondition_.notify_all();
    }
}

void LevelStreaming::forceUnloadAll() {
    for (auto& [hash, cell] : cells_) {
        if (cell.state == WorldCell::State::Loaded) {
            unloadCell(cell);
        }
    }
}

bool LevelStreaming::isCellLoaded(glm::ivec2 cellCoord) const {
    uint64_t hash = cellHash(cellCoord);
    auto it = cells_.find(hash);
    if (it == cells_.end()) return false;
    return it->second.state == WorldCell::State::Loaded;
}

WorldCell* LevelStreaming::getCellAt(const glm::vec3& position) {
    glm::ivec2 coord = worldToCell(position);
    uint64_t hash = cellHash(coord);
    auto it = cells_.find(hash);
    if (it == cells_.end()) return nullptr;
    return &it->second;
}

const WorldCell* LevelStreaming::getCellAt(const glm::vec3& position) const {
    glm::ivec2 coord = worldToCell(position);
    uint64_t hash = cellHash(coord);
    auto it = cells_.find(hash);
    if (it == cells_.end()) return nullptr;
    return &it->second;
}

bool LevelStreaming::loadWorld(const std::string& worldPath) {
    std::ifstream file(worldPath, std::ios::binary);
    if (!file.is_open()) return false;
    
    // Read header
    char magic[4];
    file.read(magic, 4);
    if (strncmp(magic, "WLVL", 4) != 0) return false;
    
    uint32_t version;
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    
    // Read cells
    uint32_t cellCount;
    file.read(reinterpret_cast<char*>(&cellCount), sizeof(cellCount));
    
    for (uint32_t i = 0; i < cellCount; ++i) {
        glm::ivec2 coord;
        file.read(reinterpret_cast<char*>(&coord), sizeof(coord));
        
        WorldCell& cell = getOrCreateCell(coord);
        
        // Read actor count
        uint32_t actorCount;
        file.read(reinterpret_cast<char*>(&actorCount), sizeof(actorCount));
        
        cell.actors.resize(actorCount);
        for (uint32_t j = 0; j < actorCount; ++j) {
            CellActor& actor = cell.actors[j];
            
            // Read type name length
            uint32_t nameLen;
            file.read(reinterpret_cast<char*>(&nameLen), sizeof(nameLen));
            
            actor.typeName.resize(nameLen);
            file.read(actor.typeName.data(), nameLen);
            
            // Read transform and bounds
            file.read(reinterpret_cast<char*>(&actor.transform), sizeof(actor.transform));
            file.read(reinterpret_cast<char*>(&actor.boundsMin), sizeof(actor.boundsMin));
            file.read(reinterpret_cast<char*>(&actor.boundsMax), sizeof(actor.boundsMax));
            
            actor.isLoaded = false;
        }
    }
    
    return true;
}

bool LevelStreaming::saveWorld(const std::string& worldPath) {
    std::ofstream file(worldPath, std::ios::binary);
    if (!file.is_open()) return false;
    
    // Write header
    file.write("WLVL", 4);
    uint32_t version = 1;
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));
    
    // Write cells
    uint32_t cellCount = static_cast<uint32_t>(cells_.size());
    file.write(reinterpret_cast<const char*>(&cellCount), sizeof(cellCount));
    
    for (const auto& [hash, cell] : cells_) {
        file.write(reinterpret_cast<const char*>(&cell.gridCoord), sizeof(cell.gridCoord));
        
        uint32_t actorCount = static_cast<uint32_t>(cell.actors.size());
        file.write(reinterpret_cast<const char*>(&actorCount), sizeof(actorCount));
        
        for (const auto& actor : cell.actors) {
            uint32_t nameLen = static_cast<uint32_t>(actor.typeName.size());
            file.write(reinterpret_cast<const char*>(&nameLen), sizeof(nameLen));
            file.write(actor.typeName.data(), nameLen);
            
            file.write(reinterpret_cast<const char*>(&actor.transform), sizeof(actor.transform));
            file.write(reinterpret_cast<const char*>(&actor.boundsMin), sizeof(actor.boundsMin));
            file.write(reinterpret_cast<const char*>(&actor.boundsMax), sizeof(actor.boundsMax));
        }
    }
    
    return true;
}

LevelStreaming::Statistics LevelStreaming::getStatistics() const {
    Statistics stats = {};
    stats.totalCells = static_cast<uint32_t>(cells_.size());
    stats.memoryUsed = memoryUsed_.load();
    stats.memoryBudget = config_.streamingBudget;
    stats.averageLoadTime = averageLoadTime_.load();
    
    for (const auto& [hash, cell] : cells_) {
        switch (cell.state) {
            case WorldCell::State::Loaded:
                stats.loadedCells++;
                break;
            case WorldCell::State::Loading:
                stats.loadingCells++;
                break;
            default:
                break;
        }
    }
    
    {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(queueMutex_));
        stats.pendingLoads = static_cast<uint32_t>(
            const_cast<std::priority_queue<StreamingRequest>&>(loadQueue_).size()
        );
        stats.pendingUnloads = static_cast<uint32_t>(
            const_cast<std::priority_queue<StreamingRequest>&>(unloadQueue_).size()
        );
    }
    
    return stats;
}

void LevelStreaming::debugDraw(VkCommandBuffer cmd, const glm::mat4& viewProj) {
    // Draw debug visualization of streaming grid
    // Color cells by state: gray = unloaded, green = loaded, yellow = loading
    // (Implementation would use a debug draw system)
}

} // namespace Sanic

