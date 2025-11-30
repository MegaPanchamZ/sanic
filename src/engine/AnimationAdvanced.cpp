/**
 * AnimationAdvanced.cpp
 * 
 * Implementation of advanced animation features.
 */

#include "AnimationAdvanced.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Sanic {

// ============================================================================
// ROOT MOTION EXTRACTION
// ============================================================================

FRootMotionMovementParams RootMotionExtractor::extractRootMotion(
    const AnimationClip& clip,
    const Skeleton& skeleton,
    float startTime,
    float endTime,
    ERootMotionMode mode
) {
    FRootMotionMovementParams result;
    
    if (mode == ERootMotionMode::NoExtraction || skeleton.rootBoneIndex < 0) {
        return result;
    }
    
    // Sample root bone at start and end times
    glm::mat4 startTransform = sampleRootTransform(clip, skeleton, startTime);
    glm::mat4 endTransform = sampleRootTransform(clip, skeleton, endTime);
    
    // Calculate delta
    glm::mat4 delta = glm::inverse(startTransform) * endTransform;
    
    // Extract translation
    glm::vec3 translation = glm::vec3(delta[3]);
    
    // Extract rotation
    glm::mat3 rotMat = glm::mat3(delta);
    glm::quat rotation = glm::quat_cast(rotMat);
    
    switch (mode) {
        case ERootMotionMode::ExtractTranslation:
            result.translation = translation;
            result.hasTranslation = glm::length(translation) > 0.0001f;
            break;
            
        case ERootMotionMode::ExtractTranslationXY:
            result.translation = glm::vec3(translation.x, 0.0f, translation.z);
            result.hasTranslation = glm::length(result.translation) > 0.0001f;
            break;
            
        case ERootMotionMode::ExtractAll:
            result.translation = translation;
            result.rotation = rotation;
            result.hasTranslation = glm::length(translation) > 0.0001f;
            result.hasRotation = std::abs(glm::dot(rotation, glm::quat())) < 0.9999f;
            break;
            
        case ERootMotionMode::IgnoreRoot:
            // Return zero/identity
            break;
            
        default:
            break;
    }
    
    return result;
}

void RootMotionExtractor::removeRootMotion(
    AnimationClip& clip,
    const Skeleton& skeleton,
    ERootMotionMode mode
) {
    if (mode == ERootMotionMode::NoExtraction || skeleton.rootBoneIndex < 0) {
        return;
    }
    
    int32_t rootBoneIdx = skeleton.rootBoneIndex;
    
    // Find root bone channel
    for (auto& channel : clip.channels) {
        if (channel.boneIndex == static_cast<uint32_t>(rootBoneIdx)) {
            // Get first frame transform as reference
            glm::vec3 firstPos = channel.positionKeys.empty() ? 
                glm::vec3(0) : channel.positionKeys[0].value;
            glm::quat firstRot = channel.rotationKeys.empty() ? 
                glm::quat() : channel.rotationKeys[0].value;
            
            // Subtract from all keyframes
            switch (mode) {
                case ERootMotionMode::ExtractTranslation:
                    for (auto& key : channel.positionKeys) {
                        key.value -= firstPos;
                    }
                    break;
                    
                case ERootMotionMode::ExtractTranslationXY:
                    for (auto& key : channel.positionKeys) {
                        key.value.x -= firstPos.x;
                        key.value.z -= firstPos.z;
                    }
                    break;
                    
                case ERootMotionMode::ExtractAll:
                    for (auto& key : channel.positionKeys) {
                        key.value -= firstPos;
                    }
                    for (auto& key : channel.rotationKeys) {
                        key.value = glm::inverse(firstRot) * key.value;
                    }
                    break;
                    
                case ERootMotionMode::IgnoreRoot:
                    for (auto& key : channel.positionKeys) {
                        key.value = glm::vec3(0);
                    }
                    for (auto& key : channel.rotationKeys) {
                        key.value = glm::quat();
                    }
                    break;
                    
                default:
                    break;
            }
            break;
        }
    }
}

std::vector<FRootMotionMovementParams> RootMotionExtractor::extractRootMotionTrack(
    const AnimationClip& clip,
    const Skeleton& skeleton,
    float sampleRate,
    ERootMotionMode mode
) {
    std::vector<FRootMotionMovementParams> track;
    
    if (clip.duration <= 0 || sampleRate <= 0) {
        return track;
    }
    
    float dt = 1.0f / sampleRate;
    size_t sampleCount = static_cast<size_t>(clip.duration * sampleRate) + 1;
    track.reserve(sampleCount);
    
    for (size_t i = 0; i < sampleCount; ++i) {
        float startTime = i * dt;
        float endTime = std::min((i + 1) * dt, clip.duration);
        
        track.push_back(extractRootMotion(clip, skeleton, startTime, endTime, mode));
    }
    
    return track;
}

