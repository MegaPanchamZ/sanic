/**
 * DestructionSystem.h
 * 
 * Chaos-style destruction and fracture system.
 * Implements Voronoi-based fracturing with strain-based breaking.
 * 
 * Key features:
 * - Voronoi fracture pattern generation
 * - Strain-based breaking thresholds
 * - Hierarchical clustering for progressive destruction
 * - Connectivity graph for structural integrity
 * - GPU-accelerated fracture computation
 */

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <functional>

// Forward declarations
class AsyncPhysics;
class VulkanContext;
namespace JPH {
    class BodyID;
    class Shape;
}

// Voronoi cell representing a fracture piece
struct VoronoiCell {
    uint32_t id;
    glm::vec3 center;               // Voronoi site position
    std::vector<glm::vec3> vertices;
    std::vector<uint32_t> faces;    // Indices into vertices, triangulated
    std::vector<uint32_t> neighbors; // Adjacent cell IDs
    
    float volume;
    float mass;
    glm::vec3 centroid;             // Center of mass
    glm::mat3 inertia;              // Inertia tensor
    
    // Connectivity
    std::vector<float> connectionStrengths;  // Strength to each neighbor
};

// Fracture piece (cluster of cells)
struct FracturePiece {
    uint32_t id;
    std::vector<uint32_t> cellIds;
    
    glm::vec3 position;
    glm::quat rotation;
    glm::vec3 velocity;
    glm::vec3 angularVelocity;
    
    float totalMass;
    glm::mat3 inertia;
    
    // Physics body (created when piece separates)
    JPH::BodyID* bodyId = nullptr;
    
    // State
    bool isReleased;        // Separated from parent cluster
    bool isActive;          // Still simulating
    float strain;           // Accumulated strain
    float strainThreshold;  // Breaking threshold
};

// Cluster hierarchy node
struct ClusterNode {
    uint32_t id;
    std::vector<uint32_t> childIds;     // Child clusters or pieces
    uint32_t parentId;
    
    glm::vec3 center;
    float radius;
    
    float totalStrain;
    float breakThreshold;
    
    bool isLeaf;            // True if contains pieces, false if contains clusters
    bool isBroken;          // Has been fractured
};

// Connectivity edge between pieces
struct ConnectivityEdge {
    uint32_t pieceA;
    uint32_t pieceB;
    float strength;         // Connection strength
    float area;             // Contact surface area
    glm::vec3 contactPoint;
    glm::vec3 contactNormal;
    bool isBroken;
};

// Destructible object configuration
struct DestructibleConfig {
    // Fracture generation
    uint32_t voronoiCellCount = 50;
    float minCellSize = 0.1f;           // Minimum cell dimension
    float cellSizeVariance = 0.5f;      // 0 = uniform, 1 = very varied
    bool useClusteredSites = true;      // Cluster voronoi sites for more realistic breaks
    
    // Breaking thresholds
    float baseStrainThreshold = 1000.0f; // Base strain to break
    float strainVariance = 0.3f;        // Variance in threshold
    float impactMultiplier = 2.0f;      // Extra strain from impacts
    
    // Connectivity
    float connectionStrength = 100.0f;  // Base connection strength
    bool useDelaunayConnectivity = true;// Use Delaunay triangulation for connectivity
    
    // Hierarchy
    uint32_t hierarchyLevels = 3;       // Levels of cluster hierarchy
    float clusterRadius = 1.0f;         // Base cluster radius
    
    // Debris
    float debrisLifetime = 10.0f;       // Seconds before debris despawns
    float debrisMinSize = 0.05f;        // Minimum debris size to simulate
    bool enableDebrisCollision = true;
    
    // GPU fracture
    bool useGPUFracture = true;
};

// High-speed collision settings for character impact
struct HighSpeedCollisionSettings {
    float minVelocityToBreak = 50.0f;       // Minimum velocity (m/s) to trigger break
    float velocityToForceMultiplier = 20.0f; // Convert velocity to impact force
    float impactRadius = 2.0f;              // Radius affected by high-speed impact
    float characterMass = 80.0f;            // Mass for impulse calculation
    bool applyImpulseToDebris = true;       // Give debris velocity from impact
    float debrisImpulseMultiplier = 0.1f;   // Scale debris impulse
};

