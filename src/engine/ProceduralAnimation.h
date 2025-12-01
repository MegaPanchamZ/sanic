/**
 * ProceduralAnimation.h
 * 
 * Procedural Animation System (Control Rig)
 * 
 * Features:
 * - Foot IK with ground adaptation
 * - Stride Warping for speed matching
 * - Banking (lean into turns)
 * - Surface alignment
 * - Orientation warping
 * 
 * Inspired by UE5's Control Rig and Motion Warping systems
 * 
 * Reference:
 *   Engine/Source/Runtime/AnimGraphRuntime/Private/AnimNodes/AnimNode_LegIK.cpp
 *   Engine/Source/Runtime/AnimGraphRuntime/Private/BoneControllers/AnimNode_OrientationWarping.cpp
 *   Engine/Source/Runtime/AnimGraphRuntime/Private/BoneControllers/AnimNode_StrideWarping.cpp
 */

#pragma once

#include "Animation.h"
#include "PhysicsSystem.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <unordered_map>

namespace Sanic {

class VulkanContext;
class PhysicsSystem;

// ============================================================================
// FOOT IK SYSTEM
// ============================================================================

/**
 * IK target for foot placement
 */
struct FootIKTarget {
    glm::vec3 footTarget;         // Target position for foot
    glm::vec3 groundNormal;       // Normal at ground contact
    glm::quat ankleRotation;      // Rotation to align with surface
    float ikWeight = 1.0f;        // Blend weight for IK
    bool isGrounded = true;       // Is foot on ground
    float groundHeight = 0.0f;    // Height of ground at this position
};

/**
 * Configuration for a single leg
 */
struct LegIKConfig {
    std::string hipBoneName;      // e.g., "thigh_l"
    std::string kneeBoneName;     // e.g., "calf_l"
    std::string ankleBoneName;    // e.g., "foot_l"
    std::string toeBoneName;      // e.g., "ball_l" (optional)
    
    glm::vec3 footOffset = glm::vec3(0.0f);  // Offset from ankle to foot sole
    float footLength = 0.25f;     // Length of foot for toe raycast
    
    // IK settings
    float maxReach = 1.5f;        // Maximum leg extension
    float minBend = 0.05f;        // Minimum knee bend to prevent hyperextension
    glm::vec3 poleVector = glm::vec3(0, 0, 1);  // Knee direction hint
    
    // Ground trace settings
    float traceUpDistance = 0.5f;     // How far above foot to start trace
    float traceDownDistance = 0.7f;   // How far below foot to trace
    float groundOffset = 0.02f;       // Small offset above ground
};

/**
 * Two-Bone IK Solver
 */
class TwoBoneIKSolver {
public:
    struct Result {
        glm::quat upperRotation;  // Hip/Shoulder
        glm::quat lowerRotation;  // Knee/Elbow
        bool reachable = true;
    };
    
    /**
     * Solve two-bone IK chain
     * @param upperPos Position of upper bone (hip)
     * @param midPos Position of middle joint (knee)
     * @param lowerPos Position of end effector (ankle)
     * @param targetPos Target position
     * @param poleVector Direction hint for middle joint
     * @return Rotations for upper and lower bones
     */
    static Result solve(
        const glm::vec3& upperPos,
        const glm::vec3& midPos,
        const glm::vec3& lowerPos,
        const glm::vec3& targetPos,
        const glm::vec3& poleVector
    );
};

/**
 * Foot IK Component
 */
class FootIKSystem {
public:
    FootIKSystem() = default;
    
    /**
     * Initialize with physics for raycasts
     */
    void initialize(PhysicsSystem* physics);
    
    /**
     * Add a leg for IK processing
     */
    void addLeg(const std::string& name, const LegIKConfig& config);
    
    /**
     * Update IK targets based on ground traces
     * @param skeleton The skeleton to modify
     * @param rootTransform World transform of character
     * @param velocity Character velocity (for foot prediction)
     */
    void update(
        Skeleton& skeleton,
        const glm::mat4& rootTransform,
        const glm::vec3& velocity,
        float deltaTime
    );
    