void RootMotionExtractor::applyRootMotion(
    glm::vec3& outPosition,
    glm::quat& outRotation,
    const FRootMotionMovementParams& rootMotion,
    const glm::quat& characterRotation
) {
    if (rootMotion.hasTranslation) {
        // Transform root motion translation by character's current rotation
        glm::vec3 worldTranslation = characterRotation * rootMotion.translation;
        outPosition += worldTranslation;
    }
    
    if (rootMotion.hasRotation) {
        outRotation = glm::normalize(rootMotion.rotation * outRotation);
    }
}

glm::mat4 RootMotionExtractor::sampleRootTransform(
    const AnimationClip& clip,
    const Skeleton& skeleton,
    float time
) {
    int32_t rootBoneIdx = skeleton.rootBoneIndex;
    if (rootBoneIdx < 0) return glm::mat4(1.0f);
    
    glm::vec3 position(0.0f);
    glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 scale(1.0f);
    
    for (const auto& channel : clip.channels) {
        if (channel.boneIndex == static_cast<uint32_t>(rootBoneIdx)) {
            // Sample position
            if (!channel.positionKeys.empty()) {
                if (channel.positionKeys.size() == 1) {
                    position = channel.positionKeys[0].value;
                } else {
                    for (size_t i = 0; i < channel.positionKeys.size() - 1; ++i) {
                        if (time <= channel.positionKeys[i + 1].time) {
                            float t = (time - channel.positionKeys[i].time) / 
                                     (channel.positionKeys[i + 1].time - channel.positionKeys[i].time);
                            position = glm::mix(channel.positionKeys[i].value, 
                                               channel.positionKeys[i + 1].value, t);
                            break;
                        }
                    }
                }
            }
            
            // Sample rotation
            if (!channel.rotationKeys.empty()) {
                if (channel.rotationKeys.size() == 1) {
                    rotation = channel.rotationKeys[0].value;
                } else {
                    for (size_t i = 0; i < channel.rotationKeys.size() - 1; ++i) {
                        if (time <= channel.rotationKeys[i + 1].time) {
                            float t = (time - channel.rotationKeys[i].time) / 
                                     (channel.rotationKeys[i + 1].time - channel.rotationKeys[i].time);
                            rotation = glm::slerp(channel.rotationKeys[i].value, 
                                                  channel.rotationKeys[i + 1].value, t);
                            break;
                        }
                    }
                }
            }
            
            break;
        }
    }
    
    return glm::translate(glm::mat4(1.0f), position) * 
           glm::mat4_cast(rotation) * 
           glm::scale(glm::mat4(1.0f), scale);
}

// ============================================================================
// ANIMATION COMPRESSION
// ============================================================================

