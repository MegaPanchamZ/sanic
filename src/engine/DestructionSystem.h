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

// Callback for destruction events
using DestructionCallback = std::function<void(uint32_t objectId, const std::vector<uint32_t>& newPieceIds)>;

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
    
    // Stats
    struct Stats {
        uint32_t activePieces;
        uint32_t pendingBreaks;
        uint32_t totalFracturedObjects;
        float totalStrainAccumulated;
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
    };
    std::vector<Debris> debris_;
    
    // Callback
    DestructionCallback callback_;
    
    bool initialized_ = false;
};