// Enhanced debris tracking with distance-based despawn
struct DebrisSettings {
    float lifetime = 10.0f;                 // Base lifetime in seconds
    float despawnDistance = 100.0f;         // Distance from player to despawn
    float lodDistanceNear = 20.0f;          // Full physics simulation distance
    float lodDistanceMid = 50.0f;           // Reduced simulation distance
    bool freezeDistantDebris = true;        // Put far debris to sleep
    uint32_t maxActiveDebris = 256;         // Limit active debris for performance
    float smallDebrisThreshold = 0.1f;      // Volume below which debris is "small"
    float smallDebrisLifetimeMultiplier = 0.5f; // Small debris dies faster
};

// Callback for destruction events
using DestructionCallback = std::function<void(uint32_t objectId, const std::vector<uint32_t>& newPieceIds)>;

// Callback for high-speed collision events
using HighSpeedCollisionCallback = std::function<void(uint32_t objectId, const glm::vec3& impactPoint, float impactForce)>;

class DestructionSystem {
public:
    DestructionSystem();
    ~DestructionSystem();
    
    /**
     * Initialize the destruction system
     */
    bool initialize(AsyncPhysics* physics, VulkanContext* context = nullptr);
    
    /**
     * Shutdown and cleanup
     */
    void shutdown();
    
    /**
     * Pre-fracture a mesh into Voronoi cells
     * @param meshId Mesh asset ID
     * @param vertices Mesh vertex positions
     * @param indices Mesh triangle indices
     * @param config Fracture configuration
     * @return Destructible object ID
     */
    uint32_t preFracture(uint32_t meshId,
                          const std::vector<glm::vec3>& vertices,
                          const std::vector<uint32_t>& indices,
                          const DestructibleConfig& config = {});
    
    /**
     * Create a destructible object instance from pre-fractured data
     */
    uint32_t createInstance(uint32_t fractureDataId,
                            const glm::vec3& position,
                            const glm::quat& rotation,
                            const glm::vec3& scale);
    
    /**
     * Apply damage/strain at a point
     * @param objectId Destructible object ID
     * @param point World-space point of impact
     * @param direction Impact direction
     * @param magnitude Impact force magnitude
     * @return True if any pieces broke off
     */
    bool applyDamage(uint32_t objectId,
                     const glm::vec3& point,
                     const glm::vec3& direction,
                     float magnitude);
    
    /**
     * Apply high-speed character collision damage
     * Used for Sonic-style destruction where character velocity determines break force
     * @param objectId Destructible object ID
     * @param characterPosition Character world position
     * @param characterVelocity Character velocity vector
     * @return True if any pieces broke off
     */
    bool applyHighSpeedCollision(uint32_t objectId,
                                  const glm::vec3& characterPosition,
                                  const glm::vec3& characterVelocity);
    
    /**
     * Check for destructible objects in a sphere and apply high-speed damage
     * @param center Sphere center (character position)
     * @param radius Detection radius
     * @param velocity Character velocity
     * @return Vector of object IDs that were damaged
     */
    std::vector<uint32_t> checkHighSpeedCollisions(const glm::vec3& center,
                                                     float radius,
                                                     const glm::vec3& velocity);
    
    /**
     * Apply explosion damage
     */
    bool applyExplosion(const glm::vec3& center,
                        float radius,
                        float force);
    
    /**
     * Update destruction simulation
     */
    void update(float deltaTime);
    
    /**
     * Get mesh data for a fracture piece (for rendering)
     */
    bool getPieceMesh(uint32_t objectId,
                      uint32_t pieceId,
                      std::vector<glm::vec3>& outVertices,
                      std::vector<uint32_t>& outIndices);
    
    /**
     * Get transforms for all active pieces
     */
    void getActiveTransforms(uint32_t objectId,
                             std::vector<glm::mat4>& outTransforms);
    
    /**
     * Set callback for destruction events
     */
    void setDestructionCallback(DestructionCallback callback) { callback_ = callback; }
    
    /**
     * Set callback for high-speed collision events
     */
    void setHighSpeedCollisionCallback(HighSpeedCollisionCallback callback) { highSpeedCallback_ = callback; }
    
    /**
     * Set player/character position for distance-based debris management
     */
    void setPlayerPosition(const glm::vec3& position) { playerPosition_ = position; }
    
    /**
     * Configure high-speed collision settings
     */
    void setHighSpeedSettings(const HighSpeedCollisionSettings& settings) { highSpeedSettings_ = settings; }
    const HighSpeedCollisionSettings& getHighSpeedSettings() const { return highSpeedSettings_; }
    
    /**
     * Configure debris tracking settings
     */
    void setDebrisSettings(const DebrisSettings& settings) { debrisSettings_ = settings; }
    const DebrisSettings& getDebrisSettings() const { return debrisSettings_; }
    
    /**
     * Get list of objects near a position (for quick spatial queries)
     */
    std::vector<uint32_t> getObjectsInRadius(const glm::vec3& center, float radius) const;
    
