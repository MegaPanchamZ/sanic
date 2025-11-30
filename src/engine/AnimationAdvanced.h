/**
 * AnimationAdvanced.h
 * 
 * Advanced Animation Features for Sanic Engine.
 * Inspired by Unreal Engine's animation system.
 * 
 * Features:
 * - Root Motion Extraction and Application
 * - Animation Compression (ACL-inspired)
 * - Skeleton Retargeting
 * - Additive Animation Support
 * - Animation Curves and Events
 * - Montage/Composite Animations
 */

#pragma once

#include "Animation.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>
#include <functional>
#include <optional>

namespace Sanic {

// Forward declarations
class VulkanContext;

// ============================================================================
// ROOT MOTION EXTRACTION
// ============================================================================

enum class ERootMotionMode : uint8_t {
    NoExtraction,           // Keep root bone in animation
    ExtractTranslation,     // Extract XYZ translation, keep rotation
    ExtractTranslationXY,   // Extract only XY translation (ground plane)
    ExtractAll,             // Extract full transform (translation + rotation)
    IgnoreRoot              // Zero out root bone transform
};

enum class ERootMotionSource : uint8_t {
    FromAnimation,          // Use animation's root motion
    FromMontageOrCurrent,   // Use montage's root motion if playing
    IgnoreRootMotion        // No root motion applied
};

struct FRootMotionMovementParams {
    glm::vec3 translation = glm::vec3(0.0f);
    glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    
    bool hasTranslation = false;
    bool hasRotation = false;
    
    FRootMotionMovementParams operator+(const FRootMotionMovementParams& other) const {
        FRootMotionMovementParams result;
        result.translation = translation + other.translation;
        result.rotation = glm::normalize(rotation * other.rotation);
        result.hasTranslation = hasTranslation || other.hasTranslation;
        result.hasRotation = hasRotation || other.hasRotation;
        return result;
    }
    
    void accumulate(const FRootMotionMovementParams& other, float weight = 1.0f) {
        if (other.hasTranslation) {
            translation += other.translation * weight;
            hasTranslation = true;
        }
        if (other.hasRotation) {
            rotation = glm::slerp(rotation, other.rotation, weight);
            hasRotation = true;
        }
    }
    
    void clear() {
        translation = glm::vec3(0.0f);
        rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        hasTranslation = false;
        hasRotation = false;
    }
};

class RootMotionExtractor {
public:
    /**
     * Extract root motion from an animation clip between two times
     */
    static FRootMotionMovementParams extractRootMotion(
        const AnimationClip& clip,
        const Skeleton& skeleton,
        float startTime,
        float endTime,
        ERootMotionMode mode = ERootMotionMode::ExtractAll
    );
    
    /**
     * Remove root motion from an animation (bake to origin)
     */
    static void removeRootMotion(
        AnimationClip& clip,
        const Skeleton& skeleton,
        ERootMotionMode mode = ERootMotionMode::ExtractAll
    );
    
    /**
     * Extract and store root motion in a separate track
     */
    static std::vector<FRootMotionMovementParams> extractRootMotionTrack(
        const AnimationClip& clip,
        const Skeleton& skeleton,
        float sampleRate = 30.0f,
        ERootMotionMode mode = ERootMotionMode::ExtractAll
    );
    
    /**
     * Apply root motion to character movement
     */
    static void applyRootMotion(
        glm::vec3& outPosition,
        glm::quat& outRotation,
        const FRootMotionMovementParams& rootMotion,
        const glm::quat& characterRotation
    );
    
private:
    static glm::mat4 sampleRootTransform(
        const AnimationClip& clip,
        const Skeleton& skeleton,
        float time
    );
};

// ============================================================================
// ANIMATION COMPRESSION (ACL-Inspired)
// ============================================================================

enum class ECompressionLevel : uint8_t {
    None = 0,               // No compression
    Low = 1,                // Minimal compression, highest quality
    Medium = 2,             // Balanced compression
    High = 3,               // Aggressive compression
    Automatic = 4           // Auto-select based on animation
};

struct AnimationCompressionSettings {
    ECompressionLevel level = ECompressionLevel::Medium;
    
    // Error thresholds
    float translationErrorThreshold = 0.01f;   // 1cm
    float rotationErrorThreshold = 0.1f;       // ~0.1 degrees
    float scaleErrorThreshold = 0.001f;        // 0.1%
    
    // Per-bone settings (bone name -> custom threshold)
    std::unordered_map<std::string, float> perBoneTranslationError;
    std::unordered_map<std::string, float> perBoneRotationError;
    