    /**
     * Apply IK to skeleton
     */
    void applyIK(Skeleton& skeleton);
    
    /**
     * Get pelvis adjustment (for lowering body on slopes)
     */
    float getPelvisOffset() const { return pelvisOffset_; }
    
    /**
     * Enable/disable foot IK
     */
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }
    
    /**
     * Set IK blend weight
     */
    void setWeight(float weight) { ikWeight_ = glm::clamp(weight, 0.0f, 1.0f); }
    float getWeight() const { return ikWeight_; }
    
    /**
     * Get foot target for a specific leg
     */
    const FootIKTarget* getFootTarget(const std::string& legName) const;
    
private:
    PhysicsSystem* physics_ = nullptr;
    bool enabled_ = true;
    float ikWeight_ = 1.0f;
    
    struct LegData {
        LegIKConfig config;
        FootIKTarget target;
        
        // Bone indices (cached)
        int32_t hipBoneIdx = -1;
        int32_t kneeBoneIdx = -1;
        int32_t ankleBoneIdx = -1;
        int32_t toeBoneIdx = -1;
        
        // Bone lengths (cached)
        float upperLength = 0.0f;
        float lowerLength = 0.0f;
        
        // Smoothing
        glm::vec3 smoothedTarget = glm::vec3(0);
        float smoothedOffset = 0.0f;
    };
    
    std::unordered_map<std::string, LegData> legs_;
    float pelvisOffset_ = 0.0f;
    
    /**
     * Perform ground trace for foot
     */
    bool traceGround(
        const glm::vec3& footPos,
        const LegIKConfig& config,
        glm::vec3& outHitPos,
        glm::vec3& outNormal
    );
    
    /**
     * Calculate pelvis offset to accommodate leg IK
     */
    void updatePelvisOffset();
};

// ============================================================================
// STRIDE WARPING
// ============================================================================

/**
 * Stride Warping scales animation playback and root motion
 * to match desired ground speed without foot sliding
 */
class StrideWarpingSystem {
public:
    struct Settings {
        float minSpeedRatio = 0.5f;       // Minimum speed ratio (prevents too slow)
        float maxSpeedRatio = 2.0f;       // Maximum speed ratio (prevents too fast)
        float blendTime = 0.2f;           // Blend time for changes
        
        // Stride length from animation (or auto-detect)
        float animationStrideLength = 0.0f;  // 0 = auto-detect
        float animationSpeed = 0.0f;         // 0 = auto-detect
        
        // Pelvis adjustment
        bool adjustPelvisForward = true;
        float pelvisForwardScale = 0.5f;
    };
    
    StrideWarpingSystem() = default;
    
    /**
     * Set stride warping settings
     */
    void setSettings(const Settings& settings) { settings_ = settings; }
    const Settings& getSettings() const { return settings_; }
    
    /**
     * Update stride warping
     * @param groundSpeed Current ground speed of character
     * @param animSpeed Speed the animation was authored for
     * @param deltaTime Frame delta
     */
    void update(float groundSpeed, float animSpeed, float deltaTime);
    
    /**
     * Apply stride warping to skeleton
     * Scales pelvis movement to match desired speed
     */
    void apply(Skeleton& skeleton);
    
    /**
     * Get playback rate modifier
     */
    float getPlaybackRate() const { return currentSpeedRatio_; }
    
    /**
     * Get forward offset for pelvis
     */
    glm::vec3 getPelvisOffset() const { return pelvisOffset_; }
    
    /**
     * Enable/disable stride warping
     */
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }
    
private:
    Settings settings_;
    bool enabled_ = true;
    
    float currentSpeedRatio_ = 1.0f;
    float targetSpeedRatio_ = 1.0f;
    glm::vec3 pelvisOffset_ = glm::vec3(0);
    glm::vec3 originalPelvisPos_ = glm::vec3(0);
};

// ============================================================================
// BANKING (LEAN INTO TURNS)
// ============================================================================

