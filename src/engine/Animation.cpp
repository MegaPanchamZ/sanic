/**
 * Animation.cpp
 * 
 * Implementation of the skeletal animation system.
 */

#include "Animation.h"
#include "VulkanContext.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>

// For glTF loading
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_INCLUDE_STB_IMAGE
#include "../../external/stb_image.h"

namespace Sanic {

// ============================================================================
// ANIMATION STATE MACHINE
// ============================================================================

void AnimationStateMachine::addState(const std::string& name, std::shared_ptr<AnimationState> state) {
    states_[name] = state;
    if (currentStateName_.empty()) {
        currentStateName_ = name;
    }
}

void AnimationStateMachine::addTransition(const std::string& from, const std::string& to,
                                          std::function<bool()> condition, float transitionTime) {
    auto it = states_.find(from);
    if (it != states_.end()) {
        AnimationState::Transition transition;
        transition.targetState = to;
        transition.condition = condition;
        transition.transitionTime = transitionTime;
        it->second->transitions.push_back(transition);
    }
}

void AnimationStateMachine::setDefaultState(const std::string& name) {
    if (states_.find(name) != states_.end()) {
        currentStateName_ = name;
        stateTime_ = 0.0f;
    }
}

void AnimationStateMachine::update(float deltaTime) {
    if (currentStateName_.empty()) return;
    
    auto currentState = states_.find(currentStateName_);
    if (currentState == states_.end()) return;
    
    // Update transition
    if (inTransition_) {
        transitionProgress_ += deltaTime / transitionTime_;
        if (transitionProgress_ >= 1.0f) {
            inTransition_ = false;
            transitionProgress_ = 0.0f;
            if (currentState->second->onEnter) {
                currentState->second->onEnter();
            }
        }
    }
    
    stateTime_ += deltaTime;
    
    // Update state
    if (currentState->second->onUpdate) {
        currentState->second->onUpdate(deltaTime);
    }
    
    // Check transitions
    for (const auto& transition : currentState->second->transitions) {
        bool shouldTransition = false;
        
        if (transition.condition && transition.condition()) {
            shouldTransition = true;
        }
        
        if (transition.hasExitTime && stateTime_ >= transition.exitTime) {
            shouldTransition = true;
        }
        
        if (shouldTransition) {
            // Start transition
            previousStateName_ = currentStateName_;
            currentStateName_ = transition.targetState;
            
            if (currentState->second->onExit) {
                currentState->second->onExit();
            }
            
            inTransition_ = true;
            transitionTime_ = transition.transitionTime;
            transitionProgress_ = 0.0f;
            stateTime_ = 0.0f;
            break;
        }
    }
    
    // Clear triggers
    triggers_.clear();
}

void AnimationStateMachine::forceState(const std::string& name) {
    if (states_.find(name) == states_.end()) return;
    
    auto currentState = states_.find(currentStateName_);
    if (currentState != states_.end() && currentState->second->onExit) {
        currentState->second->onExit();
    }
    
    previousStateName_ = currentStateName_;
    currentStateName_ = name;
    stateTime_ = 0.0f;
    inTransition_ = false;
    
    auto newState = states_.find(name);
    if (newState != states_.end() && newState->second->onEnter) {
        newState->second->onEnter();
    }
}

void AnimationStateMachine::setFloat(const std::string& name, float value) {
    floatParams_[name] = value;
}

void AnimationStateMachine::setBool(const std::string& name, bool value) {
    boolParams_[name] = value;
}

void AnimationStateMachine::setTrigger(const std::string& name) {
    triggers_[name] = true;
}

float AnimationStateMachine::getFloat(const std::string& name) const {
    auto it = floatParams_.find(name);
    return it != floatParams_.end() ? it->second : 0.0f;
}

bool AnimationStateMachine::getBool(const std::string& name) const {
    auto it = boolParams_.find(name);
    return it != boolParams_.end() ? it->second : false;
}

// ============================================================================
// ANIMATION INSTANCE
// ============================================================================

AnimationInstance::AnimationInstance(std::shared_ptr<Skeleton> skeleton)
    : skeleton_(skeleton) {
    
    if (skeleton_) {
        size_t boneCount = skeleton_->bones.size();
        boneTransforms_.resize(boneCount, glm::mat4(1.0f));
        skinningMatrices_.resize(boneCount, glm::mat4(1.0f));
    }
}

void AnimationInstance::play(const std::string& clipName, float blendTime) {
    auto& library = AnimationLibrary::getInstance();
    auto clip = library.getAnimation(clipName);
    
    if (clip) {
        if (blendTime > 0.0f && !activeClips_.empty()) {
            crossfade(clipName, blendTime);
        } else {
            activeClips_.clear();
            clipTimes_.clear();
            clipWeights_.clear();
            
            activeClips_.push_back(clip.get());
            clipTimes_.push_back(0.0f);
            clipWeights_.push_back(1.0f);
        }
        playing_ = true;
        paused_ = false;
    }
}

void AnimationInstance::stop(float blendTime) {
    if (blendTime > 0.0f) {
        // Fade out current animations
        for (auto& weight : clipWeights_) {
            weight = 0.0f;  // Will be handled in update
        }
    } else {
        activeClips_.clear();
        clipTimes_.clear();
        clipWeights_.clear();
    }
    playing_ = false;
}

void AnimationInstance::pause() {
    paused_ = true;
}

void AnimationInstance::resume() {
    paused_ = false;
}

void AnimationInstance::setTime(float time) {
    if (!clipTimes_.empty()) {
        clipTimes_[0] = time;
    }
}

void AnimationInstance::crossfade(const std::string& clipName, float duration) {
    auto& library = AnimationLibrary::getInstance();
    auto newClip = library.getAnimation(clipName);
    
    if (newClip) {
        activeClips_.push_back(newClip.get());
        clipTimes_.push_back(0.0f);
        clipWeights_.push_back(0.0f);
        
        // Store crossfade info - weight will be updated in update()
    }
}

void AnimationInstance::setLayerWeight(uint32_t layer, float weight) {
    if (layer < clipWeights_.size()) {
        clipWeights_[layer] = weight;
    }
}

void AnimationInstance::addAdditiveLayer(const std::string& clipName, float weight) {
    auto& library = AnimationLibrary::getInstance();
    auto clip = library.getAnimation(clipName);
    
    if (clip) {
        activeClips_.push_back(clip.get());
        clipTimes_.push_back(0.0f);
        clipWeights_.push_back(weight);
    }
}

void AnimationInstance::update(float deltaTime) {
    if (!playing_ || paused_ || !skeleton_) return;
    
    // Update state machine if present
    if (stateMachine_) {
        stateMachine_->update(deltaTime);
    }
    
    // Update clip times
    for (size_t i = 0; i < activeClips_.size(); ++i) {
        clipTimes_[i] += deltaTime * playbackSpeed_;
        
        if (activeClips_[i]->looping) {
            while (clipTimes_[i] >= activeClips_[i]->duration) {
                clipTimes_[i] -= activeClips_[i]->duration;
            }
        } else {
            clipTimes_[i] = std::min(clipTimes_[i], activeClips_[i]->duration);
        }
        
        // Check events
        if (eventCallback_) {
            for (const auto& event : activeClips_[i]->events) {
                float prevTime = clipTimes_[i] - deltaTime * playbackSpeed_;
                if (prevTime < event.time && clipTimes_[i] >= event.time) {
                    eventCallback_(event.name);
                }
            }
        }
    }
    
    applyToSkeleton();
}

void AnimationInstance::applyToSkeleton() {
    if (!skeleton_ || activeClips_.empty()) return;
    
    // Sample first clip as base
    std::vector<glm::mat4> basePose(skeleton_->bones.size());
    sampleClip(*activeClips_[0], clipTimes_[0], basePose);
    
    // Blend additional clips
    for (size_t i = 1; i < activeClips_.size(); ++i) {
        std::vector<glm::mat4> clipPose(skeleton_->bones.size());
        sampleClip(*activeClips_[i], clipTimes_[i], clipPose);
        blendPoses(basePose, clipPose, clipWeights_[i], basePose);
    }
    
    // Apply local transforms to skeleton
    for (size_t i = 0; i < skeleton_->bones.size(); ++i) {
        skeleton_->bones[i].localTransform = basePose[i];
    }
    
    // Calculate global transforms in hierarchy order
    for (uint32_t boneIdx : skeleton_->hierarchyOrder) {
        Bone& bone = skeleton_->bones[boneIdx];
        if (bone.parentIndex >= 0) {
            bone.globalTransform = skeleton_->bones[bone.parentIndex].globalTransform * bone.localTransform;
        } else {
            bone.globalTransform = bone.localTransform;
        }
    }
    
    // Apply IK
    if (!ikTargets_.empty()) {
        solveIK();
    }
    
    // Calculate skinning matrices
    for (size_t i = 0; i < skeleton_->bones.size(); ++i) {
        skinningMatrices_[i] = skeleton_->bones[i].globalTransform * skeleton_->bones[i].inverseBindMatrix;
        boneTransforms_[i] = skeleton_->bones[i].globalTransform;
    }
}

void AnimationInstance::sampleClip(const AnimationClip& clip, float time, 
                                   std::vector<glm::mat4>& outTransforms) {
    // Initialize with identity
    for (auto& t : outTransforms) {
        t = glm::mat4(1.0f);
    }
    
    for (const auto& channel : clip.channels) {
        if (channel.boneIndex >= outTransforms.size()) continue;
        
        glm::vec3 position(0.0f);
        glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec3 scale(1.0f);
        
        // Sample position
        if (!channel.positionKeys.empty()) {
            if (channel.positionKeys.size() == 1) {
                position = channel.positionKeys[0].value;
            } else {
                // Find keyframes
                size_t nextIdx = 0;
                for (size_t i = 0; i < channel.positionKeys.size() - 1; ++i) {
                    if (time < channel.positionKeys[i + 1].time) {
                        nextIdx = i + 1;
                        break;
                    }
                    nextIdx = i + 1;
                }
                
                size_t prevIdx = nextIdx > 0 ? nextIdx - 1 : 0;
                float t = 0.0f;
                
                float dt = channel.positionKeys[nextIdx].time - channel.positionKeys[prevIdx].time;
                if (dt > 0.0f) {
                    t = (time - channel.positionKeys[prevIdx].time) / dt;
                }
                
                position = glm::mix(channel.positionKeys[prevIdx].value, 
                                   channel.positionKeys[nextIdx].value, t);
            }
        }
        
        // Sample rotation
        if (!channel.rotationKeys.empty()) {
            if (channel.rotationKeys.size() == 1) {
                rotation = channel.rotationKeys[0].value;
            } else {
                size_t nextIdx = 0;
                for (size_t i = 0; i < channel.rotationKeys.size() - 1; ++i) {
                    if (time < channel.rotationKeys[i + 1].time) {
                        nextIdx = i + 1;
                        break;
                    }
                    nextIdx = i + 1;
                }
                
                size_t prevIdx = nextIdx > 0 ? nextIdx - 1 : 0;
                float t = 0.0f;
                
                float dt = channel.rotationKeys[nextIdx].time - channel.rotationKeys[prevIdx].time;
                if (dt > 0.0f) {
                    t = (time - channel.rotationKeys[prevIdx].time) / dt;
                }
                
                rotation = slerpQuat(channel.rotationKeys[prevIdx].value,
                                    channel.rotationKeys[nextIdx].value, t);
            }
        }
        
        // Sample scale
        if (!channel.scaleKeys.empty()) {
            if (channel.scaleKeys.size() == 1) {
                scale = channel.scaleKeys[0].value;
            } else {
                size_t nextIdx = 0;
                for (size_t i = 0; i < channel.scaleKeys.size() - 1; ++i) {
                    if (time < channel.scaleKeys[i + 1].time) {
                        nextIdx = i + 1;
                        break;
                    }
                    nextIdx = i + 1;
                }
                
                size_t prevIdx = nextIdx > 0 ? nextIdx - 1 : 0;
                float t = 0.0f;
                
                float dt = channel.scaleKeys[nextIdx].time - channel.scaleKeys[prevIdx].time;
                if (dt > 0.0f) {
                    t = (time - channel.scaleKeys[prevIdx].time) / dt;
                }
                
                scale = glm::mix(channel.scaleKeys[prevIdx].value,
                                channel.scaleKeys[nextIdx].value, t);
            }
        }
        
        // Compose transform
        glm::mat4 T = glm::translate(glm::mat4(1.0f), position);
        glm::mat4 R = glm::mat4_cast(rotation);
        glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);
        
        outTransforms[channel.boneIndex] = T * R * S;
    }
}

