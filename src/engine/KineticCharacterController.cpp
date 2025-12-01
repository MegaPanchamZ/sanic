/**
 * KineticCharacterController.cpp
 * 
 * Implementation of the kinetic character controller for high-speed traversal.
 */

#include "KineticCharacterController.h"
#include "GravitySystem.h"
#include "SplineComponent.h"
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <iostream>

namespace Sanic {

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

KineticCharacterController::KineticCharacterController() = default;

KineticCharacterController::~KineticCharacterController() {
    shutdown();
}

// ============================================================================
// INITIALIZATION
// ============================================================================

bool KineticCharacterController::initialize(
    JPH::PhysicsSystem* physics,
    GravitySystem* gravitySystem,
    const glm::vec3& position,
    const glm::quat& rotation)
{
    if (initialized_) {
        shutdown();
    }
    
    physicsSystem_ = physics;
    gravitySystem_ = gravitySystem;
    
    if (!physicsSystem_) {
        std::cerr << "KineticCharacterController: No physics system provided!" << std::endl;
        return false;
    }
    
    // Create capsule shape
    capsuleShape_ = new JPH::CapsuleShape(capsuleHalfHeight_, capsuleRadius_);
    
    // Create Jolt CharacterVirtual settings
    JPH::CharacterVirtualSettings settings;
    settings.mShape = capsuleShape_;
    settings.mMaxSlopeAngle = glm::radians(maxWalkableAngle_);
    settings.mMaxStrength = 100.0f;
    settings.mMass = 80.0f;  // kg
    settings.mPenetrationRecoverySpeed = 1.0f;
    settings.mPredictiveContactDistance = 0.1f;
    settings.mEnhancedInternalEdgeRemoval = true;
    
    // Create the character
    character_ = new JPH::CharacterVirtual(
        &settings,
        toJolt(position),
        toJolt(rotation),
        0,  // User data
        physicsSystem_
    );
    
    // Initialize state
    state_.position = position;
    state_.rotation = rotation;
    state_.currentUp = glm::vec3(0.0f, 1.0f, 0.0f);
    state_.targetUp = state_.currentUp;
    state_.movementMode = MovementMode::Falling;
    
    initialized_ = true;
    return true;
}

void KineticCharacterController::shutdown() {
    if (!initialized_) return;
    
    character_ = nullptr;
    capsuleShape_ = nullptr;
    physicsSystem_ = nullptr;
    gravitySystem_ = nullptr;
    
    initialized_ = false;
}

// ============================================================================
// MAIN UPDATE
// ============================================================================

void KineticCharacterController::update(float deltaTime, const CharacterInput& input) {
    if (!initialized_ || deltaTime <= 0.0f) return;
    
    // Clamp delta time to prevent physics explosion
    deltaTime = std::min(deltaTime, 1.0f / 30.0f);
    
    // Update gravity from gravity system if available
    if (gravitySystem_) {
        glm::vec3 gravity = gravitySystem_->getGravityAtPosition(state_.position);
        float strength = glm::length(gravity);
        if (strength > 0.0001f) {
            state_.gravityDirection = glm::normalize(gravity);
            state_.gravityStrength = strength;
        }
    }
    
    // Update invincibility
    if (state_.invincibilityTimer > 0.0f) {
        state_.invincibilityTimer -= deltaTime;
        if (state_.invincibilityTimer <= 0.0f) {
            state_.isInvincible = false;
        }
    }
    
    // Update boost
    if (boostTimer_ > 0.0f) {
        boostTimer_ -= deltaTime;
        if (boostTimer_ <= 0.0f) {
            state_.isBoosting = false;
        }
    }
    
    // Handle jump input buffering
    if (input.jumpPressed) {
        state_.timeSinceJumpPressed = 0.0f;
    } else {
        state_.timeSinceJumpPressed += deltaTime;
    }
    
    // Update based on movement mode
    switch (state_.movementMode) {
        case MovementMode::Walking:
            updateWalking(deltaTime, input);
            break;
        case MovementMode::Falling:
            updateFalling(deltaTime, input);
            break;
        case MovementMode::SplineLock:
            updateSplineLock(deltaTime, input);
            break;
        case MovementMode::Flying:
            // Direct velocity control
            state_.velocity = input.moveDirection * maxSpeed_ * input.moveScale;
            break;
        default:
            break;
    }
    
    // Apply velocity
    state_.position += state_.velocity * deltaTime;
    state_.speed = glm::length(state_.velocity);
    
    // Resolve collisions with CCD
    resolveCollisions(deltaTime);
    
    // Sync with Jolt character
    if (character_) {
        character_->SetPosition(toJolt(state_.position));
        character_->SetRotation(toJolt(state_.rotation));
        character_->SetLinearVelocity(toJolt(state_.velocity));
    }
}

// ============================================================================
// WALKING UPDATE
// ============================================================================

void KineticCharacterController::updateWalking(float deltaTime, const CharacterInput& input) {
    // Update surface adhesion
    updateSurfaceAdhesion(deltaTime);
    
    // Check if still grounded
    GroundHitResult floor = findFloor(state_.position, state_.currentUp);
    
    if (!floor.valid || !floor.isWalkable) {
        // Lost ground, start falling
        state_.movementMode = MovementMode::Falling;
        state_.timeSinceGrounded = 0.0f;
        return;
    }
    
    state_.groundHit = floor;
    state_.timeSinceGrounded = 0.0f;
    
    // Reset jump on landing
    if (!state_.canJump) {
        state_.canJump = true;
        state_.jumpCount = 0;
    }
    
    // Handle jump
    handleJump(input, deltaTime);
    if (state_.movementMode == MovementMode::Falling) {
        return;  // Jump triggered
    }
    
    // Move along floor
    moveAlongFloor(deltaTime, input);
}

// ============================================================================
// FALLING UPDATE
// ============================================================================

void KineticCharacterController::updateFalling(float deltaTime, const CharacterInput& input) {
    state_.timeSinceGrounded += deltaTime;
    
    // Check for ground
    GroundHitResult floor = findFloor(state_.position, state_.currentUp);
    
    if (floor.valid && floor.isWalkable) {
        // Check if falling downward
        float downVelocity = glm::dot(state_.velocity, -state_.currentUp);
        if (downVelocity > 0.0f) {
            // Landed
            state_.movementMode = MovementMode::Walking;
            state_.groundHit = floor;
            state_.isJumping = false;
            
            // Project velocity onto surface
            projectVelocityOntoSurface(floor.normal);
            
            // Snap to floor
            state_.position = floor.location + floor.normal * (capsuleRadius_ + capsuleHalfHeight_);
            return;
        }
    }
    
    // Apply gravity
    applyGravity(deltaTime);
    
    // Air control
    if (glm::length(input.moveDirection) > 0.0f) {
        glm::vec3 airMove = input.moveDirection * acceleration_ * airControl_ * input.moveScale;
        
        // Get horizontal velocity (perpendicular to gravity)
        glm::vec3 horizontal = state_.velocity - state_.gravityDirection * glm::dot(state_.velocity, state_.gravityDirection);
        float horizontalSpeed = glm::length(horizontal);
        
        // Only add air control if not exceeding max speed
        if (horizontalSpeed < maxSpeed_) {
            state_.velocity += airMove * deltaTime;
        }
    }
    
    // Handle jump (for double/multi jump)
    handleJump(input, deltaTime);
    
    // Slowly align to gravity
    state_.targetUp = -state_.gravityDirection;
    state_.currentUp = glm::normalize(glm::mix(state_.currentUp, state_.targetUp, 2.0f * deltaTime));
}

// ============================================================================
// SPLINE LOCK UPDATE
// ============================================================================

void KineticCharacterController::updateSplineLock(float deltaTime, const CharacterInput& input) {
    if (!state_.lockedSpline) {
        unlockFromSpline();
        return;
    }
    
    // Get tangent direction
    glm::vec3 tangent = state_.lockedSpline->getTangentAtDistance(state_.splineDistance);
    
    // Input controls speed along spline
    float inputDot = glm::dot(input.moveDirection, tangent);
    state_.splineSpeed += inputDot * acceleration_ * 2.0f * deltaTime;
    
    // Apply friction
    float friction = 0.98f;
    state_.splineSpeed *= friction;
    
    // Clamp speed
    float maxRailSpeed = maxSpeed_ * 1.5f;  // Rails can be faster
    state_.splineSpeed = glm::clamp(state_.splineSpeed, -maxRailSpeed, maxRailSpeed);
    
    // Move along spline
    state_.splineDistance += state_.splineSpeed * deltaTime;
    
    // Check bounds
    float splineLength = state_.lockedSpline->getTotalLength();
    if (!state_.lockedSpline->isLoop()) {
        if (state_.splineDistance < 0.0f || state_.splineDistance > splineLength) {
            // Exit spline
            state_.velocity = tangent * state_.splineSpeed;
            unlockFromSpline();
            return;
        }
    } else {
        // Wrap around for loops
        state_.splineDistance = fmod(state_.splineDistance + splineLength, splineLength);
    }
    
    // Update position from spline
    glm::vec3 splinePos = state_.lockedSpline->getPositionAtDistance(state_.splineDistance);
    glm::vec3 splineUp = state_.lockedSpline->getUpAtDistance(state_.splineDistance);
    
    // Apply offset (for ziplines, character hangs below)
    state_.position = splinePos + splineUp * 0.0f;  // Adjust offset as needed
    
    // Update rotation to face along spline
    if (state_.splineSpeed >= 0.0f) {
        state_.rotation = glm::quatLookAt(-tangent, splineUp);
    } else {
        state_.rotation = glm::quatLookAt(tangent, splineUp);
    }
    
    state_.currentUp = splineUp;
    state_.velocity = tangent * state_.splineSpeed;
    
    // Check for exit input
    if (input.jumpPressed) {
        // Jump off spline
        glm::vec3 exitVelocity = state_.velocity + splineUp * jumpForce_;
        unlockFromSpline();
        state_.velocity = exitVelocity;
        state_.movementMode = MovementMode::Falling;
        state_.isJumping = true;
    }
}

// ============================================================================
// GROUND DETECTION
// ============================================================================

GroundHitResult KineticCharacterController::findFloor(const glm::vec3& position, const glm::vec3& localUp) {
    GroundHitResult result;
    result.valid = false;
    
    if (!physicsSystem_) return result;
    
    // Use Jolt's CharacterVirtual ground detection if available
    if (character_) {
        // Perform a shape cast downward
        JPH::Vec3 start = toJolt(position);
        JPH::Vec3 direction = toJolt(-localUp * KineticConstants::FLOOR_CHECK_DISTANCE);
        
        JPH::RShapeCast shapeCast(
            capsuleShape_,
            JPH::Vec3::sReplicate(1.0f),
            JPH::RMat44::sTranslation(start),
            direction
        );
        
        JPH::ShapeCastSettings settings;
        settings.mActiveEdgeMode = JPH::EActiveEdgeMode::CollideWithAll;
        settings.mBackFaceModeTriangles = JPH::EBackFaceMode::IgnoreBackFaces;
        settings.mBackFaceModeConvex = JPH::EBackFaceMode::IgnoreBackFaces;
        
        JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
        
        // Cast through the physics system
        JPH::BroadPhaseQuery& broadPhase = physicsSystem_->GetBroadPhaseQuery();
        
        // Use ray cast as fallback
        JPH::RRayCast ray;
        ray.mOrigin = start;
        ray.mDirection = direction;
        
        JPH::RayCastResult rayResult;
        if (physicsSystem_->GetNarrowPhaseQuery().CastRay(ray, rayResult)) {
            result.valid = true;
            result.distance = rayResult.mFraction * KineticConstants::FLOOR_CHECK_DISTANCE;
            result.location = toGLM(ray.mOrigin + ray.mDirection * rayResult.mFraction);
            
            // Get surface normal from body
            result.hitBodyId = rayResult.mBodyID;
            
            // Use world up as default normal (we'd get this from contact in real implementation)
            result.normal = localUp;
            result.impactNormal = result.normal;
            
            // Calculate walkable angle
            result.walkableAngle = glm::degrees(glm::acos(glm::dot(result.normal, localUp)));
            result.isWalkable = result.walkableAngle <= maxWalkableAngle_;
            
            return result;
        }
    }
    
    return result;
}

// ============================================================================
// SURFACE ADHESION
// ============================================================================

void KineticCharacterController::updateSurfaceAdhesion(float deltaTime) {
    // Find ground relative to LOCAL down (not world down)
    GroundHitResult floor = findFloor(state_.position, state_.currentUp);
    
    if (!floor.valid) {
        // No ground - switch to falling
        state_.movementMode = MovementMode::Falling;
        return;
    }
    
    // Speed-based adhesion strength
    // Faster = stickier (to stay on loops)
    float speed = glm::length(state_.velocity);
    float adhesionStrength = glm::clamp(
        speed / KineticConstants::ADHESION_SPEED_THRESHOLD,
        KineticConstants::MIN_ADHESION,
        KineticConstants::MAX_ADHESION
    ) * adhesionMultiplier_;
    
    // Align character up to surface normal (smoothly)
    state_.targetUp = floor.normal;
    float alignmentRate = KineticConstants::ALIGNMENT_RATE * adhesionStrength;
    state_.currentUp = glm::normalize(
        glm::mix(state_.currentUp, state_.targetUp, alignmentRate * deltaTime)
    );
    
    // Apply snap force to keep on surface
    float snapForce = KineticConstants::SNAP_FORCE * adhesionStrength;
    state_.velocity += -floor.normal * snapForce * deltaTime;
    
    // Clamp to surface if very close
    if (floor.distance < KineticConstants::SNAP_DISTANCE) {
        state_.position = floor.location + floor.normal * (capsuleRadius_ + capsuleHalfHeight_);
    }
    
    // Update rotation to match surface orientation
    glm::vec3 forward = state_.rotation * glm::vec3(0.0f, 0.0f, -1.0f);
    forward = forward - state_.currentUp * glm::dot(forward, state_.currentUp);
    if (glm::length(forward) > 0.001f) {
        forward = glm::normalize(forward);
        glm::vec3 right = glm::cross(state_.currentUp, forward);
        forward = glm::cross(right, state_.currentUp);
        state_.rotation = glm::quatLookAt(-forward, state_.currentUp);
    }
}

// ============================================================================
// VELOCITY PROJECTION
// ============================================================================

void KineticCharacterController::projectVelocityOntoSurface(const glm::vec3& surfaceNormal) {
    // Don't project if moving away from surface
    float normalVelocity = glm::dot(state_.velocity, surfaceNormal);
    if (normalVelocity > 0.0f) return;
    
    // Project velocity onto surface plane
    // This prevents "bouncing" off slopes
    glm::vec3 tangent = state_.velocity - surfaceNormal * normalVelocity;
    float tangentSpeed = glm::length(tangent);
    
    if (tangentSpeed > 0.0001f) {
        // Maintain speed, change direction to along surface
        state_.velocity = glm::normalize(tangent) * glm::length(state_.velocity);
    }
}

// ============================================================================
// MOVE ALONG FLOOR
// ============================================================================

void KineticCharacterController::moveAlongFloor(float deltaTime, const CharacterInput& input) {
    GroundHitResult floor = findFloor(state_.position, state_.currentUp);
    if (!floor.valid) return;
    
    // Get desired movement direction
    glm::vec3 moveDir = input.moveDirection;
    if (glm::length(moveDir) < 0.001f) {
        // No input - decelerate
        float speed = glm::length(state_.velocity);
        if (speed > 0.0f) {
            speed -= deceleration_ * deltaTime;
            speed = std::max(0.0f, speed);
            if (speed > 0.0f) {
                state_.velocity = glm::normalize(state_.velocity) * speed;
            } else {
                state_.velocity = glm::vec3(0.0f);
            }
        }
        return;
    }
    
    // Project onto floor plane
    glm::vec3 floorTangent = moveDir - floor.normal * glm::dot(moveDir, floor.normal);
    if (glm::length(floorTangent) > 0.001f) {
        floorTangent = glm::normalize(floorTangent);
    } else {
        return;
    }
    
    // Apply acceleration
    float targetSpeed = maxSpeed_ * input.moveScale;
    
    // Boost multiplier
    if (state_.isBoosting) {
        targetSpeed *= 1.5f;
    }
    
    glm::vec3 targetVelocity = floorTangent * targetSpeed;
    
    // Smooth acceleration
    float accelRate = glm::length(targetVelocity) > glm::length(state_.velocity) ? acceleration_ : deceleration_;
    state_.velocity = glm::mix(state_.velocity, targetVelocity, accelRate * deltaTime);
    
    // Update facing direction
    if (glm::length(state_.velocity) > 0.1f) {
        glm::vec3 forward = glm::normalize(state_.velocity);
        state_.rotation = glm::quatLookAt(-forward, state_.currentUp);
    }
}

// ============================================================================
// GRAVITY
// ============================================================================

void KineticCharacterController::applyGravity(float deltaTime) {
    state_.velocity += state_.gravityDirection * state_.gravityStrength * deltaTime;
}

void KineticCharacterController::setGravity(const glm::vec3& direction, float strength) {
    if (glm::length(direction) > 0.0001f) {
        state_.gravityDirection = glm::normalize(direction);
    }
    state_.gravityStrength = strength;
}

// ============================================================================
// JUMPING
// ============================================================================

void KineticCharacterController::handleJump(const CharacterInput& input, float deltaTime) {
    // Coyote time - can still jump shortly after leaving ground
    bool canCoyoteJump = state_.timeSinceGrounded < KineticConstants::COYOTE_TIME && 
                         state_.jumpCount == 0;
    
    // Jump buffering - pressed jump slightly before landing
    bool hasBufferedJump = state_.timeSinceJumpPressed < KineticConstants::JUMP_BUFFER_TIME;
    
    // Check if we can jump
    bool canJump = state_.canJump && 
                   (state_.movementMode == MovementMode::Walking || 
                    canCoyoteJump || 
                    state_.jumpCount < state_.maxJumps);
    
    if (canJump && (input.jumpPressed || hasBufferedJump)) {
        performJump(jumpForce_);
    }
}

void KineticCharacterController::performJump(float jumpPower) {
    // Calculate jump velocity
    glm::vec3 jumpVelocity = state_.currentUp * jumpPower;
    
    // Add to current velocity
    state_.velocity += jumpVelocity;
    
    // Update state
    state_.movementMode = MovementMode::Falling;
    state_.isJumping = true;
    state_.jumpCount++;
    state_.timeSinceJumpPressed = 1.0f;  // Reset buffer
    
    // Disable jumping until we land (for double jump, check jumpCount)
    if (state_.jumpCount >= state_.maxJumps) {
        state_.canJump = false;
    }
}

// ============================================================================
// STEP-UP
// ============================================================================

StepUpResult KineticCharacterController::tryStepUp(const glm::vec3& hitNormal, const glm::vec3& hitLocation) {
    StepUpResult result;
    result.canStepUp = false;
    
    if (!physicsSystem_) return result;
    
    // 1. Cast upward to check headroom
    JPH::RRayCast upRay;
    upRay.mOrigin = toJolt(state_.position);
    upRay.mDirection = toJolt(state_.currentUp * maxStepHeight_);
    
    JPH::RayCastResult upResult;
    if (physicsSystem_->GetNarrowPhaseQuery().CastRay(upRay, upResult)) {
        // Not enough headroom
        return result;
    }
    
    // 2. Cast forward at raised height
    glm::vec3 raisedPos = state_.position + state_.currentUp * maxStepHeight_;
    glm::vec3 forwardDir = glm::length(state_.velocity) > 0.0f ? 
                           glm::normalize(state_.velocity) : 
                           getForward();
    
    JPH::RRayCast forwardRay;
    forwardRay.mOrigin = toJolt(raisedPos);
    forwardRay.mDirection = toJolt(forwardDir * KineticConstants::STEP_CHECK_DISTANCE);
    
    JPH::RayCastResult forwardResult;
    if (physicsSystem_->GetNarrowPhaseQuery().CastRay(forwardRay, forwardResult)) {
        // Still blocked at raised height
        return result;
    }
    
    // 3. Cast down to find new floor
    glm::vec3 checkPos = raisedPos + forwardDir * KineticConstants::STEP_CHECK_DISTANCE;
    GroundHitResult newFloor = findFloor(checkPos, state_.currentUp);
    
    if (newFloor.valid && newFloor.isWalkable) {
        result.canStepUp = true;
        result.stepHeight = newFloor.location.y - state_.position.y;
        result.newPosition = newFloor.location + newFloor.normal * (capsuleRadius_ + capsuleHalfHeight_);
    }
    
    return result;
}

// ============================================================================
// CCD
// ============================================================================

CCDResult KineticCharacterController::sweepCapsule(const glm::vec3& start, const glm::vec3& end) {
    CCDResult result;
    result.hit = false;
    result.time = 1.0f;
    
    if (!physicsSystem_) return result;
    
    // Sub-step if velocity is very high
    float distance = glm::length(end - start);
    if (distance < 0.0001f) return result;
    
    int substeps = std::max(1, static_cast<int>(distance / KineticConstants::MAX_STEP_SIZE));
    
    glm::vec3 currentPos = start;
    glm::vec3 step = (end - start) / static_cast<float>(substeps);
    
    for (int i = 0; i < substeps; i++) {
        glm::vec3 nextPos = currentPos + step;
        
        // Ray cast for this substep
        JPH::RRayCast ray;
        ray.mOrigin = toJolt(currentPos);
        ray.mDirection = toJolt(step);
        
        JPH::RayCastResult rayResult;
        if (physicsSystem_->GetNarrowPhaseQuery().CastRay(ray, rayResult)) {
            result.hit = true;
            result.position = toGLM(ray.mOrigin + ray.mDirection * rayResult.mFraction);
            result.hitBodyId = rayResult.mBodyID;
            result.time = (i + rayResult.mFraction) / static_cast<float>(substeps);
            
            // Get normal (approximation - would need contact info in real impl)
            glm::vec3 toHit = result.position - currentPos;
            if (glm::length(toHit) > 0.0001f) {
                result.normal = -glm::normalize(toHit);
            } else {
                result.normal = -glm::normalize(step);
            }
            
            return result;
        }
        
        currentPos = nextPos;
    }
    
    return result;
}

// ============================================================================
// COLLISION RESOLUTION
// ============================================================================

void KineticCharacterController::resolveCollisions(float deltaTime) {
    if (!physicsSystem_) return;
    
    // Use CCD for high-speed movement
    float speed = glm::length(state_.velocity);
    if (speed > 10.0f) {  // Only CCD for fast movement
        glm::vec3 expectedEnd = state_.position + state_.velocity * deltaTime;
        CCDResult ccd = sweepCapsule(state_.position, expectedEnd);
        
        if (ccd.hit) {
            // Hit something
            state_.position = ccd.position + ccd.normal * 0.01f;  // Small separation
            
            // Project velocity along surface
            projectVelocityOntoSurface(ccd.normal);
            
            // Try step up
            StepUpResult stepUp = tryStepUp(ccd.normal, ccd.position);
            if (stepUp.canStepUp) {
                state_.position = stepUp.newPosition;
            }
            
            // Fire collision callback
            if (collisionCallback_) {
                collisionCallback_(ccd.position, ccd.normal, ccd.hitBodyId);
            }
        }
    }
}

void KineticCharacterController::handlePenetration(const glm::vec3& penetrationNormal, float penetrationDepth) {
    // Push out of collision
    state_.position += penetrationNormal * penetrationDepth;
    
    // Remove velocity component into the collision
    float velocityIntoCollision = glm::dot(state_.velocity, -penetrationNormal);
    if (velocityIntoCollision > 0.0f) {
        state_.velocity += penetrationNormal * velocityIntoCollision;
    }
}

// ============================================================================
// SPLINE LOCK
// ============================================================================

void KineticCharacterController::lockToSpline(SplineComponent* spline, float startDistance) {
    if (!spline) return;
    
    state_.lockedSpline = spline;
    state_.splineDistance = startDistance;
    state_.splineSpeed = glm::length(state_.velocity);  // Maintain momentum
    state_.movementMode = MovementMode::SplineLock;
}

void KineticCharacterController::unlockFromSpline() {
    state_.lockedSpline = nullptr;
    state_.splineDistance = 0.0f;
    state_.splineSpeed = 0.0f;
    state_.movementMode = MovementMode::Falling;
}

// ============================================================================
// IMPULSE / FORCE
// ============================================================================

void KineticCharacterController::applyImpulse(const glm::vec3& impulse) {
    state_.velocity += impulse;
}

void KineticCharacterController::applyForce(const glm::vec3& force, float deltaTime) {
    state_.velocity += force * deltaTime;
}

// ============================================================================
// CAPSULE SIZE
// ============================================================================

void KineticCharacterController::setCapsuleSize(float radius, float halfHeight) {
    capsuleRadius_ = radius;
    capsuleHalfHeight_ = halfHeight;
    
    // Recreate capsule shape
    if (initialized_ && physicsSystem_) {
        capsuleShape_ = new JPH::CapsuleShape(halfHeight, radius);
        // Would need to update character virtual shape here
    }
}

// ============================================================================
// ABILITIES
// ============================================================================

void KineticCharacterController::boost(float power, float duration) {
    state_.isBoosting = true;
    boostTimer_ = duration;
    boostPower_ = power;
    
    // Apply velocity burst in facing direction
    state_.velocity += getForward() * power;
    
    // Grant invincibility during boost
    setInvincible(duration);
}

void KineticCharacterController::superJump(float height) {
    // Calculate required velocity for height: v = sqrt(2 * g * h)
    float jumpVelocity = glm::sqrt(2.0f * state_.gravityStrength * height);
    performJump(jumpVelocity);
}

void KineticCharacterController::setInvincible(float duration) {
    state_.isInvincible = true;
    state_.invincibilityTimer = duration;
}

} // namespace Sanic
