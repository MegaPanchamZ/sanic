/**
 * LevelStreaming.h
 * 
 * World Partition style level streaming system.
 * Implements Unreal Engine 5 style spatial hash grid streaming.
 * 
 * Key features:
 * - Spatial hash grid for world partition
 * - Distance-based streaming with priority
 * - Async loading with streaming pool
 * - HLOD for distant cells
 * - Data layers for content organization
 * - Streaming volumes for manual control
 * 
 * Architecture:
 * - World divided into cells (default 128m x 128m)
 * - Cells grouped into streaming levels
 * - HLOD actors generated for cell clusters
 * - Runtime grid managed by streaming sources
 */

#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <memory>
#include <string>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>

namespace Sanic {

// Forward declarations
class VulkanContext;
class AsyncPhysics;

/**
 * Data layer for organizing content
 */
struct DataLayer {
    uint32_t id;
    std::string name;
    
    bool isRuntime = true;          // Loaded at runtime
    bool isEditor = false;          // Editor-only layer
    bool isOptional = false;        // Can be skipped if memory low
    
    int32_t priority = 0;           // Load priority (higher = first)
};

/**
 * Streaming source (e.g., player, camera, important location)
 */
struct StreamingSource {
    uint32_t id;
    glm::vec3 position;
    glm::vec3 velocity;             // For predictive loading
    
    float streamingDistance = 256.0f;
    float hlodDistance = 512.0f;
    int32_t priority = 0;           // Higher priority sources load first
    
    bool isActive = true;
    bool useVelocityPrediction = true;
};

/**
 * Actor reference within a cell
 */
struct CellActor {
    uint32_t actorId;
    std::string typeName;
    glm::mat4 transform;
    glm::vec3 boundsMin;
    glm::vec3 boundsMax;
    
    // Streaming state
    bool isLoaded = false;
    uint32_t meshId = 0;
    uint32_t physicsBodyId = 0;
};

/**
 * World cell (smallest streaming unit)
 */
struct WorldCell {
    uint32_t id;
    glm::ivec2 gridCoord;           // Position in grid
    
    // Bounds
    glm::vec3 boundsMin;
    glm::vec3 boundsMax;
    
    // Content
    std::vector<CellActor> actors;
    std::vector<uint32_t> dataLayers;   // Which layers this cell has content in
    
    // HLOD
    uint32_t hlodLevel = 0;         // 0 = full detail, 1+ = simplified
    uint32_t hlodActorId = 0;       // Merged/simplified actor for distance
    
    // Streaming state
    enum class State {
        Unloaded,
        Loading,
        Loaded,
        Unloading
    };
    State state = State::Unloaded;
    
    float loadPriority = 0.0f;
    float distanceToSource = 0.0f;
    uint64_t lastAccessFrame = 0;
    
    // Dependencies
    std::vector<uint32_t> dependsOn;    // Cells that must load first
    std::vector<uint32_t> dependedBy;   // Cells that depend on this
};

/**
 * HLOD level definition
 */
struct HLODLevel {
    uint32_t level;
    float distance;                 // Min distance for this HLOD
    float transitionRange;          // Transition range to next level
    
    uint32_t maxTriangles;          // Max triangle budget per cell
    float textureResolution;        // Texture resolution multiplier
    bool mergeActors;               // Merge all cell actors into one
};

/**
 * Streaming volume for manual control
 */
struct StreamingVolume {
    uint32_t id;
    std::string name;
    
    glm::vec3 boundsMin;
    glm::vec3 boundsMax;
    
    enum class Mode {
        BlockLoad,                  // Prevent loading while inside
        ForceLoad,                  // Force load while inside
        ForceUnload,               // Force unload while inside
        OverrideDistance           // Override streaming distance
    };
    Mode mode = Mode::ForceLoad;
    
    float overrideDistance = 0.0f;
    std::vector<uint32_t> affectedCells;
    
    bool isEnabled = true;
};

/**
 * Spline point for path-based streaming
 */
struct SplinePoint {
    glm::vec3 position;
    glm::vec3 tangentIn;            // Incoming tangent (for Bezier/Hermite)
    glm::vec3 tangentOut;           // Outgoing tangent
    
    float streamingDistance = 256.0f;   // Override streaming distance at this point
    float hlodDistance = 512.0f;
    float roll = 0.0f;              // Roll angle for camera paths
    
    // Custom data at control point
    std::vector<uint32_t> forceLoadCells;
    std::vector<uint32_t> forceUnloadCells;
};

/**
 * Streaming spline for path-based level streaming
 * Used for linear paths (roads, rivers, railways)
 */
struct StreamingSpline {
    uint32_t id;
    std::string name;
    
    std::vector<SplinePoint> points;
    
    enum class Type {
        Linear,         // Linear interpolation
        CatmullRom,     // Catmull-Rom spline
        Bezier,         // Cubic Bezier
        Hermite         // Hermite spline
    };
    Type type = Type::CatmullRom;
    
    // Streaming settings
    float defaultStreamingDistance = 256.0f;
    float lookAheadDistance = 512.0f;   // How far ahead to preload
    float lookAheadTime = 5.0f;         // Seconds of travel to preload
    