void AnimationInstance::blendPoses(const std::vector<glm::mat4>& a, const std::vector<glm::mat4>& b,
                                   float weight, std::vector<glm::mat4>& out) {
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
        out[i] = interpolateTransform(a[i], b[i], weight);
    }
}

void AnimationInstance::setIKTarget(const std::string& boneName, const glm::vec3& worldPosition, float weight) {
    if (!skeleton_) return;
    
    uint32_t boneIdx = skeleton_->findBone(boneName);
    if (boneIdx != UINT32_MAX) {
        ikTargets_.push_back({boneIdx, worldPosition, weight});
    }
}

void AnimationInstance::clearIKTargets() {
    ikTargets_.clear();
}

void AnimationInstance::solveIK() {
    // Simple two-bone IK for each target
    for (const auto& target : ikTargets_) {
        // Find bone chain (end effector -> parent -> grandparent)
        if (target.boneIndex >= skeleton_->bones.size()) continue;
        
        Bone& endBone = skeleton_->bones[target.boneIndex];
        if (endBone.parentIndex < 0) continue;
        
        Bone& midBone = skeleton_->bones[endBone.parentIndex];
        if (midBone.parentIndex < 0) continue;
        
        Bone& startBone = skeleton_->bones[midBone.parentIndex];
        
        glm::vec3 a = glm::vec3(startBone.globalTransform[3]);
        glm::vec3 b = glm::vec3(midBone.globalTransform[3]);
        glm::vec3 c = glm::vec3(endBone.globalTransform[3]);
        
        glm::vec3 targetPos = glm::mix(c, target.targetPosition, target.weight);
        
        // Solve
        glm::quat rotA, rotB;
        glm::vec3 poleVector = glm::vec3(0.0f, 0.0f, 1.0f);  // Default pole
        
        if (solveTwoBoneIK(a, b, c, targetPos, poleVector, rotA, rotB)) {
            // Apply rotations
            startBone.localTransform = glm::mat4_cast(rotA) * startBone.localTransform;
            midBone.localTransform = glm::mat4_cast(rotB) * midBone.localTransform;
            
            // Recalculate global transforms
            midBone.globalTransform = startBone.globalTransform * midBone.localTransform;
            endBone.globalTransform = midBone.globalTransform * endBone.localTransform;
        }
    }
}

