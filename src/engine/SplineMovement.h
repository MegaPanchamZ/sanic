/**
 * SplineMovement.h
 * 
 * Spline movement component for:
 * - Full lock mode (grind rails)
 * - Lateral lock mode (2.5D sections)
 * - Velocity injection (boost rings)
 */

#pragma once

#include "SplineComponent.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <functional>

namespace Sanic {

// Forward declarations
class KineticCharacterController;

// ============================================================================
// SPLINE MOVEMENT CONFIG
// ============================================================================

struct SplineMovementConfig {
    // Movement
    float acceleration = 50.0f;
    float maxSpeed = 100.0f;
    float friction = 0.98f;
    
    // Lateral lock
    float lateralLimit = 5.0f;       // Max distance from spline center
    float lateralSpeed = 20.0f;      // Speed of lateral movement
    
    // Hang offset (for ziplines)
    glm::vec3 hangOffset = glm::vec3(0.0f, -1.5f, 0.0f);
    
    // Velocity injection
    float boostSpeed = 50.0f;
    
    // Exit conditions
    bool canJumpOff = true;
    bool exitAtEnds = true;          // Exit when reaching spline ends
    float minSpeedToExit = 1.0f;     // Minimum speed to stay locked
};

// ============================================================================
// SPLINE MOVEMENT COMPONENT
// ============================================================================

class SplineMovementComponent {
public:
    SplineMovementComponent();
    ~SplineMovementComponent() = default;
    
    /**
     * Update spline movement
     * @param dt Delta time
     * @param inputDir World-space input direction
     * @param inputSpeed Input magnitude (0-1)
     */
    void update(float dt, const glm::vec3& inputDir, float inputSpeed);
    
    // ========== LOCK MODES ==========
    
    /**
     * Lock to a spline with full position lock (grind rail)
     */
    void lockFullPosition(SplineComponent* spline, float startDistance = 0.0f);
    
    /**
     * Lock laterally to a spline (2.5D constraint)
     */
    void lockLateral(SplineComponent* spline, float startDistance = 0.0f);
    
    /**
     * Apply velocity injection from spline (boost ring)
     */
    void injectVelocity(SplineComponent* spline, const glm::vec3& currentVelocity);
    
    /**
     * Unlock from current spline
     */
    void unlock();
    
    /**
     * Check if locked
     */
    bool isLocked() const { return lockMode_ != SplineLockMode::None && lockedSpline_ != nullptr; }
    
    SplineLockMode getLockMode() const { return lockMode_; }
    
    // ========== STATE ==========
    
    SplineComponent* getLockedSpline() const { return lockedSpline_; }
    float getCurrentDistance() const { return currentDistance_; }
    void setCurrentDistance(float distance) { currentDistance_ = distance; }
    
    float getMovementSpeed() const { return movementSpeed_; }
    void setMovementSpeed(float speed) { movementSpeed_ = speed; }
    
    glm::vec3 getLateralOffset() const { return lateralOffset_; }
    
    // ========== POSITION/ROTATION ==========
    
    /**
     * Get calculated world position
     */
    glm::vec3 getPosition() const { return calculatedPosition_; }
    
    /**
     * Get calculated world rotation
     */
    glm::quat getRotation() const { return calculatedRotation_; }
    
    /**
     * Get velocity along spline
     */
    glm::vec3 getVelocity() const;
    
    /**
     * Get exit velocity (when unlocking)
     */
    glm::vec3 getExitVelocity() const { return exitVelocity_; }
    
    // ========== CONFIGURATION ==========
    
    SplineMovementConfig& getConfig() { return config_; }
    const SplineMovementConfig& getConfig() const { return config_; }
    void setConfig(const SplineMovementConfig& config) { config_ = config; }
    
    // ========== CALLBACKS ==========
    
    using ExitCallback = std::function<void(const glm::vec3& position, const glm::vec3& velocity)>;
    void setExitCallback(ExitCallback callback) { exitCallback_ = callback; }
    
    // ========== INPUT ==========
    
    void setJumpPressed(bool pressed) { jumpPressed_ = pressed; }
    
private:
    void updateFullLock(float dt, const glm::vec3& inputDir, float inputSpeed);
    void updateLateralLock(float dt, const glm::vec3& inputDir, float inputSpeed);
    void handleSplineExit();
    
    // Lock state
    SplineLockMode lockMode_ = SplineLockMode::None;
    SplineComponent* lockedSpline_ = nullptr;
    float currentDistance_ = 0.0f;
    float movementSpeed_ = 0.0f;
    glm::vec3 lateralOffset_ = glm::vec3(0.0f);
    
    // Calculated state
    glm::vec3 calculatedPosition_ = glm::vec3(0.0f);
    glm::quat calculatedRotation_ = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 exitVelocity_ = glm::vec3(0.0f);
    
    // Input
    bool jumpPressed_ = false;
    
    // Configuration
    SplineMovementConfig config_;
    
    // Callback
    ExitCallback exitCallback_;
};

} // namespace Sanic
