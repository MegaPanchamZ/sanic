/**
 * SplineComponent.h
 * 
 * Flexible spline system for:
 * - Grind rails (full movement lock)
 * - Ziplines (lock + hang offset)
 * - 2.5D sections (lateral constraint)
 * - Boost rings (velocity injection)
 * - Camera rails
 * 
 * Based on UE5's USplineComponent concepts.
 */

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>
#include <memory>

namespace Sanic {

// ============================================================================
// SPLINE TYPES
// ============================================================================

enum class SplineType {
    Linear,         // Straight lines between points
    CatmullRom,     // Smooth curves (default)
    Bezier,         // Bezier curves with tangent control
    Hermite         // Hermite splines
};

enum class SplineLockMode {
    None,           // Not locked to any spline
    FullLock,       // Grind rail - locked to spline position
    LateralLock,    // 2.5D - can move perpendicular to spline
    Velocity,       // Boost ring - inject velocity along tangent
};

// ============================================================================
// CONTROL POINT
// ============================================================================

struct SplineControlPoint {
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 tangentIn = glm::vec3(0.0f);    // For Bezier mode (arrive)
    glm::vec3 tangentOut = glm::vec3(0.0f);   // For Bezier mode (leave)
    float roll = 0.0f;                         // Banking angle in radians
    glm::vec3 scale = glm::vec3(1.0f);         // For spline mesh scaling
    
    // Optional per-point properties
    float speedMultiplier = 1.0f;
    bool isBreakpoint = false;                  // Stops at this point
};

// ============================================================================
// DISTANCE LOOKUP TABLE
// ============================================================================

struct SplineDistanceEntry {
    float distance;
    float parameter;
};

// ============================================================================
// SPLINE COMPONENT
// ============================================================================

class SplineComponent {
public:
    SplineComponent();
    ~SplineComponent() = default;
    
    // ========== CONTROL POINTS ==========
    
    /**
     * Add a control point at the end
     */
    void addControlPoint(const glm::vec3& position);
    void addControlPoint(const SplineControlPoint& point);
    
    /**
     * Insert a control point at specific index
     */
    void insertControlPoint(size_t index, const SplineControlPoint& point);
    
    /**
     * Remove a control point
     */
    void removeControlPoint(size_t index);
    
    /**
     * Clear all control points
     */
    void clearControlPoints();
    
    /**
     * Get/Set control points
     */
    const std::vector<SplineControlPoint>& getControlPoints() const { return controlPoints_; }
    SplineControlPoint& getControlPoint(size_t index) { return controlPoints_[index]; }
    const SplineControlPoint& getControlPoint(size_t index) const { return controlPoints_[index]; }
    void setControlPoint(size_t index, const SplineControlPoint& point);
    size_t getControlPointCount() const { return controlPoints_.size(); }
    
    // ========== EVALUATION AT PARAMETER T [0, 1] ==========
    
    /**
     * Get position at normalized parameter t [0, 1]
     */
    glm::vec3 evaluatePosition(float t) const;
    
    /**
     * Get tangent (derivative) at parameter t
     */
    glm::vec3 evaluateTangent(float t) const;
    
    /**
     * Get up vector with roll applied
     */
    glm::vec3 evaluateUp(float t) const;
    
    /**
     * Get right vector
     */
    glm::vec3 evaluateRight(float t) const;
    
    /**
     * Get rotation at parameter t
     */
    glm::quat evaluateRotation(float t) const;
    
    /**
     * Get scale at parameter t
     */
    glm::vec3 evaluateScale(float t) const;
    
    /**
     * Get roll at parameter t
     */
    float evaluateRoll(float t) const;
    
    /**
     * Get full transform at parameter t
     */
    glm::mat4 evaluateTransform(float t) const;
    
    // ========== EVALUATION AT DISTANCE ==========
    
    /**
     * Get position at distance along spline
     */
    glm::vec3 getPositionAtDistance(float distance) const;
    
    /**
     * Get tangent at distance along spline
     */
    glm::vec3 getTangentAtDistance(float distance) const;
    
    /**
     * Get up vector at distance
     */
    glm::vec3 getUpAtDistance(float distance) const;
    
    /**
     * Get rotation at distance
     */
    glm::quat getRotationAtDistance(float distance) const;
    
    /**
     * Get full transform at distance
     */
    glm::mat4 getTransformAtDistance(float distance) const;
    
    // ========== DISTANCE CONVERSION ==========
    
    /**
     * Convert distance to parameter t
     */
    float distanceToParameter(float distance) const;
    
    /**
     * Convert parameter t to distance
     */
    float parameterToDistance(float t) const;
    
    /**
     * Get total arc length of spline
     */
    float getTotalLength() const { return totalLength_; }
    
    // ========== CLOSEST POINT ==========
    
    /**
     * Find closest point on spline to world position
     * @return Parameter t of closest point
     */
    float findClosestParameter(const glm::vec3& worldPos) const;
    
    /**
     * Find closest distance along spline to world position
     */
    float findClosestDistance(const glm::vec3& worldPos) const;
    
    /**
     * Get closest point position
     */
    glm::vec3 findClosestPoint(const glm::vec3& worldPos) const;
    
    // ========== PROPERTIES ==========
    
    bool isLoop() const { return isLoop_; }
    void setLoop(bool loop);
    
    SplineType getType() const { return splineType_; }
    void setType(SplineType type);
    
    // Tags for gameplay (e.g., "Rail", "Zipline", "CameraPath")
    void addTag(const std::string& tag) { tags_.push_back(tag); }
    bool hasTag(const std::string& tag) const;
    const std::vector<std::string>& getTags() const { return tags_; }
    
    // World transform
    void setWorldTransform(const glm::mat4& transform);
    const glm::mat4& getWorldTransform() const { return worldTransform_; }
    
    // ========== REBUILD ==========
    
    /**
     * Rebuild distance lookup table
     * Call after modifying control points
     */
    void rebuildDistanceTable();
    
    /**
     * Auto-compute tangents for Bezier mode
     */
    void autoComputeTangents();
    
private:
    // Catmull-Rom interpolation
    static glm::vec3 catmullRom(
        const glm::vec3& p0, const glm::vec3& p1,
        const glm::vec3& p2, const glm::vec3& p3, float t);
    
    // Catmull-Rom derivative
    static glm::vec3 catmullRomDerivative(
        const glm::vec3& p0, const glm::vec3& p1,
        const glm::vec3& p2, const glm::vec3& p3, float t);
    
    // Bezier interpolation
    static glm::vec3 bezier(
        const glm::vec3& p0, const glm::vec3& t0,
        const glm::vec3& t1, const glm::vec3& p1, float t);
    
    // Helper to wrap index for looped splines
    size_t wrapIndex(int index) const;
    
    // Get segment info from parameter t
    void getSegmentInfo(float t, int& segment, float& localT) const;
    
    // Control points
    std::vector<SplineControlPoint> controlPoints_;
    
    // Properties
    bool isLoop_ = false;
    SplineType splineType_ = SplineType::CatmullRom;
    glm::mat4 worldTransform_ = glm::mat4(1.0f);
    
    // Distance lookup table
    std::vector<SplineDistanceEntry> distanceTable_;
    float totalLength_ = 0.0f;
    static constexpr int DISTANCE_TABLE_SAMPLES = 256;
    
    // Tags
    std::vector<std::string> tags_;
};

} // namespace Sanic