void AnimationInstance::setStateMachine(std::shared_ptr<AnimationStateMachine> stateMachine) {
    stateMachine_ = stateMachine;
}

// ============================================================================
// GPU SKINNING SYSTEM
// ============================================================================

GPUSkinningSystem::GPUSkinningSystem(VulkanContext& context)
    : context_(context) {
    createPipeline();
}

GPUSkinningSystem::~GPUSkinningSystem() {
    VkDevice device = context_.getDevice();
    
    if (skinningPipeline_) vkDestroyPipeline(device, skinningPipeline_, nullptr);
    if (skinningLayout_) vkDestroyPipelineLayout(device, skinningLayout_, nullptr);
    if (descriptorLayout_) vkDestroyDescriptorSetLayout(device, descriptorLayout_, nullptr);
    if (descriptorPool_) vkDestroyDescriptorPool(device, descriptorPool_, nullptr);
}

void GPUSkinningSystem::createPipeline() {
    VkDevice device = context_.getDevice();
    
    // Create descriptor set layout
    std::array<VkDescriptorSetLayoutBinding, 4> bindings = {};
    
    // Input vertices
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    // Output vertices
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    // Bone weights
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    // Bone matrices
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    
    vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorLayout_);
    
    // Create pipeline layout
    VkPushConstantRange pushConstant = {};
    pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstant.offset = 0;
    pushConstant.size = sizeof(uint32_t) * 2;  // vertexCount, vertexStride
    
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorLayout_;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstant;
    
    vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &skinningLayout_);
    
    // Create descriptor pool
    std::array<VkDescriptorPoolSize, 1> poolSizes = {};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[0].descriptorCount = 256;  // 64 meshes * 4 bindings
    
    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 64;
    
    vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool_);
    
    // Pipeline will be created when shader is loaded
    // For now, leave as null - will be set up by renderer
}