    // Coverage
    float width = 50.0f;                // Width of streaming corridor
    
    // State
    bool isEnabled = true;
    bool isClosed = false;              // Closed loop spline
    float cachedLength = 0.0f;          // Total spline length
    
    // Sampled points for fast queries (generated from control points)
    std::vector<glm::vec3> sampledPoints;
    std::vector<float> sampledDistances;    // Distance from start at each sample
    uint32_t sampleCount = 100;
};

/**
 * Spline streaming source (tracks position along a spline)
 */
struct SplineStreamingSource {
    uint32_t id;
    uint32_t splineId;
    
    float position = 0.0f;          // 0-1 parameter along spline
    float velocity = 0.0f;          // Rate of change of position
    float distanceAlongSpline = 0.0f;
    
    glm::vec3 worldPosition;        // Current world position on spline
    glm::vec3 direction;            // Forward direction at current position
    
    bool isActive = true;
};

/**
 * Streaming request
 */
struct StreamingRequest {
    uint32_t cellId;
    float priority;
    bool isLoad;                    // true = load, false = unload
    
    bool operator<(const StreamingRequest& other) const {
        return priority < other.priority;  // Lower priority = later in queue
    }
};

/**
 * Level streaming configuration
 */
struct LevelStreamingConfig {
    // Grid
    float cellSize = 128.0f;        // World units per cell
    glm::ivec2 gridExtent = {256, 256};  // Max grid size
    
    // Streaming
    float streamingDistance = 256.0f;
    float unloadDistance = 384.0f;  // Hysteresis
    uint32_t maxConcurrentLoads = 4;
    uint32_t maxLoadsPerFrame = 2;
    float loadTimeout = 30.0f;      // Seconds before giving up
    
    // Memory
    uint64_t streamingBudget = 1024 * 1024 * 1024;  // 1 GB
    uint64_t hlodBudget = 256 * 1024 * 1024;        // 256 MB
    
    // HLOD
    std::vector<HLODLevel> hlodLevels = {
        {1, 256.0f, 32.0f, 10000, 0.5f, true},
        {2, 512.0f, 64.0f, 2500, 0.25f, true},
        {3, 1024.0f, 128.0f, 500, 0.125f, true},
    };
    
    // Threading
    uint32_t streamingThreads = 2;
    bool useAsyncLoading = true;
};

/**
 * Callback types
 */
using CellLoadedCallback = std::function<void(uint32_t cellId)>;
using CellUnloadedCallback = std::function<void(uint32_t cellId)>;

/**
 * World partition level streaming system
 */
class LevelStreaming {
public:
    LevelStreaming();
    ~LevelStreaming();
    
    /**
     * Initialize streaming system
     */
    bool initialize(VulkanContext* context, AsyncPhysics* physics,
                    const LevelStreamingConfig& config = {});
    
    /**
     * Shutdown and cleanup
     */
    void shutdown();
    
    /**
     * Create/load world from file
     */
    bool loadWorld(const std::string& worldPath);
    
    /**
     * Save world to file
     */
    bool saveWorld(const std::string& worldPath);
    
    /**
     * Register a streaming source
     */
    uint32_t addStreamingSource(const StreamingSource& source);
    
    /**
     * Update streaming source position
     */
    void updateStreamingSource(uint32_t sourceId, const glm::vec3& position,
                               const glm::vec3& velocity = glm::vec3(0));
    
    /**
     * Remove a streaming source
     */
    void removeStreamingSource(uint32_t sourceId);
    
    /**
     * Add data layer
     */
    uint32_t addDataLayer(const DataLayer& layer);
    
    /**
     * Enable/disable data layer
     */
    void setDataLayerEnabled(uint32_t layerId, bool enabled);
    
    /**
     * Add streaming volume
     */
    uint32_t addStreamingVolume(const StreamingVolume& volume);
    
    /**
     * Add streaming spline for path-based streaming
     */
    uint32_t addStreamingSpline(const StreamingSpline& spline);
    
    /**
     * Update streaming spline
     */
    void updateStreamingSpline(uint32_t splineId, const std::vector<SplinePoint>& points);
    
    /**
     * Remove streaming spline
     */
    void removeStreamingSpline(uint32_t splineId);
    
    /**
     * Get streaming spline
     */
    StreamingSpline* getStreamingSpline(uint32_t splineId);
    
    /**
     * Add spline streaming source (entity following a spline)
     */
    uint32_t addSplineStreamingSource(uint32_t splineId, float initialPosition = 0.0f);
    
    /**
     * Update spline streaming source position
     */
    void updateSplineStreamingSource(uint32_t sourceId, float position, float velocity = 0.0f);
    
    /**
     * Evaluate spline at parameter t (0-1)
     */
    glm::vec3 evaluateSpline(uint32_t splineId, float t) const;
    
    /**
     * Get tangent at parameter t
     */
    glm::vec3 evaluateSplineTangent(uint32_t splineId, float t) const;
    
    /**
     * Find closest point on spline to world position
     */
    float findClosestPointOnSpline(uint32_t splineId, const glm::vec3& worldPos) const;
    