CompressedAnimationClip AnimationCompressor::compress(
    const AnimationClip& clip,
    const Skeleton& skeleton,
    const AnimationCompressionSettings& settings
) {
    CompressedAnimationClip result;
    result.name = clip.name;
    result.duration = clip.duration;
    result.sampleRate = clip.ticksPerSecond;
    result.events = clip.events;
    
    // Calculate original size
    result.originalSize = 0;
    for (const auto& channel : clip.channels) {
        result.originalSize += channel.positionKeys.size() * sizeof(PositionKeyframe);
        result.originalSize += channel.rotationKeys.size() * sizeof(RotationKeyframe);
        result.originalSize += channel.scaleKeys.size() * sizeof(ScaleKeyframe);
    }
    
    // Compress each bone track
    for (const auto& channel : clip.channels) {
        CompressedAnimationClip::CompressedBoneTrack track;
        track.boneIndex = channel.boneIndex;
        
        // Check if track is constant
        bool isConstant = true;
        
        // Compress translation
        if (!channel.positionKeys.empty()) {
            // Find bounds
            track.translationMin = glm::vec3(std::numeric_limits<float>::max());
            glm::vec3 translationMax = glm::vec3(std::numeric_limits<float>::lowest());
            
            for (const auto& key : channel.positionKeys) {
                track.translationMin = glm::min(track.translationMin, key.value);
                translationMax = glm::max(translationMax, key.value);
            }
            
            track.translationRange = translationMax - track.translationMin;
            
            // Check if constant
            if (glm::length(track.translationRange) < settings.constantThreshold) {
                track.isConstant = true;
                track.translationData.resize(3);
                track.translationData[0] = 32768; // Center value
                track.translationData[1] = 32768;
                track.translationData[2] = 32768;
            } else {
                isConstant = false;
                
                // Keyframe reduction if enabled
                std::vector<size_t> keyIndices;
                if (settings.useCurveFitting) {
                    std::vector<float> xValues, yValues, zValues;
                    for (const auto& key : channel.positionKeys) {
                        xValues.push_back(key.value.x);
                        yValues.push_back(key.value.y);
                        zValues.push_back(key.value.z);
                    }
                    keyIndices = selectKeyframes(xValues, settings.translationErrorThreshold);
                } else {
                    for (size_t i = 0; i < channel.positionKeys.size(); ++i) {
                        keyIndices.push_back(i);
                    }
                }
                
                // Quantize selected keyframes
                for (size_t idx : keyIndices) {
                    const auto& key = channel.positionKeys[idx];
                    glm::vec3 normalized = (key.value - track.translationMin) / 
                                           glm::max(track.translationRange, glm::vec3(0.0001f));
                    
                    track.translationData.push_back(static_cast<uint16_t>(normalized.x * 65535));
                    track.translationData.push_back(static_cast<uint16_t>(normalized.y * 65535));
                    track.translationData.push_back(static_cast<uint16_t>(normalized.z * 65535));
                    
                    track.keyframeTimes.push_back(
                        static_cast<uint16_t>((key.time / clip.duration) * 65535));
                }
            }
        }
        
        // Compress rotation using smallest-3 quaternion encoding
        if (!channel.rotationKeys.empty()) {
            for (const auto& key : channel.rotationKeys) {
                uint16_t compressed[3];
                compressQuaternion(key.value, compressed);
                track.rotationData.push_back(compressed[0]);
                track.rotationData.push_back(compressed[1]);
                track.rotationData.push_back(compressed[2]);
            }
        }
        
        // Compress scale (similar to translation)
        if (!channel.scaleKeys.empty()) {
            track.scaleMin = glm::vec3(std::numeric_limits<float>::max());
            glm::vec3 scaleMax = glm::vec3(std::numeric_limits<float>::lowest());
            
            for (const auto& key : channel.scaleKeys) {
                track.scaleMin = glm::min(track.scaleMin, key.value);
                scaleMax = glm::max(scaleMax, key.value);
            }
            
            track.scaleRange = scaleMax - track.scaleMin;
            
            for (const auto& key : channel.scaleKeys) {
                glm::vec3 normalized = (key.value - track.scaleMin) / 
                                       glm::max(track.scaleRange, glm::vec3(0.0001f));
                
                track.scaleData.push_back(static_cast<uint16_t>(normalized.x * 65535));
                track.scaleData.push_back(static_cast<uint16_t>(normalized.y * 65535));
                track.scaleData.push_back(static_cast<uint16_t>(normalized.z * 65535));
            }
        }
        
        track.isConstant = isConstant && track.isConstant;
        
        // Skip identity tracks if configured
        if (settings.removeIdentityTracks && track.isConstant) {
            // Check if this is an identity transform
            bool isIdentity = true;
            // ... identity check logic
            if (isIdentity) continue;
        }
        
        result.boneTracks.push_back(std::move(track));
    }
    
    // Calculate compressed size
    result.compressedSize = 0;
    for (const auto& track : result.boneTracks) {
        result.compressedSize += track.translationData.size() * sizeof(uint16_t);
        result.compressedSize += track.rotationData.size() * sizeof(uint16_t);
        result.compressedSize += track.scaleData.size() * sizeof(uint16_t);
        result.compressedSize += track.keyframeTimes.size() * sizeof(uint16_t);
        result.compressedSize += sizeof(track.translationMin) + sizeof(track.translationRange);
        result.compressedSize += sizeof(track.scaleMin) + sizeof(track.scaleRange);
    }
    
    result.compressionRatio = result.originalSize > 0 ? 
        static_cast<float>(result.compressedSize) / result.originalSize : 1.0f;
    
    return result;
}

