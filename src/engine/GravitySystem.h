/**
 * GravitySystem.h
 * 
 * Variable gravity volume system for:
 * - Loops (gravity toward center)
 * - Planetoids (spherical gravity)
 * - Twisted tubes (spline-based gravity)
 * - Ceiling walk areas (directional)
 */

#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <unordered_map>

namespace Sanic {

// Forward declarations
class SplineComponent;

// ============================================================================
// GRAVITY VOLUME TYPES
// ============================================================================

enum class GravityVolumeType {
    Directional,  // Constant direction (e.g., ceiling walk area)
    Spherical,    // Toward center (planetoids)
    SplineBased,  // Perpendicular to spline (tubes/loops)
    Cylindrical,  // Toward axis (for rotating sections)
    Point,        // Toward a point (black holes)
};

enum class GravityVolumeShape {
    Box,
    Sphere,
    Capsule,
    Infinite,     // Affects entire world (for base gravity)
};

// ============================================================================
// GRAVITY VOLUME
// ============================================================================

struct GravityVolume {
    uint32_t id = 0;
    GravityVolumeType type = GravityVolumeType::Directional;
    GravityVolumeShape shape = GravityVolumeShape::Box;
    
    // Transform
    glm::vec3 center = glm::vec3(0.0f);
    glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    
    // Shape dimensions
    glm::vec3 halfExtents = glm::vec3(5.0f);  // For box
    float radius = 5.0f;                       // For sphere
    float height = 10.0f;                      // For capsule
    
    // Gravity properties
    glm::vec3 direction = glm::vec3(0.0f, -1.0f, 0.0f);  // For Directional type
    float strength = 9.81f;
    float blendRadius = 2.0f;    // Transition zone at edges
    
    // For SplineBased
    SplineComponent* spline = nullptr;
    
    // For Point/Spherical
    glm::vec3 attractionPoint = glm::vec3(0.0f);  // Defaults to center
    
    // Priority (higher priority overrides lower)
    int priority = 0;
    
    // Flags
    bool enabled = true;
    bool invertGravity = false;  // Push away instead of toward
    
    // Falloff
    enum class FalloffType { None, Linear, InverseSquare, Smooth };
    FalloffType falloff = FalloffType::None;
    float falloffStart = 0.0f;   // Distance from center where falloff begins
    float falloffEnd = 1.0f;     // Distance where gravity reaches minimum
    float minimumStrength = 0.0f; // Gravity strength at falloff end
    
    // Tags for gameplay logic
    std::vector<std::string> tags;
};

// ============================================================================
// GRAVITY QUERY RESULT
// ============================================================================

struct GravityQueryResult {
    glm::vec3 gravity = glm::vec3(0.0f, -9.81f, 0.0f);
    glm::vec3 direction = glm::vec3(0.0f, -1.0f, 0.0f);
    float strength = 9.81f;
    
    // Which volume(s) contributed
    std::vector<uint32_t> activeVolumeIds;
    
    // Dominant volume (highest priority affecting position)
    uint32_t dominantVolumeId = 0;
    float blendFactor = 1.0f;  // 0 = at edge, 1 = fully inside
};

// ============================================================================
// GRAVITY SYSTEM
// ============================================================================

class GravitySystem {
public:
    GravitySystem();
    ~GravitySystem() = default;
    
    // ========== VOLUME MANAGEMENT ==========
    
    /**
     * Add a gravity volume
     * @return Volume ID
     */
    uint32_t addVolume(const GravityVolume& volume);
    
    /**
     * Remove a volume by ID
     */
    void removeVolume(uint32_t id);
    
    /**
     * Get a volume by ID
     */
    GravityVolume* getVolume(uint32_t id);
    const GravityVolume* getVolume(uint32_t id) const;
    
    /**
     * Clear all volumes
     */
    void clearVolumes();
    
    /**
     * Get all volumes
     */
    const std::vector<GravityVolume>& getVolumes() const { return volumes_; }
    
    // ========== GRAVITY QUERIES ==========
    
    /**
     * Get gravity at a world position
     */
    glm::vec3 getGravityAtPosition(const glm::vec3& position) const;
    
    /**
     * Get detailed gravity query result
     */
    GravityQueryResult queryGravity(const glm::vec3& position) const;
    
    /**
     * Get gravity direction at position (normalized)
     */
    glm::vec3 getGravityDirection(const glm::vec3& position) const;
    
    /**
     * Get gravity strength at position
     */
    float getGravityStrength(const glm::vec3& position) const;
    
    // ========== DEFAULT GRAVITY ==========
    
    void setDefaultGravity(const glm::vec3& gravity) { defaultGravity_ = gravity; }
    glm::vec3 getDefaultGravity() const { return defaultGravity_; }
    
    // ========== HELPER CREATORS ==========
    
    /**
     * Create a spherical gravity volume (planetoid)
     */
    uint32_t createSphericalVolume(
        const glm::vec3& center,
        float radius,
        float strength = 9.81f,
        int priority = 0);
    
    /**
     * Create a directional gravity volume (ceiling walk)
     */
    uint32_t createDirectionalVolume(
        const glm::vec3& center,
        const glm::vec3& halfExtents,
        const glm::vec3& direction,
        float strength = 9.81f,
        int priority = 0);
    
    /**
     * Create a spline-based gravity volume (loops/tubes)
     */
    uint32_t createSplineVolume(
        SplineComponent* spline,
        float radius,
        float strength = 9.81f,
        int priority = 0);
    
private:
    /**
     * Calculate influence (0-1) of a volume at a position
     */
    float calculateInfluence(const GravityVolume& volume, const glm::vec3& pos) const;
    
    /**
     * Calculate gravity vector from a volume at a position
     */
    glm::vec3 calculateVolumeGravity(const GravityVolume& volume, const glm::vec3& pos) const;
    
    /**
     * Check if position is inside a volume's shape
     */
    bool isInsideVolume(const GravityVolume& volume, const glm::vec3& pos) const;
    
    /**
     * Get signed distance to volume boundary
     */
    float getDistanceToVolume(const GravityVolume& volume, const glm::vec3& pos) const;
    
    std::vector<GravityVolume> volumes_;
    std::unordered_map<uint32_t, size_t> idToIndex_;
    uint32_t nextVolumeId_ = 1;
    
    glm::vec3 defaultGravity_ = glm::vec3(0.0f, -9.81f, 0.0f);
};

} // namespace Sanic