    /**
     * Check if an object is still intact (not fully destroyed)
     */
    bool isObjectIntact(uint32_t objectId) const;
    
    // Stats
    struct Stats {
        uint32_t activePieces;
        uint32_t pendingBreaks;
        uint32_t totalFracturedObjects;
        float totalStrainAccumulated;
        uint32_t activeDebrisCount;
        uint32_t sleepingDebrisCount;
        uint32_t highSpeedBreaksThisFrame;
    };
    Stats getStats() const;
    
private:
    // Voronoi diagram generation
    struct VoronoiDiagram {
        std::vector<VoronoiCell> cells;
        std::vector<ConnectivityEdge> edges;
        glm::vec3 boundsMin;
        glm::vec3 boundsMax;
    };
    
    VoronoiDiagram generateVoronoi(const std::vector<glm::vec3>& vertices,
                                    const std::vector<uint32_t>& indices,
                                    const DestructibleConfig& config);
    
    // Site generation
    std::vector<glm::vec3> generateVoronoiSites(const glm::vec3& boundsMin,
                                                  const glm::vec3& boundsMax,
                                                  uint32_t count,
                                                  bool clustered);
    
    // Mesh clipping
    void clipMeshToCell(const std::vector<glm::vec3>& vertices,
                        const std::vector<uint32_t>& indices,
                        VoronoiCell& cell);
    
    // Build cluster hierarchy
    void buildHierarchy(uint32_t fractureDataId, const DestructibleConfig& config);
    
    // Connectivity
    void buildConnectivityGraph(VoronoiDiagram& diagram, bool useDelaunay);
    
    // Breaking
    void processBreaking(uint32_t objectId);
    void breakConnection(uint32_t objectId, uint32_t edgeIndex);
    void releasePiece(uint32_t objectId, uint32_t pieceId);
    
    // Physics integration
    void createPieceBody(uint32_t objectId, uint32_t pieceId);
    
    // Cleanup
    void cleanupDebris(float deltaTime);
    
    AsyncPhysics* physics_ = nullptr;
    VulkanContext* context_ = nullptr;
    
    // Pre-fractured data storage
    struct FractureData {
        VoronoiDiagram voronoi;
        std::vector<ClusterNode> hierarchy;
        DestructibleConfig config;
        
        // Cached mesh data per cell
        std::vector<std::vector<glm::vec3>> cellVertices;
        std::vector<std::vector<uint32_t>> cellIndices;
    };
    std::unordered_map<uint32_t, FractureData> fractureData_;
    uint32_t nextFractureId_ = 1;
    
    // Active instances
    struct DestructibleInstance {
        uint32_t fractureDataId;
        glm::mat4 transform;
        std::vector<FracturePiece> pieces;
        std::vector<ConnectivityEdge> edges;
        std::vector<ClusterNode> clusters;  // Instance-specific hierarchy state
        
        bool isDestroyed;
        float totalStrain;
    };
    std::unordered_map<uint32_t, DestructibleInstance> instances_;
    uint32_t nextInstanceId_ = 1;
    
    // Pending breaks
    struct PendingBreak {
        uint32_t objectId;
        uint32_t pieceId;
        float strain;
    };
    std::vector<PendingBreak> pendingBreaks_;
    
    // Debris tracking
    struct Debris {
        uint32_t objectId;
        uint32_t pieceId;
        float lifetime;
        float volume;           // For size-based lifetime
        bool isSleeping;        // Physics frozen for distant debris
        glm::vec3 lastPosition; // For velocity-based wakeup
    };
    std::vector<Debris> debris_;
    
    // High-speed collision tracking
    HighSpeedCollisionSettings highSpeedSettings_;
    DebrisSettings debrisSettings_;
    glm::vec3 playerPosition_ = glm::vec3(0.0f);
    uint32_t highSpeedBreaksThisFrame_ = 0;
    
    // Spatial acceleration structure for quick queries
    struct SpatialCell {
        std::vector<uint32_t> objectIds;
    };
    std::unordered_map<uint64_t, SpatialCell> spatialGrid_;
    float spatialCellSize_ = 10.0f;
    
    // Helpers for spatial grid
    uint64_t getSpatialKey(const glm::vec3& position) const;
    void addToSpatialGrid(uint32_t objectId, const glm::vec3& position);
    void removeFromSpatialGrid(uint32_t objectId, const glm::vec3& position);
    
    // Callback
    DestructionCallback callback_;
    HighSpeedCollisionCallback highSpeedCallback_;
    
    bool initialized_ = false;
};