AnimationClip AnimationCompressor::decompress(
    const CompressedAnimationClip& compressed,
    const Skeleton& skeleton
) {
    AnimationClip result;
    result.name = compressed.name;
    result.duration = compressed.duration;
    result.ticksPerSecond = compressed.sampleRate;
    result.events = compressed.events;
    
    for (const auto& track : compressed.boneTracks) {
        AnimationChannel channel;
        channel.boneIndex = track.boneIndex;
        
        // Decompress translation
        size_t numTransKeys = track.translationData.size() / 3;
        for (size_t i = 0; i < numTransKeys; ++i) {
            PositionKeyframe key;
            
            if (!track.keyframeTimes.empty() && i < track.keyframeTimes.size()) {
                key.time = (track.keyframeTimes[i] / 65535.0f) * compressed.duration;
            } else {
                key.time = (static_cast<float>(i) / std::max(numTransKeys - 1, size_t(1))) * compressed.duration;
            }
            
            glm::vec3 normalized(
                track.translationData[i * 3] / 65535.0f,
                track.translationData[i * 3 + 1] / 65535.0f,
                track.translationData[i * 3 + 2] / 65535.0f
            );
            
            key.value = track.translationMin + normalized * track.translationRange;
            channel.positionKeys.push_back(key);
        }
        
        // Decompress rotation
        size_t numRotKeys = track.rotationData.size() / 3;
        for (size_t i = 0; i < numRotKeys; ++i) {
            RotationKeyframe key;
            key.time = (static_cast<float>(i) / std::max(numRotKeys - 1, size_t(1))) * compressed.duration;
            
            uint16_t data[3] = {
                track.rotationData[i * 3],
                track.rotationData[i * 3 + 1],
                track.rotationData[i * 3 + 2]
            };
            key.value = decompressQuaternion(data);
            channel.rotationKeys.push_back(key);
        }
        
        // Decompress scale
        size_t numScaleKeys = track.scaleData.size() / 3;
        for (size_t i = 0; i < numScaleKeys; ++i) {
            ScaleKeyframe key;
            key.time = (static_cast<float>(i) / std::max(numScaleKeys - 1, size_t(1))) * compressed.duration;
            
            glm::vec3 normalized(
                track.scaleData[i * 3] / 65535.0f,
                track.scaleData[i * 3 + 1] / 65535.0f,
                track.scaleData[i * 3 + 2] / 65535.0f
            );
            
            key.value = track.scaleMin + normalized * track.scaleRange;
            channel.scaleKeys.push_back(key);
        }
        
        result.channels.push_back(std::move(channel));
    }
    
    return result;
}

void AnimationCompressor::compressQuaternion(const glm::quat& q, uint16_t* out) {
    // Smallest-3 encoding: drop the largest component, encode the other 3
    // With sign bit recovery using the dropped component's known constraint
    
    float components[4] = {q.x, q.y, q.z, q.w};
    int largestIdx = 0;
    float largestVal = std::abs(components[0]);
    
    for (int i = 1; i < 4; ++i) {
        float absVal = std::abs(components[i]);
        if (absVal > largestVal) {
            largestVal = absVal;
            largestIdx = i;
        }
    }
    
    // Ensure the dropped component is positive (quaternion negation invariance)
    float sign = components[largestIdx] >= 0 ? 1.0f : -1.0f;
    
    // Encode 3 components + 2-bit index = 48 bits total (3 x 16-bit)
    int outIdx = 0;
    for (int i = 0; i < 4; ++i) {
        if (i != largestIdx) {
            // Range is [-1/sqrt(2), 1/sqrt(2)] for non-largest components
            float normalized = (components[i] * sign + 0.7071067811865f) / 1.4142135623731f;
            out[outIdx++] = static_cast<uint16_t>(std::clamp(normalized, 0.0f, 1.0f) * 65535);
        }
    }
    
    // Encode largest index in high bits of last component
    out[2] = (out[2] & 0x3FFF) | (static_cast<uint16_t>(largestIdx) << 14);
}

glm::quat AnimationCompressor::decompressQuaternion(const uint16_t* data) {
    int largestIdx = (data[2] >> 14) & 0x3;
    
    float components[4];
    float sumSquares = 0.0f;
    
    int dataIdx = 0;
    for (int i = 0; i < 4; ++i) {
        if (i != largestIdx) {
            uint16_t val = (i == 2 || (i > 2 && largestIdx <= 2)) ? 
                           (data[dataIdx] & 0x3FFF) : data[dataIdx];
            if (i == 2 && largestIdx != 2) {
                val = data[dataIdx]; // Use full 16 bits for first two
            }
            
            // Actually simpler decode
            float normalized = data[dataIdx++] / 65535.0f;
            components[i] = normalized * 1.4142135623731f - 0.7071067811865f;
            sumSquares += components[i] * components[i];
        }
    }
    
    // Recover largest component from unit quaternion constraint
    components[largestIdx] = std::sqrt(std::max(0.0f, 1.0f - sumSquares));
    
    return glm::quat(components[3], components[0], components[1], components[2]);
}

