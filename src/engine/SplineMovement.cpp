/**
 * SplineMovement.cpp
 * 
 * Implementation of spline movement modes.
 */

#include "SplineMovement.h"
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace Sanic {

// ============================================================================
// CONSTRUCTOR
// ============================================================================

SplineMovementComponent::SplineMovementComponent() = default;

// ============================================================================
// UPDATE
// ============================================================================

void SplineMovementComponent::update(float dt, const glm::vec3& inputDir, float inputSpeed) {
    if (lockMode_ == SplineLockMode::None || !lockedSpline_) {
        return;
    }
    
    switch (lockMode_) {
        case SplineLockMode::FullLock:
            updateFullLock(dt, inputDir, inputSpeed);
            break;
        case SplineLockMode::LateralLock:
            updateLateralLock(dt, inputDir, inputSpeed);
            break;
        case SplineLockMode::Velocity:
            // Velocity injection is instant, handled in injectVelocity()
            lockMode_ = SplineLockMode::None;
            break;
        default:
            break;
    }
}

// ============================================================================
// FULL LOCK UPDATE (Grind Rails)
// ============================================================================

void SplineMovementComponent::updateFullLock(float dt, const glm::vec3& inputDir, float inputSpeed) {
    // Get tangent direction
    glm::vec3 tangent = lockedSpline_->getTangentAtDistance(currentDistance_);
    
    // Input controls speed along spline
    float inputDot = glm::dot(inputDir, tangent);
    movementSpeed_ += inputDot * config_.acceleration * inputSpeed * dt;
    
    // Apply friction
    movementSpeed_ *= config_.friction;
    
    // Clamp speed
    movementSpeed_ = glm::clamp(movementSpeed_, -config_.maxSpeed, config_.maxSpeed);
    
    // Move along spline
    currentDistance_ += movementSpeed_ * dt;
    
    // Check bounds
    float splineLength = lockedSpline_->getTotalLength();
    
    if (!lockedSpline_->isLoop()) {
        if (currentDistance_ < 0.0f || currentDistance_ > splineLength) {
            if (config_.exitAtEnds) {
                handleSplineExit();
                return;
            } else {
                // Clamp to ends
                currentDistance_ = glm::clamp(currentDistance_, 0.0f, splineLength);
                movementSpeed_ = 0.0f;
            }
        }
    } else {
        // Wrap around for loops
        currentDistance_ = std::fmod(currentDistance_ + splineLength, splineLength);
    }
    
    // Check speed exit condition
    if (std::abs(movementSpeed_) < config_.minSpeedToExit && inputSpeed < 0.1f) {
        // Not moving, might want to exit
    }
    
    // Check jump exit
    if (jumpPressed_ && config_.canJumpOff) {
        handleSplineExit();
        return;
    }
    
    // Update position from spline
    glm::vec3 splinePos = lockedSpline_->getPositionAtDistance(currentDistance_);
    glm::vec3 splineUp = lockedSpline_->getUpAtDistance(currentDistance_);
    
    // Apply hang offset
    calculatedPosition_ = splinePos + 
        splineUp * config_.hangOffset.y +
        lockedSpline_->evaluateRight(lockedSpline_->distanceToParameter(currentDistance_)) * config_.hangOffset.x;
    
    // Update rotation to face along spline
    if (movementSpeed_ >= 0.0f) {
        calculatedRotation_ = glm::quatLookAt(-tangent, splineUp);
    } else {
        calculatedRotation_ = glm::quatLookAt(tangent, splineUp);
    }
    
    // Store exit velocity
    exitVelocity_ = tangent * movementSpeed_;
}

// ============================================================================
// LATERAL LOCK UPDATE (2.5D Sections)
// ============================================================================

void SplineMovementComponent::updateLateralLock(float dt, const glm::vec3& inputDir, float inputSpeed) {
    // Find closest point on spline
    float closestParam = lockedSpline_->findClosestParameter(calculatedPosition_);
    currentDistance_ = lockedSpline_->parameterToDistance(closestParam);
    
    glm::vec3 splinePos = lockedSpline_->evaluatePosition(closestParam);
    glm::vec3 tangent = lockedSpline_->evaluateTangent(closestParam);
    glm::vec3 splineUp = lockedSpline_->evaluateUp(closestParam);
    glm::vec3 splineRight = glm::cross(tangent, splineUp);
    
    // Decompose input into forward and lateral components
    float forwardInput = glm::dot(inputDir, tangent) * inputSpeed;
    float lateralInput = glm::dot(inputDir, splineRight) * inputSpeed;
    
    // Move along spline (forward/backward)
    movementSpeed_ += forwardInput * config_.acceleration * dt;
    movementSpeed_ *= config_.friction;
    movementSpeed_ = glm::clamp(movementSpeed_, -config_.maxSpeed, config_.maxSpeed);
    
    currentDistance_ += movementSpeed_ * dt;
    
    // Handle spline bounds
    float splineLength = lockedSpline_->getTotalLength();
    if (!lockedSpline_->isLoop()) {
        if (currentDistance_ < 0.0f || currentDistance_ > splineLength) {
            if (config_.exitAtEnds) {
                handleSplineExit();
                return;
            }
            currentDistance_ = glm::clamp(currentDistance_, 0.0f, splineLength);
        }
    } else {
        currentDistance_ = std::fmod(currentDistance_ + splineLength, splineLength);
    }
    
    // Update lateral offset
    float lateralDelta = lateralInput * config_.lateralSpeed * dt;
    lateralOffset_.x += lateralDelta;
    
    // Clamp lateral distance
    lateralOffset_.x = glm::clamp(lateralOffset_.x, -config_.lateralLimit, config_.lateralLimit);
    
    // Calculate final position
    splinePos = lockedSpline_->getPositionAtDistance(currentDistance_);
    tangent = lockedSpline_->getTangentAtDistance(currentDistance_);
    splineUp = lockedSpline_->getUpAtDistance(currentDistance_);
    splineRight = glm::cross(tangent, splineUp);
    
    calculatedPosition_ = splinePos + splineRight * lateralOffset_.x + splineUp * lateralOffset_.y;
    
    // Rotation faces forward along spline
    if (movementSpeed_ >= 0.0f) {
        calculatedRotation_ = glm::quatLookAt(-tangent, splineUp);
    } else {
        calculatedRotation_ = glm::quatLookAt(tangent, splineUp);
    }
    
    // Store exit velocity (includes lateral component)
    exitVelocity_ = tangent * movementSpeed_;
    
    // Check jump exit
    if (jumpPressed_ && config_.canJumpOff) {
        handleSplineExit();
    }
}

// ============================================================================
// LOCK MODES
// ============================================================================

void SplineMovementComponent::lockFullPosition(SplineComponent* spline, float startDistance) {
    if (!spline) return;
    
    lockedSpline_ = spline;
    lockMode_ = SplineLockMode::FullLock;
    currentDistance_ = startDistance;
    lateralOffset_ = glm::vec3(0.0f);
    
    // Initialize position
    calculatedPosition_ = spline->getPositionAtDistance(startDistance);
    calculatedRotation_ = spline->getRotationAtDistance(startDistance);
}

void SplineMovementComponent::lockLateral(SplineComponent* spline, float startDistance) {
    if (!spline) return;
    
    lockedSpline_ = spline;
    lockMode_ = SplineLockMode::LateralLock;
    currentDistance_ = startDistance;
    lateralOffset_ = glm::vec3(0.0f);
    
    // Initialize position
    calculatedPosition_ = spline->getPositionAtDistance(startDistance);
    calculatedRotation_ = spline->getRotationAtDistance(startDistance);
}

void SplineMovementComponent::injectVelocity(SplineComponent* spline, const glm::vec3& currentVelocity) {
    if (!spline) return;
    
    // Find closest point
    float param = spline->findClosestParameter(calculatedPosition_);
    glm::vec3 tangent = spline->evaluateTangent(param);
    
    // Project current velocity onto spline tangent
    float speedAlongSpline = glm::dot(currentVelocity, tangent);
    
    // Add boost speed
    float newSpeed = std::max(speedAlongSpline, 0.0f) + config_.boostSpeed;
    
    // Set exit velocity
    exitVelocity_ = tangent * newSpeed;
    
    // This mode is instant - don't lock
    lockMode_ = SplineLockMode::Velocity;
    lockedSpline_ = spline;
}

void SplineMovementComponent::unlock() {
    // Store velocity before unlock
    if (lockedSpline_ && lockMode_ != SplineLockMode::None) {
        glm::vec3 tangent = lockedSpline_->getTangentAtDistance(currentDistance_);
        exitVelocity_ = tangent * movementSpeed_;
    }
    
    lockMode_ = SplineLockMode::None;
    lockedSpline_ = nullptr;
    currentDistance_ = 0.0f;
    movementSpeed_ = 0.0f;
}

// ============================================================================
// HELPERS
// ============================================================================

glm::vec3 SplineMovementComponent::getVelocity() const {
    if (!lockedSpline_ || lockMode_ == SplineLockMode::None) {
        return glm::vec3(0.0f);
    }
    
    glm::vec3 tangent = lockedSpline_->getTangentAtDistance(currentDistance_);
    return tangent * movementSpeed_;
}

void SplineMovementComponent::handleSplineExit() {
    // Calculate exit velocity
    if (lockedSpline_) {
        glm::vec3 tangent = lockedSpline_->getTangentAtDistance(currentDistance_);
        glm::vec3 up = lockedSpline_->getUpAtDistance(currentDistance_);
        
        exitVelocity_ = tangent * movementSpeed_;
        
        // If jumping off, add upward velocity
        if (jumpPressed_) {
            exitVelocity_ += up * 10.0f;  // Jump force
        }
    }
    
    // Fire callback
    if (exitCallback_) {
        exitCallback_(calculatedPosition_, exitVelocity_);
    }
    
    // Unlock
    lockMode_ = SplineLockMode::None;
    lockedSpline_ = nullptr;
}

} // namespace Sanic
