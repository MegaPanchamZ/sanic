/**
 * Animation.h
 * 
 * Skeletal Animation System for Nanite-compatible meshes.
 * 
 * Key Features:
 * - Skeleton hierarchy with bone transforms
 * - Animation clips with keyframe interpolation
 * - Animation blending and layering
 * - GPU skinning via compute shader (pre-Nanite stage)
 * - Animation state machine for gameplay
 * 
 * Integration with Nanite:
 * - Compute shader deforms vertices before meshlet generation
 * - Updates BLAS for ray tracing with deformed geometry
 * - Supports per-cluster bone influence for LOD
 */

#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>
#include <functional>

class VulkanContext;

namespace Sanic {

// ============================================================================
// SKELETON DATA STRUCTURES
// ============================================================================

constexpr uint32_t MAX_BONES = 256;
constexpr uint32_t MAX_BONE_INFLUENCES = 4;

struct Bone {
    std::string name;
    int32_t parentIndex;                // -1 for root
    glm::mat4 inverseBindMatrix;        // Inverse of bind pose
    glm::mat4 localBindPose;            // Local transform at bind pose
    
    // Runtime data
    glm::mat4 localTransform;           // Current local transform
    glm::mat4 globalTransform;          // Current world transform
    glm::mat4 skinningMatrix;           // inverseBindMatrix * globalTransform
};

struct Skeleton {
    std::string name;
    std::vector<Bone> bones;
    std::unordered_map<std::string, uint32_t> boneNameToIndex;
    int32_t rootBoneIndex = -1;
    
    // Precomputed bone hierarchy for efficient traversal
    std::vector<uint32_t> hierarchyOrder;  // Bones in parent-first order
    
    uint32_t findBone(const std::string& name) const {
        auto it = boneNameToIndex.find(name);
        return it != boneNameToIndex.end() ? it->second : UINT32_MAX;
    }
};

// Vertex bone weights (stored in mesh)
struct BoneWeight {
    uint32_t boneIndices[MAX_BONE_INFLUENCES];
    float weights[MAX_BONE_INFLUENCES];
};

// ============================================================================
// ANIMATION CLIPS
// ============================================================================

// Keyframe types
template<typename T>
struct Keyframe {
    float time;
    T value;
};

using PositionKeyframe = Keyframe<glm::vec3>;
using RotationKeyframe = Keyframe<glm::quat>;
using ScaleKeyframe = Keyframe<glm::vec3>;

// Channel for a single bone's animation
struct AnimationChannel {
    uint32_t boneIndex;
    std::vector<PositionKeyframe> positionKeys;
    std::vector<RotationKeyframe> rotationKeys;
    std::vector<ScaleKeyframe> scaleKeys;
    
    // Interpolation modes
    enum class Interpolation { Step, Linear, CubicSpline };
    Interpolation positionInterp = Interpolation::Linear;
    Interpolation rotationInterp = Interpolation::Linear;
    Interpolation scaleInterp = Interpolation::Linear;
};

struct AnimationClip {
    std::string name;
    float duration;                     // Total duration in seconds
    float ticksPerSecond = 30.0f;
    std::vector<AnimationChannel> channels;
    
    // Flags
    bool looping = true;
    float blendInTime = 0.0f;
    float blendOutTime = 0.0f;
    
    // Animation events (for gameplay callbacks)
    struct Event {
        float time;
        std::string name;
    };
    std::vector<Event> events;
};

// ============================================================================
// ANIMATION STATE MACHINE
// ============================================================================

// Blend tree node types
enum class BlendNodeType {
    Clip,           // Single animation
    Blend1D,        // 1D blend (e.g., walk speed)
    Blend2D,        // 2D blend (e.g., movement direction)
    Additive,       // Additive layer
    Override        // Full override
};

struct BlendNode {
    BlendNodeType type;
    std::string clipName;
    float blendWeight = 1.0f;
    
    // For Blend1D/2D
    std::vector<std::shared_ptr<BlendNode>> children;
    std::vector<float> thresholds;      // Blend thresholds for 1D
    std::vector<glm::vec2> positions;   // Blend positions for 2D
    