uint32_t GPUSkinningSystem::registerMesh(const SkinningSetup& setup) {
    uint32_t handle;
    
    if (!freeSlots_.empty()) {
        handle = freeSlots_.back();
        freeSlots_.pop_back();
        instances_[handle] = {setup, VK_NULL_HANDLE, true};
    } else {
        handle = static_cast<uint32_t>(instances_.size());
        instances_.push_back({setup, VK_NULL_HANDLE, true});
    }
    
    // Allocate descriptor set
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorLayout_;
    
    vkAllocateDescriptorSets(context_.getDevice(), &allocInfo, &instances_[handle].descriptorSet);
    
    // Update descriptor set
    std::array<VkDescriptorBufferInfo, 4> bufferInfos = {};
    bufferInfos[0] = {setup.vertexBuffer, 0, VK_WHOLE_SIZE};
    bufferInfos[1] = {setup.skinnedBuffer, 0, VK_WHOLE_SIZE};
    bufferInfos[2] = {setup.boneWeightBuffer, 0, VK_WHOLE_SIZE};
    bufferInfos[3] = {setup.boneMatrixBuffer, 0, VK_WHOLE_SIZE};
    
    std::array<VkWriteDescriptorSet, 4> writes = {};
    for (size_t i = 0; i < 4; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = instances_[handle].descriptorSet;
        writes[i].dstBinding = static_cast<uint32_t>(i);
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = &bufferInfos[i];
    }
    
    vkUpdateDescriptorSets(context_.getDevice(), static_cast<uint32_t>(writes.size()), 
                          writes.data(), 0, nullptr);
    
    return handle;
}

