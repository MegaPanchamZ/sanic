/**
 * NavigationSystem.h
 * 
 * AI Navigation using Recast/Detour NavMesh
 * 
 * Features:
 * - NavMesh generation from level geometry
 * - Pathfinding with A* through Detour
 * - Path smoothing and string-pulling
 * - Dynamic obstacle avoidance
 * - Off-mesh links (jumps, ladders, etc.)
 * - NavMesh streaming for large worlds
 * 
 * Reference:
 *   Engine/Source/Runtime/NavigationSystem/
 *   Engine/Source/Runtime/AIModule/
 *   Engine/Source/Runtime/Navmesh/ (Recast/Detour)
 */

#pragma once

#include "ECS.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <queue>

// Forward declarations for Recast/Detour
struct rcConfig;
struct rcHeightfield;
struct rcCompactHeightfield;
struct rcContourSet;
struct rcPolyMesh;
struct rcPolyMeshDetail;
class dtNavMesh;
class dtNavMeshQuery;
class dtCrowd;
class dtQueryFilter;

namespace Sanic {

// ============================================================================
// NAVIGATION MESH
// ============================================================================

/**
 * Settings for NavMesh generation
 */
struct NavMeshSettings {
    // Cell size (resolution)
    float cellSize = 0.3f;            // Width/depth of a cell
    float cellHeight = 0.2f;          // Height of a cell
    
    // Agent settings
    float agentRadius = 0.5f;
    float agentHeight = 2.0f;
    float agentMaxClimb = 0.5f;       // Maximum step height
    float agentMaxSlope = 45.0f;      // Maximum walkable slope in degrees
    
    // Region settings
    float regionMinSize = 8.0f;       // Minimum region area (cells)
    float regionMergeSize = 20.0f;    // Merge regions smaller than this
    
    // Polygon settings
    float edgeMaxLen = 12.0f;         // Maximum edge length
    float edgeMaxError = 1.3f;        // Maximum distance from contour to polygon
    int vertsPerPoly = 6;             // Maximum vertices per polygon (max 6)
    
    // Detail mesh
    float detailSampleDist = 6.0f;
    float detailSampleMaxError = 1.0f;
    
    // Tiling
    bool useTiles = true;
    float tileSize = 48.0f;           // Tile size in cells
};

/**
 * Input geometry for NavMesh building
 */
struct NavMeshInputGeometry {
    std::vector<glm::vec3> vertices;
    std::vector<uint32_t> indices;
    
    // Bounds
    glm::vec3 boundsMin = glm::vec3(FLT_MAX);
    glm::vec3 boundsMax = glm::vec3(-FLT_MAX);
    
    void addTriangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c);
    void calculateBounds();
};

/**
 * Represents a navigation mesh tile
 */
struct NavMeshTile {
    int tileX = 0;
    int tileY = 0;
    
    std::vector<uint8_t> data;  // Serialized tile data
    bool loaded = false;
};

/**
 * Off-mesh connection (jump points, ladders, etc.)
 */
struct OffMeshConnection {
    glm::vec3 startPos;
    glm::vec3 endPos;
    float radius = 0.5f;
    
    enum class Direction {
        Bidirectional,
        StartToEnd,
        EndToStart
    } direction = Direction::Bidirectional;
    
    uint32_t areaType = 0;
    uint32_t userId = 0;
    
    // Cost modifiers
    float costMultiplier = 1.0f;
};

/**
 * Navigation mesh manager
 */
class NavigationMesh {
public:
    NavigationMesh();
    ~NavigationMesh();
    
    /**
     * Build NavMesh from geometry
     */
    bool build(const NavMeshInputGeometry& geometry, const NavMeshSettings& settings = {});
    
    /**
     * Build a single tile (for streaming)
     */
    bool buildTile(int tileX, int tileY, const NavMeshInputGeometry& geometry);
    
    /**
     * Remove a tile
     */
    void removeTile(int tileX, int tileY);
    
    /**
     * Add off-mesh connection
     */
    bool addOffMeshConnection(const OffMeshConnection& connection);
    
    /**
     * Remove off-mesh connection
     */
    void removeOffMeshConnection(uint32_t userId);
    
    /**
     * Save NavMesh to file
     */
    bool saveToFile(const std::string& path) const;
    
    /**
     * Load NavMesh from file
     */
    bool loadFromFile(const std::string& path);
    
    /**
     * Get the Detour NavMesh (for direct queries)
     */
    dtNavMesh* getNavMesh() { return navMesh_; }
    const dtNavMesh* getNavMesh() const { return navMesh_; }
    
    /**
     * Check if NavMesh is valid
     */
    bool isValid() const { return navMesh_ != nullptr; }
    
    /**
     * Get settings used to build this NavMesh
     */
    const NavMeshSettings& getSettings() const { return settings_; }
    
    /**
     * Get bounds of the NavMesh
     */
    glm::vec3 getBoundsMin() const { return boundsMin_; }
    glm::vec3 getBoundsMax() const { return boundsMax_; }
    
private:
    dtNavMesh* navMesh_ = nullptr;
    NavMeshSettings settings_;
    
