/**
 * ProceduralAnimation.cpp
 * 
 * Implementation of procedural animation systems
 */

#include "ProceduralAnimation.h"
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <algorithm>
#include <cmath>

namespace Sanic {

// ============================================================================
// TWO-BONE IK SOLVER
// ============================================================================

TwoBoneIKSolver::Result TwoBoneIKSolver::solve(
    const glm::vec3& upperPos,
    const glm::vec3& midPos,
    const glm::vec3& lowerPos,
    const glm::vec3& targetPos,
    const glm::vec3& poleVector
) {
    Result result;
    
    // Calculate bone lengths
    float upperLength = glm::length(midPos - upperPos);
    float lowerLength = glm::length(lowerPos - midPos);
    float totalLength = upperLength + lowerLength;
    
    // Vector from upper to target
    glm::vec3 toTarget = targetPos - upperPos;
    float targetDist = glm::length(toTarget);
    
    // Check reachability
    if (targetDist > totalLength * 0.999f) {
        result.reachable = false;
        targetDist = totalLength * 0.999f;
    } else if (targetDist < std::abs(upperLength - lowerLength) * 1.001f) {
        result.reachable = false;
        targetDist = std::abs(upperLength - lowerLength) * 1.001f;
    }
    
    // Normalize direction to target
    glm::vec3 targetDir = glm::normalize(toTarget);
    
    // Law of cosines to find knee angle
    float cosKnee = (upperLength * upperLength + lowerLength * lowerLength - targetDist * targetDist) /
                    (2.0f * upperLength * lowerLength);
    cosKnee = glm::clamp(cosKnee, -1.0f, 1.0f);
    float kneeAngle = std::acos(cosKnee);
    
    // Find upper angle
    float cosUpper = (upperLength * upperLength + targetDist * targetDist - lowerLength * lowerLength) /
                     (2.0f * upperLength * targetDist);
    cosUpper = glm::clamp(cosUpper, -1.0f, 1.0f);
    float upperAngle = std::acos(cosUpper);
    
    // Calculate the plane normal using pole vector
    glm::vec3 ikPlaneNormal = glm::normalize(glm::cross(toTarget, poleVector));
    if (glm::length(ikPlaneNormal) < 0.001f) {
        // Target and pole are parallel, use a default plane
        ikPlaneNormal = glm::normalize(glm::cross(toTarget, glm::vec3(0, 0, 1)));
    }
    
    // Create rotation for upper bone
    // First, rotate to point at target
    glm::vec3 originalUpperDir = glm::normalize(midPos - upperPos);
    glm::quat toTargetRot = glm::rotation(originalUpperDir, targetDir);
    
    // Then, rotate by upper angle within the IK plane
    glm::quat upperBend = glm::angleAxis(-upperAngle, ikPlaneNormal);
    result.upperRotation = upperBend * toTargetRot;
    
    // Calculate lower bone rotation
    // The knee bends by (pi - kneeAngle) from straight
    float bendAngle = glm::pi<float>() - kneeAngle;
    result.lowerRotation = glm::angleAxis(-bendAngle, glm::vec3(1, 0, 0));  // Local X axis for knee
    
    return result;
}

// ============================================================================
// FOOT IK SYSTEM
// ============================================================================

void FootIKSystem::initialize(PhysicsSystem* physics) {
    physics_ = physics;
}

void FootIKSystem::addLeg(const std::string& name, const LegIKConfig& config) {
    LegData data;
    data.config = config;
    legs_[name] = data;
}

void FootIKSystem::update(
    Skeleton& skeleton,
    const glm::mat4& rootTransform,
    const glm::vec3& velocity,
    float deltaTime
) {
    if (!enabled_ || !physics_) return;
    
    for (auto& [name, leg] : legs_) {
        // Cache bone indices if not done
        if (leg.hipBoneIdx < 0) {
            leg.hipBoneIdx = skeleton.findBone(leg.config.hipBoneName);
            leg.kneeBoneIdx = skeleton.findBone(leg.config.kneeBoneName);
            leg.ankleBoneIdx = skeleton.findBone(leg.config.ankleBoneName);
            if (!leg.config.toeBoneName.empty()) {
                leg.toeBoneIdx = skeleton.findBone(leg.config.toeBoneName);
            }
            
            // Calculate bone lengths from bind pose
            if (leg.hipBoneIdx >= 0 && leg.kneeBoneIdx >= 0) {
                // Get bind pose positions
                glm::vec3 hipBind = skeleton.bones[leg.hipBoneIdx].localBindPose[3];
                glm::vec3 kneeBind = skeleton.bones[leg.kneeBoneIdx].localBindPose[3];
                glm::vec3 ankleBind = skeleton.bones[leg.ankleBoneIdx].localBindPose[3];
                
                leg.upperLength = glm::length(kneeBind - hipBind);
                leg.lowerLength = glm::length(ankleBind - kneeBind);
            }
        }
        
        if (leg.ankleBoneIdx < 0) continue;
        
        // Get current foot world position
        glm::mat4 footWorld = rootTransform * skeleton.bones[leg.ankleBoneIdx].globalTransform;
        glm::vec3 footPos = glm::vec3(footWorld[3]) + leg.config.footOffset;
        
        // Trace ground
        glm::vec3 hitPos, hitNormal;
        if (traceGround(footPos, leg.config, hitPos, hitNormal)) {
            leg.target.isGrounded = true;
            leg.target.groundNormal = hitNormal;
            leg.target.groundHeight = hitPos.y;
            
            // Target position is hit position plus offset
            glm::vec3 targetPos = hitPos + glm::vec3(0, leg.config.groundOffset, 0);
            
            // Smooth the target
            float smoothFactor = glm::clamp(deltaTime * 10.0f, 0.0f, 1.0f);
            leg.smoothedTarget = glm::mix(leg.smoothedTarget, targetPos, smoothFactor);
            leg.target.footTarget = leg.smoothedTarget;
            
            // Calculate ankle rotation to align with ground
            glm::vec3 footForward = glm::normalize(glm::vec3(footWorld[2]));
            glm::vec3 groundRight = glm::normalize(glm::cross(hitNormal, footForward));
            glm::vec3 groundForward = glm::normalize(glm::cross(groundRight, hitNormal));
            
            glm::mat3 groundRot(groundRight, hitNormal, groundForward);
            leg.target.ankleRotation = glm::quat_cast(groundRot);
            
        } else {
            leg.target.isGrounded = false;
            leg.target.footTarget = footPos;
        }
        
        leg.target.ikWeight = ikWeight_;
    }
    
    updatePelvisOffset();
}

void FootIKSystem::applyIK(Skeleton& skeleton) {
    if (!enabled_) return;
    
    for (const auto& [name, leg] : legs_) {
        if (leg.hipBoneIdx < 0 || leg.target.ikWeight <= 0.0f) continue;
        
        // Get bone positions
        glm::vec3 hipPos = glm::vec3(skeleton.bones[leg.hipBoneIdx].globalTransform[3]);
        glm::vec3 kneePos = glm::vec3(skeleton.bones[leg.kneeBoneIdx].globalTransform[3]);
        glm::vec3 anklePos = glm::vec3(skeleton.bones[leg.ankleBoneIdx].globalTransform[3]);
        
        // Solve IK
        auto result = TwoBoneIKSolver::solve(
            hipPos, kneePos, anklePos,
            leg.target.footTarget,
            leg.config.poleVector
        );
        
        // Blend rotations with IK weight
        float weight = leg.target.ikWeight;
        
        if (weight > 0.0f) {
            // Apply hip rotation
            glm::quat currentHipRot = glm::quat_cast(skeleton.bones[leg.hipBoneIdx].localTransform);
            glm::quat blendedHipRot = glm::slerp(currentHipRot, 
                                                  currentHipRot * result.upperRotation, weight);
            skeleton.bones[leg.hipBoneIdx].localTransform = glm::mat4_cast(blendedHipRot) * 
                glm::translate(glm::mat4(1.0f), glm::vec3(skeleton.bones[leg.hipBoneIdx].localTransform[3]));
            
            // Apply knee rotation
            glm::quat currentKneeRot = glm::quat_cast(skeleton.bones[leg.kneeBoneIdx].localTransform);
            glm::quat blendedKneeRot = glm::slerp(currentKneeRot,
                                                   currentKneeRot * result.lowerRotation, weight);
            skeleton.bones[leg.kneeBoneIdx].localTransform = glm::mat4_cast(blendedKneeRot) *
                glm::translate(glm::mat4(1.0f), glm::vec3(skeleton.bones[leg.kneeBoneIdx].localTransform[3]));
            
            // Apply ankle rotation for ground alignment
            if (leg.target.isGrounded) {
                glm::quat currentAnkleRot = glm::quat_cast(skeleton.bones[leg.ankleBoneIdx].localTransform);
                glm::quat blendedAnkleRot = glm::slerp(currentAnkleRot, leg.target.ankleRotation, weight);
                skeleton.bones[leg.ankleBoneIdx].localTransform = glm::mat4_cast(blendedAnkleRot) *
                    glm::translate(glm::mat4(1.0f), glm::vec3(skeleton.bones[leg.ankleBoneIdx].localTransform[3]));
            }
        }
    }
}

const FootIKTarget* FootIKSystem::getFootTarget(const std::string& legName) const {
    auto it = legs_.find(legName);
    return it != legs_.end() ? &it->second.target : nullptr;
}

bool FootIKSystem::traceGround(
    const glm::vec3& footPos,
    const LegIKConfig& config,
    glm::vec3& outHitPos,
    glm::vec3& outNormal
) {
    if (!physics_) return false;
    
    // Trace from above foot downward
    glm::vec3 start = footPos + glm::vec3(0, config.traceUpDistance, 0);
    glm::vec3 end = footPos - glm::vec3(0, config.traceDownDistance, 0);
    
    // Use physics raycast
    auto& bodyInterface = physics_->getBodyInterface();
    // Note: Actual raycast implementation would use Jolt's raycast API
    // For now, we'll simulate a ground hit at y=0
    
    float groundY = 0.0f;  // Placeholder - actual implementation would raycast
    
    if (footPos.y - config.traceDownDistance < groundY && 
        footPos.y + config.traceUpDistance > groundY) {
        outHitPos = glm::vec3(footPos.x, groundY, footPos.z);
        outNormal = glm::vec3(0, 1, 0);
        return true;
    }
    
    return false;
}

void FootIKSystem::updatePelvisOffset() {
    if (legs_.empty()) {
        pelvisOffset_ = 0.0f;
        return;
    }
    
    // Find the lowest foot target relative to default position
    float lowestOffset = 0.0f;
    
    for (const auto& [name, leg] : legs_) {
        if (leg.target.isGrounded) {
            float offset = leg.target.groundHeight - leg.target.footTarget.y;
            lowestOffset = std::min(lowestOffset, offset);
        }
    }
    
    // Pelvis needs to drop to accommodate the lowest foot
    pelvisOffset_ = lowestOffset;
}

// ============================================================================
// STRIDE WARPING SYSTEM
// ============================================================================

void StrideWarpingSystem::update(float groundSpeed, float animSpeed, float deltaTime) {
    if (!enabled_ || animSpeed <= 0.0f) return;
    
    // Calculate target speed ratio
    targetSpeedRatio_ = groundSpeed / animSpeed;
    
    // Clamp to valid range
    targetSpeedRatio_ = glm::clamp(targetSpeedRatio_, settings_.minSpeedRatio, settings_.maxSpeedRatio);
    
    // Smooth transition
    float blendFactor = glm::clamp(deltaTime / settings_.blendTime, 0.0f, 1.0f);
    currentSpeedRatio_ = glm::mix(currentSpeedRatio_, targetSpeedRatio_, blendFactor);
}

void StrideWarpingSystem::apply(Skeleton& skeleton) {
    if (!enabled_) return;
    
    // Find pelvis bone
    uint32_t pelvisIdx = skeleton.findBone("pelvis");
    if (pelvisIdx == UINT32_MAX) return;
    
    // Store original position on first call
    if (originalPelvisPos_ == glm::vec3(0)) {
        originalPelvisPos_ = glm::vec3(skeleton.bones[pelvisIdx].localTransform[3]);
    }
    
    // Scale pelvis forward/back movement
    glm::vec3 currentPos = glm::vec3(skeleton.bones[pelvisIdx].localTransform[3]);
    glm::vec3 offset = currentPos - originalPelvisPos_;
    
    // Scale the forward component (Z)
    if (settings_.adjustPelvisForward) {
        offset.z *= currentSpeedRatio_ * settings_.pelvisForwardScale;
    }
    
    pelvisOffset_ = offset;
    
    // Apply offset
    skeleton.bones[pelvisIdx].localTransform[3] = glm::vec4(originalPelvisPos_ + offset, 1.0f);
}

// ============================================================================
// BANKING SYSTEM
// ============================================================================

void BankingSystem::update(float angularVelocity, float deltaTime) {
    if (!enabled_) return;
    
    // Calculate target bank angle based on angular velocity
    targetBankAngle_ = angularVelocity * settings_.bankFactor;
    
    // Clamp to maximum
    targetBankAngle_ = glm::clamp(targetBankAngle_, -settings_.maxBankAngle, settings_.maxBankAngle);
    
    // Smooth blend
    float blendFactor = glm::clamp(deltaTime * settings_.blendSpeed, 0.0f, 1.0f);
    currentBankAngle_ = glm::mix(currentBankAngle_, targetBankAngle_, blendFactor);
}

void BankingSystem::apply(Skeleton& skeleton, const std::vector<std::string>& spineBoneNames) {
    if (!enabled_ || std::abs(currentBankAngle_) < 0.001f) return;
    
    // Apply bank angle distributed across spine bones
    for (size_t i = 0; i < spineBoneNames.size() && i < settings_.spineDistribution.size(); ++i) {
        uint32_t boneIdx = skeleton.findBone(spineBoneNames[i]);
        if (boneIdx == UINT32_MAX) continue;
        
        float boneAngle = glm::radians(currentBankAngle_) * settings_.spineDistribution[i];
        
        // Bank is rotation around forward axis (Z)
        glm::quat bankRot = glm::angleAxis(boneAngle, glm::vec3(0, 0, 1));
        
        // Apply to bone
        glm::quat currentRot = glm::quat_cast(skeleton.bones[boneIdx].localTransform);
        glm::quat newRot = currentRot * bankRot;
        
        glm::vec3 pos = glm::vec3(skeleton.bones[boneIdx].localTransform[3]);
        skeleton.bones[boneIdx].localTransform = glm::mat4_cast(newRot);
        skeleton.bones[boneIdx].localTransform[3] = glm::vec4(pos, 1.0f);
    }
    
    // Counter-bank head if enabled
    if (settings_.counterBankHead) {
        uint32_t headIdx = skeleton.findBone("head");
        if (headIdx != UINT32_MAX) {
            float counterAngle = -glm::radians(currentBankAngle_) * settings_.headCounterFactor;
            glm::quat counterRot = glm::angleAxis(counterAngle, glm::vec3(0, 0, 1));
            
            glm::quat currentRot = glm::quat_cast(skeleton.bones[headIdx].localTransform);
            glm::quat newRot = currentRot * counterRot;
            
            glm::vec3 pos = glm::vec3(skeleton.bones[headIdx].localTransform[3]);
            skeleton.bones[headIdx].localTransform = glm::mat4_cast(newRot);
            skeleton.bones[headIdx].localTransform[3] = glm::vec4(pos, 1.0f);
        }
    }
}

// ============================================================================
// ORIENTATION WARPING SYSTEM
// ============================================================================

void OrientationWarpingSystem::update(float movementAngle, float speed, float deltaTime) {
    if (!enabled_) return;
    
    // Scale warp based on speed
    float speedFactor = glm::clamp(
        (speed - settings_.minSpeedForWarp) / (settings_.fullWarpSpeed - settings_.minSpeedForWarp),
        0.0f, 1.0f
    );
    
    // Calculate target warp angle
    targetWarpAngle_ = movementAngle * speedFactor;
    
    // Clamp to maximum
    float maxRad = glm::radians(settings_.maxWarpAngle);
    targetWarpAngle_ = glm::clamp(targetWarpAngle_, -maxRad, maxRad);
    
    // Smooth blend
    float blendFactor = glm::clamp(deltaTime * settings_.blendSpeed, 0.0f, 1.0f);
    currentWarpAngle_ = glm::mix(currentWarpAngle_, targetWarpAngle_, blendFactor);
}

void OrientationWarpingSystem::apply(Skeleton& skeleton, const std::vector<std::string>& spineBoneNames) {
    if (!enabled_ || std::abs(currentWarpAngle_) < 0.001f) return;
    
    // Distribute rotation across spine
    for (size_t i = 0; i < spineBoneNames.size() && i < settings_.spineDistribution.size(); ++i) {
        uint32_t boneIdx = skeleton.findBone(spineBoneNames[i]);
        if (boneIdx == UINT32_MAX) continue;
        
        float boneAngle = currentWarpAngle_ * settings_.spineDistribution[i];
        
        // Orientation is rotation around up axis (Y)
        glm::quat orientRot = glm::angleAxis(boneAngle, glm::vec3(0, 1, 0));
        
        // Apply to bone
        glm::quat currentRot = glm::quat_cast(skeleton.bones[boneIdx].localTransform);
        glm::quat newRot = currentRot * orientRot;
        
        glm::vec3 pos = glm::vec3(skeleton.bones[boneIdx].localTransform[3]);
        skeleton.bones[boneIdx].localTransform = glm::mat4_cast(newRot);
        skeleton.bones[boneIdx].localTransform[3] = glm::vec4(pos, 1.0f);
    }
}

// ============================================================================
// SURFACE ALIGNMENT SYSTEM
// ============================================================================

void SurfaceAlignmentSystem::initialize(PhysicsSystem* physics) {
    physics_ = physics;
}

void SurfaceAlignmentSystem::update(const glm::vec3& position, const glm::vec3& currentUp, float deltaTime) {
    if (!enabled_ || !physics_) return;
    
    // Trace down to find ground normal
    glm::vec3 start = position + glm::vec3(0, 0.5f, 0);
    glm::vec3 end = position - glm::vec3(0, settings_.traceDistance, 0);
    
    // Placeholder for actual physics raycast
    // In real implementation, this would query the physics system
    glm::vec3 hitNormal = glm::vec3(0, 1, 0);
    bool hit = true;  // Assume we hit ground
    
    if (hit) {
        // Check slope angle
        float slopeAngle = glm::degrees(std::acos(glm::dot(hitNormal, glm::vec3(0, 1, 0))));
        
        if (slopeAngle <= settings_.maxSlopeAngle) {
            surfaceNormal_ = hitNormal;
            
            // Calculate target rotation to align with surface
            targetRotation_ = glm::rotation(glm::vec3(0, 1, 0), surfaceNormal_);
        }
    }
    
    // Smooth blend
    float blendFactor = glm::clamp(deltaTime * settings_.blendSpeed, 0.0f, 1.0f);
    currentRotation_ = glm::slerp(currentRotation_, targetRotation_, blendFactor);
}

// ============================================================================
// PROCEDURAL ANIMATION CONTROLLER
// ============================================================================

ProceduralAnimationController::ProceduralAnimationController() = default;

void ProceduralAnimationController::initialize(PhysicsSystem* physics) {
    footIK_.initialize(physics);
    surfaceAlignment_.initialize(physics);
    initialized_ = true;
}

void ProceduralAnimationController::setupCharacter(const CharacterConfig& config) {
    config_ = config;
    
    // Setup foot IK for each leg
    for (const auto& [name, legConfig] : config.legs) {
        footIK_.addLeg(name, legConfig);
    }
}

void ProceduralAnimationController::update(
    Skeleton& skeleton,
    const glm::mat4& rootTransform,
    const glm::vec3& velocity,
    float angularVelocity,
    float movementAngle,
    float deltaTime
) {
    if (!initialized_) return;
    
    float speed = glm::length(glm::vec2(velocity.x, velocity.z));
    
    // Update all systems
    footIK_.update(skeleton, rootTransform, velocity, deltaTime);
    
    // Determine animation speed based on current locomotion state
    float animSpeed = speed < 3.0f ? config_.walkAnimSpeed : config_.runAnimSpeed;
    strideWarping_.update(speed, animSpeed, deltaTime);
    
    banking_.update(angularVelocity, deltaTime);
    orientationWarping_.update(movementAngle, speed, deltaTime);
    
    glm::vec3 position = glm::vec3(rootTransform[3]);
    surfaceAlignment_.update(position, glm::vec3(0, 1, 0), deltaTime);
}

void ProceduralAnimationController::apply(Skeleton& skeleton) {
    if (!initialized_) return;
    
    // Apply in order
    surfaceAlignment_.getRootRotation();  // Would apply to root
    footIK_.applyIK(skeleton);
    strideWarping_.apply(skeleton);
    banking_.apply(skeleton, config_.spineBones);
    orientationWarping_.apply(skeleton, config_.spineBones);
}

void ProceduralAnimationController::setAllEnabled(bool enabled) {
    footIK_.setEnabled(enabled);
    strideWarping_.setEnabled(enabled);
    banking_.setEnabled(enabled);
    orientationWarping_.setEnabled(enabled);
    surfaceAlignment_.setEnabled(enabled);
}

} // namespace Sanic