std::vector<size_t> AnimationCompressor::selectKeyframes(
    const std::vector<float>& values,
    float tolerance
) {
    std::vector<size_t> result;
    if (values.empty()) return result;
    
    result.push_back(0); // Always include first
    
    // Douglas-Peucker-like simplification
    std::function<void(size_t, size_t)> simplify = [&](size_t start, size_t end) {
        if (end - start < 2) return;
        
        float maxDist = 0;
        size_t maxIdx = start;
        
        // Find point with maximum distance from line
        float startVal = values[start];
        float endVal = values[end];
        float slope = (endVal - startVal) / (end - start);
        
        for (size_t i = start + 1; i < end; ++i) {
            float expected = startVal + slope * (i - start);
            float dist = std::abs(values[i] - expected);
            if (dist > maxDist) {
                maxDist = dist;
                maxIdx = i;
            }
        }
        
        if (maxDist > tolerance) {
            simplify(start, maxIdx);
            result.push_back(maxIdx);
            simplify(maxIdx, end);
        }
    };
    
    if (values.size() > 1) {
        simplify(0, values.size() - 1);
        result.push_back(values.size() - 1); // Always include last
    }
    
    std::sort(result.begin(), result.end());
    return result;
}

// ============================================================================
// SKELETON RETARGETING
// ============================================================================

std::shared_ptr<AnimationClip> AnimationRetargeter::retarget(
    const AnimationClip& sourceClip,
    const Skeleton& sourceSkeleton,
    const Skeleton& targetSkeleton,
    const RetargetingProfile& profile,
    ERetargetingMode mode
) {
    auto result = std::make_shared<AnimationClip>();
    result->name = sourceClip.name + "_retargeted";
    result->duration = sourceClip.duration;
    result->ticksPerSecond = sourceClip.ticksPerSecond;
    result->looping = sourceClip.looping;
    result->events = sourceClip.events;
    
    // Build bone mapping lookup
    std::unordered_map<uint32_t, const BoneMapping*> sourceBoneToMapping;
    for (const auto& mapping : profile.boneMappings) {
        uint32_t srcIdx = sourceSkeleton.findBone(mapping.sourceBone);
        if (srcIdx != UINT32_MAX) {
            sourceBoneToMapping[srcIdx] = &mapping;
        }
    }
    
    // Process each channel
    for (const auto& srcChannel : sourceClip.channels) {
        auto mappingIt = sourceBoneToMapping.find(srcChannel.boneIndex);
        if (mappingIt == sourceBoneToMapping.end()) continue;
        
        const BoneMapping& mapping = *mappingIt->second;
        uint32_t targetBoneIdx = targetSkeleton.findBone(mapping.targetBone);
        if (targetBoneIdx == UINT32_MAX) continue;
        
        AnimationChannel dstChannel;
        dstChannel.boneIndex = targetBoneIdx;
        
        // Get bind poses
        const Bone& srcBone = sourceSkeleton.bones[srcChannel.boneIndex];
        const Bone& dstBone = targetSkeleton.bones[targetBoneIdx];
        
        // Retarget position keyframes
        for (const auto& srcKey : srcChannel.positionKeys) {
            PositionKeyframe dstKey;
            dstKey.time = srcKey.time;
            
            // Scale translation by bone length ratio
            dstKey.value = srcKey.value * mapping.lengthScale + mapping.translationOffset;
            
            dstChannel.positionKeys.push_back(dstKey);
        }
        
        // Retarget rotation keyframes
        for (const auto& srcKey : srcChannel.rotationKeys) {
            RotationKeyframe dstKey;
            dstKey.time = srcKey.time;
            
            // Apply rotation offset
            dstKey.value = glm::normalize(mapping.rotationOffset * srcKey.value);
            
            dstChannel.rotationKeys.push_back(dstKey);
        }
        
        // Copy scale keyframes (usually unchanged)
        dstChannel.scaleKeys = srcChannel.scaleKeys;
        
        result->channels.push_back(std::move(dstChannel));
    }
    
    return result;
}