    // Keyframe reduction
    bool removeConstantTracks = true;
    bool removeIdentityTracks = true;
    float constantThreshold = 0.0001f;
    
    // Quantization
    bool quantizeTranslation = true;
    bool quantizeRotation = true;
    bool quantizeScale = true;
    uint8_t translationBits = 16;
    uint8_t rotationBits = 12;       // Per component
    uint8_t scaleBits = 12;
    
    // Curve fitting
    bool useCurveFitting = true;
    float curveTolerance = 0.01f;
    
    // Bind pose stripping
    bool stripBindPose = true;       // Store delta from bind pose
    
    // Additive optimization
    bool stripBaseAnimation = false; // For additive anims, don't store base pose
};

struct CompressedAnimationClip {
    std::string name;
    float duration;
    float sampleRate;
    
    // Compressed bone data
    struct CompressedBoneTrack {
        uint32_t boneIndex;
        
        // Translation: quantized + delta compressed
        std::vector<uint16_t> translationData;
        glm::vec3 translationMin;
        glm::vec3 translationRange;
        
        // Rotation: smallest-3 quaternion compression
        std::vector<uint16_t> rotationData;  // 48 bits per quaternion
        
        // Scale: quantized
        std::vector<uint16_t> scaleData;
        glm::vec3 scaleMin;
        glm::vec3 scaleRange;
        
        // Keyframe times (variable rate)
        std::vector<uint16_t> keyframeTimes; // Normalized to [0, 65535]
        bool isConstant = false;
    };
    std::vector<CompressedBoneTrack> boneTracks;
    
    // Compression stats
    size_t originalSize = 0;
    size_t compressedSize = 0;
    float compressionRatio = 1.0f;
    float maxError = 0.0f;
    
    // Root motion (stored separately)
    std::vector<FRootMotionMovementParams> rootMotionTrack;
    
    // Animation events
    std::vector<AnimationClip::Event> events;
};

class AnimationCompressor {
public:
    /**
     * Compress an animation clip
     */
    static CompressedAnimationClip compress(
        const AnimationClip& clip,
        const Skeleton& skeleton,
        const AnimationCompressionSettings& settings = {}
    );
    
    /**
     * Decompress to standard animation clip
     */
    static AnimationClip decompress(
        const CompressedAnimationClip& compressed,
        const Skeleton& skeleton
    );
    
    /**
     * Sample compressed animation at time
     */
    static void sampleCompressed(
        const CompressedAnimationClip& compressed,
        float time,
        std::vector<glm::mat4>& outBoneTransforms
    );
    
    /**
     * Calculate compression statistics
     */
    struct CompressionStats {
        float compressionRatio;
        float maxPositionError;
        float maxRotationError;
        float avgPositionError;
        float avgRotationError;
        size_t removedKeyframes;
        size_t constantTracks;
    };
    
    static CompressionStats calculateStats(
        const AnimationClip& original,
        const CompressedAnimationClip& compressed,
        const Skeleton& skeleton
    );
    
private:
    // Quaternion compression using smallest-3 encoding
    static void compressQuaternion(const glm::quat& q, uint16_t* out);
    static glm::quat decompressQuaternion(const uint16_t* data);
    
    // Keyframe reduction
    static std::vector<size_t> selectKeyframes(
        const std::vector<float>& values,
        float tolerance
    );
    
    // Curve fitting
    static std::vector<float> fitCurve(
        const std::vector<float>& samples,
        const std::vector<float>& times,
        float tolerance
    );
};

// ============================================================================
// SKELETON RETARGETING
// ============================================================================

enum class ERetargetingMode : uint8_t {
    Skeleton,              // Transfer to different skeleton
    Scale,                 // Apply scale only
    AnimationScaled,       // Scale animation to target skeleton
    AnimationRelative,     // Relative bone transforms
    OrientAndScale         // Reorient and scale
};

struct BoneMapping {
    std::string sourceBone;
    std::string targetBone;
    
    // Optional transform adjustment
    glm::quat rotationOffset = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 translationOffset = glm::vec3(0.0f);
    float lengthScale = 1.0f;
};

struct RetargetingProfile {
    std::string name;
    std::string sourceSkeletonName;
    std::string targetSkeletonName;
    
    std::vector<BoneMapping> boneMappings;
    
    // Chain definitions for IK-based retargeting
    struct BoneChain {
        std::string chainName;          // e.g., "LeftArm", "Spine"
        std::vector<std::string> bones; // Ordered from root to tip
        bool useIK = false;             // Use IK to match end positions
    };
    std::vector<BoneChain> chains;
    