    /**
     * Get streaming distance at spline position
     */
    float getSplineStreamingDistance(uint32_t splineId, float t) const;
    
    /**
     * Add actor to cell
     */
    void addActorToCell(glm::ivec2 cellCoord, const CellActor& actor);
    
    /**
     * Generate HLOD for cells
     */
    void generateHLOD(const glm::ivec2& regionMin, const glm::ivec2& regionMax);
    
    /**
     * Update streaming (call every frame)
     */
    void update(float deltaTime, uint64_t frameNumber);
    
    /**
     * Force load cells around a position
     */
    void forceLoadRadius(const glm::vec3& position, float radius, bool waitForComplete = false);
    
    /**
     * Force unload cells
     */
    void forceUnloadAll();
    
    /**
     * Query if cell is loaded
     */
    bool isCellLoaded(glm::ivec2 cellCoord) const;
    
    /**
     * Get cell at position
     */
    WorldCell* getCellAt(const glm::vec3& position);
    const WorldCell* getCellAt(const glm::vec3& position) const;
    
    /**
     * Set callbacks
     */
    void setOnCellLoaded(CellLoadedCallback callback) { onCellLoaded_ = callback; }
    void setOnCellUnloaded(CellUnloadedCallback callback) { onCellUnloaded_ = callback; }
    
    // Statistics
    struct Statistics {
        uint32_t totalCells;
        uint32_t loadedCells;
        uint32_t loadingCells;
        uint32_t pendingLoads;
        uint32_t pendingUnloads;
        uint64_t memoryUsed;
        uint64_t memoryBudget;
        float averageLoadTime;
    };
    Statistics getStatistics() const;
    
    // Debug
    void debugDraw(VkCommandBuffer cmd, const glm::mat4& viewProj);
    
private:
    // Internal methods
    glm::ivec2 worldToCell(const glm::vec3& worldPos) const;
    glm::vec3 cellToWorld(glm::ivec2 cellCoord) const;
    uint64_t cellHash(glm::ivec2 coord) const;
    
    WorldCell& getOrCreateCell(glm::ivec2 coord);
    
    void updateStreamingPriorities();
    void processStreamingQueue();
    void streamingThreadFunc();
    
    bool loadCell(WorldCell& cell);
    bool unloadCell(WorldCell& cell);
    
    void loadCellActors(WorldCell& cell);
    void unloadCellActors(WorldCell& cell);
    
    void updateHLODVisibility();
    
    // Spline helpers
    void resampleSpline(StreamingSpline& spline);
    glm::vec3 evaluateCatmullRom(const std::vector<SplinePoint>& points, float t, bool closed) const;
    glm::vec3 evaluateBezier(const std::vector<SplinePoint>& points, float t) const;
    glm::vec3 evaluateHermite(const std::vector<SplinePoint>& points, float t) const;
    void updateSplineStreamingSources();
    void collectCellsAlongSpline(const StreamingSpline& spline, float t, float lookAhead, 
                                  std::vector<uint32_t>& outCells) const;
    
    VulkanContext* context_ = nullptr;
    AsyncPhysics* physics_ = nullptr;
    LevelStreamingConfig config_;
    
    // Grid storage
    std::unordered_map<uint64_t, WorldCell> cells_;
    
    // Streaming sources
    std::unordered_map<uint32_t, StreamingSource> sources_;
    uint32_t nextSourceId_ = 1;
    
    // Data layers
    std::unordered_map<uint32_t, DataLayer> dataLayers_;
    std::unordered_set<uint32_t> enabledLayers_;
    uint32_t nextLayerId_ = 1;
    
    // Streaming volumes
    std::unordered_map<uint32_t, StreamingVolume> streamingVolumes_;
    uint32_t nextVolumeId_ = 1;
    
    // Streaming splines
    std::unordered_map<uint32_t, StreamingSpline> streamingSplines_;
    std::unordered_map<uint32_t, SplineStreamingSource> splineStreamingSources_;
    uint32_t nextSplineId_ = 1;
    uint32_t nextSplineSourceId_ = 1;
    
    // Streaming queue
    std::priority_queue<StreamingRequest> loadQueue_;
    std::priority_queue<StreamingRequest> unloadQueue_;
    std::mutex queueMutex_;
    
    // Streaming threads
    std::vector<std::thread> streamingThreads_;
    std::atomic<bool> shutdownRequested_{false};
    std::condition_variable streamingCondition_;
    std::mutex streamingMutex_;
    
    // Active loads tracking
    std::unordered_set<uint32_t> activeLoads_;
    std::mutex activeLoadsMutex_;
    
    // Memory tracking
    std::atomic<uint64_t> memoryUsed_{0};
    
    // Frame tracking
    uint64_t currentFrame_ = 0;
    
    // Callbacks
    CellLoadedCallback onCellLoaded_;
    CellUnloadedCallback onCellUnloaded_;
    
    // Statistics
    std::atomic<float> averageLoadTime_{0.0f};
    std::atomic<uint32_t> loadCount_{0};
    
    bool initialized_ = false;
};

} // namespace Sanic