RetargetingProfile AnimationRetargeter::createProfile(
    const Skeleton& source,
    const Skeleton& target,
    const std::unordered_map<std::string, std::string>& nameMapping
) {
    RetargetingProfile profile;
    profile.sourceSkeletonName = source.name;
    profile.targetSkeletonName = target.name;
    
    // Common bone name patterns
    static const std::vector<std::pair<std::vector<std::string>, std::string>> bonePatterns = {
        {{"hips", "pelvis", "root"}, "Hips"},
        {{"spine", "spine_01", "spine1"}, "Spine"},
        {{"spine_02", "spine2", "chest"}, "Spine1"},
        {{"spine_03", "spine3", "upper_chest"}, "Spine2"},
        {{"neck", "neck_01"}, "Neck"},
        {{"head", "head_01"}, "Head"},
        {{"shoulder_l", "leftshoulder", "clavicle_l"}, "LeftShoulder"},
        {{"arm_l", "upperarm_l", "leftupperarm"}, "LeftArm"},
        {{"forearm_l", "lowerarm_l", "leftforearm"}, "LeftForeArm"},
        {{"hand_l", "lefthand"}, "LeftHand"},
        {{"shoulder_r", "rightshoulder", "clavicle_r"}, "RightShoulder"},
        {{"arm_r", "upperarm_r", "rightupperarm"}, "RightArm"},
        {{"forearm_r", "lowerarm_r", "rightforearm"}, "RightForeArm"},
        {{"hand_r", "righthand"}, "RightHand"},
        {{"thigh_l", "upperleg_l", "leftupperleg"}, "LeftUpLeg"},
        {{"calf_l", "lowerleg_l", "leftleg"}, "LeftLeg"},
        {{"foot_l", "leftfoot"}, "LeftFoot"},
        {{"toe_l", "lefttoebase"}, "LeftToeBase"},
        {{"thigh_r", "upperleg_r", "rightupperleg"}, "RightUpLeg"},
        {{"calf_r", "lowerleg_r", "rightleg"}, "RightLeg"},
        {{"foot_r", "rightfoot"}, "RightFoot"},
        {{"toe_r", "righttoebase"}, "RightToeBase"}
    };
    
    auto normalizeNoneName = [](std::string name) {
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        std::replace(name.begin(), name.end(), ' ', '_');
        std::replace(name.begin(), name.end(), '-', '_');
        return name;
    };
    
    auto findMatchingBone = [&](const Skeleton& skeleton, const std::vector<std::string>& patterns) -> std::string {
        for (const auto& bone : skeleton.bones) {
            std::string normalized = normalizeNoneName(bone.name);
            for (const auto& pattern : patterns) {
                if (normalized.find(pattern) != std::string::npos) {
                    return bone.name;
                }
            }
        }
        return "";
    };
    
    // First, apply explicit mappings
    for (const auto& [srcName, dstName] : nameMapping) {
        BoneMapping mapping;
        mapping.sourceBone = srcName;
        mapping.targetBone = dstName;
        mapping.lengthScale = 1.0f;
        profile.boneMappings.push_back(mapping);
    }
    
    // Then try to auto-match remaining bones
    for (const auto& srcBone : source.bones) {
        // Skip if already mapped
        bool alreadyMapped = false;
        for (const auto& existing : profile.boneMappings) {
            if (existing.sourceBone == srcBone.name) {
                alreadyMapped = true;
                break;
            }
        }
        if (alreadyMapped) continue;
        
        // Try pattern matching
        std::string normalizedSrc = normalizeNoneName(srcBone.name);
        
        for (const auto& dstBone : target.bones) {
            std::string normalizedDst = normalizeNoneName(dstBone.name);
            
            // Exact match (after normalization)
            if (normalizedSrc == normalizedDst) {
                BoneMapping mapping;
                mapping.sourceBone = srcBone.name;
                mapping.targetBone = dstBone.name;
                mapping.lengthScale = 1.0f; // Could compute from bind poses
                profile.boneMappings.push_back(mapping);
                break;
            }
        }
    }
    
    return profile;
}

AnimationRetargeter::ValidationResult AnimationRetargeter::validateProfile(
    const RetargetingProfile& profile,
    const Skeleton& source,
    const Skeleton& target
) {
    ValidationResult result;
    
    std::unordered_set<std::string> mappedSourceBones;
    std::unordered_set<std::string> mappedTargetBones;
    
    for (const auto& mapping : profile.boneMappings) {
        mappedSourceBones.insert(mapping.sourceBone);
        mappedTargetBones.insert(mapping.targetBone);
        
        // Verify source bone exists
        if (source.findBone(mapping.sourceBone) == UINT32_MAX) {
            result.warnings.push_back("Source bone not found: " + mapping.sourceBone);
            result.isValid = false;
        }
        
        // Verify target bone exists
        if (target.findBone(mapping.targetBone) == UINT32_MAX) {
            result.warnings.push_back("Target bone not found: " + mapping.targetBone);
            result.isValid = false;
        }
    }
    
    // Find unmapped bones
    for (const auto& bone : source.bones) {
        if (mappedSourceBones.find(bone.name) == mappedSourceBones.end()) {
            result.unmappedSourceBones.push_back(bone.name);
        }
    }
    
    for (const auto& bone : target.bones) {
        if (mappedTargetBones.find(bone.name) == mappedTargetBones.end()) {
            result.unmappedTargetBones.push_back(bone.name);
        }
    }
    
    return result;
}