void GPUSkinningSystem::unregisterMesh(uint32_t handle) {
    if (handle < instances_.size()) {
        freeSlots_.push_back(handle);
    }
}

void GPUSkinningSystem::updateBoneMatrices(uint32_t handle, const std::vector<glm::mat4>& matrices) {
    if (handle >= instances_.size()) return;
    
    instances_[handle].dirty = true;
    
    // Map and copy matrices to GPU buffer
    // This would be done through VMA or direct memory mapping
    // For now, mark for update
}

void GPUSkinningSystem::dispatchSkinning(VkCommandBuffer cmd) {
    if (!skinningPipeline_) return;
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, skinningPipeline_);
    
    for (size_t i = 0; i < instances_.size(); ++i) {
        if (!instances_[i].dirty) continue;
        
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, skinningLayout_,
                               0, 1, &instances_[i].descriptorSet, 0, nullptr);
        
        uint32_t pushData[2] = {instances_[i].setup.vertexCount, instances_[i].setup.vertexStride};
        vkCmdPushConstants(cmd, skinningLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pushData), pushData);
        
        uint32_t groupCount = (instances_[i].setup.vertexCount + 63) / 64;
        vkCmdDispatch(cmd, groupCount, 1, 1);
        
        instances_[i].dirty = false;
        pendingBLASUpdates_.push_back(static_cast<uint32_t>(i));
    }
}