    float blendParameter = 0.0f;
    glm::vec2 blendPosition = glm::vec2(0.0f);
};

// State in the animation state machine
struct AnimationState {
    std::string name;
    std::shared_ptr<BlendNode> blendTree;
    
    // Transition rules
    struct Transition {
        std::string targetState;
        std::function<bool()> condition;
        float transitionTime = 0.2f;
        bool hasExitTime = false;
        float exitTime = 0.0f;
    };
    std::vector<Transition> transitions;
    
    // State callbacks
    std::function<void()> onEnter;
    std::function<void()> onExit;
    std::function<void(float)> onUpdate;
};

class AnimationStateMachine {
public:
    void addState(const std::string& name, std::shared_ptr<AnimationState> state);
    void addTransition(const std::string& from, const std::string& to,
                      std::function<bool()> condition, float transitionTime = 0.2f);
    
    void setDefaultState(const std::string& name);
    void update(float deltaTime);
    void forceState(const std::string& name);
    
    const std::string& getCurrentState() const { return currentStateName_; }
    float getStateTime() const { return stateTime_; }
    
    // Parameters for blend trees and conditions
    void setFloat(const std::string& name, float value);
    void setBool(const std::string& name, bool value);
    void setTrigger(const std::string& name);
    
    float getFloat(const std::string& name) const;
    bool getBool(const std::string& name) const;
    
private:
    std::unordered_map<std::string, std::shared_ptr<AnimationState>> states_;
    std::string currentStateName_;
    std::string previousStateName_;
    float stateTime_ = 0.0f;
    float transitionTime_ = 0.0f;
    float transitionProgress_ = 0.0f;
    bool inTransition_ = false;
    
    std::unordered_map<std::string, float> floatParams_;
    std::unordered_map<std::string, bool> boolParams_;
    std::unordered_map<std::string, bool> triggers_;
};

// ============================================================================
// ANIMATION INSTANCE (Runtime)
// ============================================================================

class AnimationInstance {
public:
    AnimationInstance(std::shared_ptr<Skeleton> skeleton);
    
    // Playback control
    void play(const std::string& clipName, float blendTime = 0.2f);
    void stop(float blendTime = 0.2f);
    void pause();
    void resume();
    
    void setSpeed(float speed) { playbackSpeed_ = speed; }
    void setTime(float time);
    
    // Blending
    void crossfade(const std::string& clipName, float duration);
    void setLayerWeight(uint32_t layer, float weight);
    void addAdditiveLayer(const std::string& clipName, float weight);
    
    // Update and apply
    void update(float deltaTime);
    void applyToSkeleton();
    
    // Get bone transforms for GPU upload
    const std::vector<glm::mat4>& getSkinningMatrices() const { return skinningMatrices_; }
    const std::vector<glm::mat4>& getBoneTransforms() const { return boneTransforms_; }
    
    // Event callbacks
    using EventCallback = std::function<void(const std::string&)>;
    void setEventCallback(EventCallback callback) { eventCallback_ = callback; }
    
    // State machine integration
    void setStateMachine(std::shared_ptr<AnimationStateMachine> stateMachine);
    AnimationStateMachine* getStateMachine() { return stateMachine_.get(); }
    
    // IK targets (Inverse Kinematics)
    void setIKTarget(const std::string& boneName, const glm::vec3& worldPosition, float weight = 1.0f);
    void clearIKTargets();
    
private:
    std::shared_ptr<Skeleton> skeleton_;
    std::vector<AnimationClip*> activeClips_;
    std::vector<float> clipTimes_;
    std::vector<float> clipWeights_;
    
    std::vector<glm::mat4> boneTransforms_;
    std::vector<glm::mat4> skinningMatrices_;
    
    float playbackSpeed_ = 1.0f;
    bool playing_ = false;
    bool paused_ = false;
    
    EventCallback eventCallback_;
    std::shared_ptr<AnimationStateMachine> stateMachine_;
    
    // IK data
    struct IKTarget {
        uint32_t boneIndex;
        glm::vec3 targetPosition;
        float weight;
    };
    std::vector<IKTarget> ikTargets_;
    