    // Root settings
    bool preserveRootMotion = true;
    float rootScale = 1.0f;
    glm::vec3 rootOffset = glm::vec3(0.0f);
    
    // Auto-generate mappings based on bone names
    static RetargetingProfile autoGenerate(
        const Skeleton& source,
        const Skeleton& target
    );
};

class AnimationRetargeter {
public:
    /**
     * Retarget an animation from one skeleton to another
     */
    static std::shared_ptr<AnimationClip> retarget(
        const AnimationClip& sourceClip,
        const Skeleton& sourceSkeleton,
        const Skeleton& targetSkeleton,
        const RetargetingProfile& profile,
        ERetargetingMode mode = ERetargetingMode::OrientAndScale
    );
    
    /**
     * Retarget a single pose
     */
    static void retargetPose(
        const std::vector<glm::mat4>& sourcePose,
        std::vector<glm::mat4>& targetPose,
        const Skeleton& sourceSkeleton,
        const Skeleton& targetSkeleton,
        const RetargetingProfile& profile,
        ERetargetingMode mode = ERetargetingMode::OrientAndScale
    );
    
    /**
     * Create a retargeting profile from bone name matching
     */
    static RetargetingProfile createProfile(
        const Skeleton& source,
        const Skeleton& target,
        const std::unordered_map<std::string, std::string>& nameMapping = {}
    );
    
    /**
     * Validate a retargeting profile
     */
    struct ValidationResult {
        bool isValid = true;
        std::vector<std::string> unmappedSourceBones;
        std::vector<std::string> unmappedTargetBones;
        std::vector<std::string> warnings;
    };
    
    static ValidationResult validateProfile(
        const RetargetingProfile& profile,
        const Skeleton& source,
        const Skeleton& target
    );
    
private:
    static glm::mat4 retargetBone(
        const glm::mat4& sourceLocal,
        const glm::mat4& sourceBindPose,
        const glm::mat4& targetBindPose,
        const BoneMapping& mapping,
        ERetargetingMode mode
    );
    
    static void computeChainIK(
        const RetargetingProfile::BoneChain& chain,
        const std::vector<glm::mat4>& sourcePose,
        std::vector<glm::mat4>& targetPose,
        const Skeleton& source,
        const Skeleton& target
    );
};

// ============================================================================
// ADDITIVE ANIMATION
// ============================================================================

enum class EAdditiveBasePoseType : uint8_t {
    SkeletonBindPose,      // Use skeleton's bind pose as base
    FirstFrame,            // Use first frame of animation
    CustomPose,            // User-specified pose
    RefAnimation           // Reference animation at specific time
};

struct AdditiveAnimationSettings {
    EAdditiveBasePoseType basePoseType = EAdditiveBasePoseType::SkeletonBindPose;
    
    // For RefAnimation type
    std::string refAnimationPath;
    float refAnimationTime = 0.0f;
    
    // For CustomPose type
    std::vector<glm::mat4> customBasePose;
    
    // Blending
    bool preMultiply = true;  // Apply additive before or after base
};

class AdditiveAnimationProcessor {
public:
    /**
     * Convert animation to additive
     */
    static std::shared_ptr<AnimationClip> makeAdditive(
        const AnimationClip& sourceClip,
        const Skeleton& skeleton,
        const AdditiveAnimationSettings& settings = {}
    );
    
    /**
     * Apply additive animation on top of base pose
     */
    static void applyAdditive(
        std::vector<glm::mat4>& basePose,
        const std::vector<glm::mat4>& additivePose,
        float weight = 1.0f,
        bool preMultiply = true
    );
    
    /**
     * Blend two additive animations
     */
    static void blendAdditive(
        std::vector<glm::mat4>& poseA,
        const std::vector<glm::mat4>& poseB,
        float weightB
    );
};

// ============================================================================
// ANIMATION MONTAGE / COMPOSITE
// ============================================================================

struct AnimationMontageSection {
    std::string name;
    float startTime;
    float endTime;
    
    // Branching
    std::string nextSectionName;     // Default next section
    std::vector<std::pair<std::string, std::string>> branchOptions; // (trigger, sectionName)
    
    // Looping
    int loopCount = 0;              // 0 = no loop, -1 = infinite
    
    // Root motion
    FRootMotionMovementParams rootMotionDelta;
};

struct AnimationMontageSlot {
    std::string slotName;           // e.g., "UpperBody", "FullBody"
    uint32_t slotGroupIndex = 0;
    float blendInTime = 0.2f;
    float blendOutTime = 0.2f;
};

class AnimationMontage {
public:
    AnimationMontage(const std::string& name) : name_(name) {}
    