// ============================================================================
// ADDITIVE ANIMATION
// ============================================================================

std::shared_ptr<AnimationClip> AdditiveAnimationProcessor::makeAdditive(
    const AnimationClip& sourceClip,
    const Skeleton& skeleton,
    const AdditiveAnimationSettings& settings
) {
    auto result = std::make_shared<AnimationClip>(sourceClip);
    result->name = sourceClip.name + "_additive";
    
    // Get base pose
    std::vector<glm::mat4> basePose(skeleton.bones.size(), glm::mat4(1.0f));
    
    switch (settings.basePoseType) {
        case EAdditiveBasePoseType::SkeletonBindPose:
            for (size_t i = 0; i < skeleton.bones.size(); ++i) {
                basePose[i] = skeleton.bones[i].localBindPose;
            }
            break;
            
        case EAdditiveBasePoseType::FirstFrame:
            // Sample animation at time 0
            for (const auto& channel : sourceClip.channels) {
                if (channel.boneIndex < basePose.size()) {
                    glm::vec3 pos(0);
                    glm::quat rot(1, 0, 0, 0);
                    glm::vec3 scale(1);
                    
                    if (!channel.positionKeys.empty()) pos = channel.positionKeys[0].value;
                    if (!channel.rotationKeys.empty()) rot = channel.rotationKeys[0].value;
                    if (!channel.scaleKeys.empty()) scale = channel.scaleKeys[0].value;
                    
                    basePose[channel.boneIndex] = 
                        glm::translate(glm::mat4(1), pos) *
                        glm::mat4_cast(rot) *
                        glm::scale(glm::mat4(1), scale);
                }
            }
            break;
            
        case EAdditiveBasePoseType::CustomPose:
            basePose = settings.customBasePose;
            break;
            
        default:
            break;
    }
    
    // Subtract base pose from all keyframes
    for (auto& channel : result->channels) {
        if (channel.boneIndex >= basePose.size()) continue;
        
        // Decompose base pose
        glm::vec3 basePos = glm::vec3(basePose[channel.boneIndex][3]);
        glm::quat baseRot = glm::quat_cast(glm::mat3(basePose[channel.boneIndex]));
        
        // Subtract from position keys
        for (auto& key : channel.positionKeys) {
            key.value -= basePos;
        }
        
        // Multiply by inverse rotation
        glm::quat invBaseRot = glm::inverse(baseRot);
        for (auto& key : channel.rotationKeys) {
            key.value = invBaseRot * key.value;
        }
    }
    
    return result;
}

void AdditiveAnimationProcessor::applyAdditive(
    std::vector<glm::mat4>& basePose,
    const std::vector<glm::mat4>& additivePose,
    float weight,
    bool preMultiply
) {
    size_t count = std::min(basePose.size(), additivePose.size());
    
    for (size_t i = 0; i < count; ++i) {
        // Decompose additive
        glm::vec3 addPos = glm::vec3(additivePose[i][3]) * weight;
        glm::quat addRot = glm::quat_cast(glm::mat3(additivePose[i]));
        addRot = glm::slerp(glm::quat(1, 0, 0, 0), addRot, weight);
        
        // Apply to base
        glm::vec3 basePos = glm::vec3(basePose[i][3]);
        glm::quat baseRot = glm::quat_cast(glm::mat3(basePose[i]));
        glm::vec3 baseScale(
            glm::length(basePose[i][0]),
            glm::length(basePose[i][1]),
            glm::length(basePose[i][2])
        );
        
        glm::vec3 finalPos = basePos + addPos;
        glm::quat finalRot = preMultiply ? (addRot * baseRot) : (baseRot * addRot);
        
        basePose[i] = glm::translate(glm::mat4(1), finalPos) *
                      glm::mat4_cast(finalRot) *
                      glm::scale(glm::mat4(1), baseScale);
    }
}