/**
 * Banking tilts character into turns based on angular velocity
 */
class BankingSystem {
public:
    struct Settings {
        float bankFactor = 20.0f;         // Degrees per unit angular velocity
        float maxBankAngle = 30.0f;       // Maximum bank angle in degrees
        float blendSpeed = 5.0f;          // How fast to blend bank
        
        // Spine distribution (how much each spine bone contributes)
        std::vector<float> spineDistribution = { 0.3f, 0.5f, 0.2f };
        
        // Optional: also bank the head counter to maintain level gaze
        bool counterBankHead = true;
        float headCounterFactor = 0.5f;
    };
    
    BankingSystem() = default;
    
    /**
     * Set banking settings
     */
    void setSettings(const Settings& settings) { settings_ = settings; }
    const Settings& getSettings() const { return settings_; }
    
    /**
     * Update banking based on angular velocity
     * @param angularVelocity Angular velocity (Y component for yaw)
     * @param deltaTime Frame delta
     */
    void update(float angularVelocity, float deltaTime);
    
    /**
     * Apply banking rotations to spine bones
     * @param skeleton Target skeleton
     * @param spineBoneNames Names of spine bones (in order from pelvis to head)
     */
    void apply(Skeleton& skeleton, const std::vector<std::string>& spineBoneNames);
    
    /**
     * Get current bank angle in degrees
     */
    float getBankAngle() const { return currentBankAngle_; }
    
    /**
     * Enable/disable banking
     */
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }
    
private:
    Settings settings_;
    bool enabled_ = true;
    
    float currentBankAngle_ = 0.0f;
    float targetBankAngle_ = 0.0f;
};

// ============================================================================
// ORIENTATION WARPING
// ============================================================================

/**
 * Orientation Warping adjusts animation root to face movement direction
 * while keeping the rest of the animation natural
 */
class OrientationWarpingSystem {
public:
    struct Settings {
        float maxWarpAngle = 90.0f;       // Maximum angle to warp
        float blendSpeed = 10.0f;         // Blend speed
        
        // Distribute rotation across spine
        std::vector<float> spineDistribution = { 0.2f, 0.3f, 0.3f, 0.2f };
        
        // Blend based on speed (don't warp when stopped)
        float minSpeedForWarp = 0.1f;
        float fullWarpSpeed = 1.0f;
    };
    
    OrientationWarpingSystem() = default;
    
    /**
     * Set settings
     */
    void setSettings(const Settings& settings) { settings_ = settings; }
    const Settings& getSettings() const { return settings_; }
    
    /**
     * Update orientation warping
     * @param movementAngle Angle of movement relative to character forward (radians)
     * @param speed Current movement speed
     * @param deltaTime Frame delta
     */
    void update(float movementAngle, float speed, float deltaTime);
    
    /**
     * Apply warping to spine
     */
    void apply(Skeleton& skeleton, const std::vector<std::string>& spineBoneNames);
    
    /**
     * Get current warp angle
     */
    float getWarpAngle() const { return currentWarpAngle_; }
    
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }
    
private:
    Settings settings_;
    bool enabled_ = true;
    
    float currentWarpAngle_ = 0.0f;
    float targetWarpAngle_ = 0.0f;
};

// ============================================================================
// SURFACE ALIGNMENT
// ============================================================================

/**
 * Aligns character root to surface normal (for walking on slopes)
 */
class SurfaceAlignmentSystem {
public:
    struct Settings {
        float maxSlopeAngle = 45.0f;      // Maximum slope to align to
        float blendSpeed = 5.0f;          // Blend speed
        float traceDistance = 1.0f;       // Distance to trace for ground
        
        // Body adaptation
        bool adjustHips = true;
        float hipRotationScale = 0.5f;    // How much hips rotate vs feet
    };
    
    SurfaceAlignmentSystem() = default;
    
    void setSettings(const Settings& settings) { settings_ = settings; }
    const Settings& getSettings() const { return settings_; }
    
    /**
     * Initialize with physics for raycasts
     */
    void initialize(PhysicsSystem* physics);
    