VkBuffer GPUSkinningSystem::getSkinnedBuffer(uint32_t handle) const {
    if (handle < instances_.size()) {
        return instances_[handle].setup.skinnedBuffer;
    }
    return VK_NULL_HANDLE;
}

void GPUSkinningSystem::markBLASForUpdate(uint32_t handle) {
    pendingBLASUpdates_.push_back(handle);
}

// ============================================================================
// ANIMATION LIBRARY
// ============================================================================

AnimationLibrary& AnimationLibrary::getInstance() {
    static AnimationLibrary instance;
    return instance;
}

std::shared_ptr<Skeleton> AnimationLibrary::loadSkeleton(const std::string& path) {
    // Check cache first
    auto it = skeletonCache_.find(path);
    if (it != skeletonCache_.end()) {
        return it->second;
    }
    
    // TODO: Implement glTF loading for skeleton
    // For now, create empty skeleton
    auto skeleton = std::make_shared<Skeleton>();
    skeleton->name = path;
    
    skeletonCache_[path] = skeleton;
    return skeleton;
}

std::shared_ptr<AnimationClip> AnimationLibrary::loadAnimation(const std::string& path, 
                                                                const std::string& animName) {
    std::string key = path + ":" + animName;
    
    auto it = animationCache_.find(key);
    if (it != animationCache_.end()) {
        return it->second;
    }
    
    // TODO: Implement glTF loading for animations
    auto clip = std::make_shared<AnimationClip>();
    clip->name = animName.empty() ? path : animName;
    clip->duration = 1.0f;
    
    animationCache_[key] = clip;
    return clip;
}

std::vector<std::shared_ptr<AnimationClip>> AnimationLibrary::loadAllAnimations(const std::string& path) {
    std::vector<std::shared_ptr<AnimationClip>> clips;
    // TODO: Load all animations from glTF file
    return clips;
}

std::shared_ptr<Skeleton> AnimationLibrary::getSkeleton(const std::string& name) {
    auto it = skeletonCache_.find(name);
    return it != skeletonCache_.end() ? it->second : nullptr;
}

std::shared_ptr<AnimationClip> AnimationLibrary::getAnimation(const std::string& name) {
    auto it = animationCache_.find(name);
    return it != animationCache_.end() ? it->second : nullptr;
}

void AnimationLibrary::unloadSkeleton(const std::string& name) {
    skeletonCache_.erase(name);
}

void AnimationLibrary::unloadAnimation(const std::string& name) {
    animationCache_.erase(name);
}