// ============================================================================
// ANIMATION CURVE
// ============================================================================

float AnimationCurve::evaluate(float time) const {
    if (keyframes.empty()) return 0.0f;
    if (keyframes.size() == 1) return keyframes[0].value;
    
    // Find surrounding keyframes
    size_t i = 0;
    for (; i < keyframes.size() - 1; ++i) {
        if (time < keyframes[i + 1].time) break;
    }
    
    if (i >= keyframes.size() - 1) {
        return keyframes.back().value;
    }
    
    const auto& k0 = keyframes[i];
    const auto& k1 = keyframes[i + 1];
    
    float dt = k1.time - k0.time;
    if (dt < 0.0001f) return k0.value;
    
    float t = (time - k0.time) / dt;
    
    // Hermite interpolation
    float t2 = t * t;
    float t3 = t2 * t;
    
    float h00 = 2*t3 - 3*t2 + 1;
    float h10 = t3 - 2*t2 + t;
    float h01 = -2*t3 + 3*t2;
    float h11 = t3 - t2;
    
    return h00 * k0.value + h10 * dt * k0.outTangent +
           h01 * k1.value + h11 * dt * k1.inTangent;
}

void AnimationCurve::addKey(float time, float value) {
    Keyframe key;
    key.time = time;
    key.value = value;
    
    auto it = std::lower_bound(keyframes.begin(), keyframes.end(), key,
        [](const Keyframe& a, const Keyframe& b) { return a.time < b.time; });
    
    keyframes.insert(it, key);
    autoTangents();
}

void AnimationCurve::autoTangents() {
    for (size_t i = 0; i < keyframes.size(); ++i) {
        if (keyframes[i].mode != Keyframe::TangentMode::Auto) continue;
        
        float prevSlope = 0, nextSlope = 0;
        
        if (i > 0) {
            float dt = keyframes[i].time - keyframes[i-1].time;
            if (dt > 0) prevSlope = (keyframes[i].value - keyframes[i-1].value) / dt;
        }
        
        if (i < keyframes.size() - 1) {
            float dt = keyframes[i+1].time - keyframes[i].time;
            if (dt > 0) nextSlope = (keyframes[i+1].value - keyframes[i].value) / dt;
        }
        
        keyframes[i].inTangent = prevSlope;
        keyframes[i].outTangent = nextSlope;
    }
}

// ============================================================================
// ANIMATION MONTAGE
// ============================================================================

void AnimationMontage::addSection(const AnimationMontageSection& section) {
    sections_.push_back(section);
    duration_ = std::max(duration_, section.endTime);
}

const AnimationMontageSection* AnimationMontage::getSection(const std::string& name) const {
    for (const auto& section : sections_) {
        if (section.name == name) return &section;
    }
    return nullptr;
}

void AnimationMontage::play(float startTime) {
    position_ = startTime;
    playing_ = true;
    paused_ = false;
    
    // Find initial section
    for (const auto& section : sections_) {
        if (startTime >= section.startTime && startTime < section.endTime) {
            currentSection_ = section.name;
            break;
        }
    }
}

void AnimationMontage::update(float deltaTime) {
    if (!playing_ || paused_) return;
    
    float prevPosition = position_;
    position_ += deltaTime * playRate_;
    
    // Find current section and handle transitions
    for (const auto& section : sections_) {
        if (position_ >= section.startTime && position_ < section.endTime) {
            if (currentSection_ != section.name) {
                currentSection_ = section.name;
                sectionLoopCounter_ = 0;
            }
            
            // Check for end of section
            if (position_ >= section.endTime - 0.001f) {
                if (section.loopCount != 0 && 
                    (section.loopCount < 0 || sectionLoopCounter_ < section.loopCount)) {
                    position_ = section.startTime;
                    sectionLoopCounter_++;
                } else if (!section.nextSectionName.empty()) {
                    jumpToSection(section.nextSectionName);
                } else {
                    playing_ = false;
                }
            }
            break;
        }
    }
    
    // Clamp to duration
    if (position_ >= duration_) {
        position_ = duration_;
        playing_ = false;
    }
}

void AnimationMontage::jumpToSection(const std::string& name) {
    const auto* section = getSection(name);
    if (section) {
        currentSection_ = name;
        position_ = section->startTime;
        sectionLoopCounter_ = 0;
    }
}

FRootMotionMovementParams AnimationMontage::consumeRootMotion() {
    FRootMotionMovementParams result = pendingRootMotion_;
    pendingRootMotion_.clear();
    return result;
}

} // namespace Sanic