    /**
     * Update surface alignment
     * @param position Character position
     * @param currentUp Current up vector
     * @param deltaTime Frame delta
     */
    void update(const glm::vec3& position, const glm::vec3& currentUp, float deltaTime);
    
    /**
     * Get the adjusted rotation for root
     */
    glm::quat getRootRotation() const { return currentRotation_; }
    
    /**
     * Get the surface normal
     */
    glm::vec3 getSurfaceNormal() const { return surfaceNormal_; }
    
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }
    
private:
    Settings settings_;
    PhysicsSystem* physics_ = nullptr;
    bool enabled_ = true;
    
    glm::vec3 surfaceNormal_ = glm::vec3(0, 1, 0);
    glm::quat currentRotation_ = glm::quat(1, 0, 0, 0);
    glm::quat targetRotation_ = glm::quat(1, 0, 0, 0);
};

// ============================================================================
// PROCEDURAL ANIMATION CONTROLLER
// ============================================================================

/**
 * Combines all procedural animation systems
 */
class ProceduralAnimationController {
public:
    ProceduralAnimationController();
    ~ProceduralAnimationController() = default;
    
    /**
     * Initialize all systems
     */
    void initialize(PhysicsSystem* physics);
    
    /**
     * Setup for a specific skeleton
     * @param skeleton Target skeleton
     * @param config Configuration with bone names, etc.
     */
    struct CharacterConfig {
        // Spine bones (bottom to top)
        std::vector<std::string> spineBones = { "spine_01", "spine_02", "spine_03" };
        std::string pelvisBone = "pelvis";
        std::string headBone = "head";
        
        // Leg configs
        std::vector<std::pair<std::string, LegIKConfig>> legs;
        
        // Animation speeds
        float walkAnimSpeed = 2.5f;       // Speed the walk anim was authored for
        float runAnimSpeed = 6.0f;        // Speed the run anim was authored for
    };
    
    void setupCharacter(const CharacterConfig& config);
    
    /**
     * Update all procedural animation
     * @param skeleton Target skeleton
     * @param rootTransform Character world transform
     * @param velocity Current velocity
     * @param angularVelocity Current angular velocity (Y = yaw)
     * @param movementAngle Movement direction relative to facing
     * @param deltaTime Frame delta
     */
    void update(
        Skeleton& skeleton,
        const glm::mat4& rootTransform,
        const glm::vec3& velocity,
        float angularVelocity,
        float movementAngle,
        float deltaTime
    );
    
    /**
     * Apply all procedural modifications to skeleton
     */
    void apply(Skeleton& skeleton);
    
    // System access
    FootIKSystem& getFootIK() { return footIK_; }
    StrideWarpingSystem& getStrideWarping() { return strideWarping_; }
    BankingSystem& getBanking() { return banking_; }
    OrientationWarpingSystem& getOrientationWarping() { return orientationWarping_; }
    SurfaceAlignmentSystem& getSurfaceAlignment() { return surfaceAlignment_; }
    
    /**
     * Enable/disable all systems
     */
    void setAllEnabled(bool enabled);
    
private:
    FootIKSystem footIK_;
    StrideWarpingSystem strideWarping_;
    BankingSystem banking_;
    OrientationWarpingSystem orientationWarping_;
    SurfaceAlignmentSystem surfaceAlignment_;
    
    CharacterConfig config_;
    bool initialized_ = false;
};

// ============================================================================
// ECS COMPONENTS
// ============================================================================

/**
 * Component for procedural animation on an entity
 */
struct ProceduralAnimationComponent {
    std::shared_ptr<ProceduralAnimationController> controller;
    ProceduralAnimationController::CharacterConfig config;
    
    // Per-entity settings
    bool footIKEnabled = true;
    bool strideWarpingEnabled = true;
    bool bankingEnabled = true;
    bool orientationWarpingEnabled = true;
    bool surfaceAlignmentEnabled = true;
    
    float bankAngle = 0.0f;  // Current bank angle (read-only)
};

} // namespace Sanic