    void sampleClip(const AnimationClip& clip, float time, std::vector<glm::mat4>& outTransforms);
    void blendPoses(const std::vector<glm::mat4>& a, const std::vector<glm::mat4>& b, 
                   float weight, std::vector<glm::mat4>& out);
    void solveIK();
};

// ============================================================================
// GPU SKINNING SYSTEM
// ============================================================================

class GPUSkinningSystem {
public:
    GPUSkinningSystem(VulkanContext& context);
    ~GPUSkinningSystem();
    
    // Initialize for a skinned mesh
    struct SkinningSetup {
        VkBuffer vertexBuffer;          // Original vertices
        VkBuffer skinnedBuffer;         // Output skinned vertices
        VkBuffer boneWeightBuffer;      // Bone indices and weights
        VkBuffer boneMatrixBuffer;      // Skinning matrices (updated per frame)
        uint32_t vertexCount;
        uint32_t vertexStride;
    };
    
    uint32_t registerMesh(const SkinningSetup& setup);
    void unregisterMesh(uint32_t handle);
    
    // Update bone matrices for an instance
    void updateBoneMatrices(uint32_t handle, const std::vector<glm::mat4>& matrices);
    
    // Dispatch skinning compute shader
    void dispatchSkinning(VkCommandBuffer cmd);
    
    // Get skinned vertex buffer for mesh rendering
    VkBuffer getSkinnedBuffer(uint32_t handle) const;
    
    // Integration with BLAS updates for ray tracing
    void markBLASForUpdate(uint32_t handle);
    const std::vector<uint32_t>& getPendingBLASUpdates() const { return pendingBLASUpdates_; }
    void clearBLASUpdates() { pendingBLASUpdates_.clear(); }
    
private:
    VulkanContext& context_;
    
    VkPipeline skinningPipeline_;
    VkPipelineLayout skinningLayout_;
    VkDescriptorSetLayout descriptorLayout_;
    VkDescriptorPool descriptorPool_;
    
    struct SkinningInstance {
        SkinningSetup setup;
        VkDescriptorSet descriptorSet;
        bool dirty = true;
    };
    std::vector<SkinningInstance> instances_;
    std::vector<uint32_t> freeSlots_;
    std::vector<uint32_t> pendingBLASUpdates_;
    
    void createPipeline();
};

// ============================================================================
// ANIMATION LIBRARY
// ============================================================================

class AnimationLibrary {
public:
    static AnimationLibrary& getInstance();
    
    // Load from glTF
    std::shared_ptr<Skeleton> loadSkeleton(const std::string& path);
    std::shared_ptr<AnimationClip> loadAnimation(const std::string& path, const std::string& animName = "");
    
    // Load all animations from a glTF file
    std::vector<std::shared_ptr<AnimationClip>> loadAllAnimations(const std::string& path);
    
    // Cache management
    std::shared_ptr<Skeleton> getSkeleton(const std::string& name);
    std::shared_ptr<AnimationClip> getAnimation(const std::string& name);
    
    void unloadSkeleton(const std::string& name);
    void unloadAnimation(const std::string& name);
    void clearCache();
    
private:
    AnimationLibrary() = default;
    
    std::unordered_map<std::string, std::shared_ptr<Skeleton>> skeletonCache_;
    std::unordered_map<std::string, std::shared_ptr<AnimationClip>> animationCache_;
};

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

// Quaternion/matrix interpolation
glm::quat slerpQuat(const glm::quat& a, const glm::quat& b, float t);
glm::mat4 interpolateTransform(const glm::mat4& a, const glm::mat4& b, float t);

// Two-bone IK solver
bool solveTwoBoneIK(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                    const glm::vec3& target, const glm::vec3& poleVector,
                    glm::quat& outRotA, glm::quat& outRotB);

// FABRIK solver for arbitrary chains
void solveFABRIK(std::vector<glm::vec3>& positions, const glm::vec3& target,
                 const std::vector<float>& boneLengths, uint32_t iterations = 10);

} // namespace Sanic
