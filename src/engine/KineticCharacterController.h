/**
 * KineticCharacterController.h
 * 
 * Custom character controller built on Jolt Physics for high-speed traversal.
 * Designed for 700+ mph gameplay without physics breaking.
 * 
 * Key features:
 * - Surface adhesion at high speeds (sticking to loops, walls)
 * - Variable gravity per-area
 * - Velocity projection for smooth slope transitions
 * - CCD to prevent tunneling
 * - Step-up and obstacle handling
 * 
 * Based on UE5's CharacterMovementComponent concepts.
 */

#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <functional>

// Forward declarations
class AsyncPhysics;
class GravitySystem;
class SplineComponent;

namespace Sanic {

// ============================================================================
// CONSTANTS
// ============================================================================

namespace KineticConstants {
    // Ground detection
    constexpr float FLOOR_CHECK_DISTANCE = 0.5f;
    constexpr float MAX_WALKABLE_ANGLE = 50.0f;  // degrees
    constexpr float SNAP_DISTANCE = 0.1f;
    
    // Surface adhesion
    constexpr float ADHESION_SPEED_THRESHOLD = 100.0f;  // m/s
    constexpr float MIN_ADHESION = 0.1f;
    constexpr float MAX_ADHESION = 1.0f;
    constexpr float ALIGNMENT_RATE = 5.0f;
    constexpr float SNAP_FORCE = 50.0f;
    
    // Movement
    constexpr float DEFAULT_MAX_SPEED = 300.0f;  // m/s (~670 mph)
    constexpr float ACCELERATION = 50.0f;
    constexpr float DECELERATION = 30.0f;
    constexpr float AIR_CONTROL = 0.3f;
    
    // Step-up
    constexpr float MAX_STEP_HEIGHT = 0.5f;
    constexpr float STEP_CHECK_DISTANCE = 0.3f;
    
    // CCD
    constexpr float MAX_STEP_SIZE = 1.0f;  // Max movement per substep for CCD
    
    // Capsule defaults
    constexpr float DEFAULT_CAPSULE_RADIUS = 0.4f;
    constexpr float DEFAULT_CAPSULE_HALF_HEIGHT = 0.9f;
    
    // Jump
    constexpr float DEFAULT_JUMP_FORCE = 10.0f;
    constexpr float COYOTE_TIME = 0.15f;  // seconds
    constexpr float JUMP_BUFFER_TIME = 0.1f;
}

// ============================================================================
// GROUND HIT RESULT
// ============================================================================

struct GroundHitResult {
    bool valid = false;
    glm::vec3 location = glm::vec3(0.0f);
    glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 impactNormal = glm::vec3(0.0f, 1.0f, 0.0f);  // Actual geometry normal
    float distance = 0.0f;
    float walkableAngle = 0.0f;  // Angle from character up
    uint32_t physMaterialId = 0;
    bool isWalkable = false;  // Based on slope angle
    JPH::BodyID hitBodyId;
};

// ============================================================================
// STEP-UP RESULT
// ============================================================================

struct StepUpResult {
    bool canStepUp = false;
    float stepHeight = 0.0f;
    glm::vec3 newPosition = glm::vec3(0.0f);
};

// ============================================================================
// CCD RESULT
// ============================================================================

struct CCDResult {
    bool hit = false;
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 normal = glm::vec3(0.0f);
    float time = 1.0f;  // 0-1, how far along movement the hit occurred
    JPH::BodyID hitBodyId;
};

// ============================================================================
// MOVEMENT MODE
// ============================================================================

enum class MovementMode {
    Walking,     // On ground
    Falling,     // In air
    Flying,      // No gravity (debug/special)
    Swimming,    // In water volume
    SplineLock,  // Locked to spline (rail grinding)
    Custom       // Game-specific modes
};

// ============================================================================
// CHARACTER INPUT
// ============================================================================

struct CharacterInput {
    glm::vec3 moveDirection = glm::vec3(0.0f);  // World-space desired movement
    float moveScale = 0.0f;                      // 0-1 analog stick magnitude
    bool jumpPressed = false;
    bool jumpHeld = false;
    bool boostPressed = false;
    bool crouchPressed = false;
    bool crouchHeld = false;
    
    // For spline interaction
    bool interactPressed = false;
};

// ============================================================================
// CHARACTER STATE
// ============================================================================

struct CharacterState {
    // Transform
    glm::vec3 position = glm::vec3(0.0f);
    glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    
    // Movement
    glm::vec3 velocity = glm::vec3(0.0f);
    float speed = 0.0f;
    MovementMode movementMode = MovementMode::Falling;
    