void AnimationLibrary::clearCache() {
    skeletonCache_.clear();
    animationCache_.clear();
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

glm::quat slerpQuat(const glm::quat& a, const glm::quat& b, float t) {
    return glm::slerp(a, b, t);
}

glm::mat4 interpolateTransform(const glm::mat4& a, const glm::mat4& b, float t) {
    // Decompose matrices
    glm::vec3 posA = glm::vec3(a[3]);
    glm::vec3 posB = glm::vec3(b[3]);
    
    glm::vec3 scaleA = glm::vec3(glm::length(a[0]), glm::length(a[1]), glm::length(a[2]));
    glm::vec3 scaleB = glm::vec3(glm::length(b[0]), glm::length(b[1]), glm::length(b[2]));
    
    glm::mat3 rotMatA = glm::mat3(a[0] / scaleA.x, a[1] / scaleA.y, a[2] / scaleA.z);
    glm::mat3 rotMatB = glm::mat3(b[0] / scaleB.x, b[1] / scaleB.y, b[2] / scaleB.z);
    
    glm::quat rotA = glm::quat_cast(rotMatA);
    glm::quat rotB = glm::quat_cast(rotMatB);
    
    // Interpolate
    glm::vec3 pos = glm::mix(posA, posB, t);
    glm::quat rot = glm::slerp(rotA, rotB, t);
    glm::vec3 scale = glm::mix(scaleA, scaleB, t);
    
    // Recompose
    glm::mat4 T = glm::translate(glm::mat4(1.0f), pos);
    glm::mat4 R = glm::mat4_cast(rot);
    glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);
    
    return T * R * S;
}

bool solveTwoBoneIK(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                    const glm::vec3& target, const glm::vec3& poleVector,
                    glm::quat& outRotA, glm::quat& outRotB) {
    
    float lenAB = glm::length(b - a);
    float lenBC = glm::length(c - b);
    float lenAT = glm::length(target - a);
    
    // Check if target is reachable
    if (lenAT > lenAB + lenBC) {
        // Target too far, extend fully toward it
        glm::vec3 dir = glm::normalize(target - a);
        glm::vec3 origDir = glm::normalize(b - a);
        // Compute rotation from origDir to dir
        glm::vec3 axis = glm::cross(origDir, dir);
        if (glm::length(axis) > 1e-6f) {
            axis = glm::normalize(axis);
            float angle = std::acos(glm::clamp(glm::dot(origDir, dir), -1.0f, 1.0f));
            outRotA = glm::angleAxis(angle, axis);
        } else {
            outRotA = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        }
        outRotB = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        return false;
    }
    
    if (lenAT < std::abs(lenAB - lenBC)) {
        // Target too close
        return false;
    }
    
    // Law of cosines to find angles
    float cosAngleA = (lenAB * lenAB + lenAT * lenAT - lenBC * lenBC) / (2.0f * lenAB * lenAT);
    float cosAngleB = (lenAB * lenAB + lenBC * lenBC - lenAT * lenAT) / (2.0f * lenAB * lenBC);
    
    cosAngleA = glm::clamp(cosAngleA, -1.0f, 1.0f);
    cosAngleB = glm::clamp(cosAngleB, -1.0f, 1.0f);
    
    float angleA = std::acos(cosAngleA);
    float angleB = std::acos(cosAngleB);
    
    // Calculate rotation axis using pole vector
    glm::vec3 toTarget = glm::normalize(target - a);
    glm::vec3 axis = glm::normalize(glm::cross(toTarget, poleVector));
    
    outRotA = glm::angleAxis(-angleA, axis);
    outRotB = glm::angleAxis(glm::pi<float>() - angleB, axis);
    
    return true;
}

void solveFABRIK(std::vector<glm::vec3>& positions, const glm::vec3& target,
                 const std::vector<float>& boneLengths, uint32_t iterations) {
    
    if (positions.size() < 2) return;
    
    glm::vec3 base = positions[0];
    
    for (uint32_t iter = 0; iter < iterations; ++iter) {
        // Forward pass (from end to base)
        positions.back() = target;
        
        for (int i = static_cast<int>(positions.size()) - 2; i >= 0; --i) {
            glm::vec3 dir = glm::normalize(positions[i] - positions[i + 1]);
            positions[i] = positions[i + 1] + dir * boneLengths[i];
        }
        
        // Backward pass (from base to end)
        positions[0] = base;
        
        for (size_t i = 0; i < positions.size() - 1; ++i) {
            glm::vec3 dir = glm::normalize(positions[i + 1] - positions[i]);
            positions[i + 1] = positions[i] + dir * boneLengths[i];
        }
    }
}

} // namespace Sanic