    glm::vec3 boundsMin_ = glm::vec3(0);
    glm::vec3 boundsMax_ = glm::vec3(0);
    
    std::vector<OffMeshConnection> offMeshConnections_;
    
    // Build helpers
    bool buildSingleTile(const NavMeshInputGeometry& geometry);
    bool buildTiledMesh(const NavMeshInputGeometry& geometry);
};

// ============================================================================
// PATHFINDING
// ============================================================================

/**
 * Path query filter (for area costs and flags)
 */
class NavQueryFilter {
public:
    NavQueryFilter();
    ~NavQueryFilter();
    
    /**
     * Set area cost (higher = harder to traverse)
     */
    void setAreaCost(uint32_t areaId, float cost);
    
    /**
     * Get area cost
     */
    float getAreaCost(uint32_t areaId) const;
    
    /**
     * Set which area flags are walkable
     */
    void setIncludeFlags(uint16_t flags);
    void setExcludeFlags(uint16_t flags);
    
    /**
     * Get the Detour filter
     */
    dtQueryFilter* getFilter() { return filter_; }
    const dtQueryFilter* getFilter() const { return filter_; }
    
private:
    dtQueryFilter* filter_ = nullptr;
};

/**
 * Result of a pathfinding query
 */
struct PathResult {
    bool success = false;
    bool partial = false;          // Path found but couldn't reach exact target
    
    std::vector<glm::vec3> path;   // Smoothed path points
    float totalCost = 0.0f;
    
    // Status
    enum class Status {
        Success,
        PartialPath,
        NoPath,
        InvalidStart,
        InvalidEnd,
        OutOfNodes
    } status = Status::NoPath;
};

/**
 * Navigation query interface
 */
class NavigationQuery {
public:
    NavigationQuery(NavigationMesh& navMesh);
    ~NavigationQuery();
    
    /**
     * Find path between two points
     */
    PathResult findPath(
        const glm::vec3& start,
        const glm::vec3& end,
        const NavQueryFilter& filter = NavQueryFilter()
    );
    
    /**
     * Find path asynchronously
     */
    using PathCallback = std::function<void(const PathResult&)>;
    void findPathAsync(
        const glm::vec3& start,
        const glm::vec3& end,
        PathCallback callback,
        const NavQueryFilter& filter = NavQueryFilter()
    );
    
    /**
     * Find nearest point on NavMesh
     */
    bool findNearestPoint(
        const glm::vec3& point,
        glm::vec3& outNearest,
        float searchRadius = 2.0f
    );
    
    /**
     * Raycast on NavMesh
     */
    bool raycast(
        const glm::vec3& start,
        const glm::vec3& end,
        glm::vec3& outHitPoint,
        glm::vec3& outHitNormal
    );
    
    /**
     * Check if point is on NavMesh
     */
    bool isPointOnNavMesh(const glm::vec3& point, float tolerance = 0.5f);
    
    /**
     * Get random point on NavMesh
     */
    glm::vec3 getRandomPoint();
    
    /**
     * Get random point within radius
     */
    glm::vec3 getRandomPointInRadius(const glm::vec3& center, float radius);
    
    /**
     * Project point to NavMesh
     */
    bool projectToNavMesh(const glm::vec3& point, glm::vec3& outProjected, float searchHeight = 5.0f);
    
private:
    NavigationMesh& navMesh_;
    dtNavMeshQuery* query_ = nullptr;
    
    // Path finding internals
    static const int MAX_POLYS = 256;
    static const int MAX_SMOOTH = 2048;
    
    std::vector<uint64_t> polyPath_;
    
    // String pulling for path smoothing
    void smoothPath(const std::vector<uint64_t>& polys, int polyCount,
                    const glm::vec3& start, const glm::vec3& end,
                    std::vector<glm::vec3>& outPath);
};

// ============================================================================
// CROWD SIMULATION
// ============================================================================

/**
 * Parameters for a crowd agent
 */
struct CrowdAgentParams {
    float radius = 0.5f;
    float height = 2.0f;
    float maxAcceleration = 8.0f;
    float maxSpeed = 3.5f;
    
    // Collision
    float collisionQueryRange = 12.0f;
    float pathOptimizationRange = 30.0f;
    
    // Separation
    float separationWeight = 2.0f;
    
    // Update flags
    bool anticipateTurns = true;
    bool optimizeVisibility = true;
    bool optimizeTopology = true;
    bool obstacleAvoidance = true;
    bool separation = true;
    
    // Obstacle avoidance quality
    int obstacleAvoidanceType = 3;  // 0-3, higher = better but slower
};

/**
 * State of a crowd agent
 */
struct CrowdAgentState {
    glm::vec3 position;
    glm::vec3 velocity;
    glm::vec3 desiredVelocity;
    
    bool active = false;
    bool reachedTarget = false;
    bool partialPath = false;
    
    // Current path
    std::vector<glm::vec3> corridor;
    int currentCornerIdx = 0;
};

/**
 * Crowd navigation manager
 */
class CrowdManager {
public:
    CrowdManager(NavigationMesh& navMesh);
    ~CrowdManager();
    