    // Ground
    GroundHitResult groundHit;
    float timeSinceGrounded = 0.0f;  // For coyote time
    float timeSinceJumpPressed = 1.0f;  // For jump buffering
    
    // Surface alignment
    glm::vec3 currentUp = glm::vec3(0.0f, 1.0f, 0.0f);  // Character's "up" direction
    glm::vec3 targetUp = glm::vec3(0.0f, 1.0f, 0.0f);
    
    // Gravity
    glm::vec3 gravityDirection = glm::vec3(0.0f, -1.0f, 0.0f);
    float gravityStrength = 9.81f;
    
    // Jumping
    bool isJumping = false;
    bool canJump = true;
    int jumpCount = 0;
    int maxJumps = 2;  // Double jump by default
    
    // Abilities
    bool isBoosting = false;
    bool isInvincible = false;
    float invincibilityTimer = 0.0f;
    
    // Spline lock
    SplineComponent* lockedSpline = nullptr;
    float splineDistance = 0.0f;
    float splineSpeed = 0.0f;
};

// ============================================================================
// KINETIC CHARACTER CONTROLLER
// ============================================================================

class KineticCharacterController {
public:
    KineticCharacterController();
    ~KineticCharacterController();
    
    /**
     * Initialize the controller
     * @param physics Physics system
     * @param gravitySystem Optional gravity system for variable gravity
     * @param position Initial position
     * @param rotation Initial rotation
     * @return True if successful
     */
    bool initialize(JPH::PhysicsSystem* physics,
                    GravitySystem* gravitySystem = nullptr,
                    const glm::vec3& position = glm::vec3(0.0f),
                    const glm::quat& rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    
    /**
     * Shutdown and cleanup
     */
    void shutdown();
    
    /**
     * Update the controller
     * @param deltaTime Frame time
     * @param input Player input
     */
    void update(float deltaTime, const CharacterInput& input);
    
    // ========== GROUND DETECTION ==========
    
    /**
     * Find floor relative to character's local up direction
     */
    GroundHitResult findFloor(const glm::vec3& position, const glm::vec3& localUp);
    
    /**
     * Check if standing on walkable surface
     */
    bool isGrounded() const { return state_.movementMode == MovementMode::Walking; }
    
    // ========== SURFACE ADHESION ==========
    
    /**
     * Update surface adhesion (for sticking to loops/walls)
     */
    void updateSurfaceAdhesion(float deltaTime);
    
    /**
     * Set surface adhesion strength multiplier
     */
    void setSurfaceAdhesion(float strength) { adhesionMultiplier_ = strength; }
    
    // ========== VELOCITY ==========
    
    /**
     * Project velocity onto surface for smooth transitions
     */
    void projectVelocityOntoSurface(const glm::vec3& surfaceNormal);
    
    /**
     * Apply impulse to character
     */
    void applyImpulse(const glm::vec3& impulse);
    
    /**
     * Apply force over time
     */
    void applyForce(const glm::vec3& force, float deltaTime);
    
    /**
     * Set velocity directly
     */
    void setVelocity(const glm::vec3& velocity) { state_.velocity = velocity; }
    
    /**
     * Get current velocity
     */
    glm::vec3 getVelocity() const { return state_.velocity; }
    
    /**
     * Get current speed
     */
    float getSpeed() const { return state_.speed; }
    
    // ========== GRAVITY ==========
    
    /**
     * Set gravity direction and strength
     */
    void setGravity(const glm::vec3& direction, float strength);
    
    /**
     * Get current gravity vector
     */
    glm::vec3 getGravity() const { return state_.gravityDirection * state_.gravityStrength; }
    
    // ========== STEP-UP ==========
    
    /**
     * Try to step up onto an obstacle
     */
    StepUpResult tryStepUp(const glm::vec3& hitNormal, const glm::vec3& hitLocation);
    
    // ========== CCD ==========
    
    /**
     * Sweep capsule for CCD
     */
    CCDResult sweepCapsule(const glm::vec3& start, const glm::vec3& end);
    
    // ========== SPLINE LOCK ==========
    
    /**
     * Lock character to a spline (for rail grinding)
     */
    void lockToSpline(SplineComponent* spline, float startDistance = 0.0f);
    
    /**
     * Unlock from current spline
     */
    void unlockFromSpline();
    
    /**
     * Check if locked to spline
     */
    bool isLockedToSpline() const { return state_.lockedSpline != nullptr; }
    
    // ========== STATE ACCESS ==========
    
    const CharacterState& getState() const { return state_; }
    CharacterState& getState() { return state_; }
    
    glm::vec3 getPosition() const { return state_.position; }
    void setPosition(const glm::vec3& position) { state_.position = position; }
    
    glm::quat getRotation() const { return state_.rotation; }
    void setRotation(const glm::quat& rotation) { state_.rotation = rotation; }
    
    glm::vec3 getForward() const { return state_.rotation * glm::vec3(0.0f, 0.0f, -1.0f); }
    glm::vec3 getRight() const { return state_.rotation * glm::vec3(1.0f, 0.0f, 0.0f); }
    glm::vec3 getUp() const { return state_.currentUp; }
    
    MovementMode getMovementMode() const { return state_.movementMode; }
    void setMovementMode(MovementMode mode) { state_.movementMode = mode; }
    
    // ========== CONFIGURATION ==========
    
    void setMaxSpeed(float speed) { maxSpeed_ = speed; }
    float getMaxSpeed() const { return maxSpeed_; }
    
    void setMaxWalkableAngle(float degrees) { maxWalkableAngle_ = degrees; }
    void setMaxStepHeight(float height) { maxStepHeight_ = height; }
    
    void setCapsuleSize(float radius, float halfHeight);
    
    // ========== ABILITIES ==========
    
    void boost(float power, float duration);
    void superJump(float height);
    void setInvincible(float duration);
    
    // ========== DEBUG ==========
    
    void setDebugDraw(bool enable) { debugDraw_ = enable; }
    
    // Collision callback
    using CollisionCallback = std::function<void(const glm::vec3& point, const glm::vec3& normal, JPH::BodyID hitBody)>;
    void setCollisionCallback(CollisionCallback callback) { collisionCallback_ = callback; }
    
private:
    // Movement phases
    void updateWalking(float deltaTime, const CharacterInput& input);
    void updateFalling(float deltaTime, const CharacterInput& input);
    void updateSplineLock(float deltaTime, const CharacterInput& input);
    
    // Movement helpers
    void moveAlongFloor(float deltaTime, const CharacterInput& input);
    void applyGravity(float deltaTime);
    void handleJump(const CharacterInput& input, float deltaTime);
    void performJump(float jumpForce);
    
    // Collision helpers
    void resolveCollisions(float deltaTime);
    void handlePenetration(const glm::vec3& penetrationNormal, float penetrationDepth);
    
    // Jolt helpers
    static glm::vec3 toGLM(const JPH::Vec3& v) { return glm::vec3(v.GetX(), v.GetY(), v.GetZ()); }
    static JPH::Vec3 toJolt(const glm::vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
    static glm::quat toGLM(const JPH::Quat& q) { return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()); }
    static JPH::Quat toJolt(const glm::quat& q) { return JPH::Quat(q.x, q.y, q.z, q.w); }
    
    // State
    CharacterState state_;
    
    // Physics
    JPH::PhysicsSystem* physicsSystem_ = nullptr;
    GravitySystem* gravitySystem_ = nullptr;
    
    // Jolt character
    JPH::Ref<JPH::CharacterVirtual> character_;
    JPH::Ref<JPH::CapsuleShape> capsuleShape_;
    
    // Configuration
    float maxSpeed_ = KineticConstants::DEFAULT_MAX_SPEED;
    float acceleration_ = KineticConstants::ACCELERATION;
    float deceleration_ = KineticConstants::DECELERATION;
    float airControl_ = KineticConstants::AIR_CONTROL;
    float maxWalkableAngle_ = KineticConstants::MAX_WALKABLE_ANGLE;
    float maxStepHeight_ = KineticConstants::MAX_STEP_HEIGHT;
    float capsuleRadius_ = KineticConstants::DEFAULT_CAPSULE_RADIUS;
    float capsuleHalfHeight_ = KineticConstants::DEFAULT_CAPSULE_HALF_HEIGHT;
    float jumpForce_ = KineticConstants::DEFAULT_JUMP_FORCE;
    float adhesionMultiplier_ = 1.0f;
    
    // Boost state
    float boostTimer_ = 0.0f;
    float boostPower_ = 0.0f;
    
    // Callbacks
    CollisionCallback collisionCallback_;
    
    // Debug
    bool debugDraw_ = false;
    bool initialized_ = false;
};

} // namespace Sanic