    // Add animations to montage
    void addAnimation(std::shared_ptr<AnimationClip> clip, float startTime = -1.0f);
    
    // Section management
    void addSection(const AnimationMontageSection& section);
    const AnimationMontageSection* getSection(const std::string& name) const;
    void jumpToSection(const std::string& name);
    
    // Playback
    void play(float startTime = 0.0f);
    void stop(float blendOutTime = 0.2f);
    void pause();
    void resume();
    
    void update(float deltaTime);
    
    // Get current pose
    void getCurrentPose(std::vector<glm::mat4>& outPose) const;
    
    // Root motion
    FRootMotionMovementParams consumeRootMotion();
    
    // Events
    using NotifyCallback = std::function<void(const std::string&)>;
    void setNotifyCallback(NotifyCallback callback) { notifyCallback_ = callback; }
    
    // Properties
    float getDuration() const { return duration_; }
    float getPosition() const { return position_; }
    bool isPlaying() const { return playing_; }
    const std::string& getCurrentSectionName() const { return currentSection_; }
    
    // Slot
    void setSlot(const AnimationMontageSlot& slot) { slot_ = slot; }
    const AnimationMontageSlot& getSlot() const { return slot_; }
    
private:
    std::string name_;
    std::vector<std::shared_ptr<AnimationClip>> clips_;
    std::vector<AnimationMontageSection> sections_;
    AnimationMontageSlot slot_;
    
    float duration_ = 0.0f;
    float position_ = 0.0f;
    float playRate_ = 1.0f;
    bool playing_ = false;
    bool paused_ = false;
    
    std::string currentSection_;
    int sectionLoopCounter_ = 0;
    
    FRootMotionMovementParams pendingRootMotion_;
    NotifyCallback notifyCallback_;
};

// ============================================================================
// ANIMATION CURVES
// ============================================================================

struct AnimationCurve {
    std::string name;
    
    struct Keyframe {
        float time;
        float value;
        float inTangent = 0.0f;
        float outTangent = 0.0f;
        enum class TangentMode { Auto, Linear, Constant, Free } mode = TangentMode::Auto;
    };
    std::vector<Keyframe> keyframes;
    
    // Sampling
    float evaluate(float time) const;
    float evaluateDerivative(float time) const;
    
    // Editing
    void addKey(float time, float value);
    void removeKey(size_t index);
    void autoTangents();
    
    // Utility
    float getDuration() const;
    float getMinValue() const;
    float getMaxValue() const;
};

class AnimationCurveLibrary {
public:
    static AnimationCurveLibrary& getInstance();
    
    // Predefined curves
    static AnimationCurve linear();
    static AnimationCurve easeIn();
    static AnimationCurve easeOut();
    static AnimationCurve easeInOut();
    static AnimationCurve bounce();
    static AnimationCurve elastic();
    static AnimationCurve overshoot();
    
    // Custom curve storage
    void registerCurve(const std::string& name, const AnimationCurve& curve);
    const AnimationCurve* getCurve(const std::string& name) const;
    
private:
    AnimationCurveLibrary() = default;
    std::unordered_map<std::string, AnimationCurve> curves_;
};

// ============================================================================
// ANIMATION NOTIFY SYSTEM
// ============================================================================

struct AnimationNotify {
    std::string name;
    float time;
    
    // Optional data
    std::unordered_map<std::string, std::string> params;
    
    // Types
    enum class Type {
        Event,          // Instantaneous event
        State,          // Start of state (has end time)
        Sound,          // Play sound
        Particle,       // Spawn particle
        Camera,         // Camera shake/effect
        Custom          // User-defined
    } type = Type::Event;
    
    // For state-type notifies
    float endTime = -1.0f;
    
    // Attached data
    std::string soundCue;
    std::string particleSystem;
    glm::vec3 socketOffset = glm::vec3(0.0f);
    std::string boneName;
};

// ============================================================================
// ANIMATION POSE SNAPSHOT
// ============================================================================

class AnimationPoseSnapshot {
public:
    void capture(const std::vector<glm::mat4>& pose);
    void apply(std::vector<glm::mat4>& pose, float weight = 1.0f) const;
    
    // Blend between snapshots
    static void blend(
        const AnimationPoseSnapshot& a,
        const AnimationPoseSnapshot& b,
        float t,
        std::vector<glm::mat4>& outPose
    );
    
    bool isValid() const { return !bones_.empty(); }
    
private:
    std::vector<glm::mat4> bones_;
    std::vector<bool> valid_;
};

} // namespace Sanic