    /**
     * Initialize crowd simulation
     */
    bool initialize(int maxAgents = 128);
    
    /**
     * Shutdown crowd
     */
    void shutdown();
    
    /**
     * Add an agent to the crowd
     * @return Agent ID, or -1 on failure
     */
    int addAgent(const glm::vec3& position, const CrowdAgentParams& params = {});
    
    /**
     * Remove an agent
     */
    void removeAgent(int agentId);
    
    /**
     * Set agent target
     */
    bool setAgentTarget(int agentId, const glm::vec3& target);
    
    /**
     * Clear agent target (stop movement)
     */
    void clearAgentTarget(int agentId);
    
    /**
     * Set agent velocity directly
     */
    void setAgentVelocity(int agentId, const glm::vec3& velocity);
    
    /**
     * Update agent parameters
     */
    void setAgentParams(int agentId, const CrowdAgentParams& params);
    
    /**
     * Get agent state
     */
    CrowdAgentState getAgentState(int agentId) const;
    
    /**
     * Update all agents
     */
    void update(float deltaTime);
    
    /**
     * Get number of active agents
     */
    int getActiveAgentCount() const;
    
    /**
     * Get maximum agents
     */
    int getMaxAgents() const { return maxAgents_; }
    
private:
    NavigationMesh& navMesh_;
    dtCrowd* crowd_ = nullptr;
    int maxAgents_ = 0;
};

// ============================================================================
// NAVIGATION SYSTEM (ECS)
// ============================================================================

/**
 * Component for entities that use navigation
 */
struct NavigationComponent {
    // Current path
    std::vector<glm::vec3> path;
    int currentWaypoint = 0;
    
    // Target
    glm::vec3 targetPosition = glm::vec3(0);
    bool hasTarget = false;
    
    // Movement
    float moveSpeed = 3.5f;
    float turnSpeed = 360.0f;  // Degrees per second
    float arrivalDistance = 0.5f;
    
    // State
    bool isMoving = false;
    bool reachedDestination = false;
    bool pathPending = false;
    
    // Crowd agent (if using crowd)
    int crowdAgentId = -1;
    CrowdAgentParams crowdParams;
    
    // Query filter
    std::shared_ptr<NavQueryFilter> filter;
    
    // Path following settings
    float pathRecalculateDistance = 2.0f;  // Recalculate if target moves this far
    float stuckCheckTime = 2.0f;           // Check if stuck after this time
    float stuckMoveThreshold = 0.1f;       // Minimum movement to not be stuck
};

/**
 * System for AI navigation
 */
class NavigationSystem : public System {
public:
    NavigationSystem();
    
    void init(World& world) override;
    void update(World& world, float deltaTime) override;
    void shutdown(World& world) override;
    
    /**
     * Set the navigation mesh
     */
    void setNavMesh(std::shared_ptr<NavigationMesh> navMesh);
    
    /**
     * Get the navigation mesh
     */
    NavigationMesh* getNavMesh() { return navMesh_.get(); }
    
    /**
     * Get navigation query
     */
    NavigationQuery* getQuery() { return query_.get(); }
    
    /**
     * Get crowd manager
     */
    CrowdManager* getCrowdManager() { return crowd_.get(); }
    
    /**
     * Request path for an entity
     */
    void requestPath(Entity entity, const glm::vec3& target);
    
    /**
     * Stop entity navigation
     */
    void stopNavigation(Entity entity);
    
    /**
     * Check if entity has reached destination
     */
    bool hasReachedDestination(Entity entity) const;
    
    /**
     * Build NavMesh from world geometry
     */
    bool buildNavMeshFromWorld(World& world, const NavMeshSettings& settings = {});
    
private:
    std::shared_ptr<NavigationMesh> navMesh_;
    std::unique_ptr<NavigationQuery> query_;
    std::unique_ptr<CrowdManager> crowd_;
    
    // Pending path requests
    struct PathRequest {
        Entity entity;
        glm::vec3 target;
    };
    std::queue<PathRequest> pendingRequests_;
    
    void processPathRequests(World& world);
    void updatePathFollowing(World& world, float deltaTime);
    void updateCrowdAgents(World& world, float deltaTime);
};

// ============================================================================
// AREA TYPES
// ============================================================================

namespace NavArea {
    constexpr uint8_t WALKABLE = 0;
    constexpr uint8_t WATER = 1;
    constexpr uint8_t GRASS = 2;
    constexpr uint8_t ROAD = 3;
    constexpr uint8_t DOOR = 4;
    constexpr uint8_t JUMP = 5;
    constexpr uint8_t CLIMB = 6;
    constexpr uint8_t DISABLED = 255;
}

namespace NavFlag {
    constexpr uint16_t WALK = 0x01;
    constexpr uint16_t SWIM = 0x02;
    constexpr uint16_t DOOR = 0x04;
    constexpr uint16_t JUMP = 0x08;
    constexpr uint16_t DISABLED = 0x10;
    constexpr uint16_t ALL = 0xFFFF;
}

} // namespace Sanic
