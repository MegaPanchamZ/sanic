/**
 * GravitySystem.cpp
 * 
 * Implementation of the variable gravity volume system.
 */

#include "GravitySystem.h"
#include "SplineComponent.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace Sanic {

// ============================================================================
// CONSTRUCTOR
// ============================================================================

GravitySystem::GravitySystem() {
    volumes_.reserve(64);
}

// ============================================================================
// VOLUME MANAGEMENT
// ============================================================================

uint32_t GravitySystem::addVolume(const GravityVolume& volume) {
    GravityVolume newVolume = volume;
    newVolume.id = nextVolumeId_++;
    
    volumes_.push_back(newVolume);
    idToIndex_[newVolume.id] = volumes_.size() - 1;
    
    // Sort by priority (higher priority first)
    std::sort(volumes_.begin(), volumes_.end(),
        [](const GravityVolume& a, const GravityVolume& b) {
            return a.priority > b.priority;
        });
    
    // Rebuild index map
    idToIndex_.clear();
    for (size_t i = 0; i < volumes_.size(); i++) {
        idToIndex_[volumes_[i].id] = i;
    }
    
    return newVolume.id;
}

void GravitySystem::removeVolume(uint32_t id) {
    auto it = idToIndex_.find(id);
    if (it == idToIndex_.end()) return;
    
    size_t index = it->second;
    volumes_.erase(volumes_.begin() + index);
    
    // Rebuild index map
    idToIndex_.clear();
    for (size_t i = 0; i < volumes_.size(); i++) {
        idToIndex_[volumes_[i].id] = i;
    }
}

GravityVolume* GravitySystem::getVolume(uint32_t id) {
    auto it = idToIndex_.find(id);
    if (it == idToIndex_.end()) return nullptr;
    return &volumes_[it->second];
}

const GravityVolume* GravitySystem::getVolume(uint32_t id) const {
    auto it = idToIndex_.find(id);
    if (it == idToIndex_.end()) return nullptr;
    return &volumes_[it->second];
}

void GravitySystem::clearVolumes() {
    volumes_.clear();
    idToIndex_.clear();
}

// ============================================================================
// GRAVITY QUERIES
// ============================================================================

glm::vec3 GravitySystem::getGravityAtPosition(const glm::vec3& position) const {
    GravityQueryResult result = queryGravity(position);
    return result.gravity;
}

GravityQueryResult GravitySystem::queryGravity(const glm::vec3& position) const {
    GravityQueryResult result;
    result.gravity = defaultGravity_;
    result.direction = glm::length(defaultGravity_) > 0.0001f ? 
                       glm::normalize(defaultGravity_) : glm::vec3(0.0f, -1.0f, 0.0f);
    result.strength = glm::length(defaultGravity_);
    
    if (volumes_.empty()) {
        return result;
    }
    
    // Collect all affecting volumes
    std::vector<std::pair<float, const GravityVolume*>> activeVolumes;
    
    for (const auto& volume : volumes_) {
        if (!volume.enabled) continue;
        
        float influence = calculateInfluence(volume, position);
        if (influence > 0.0f) {
            activeVolumes.push_back({influence, &volume});
            result.activeVolumeIds.push_back(volume.id);
        }
    }
    
    if (activeVolumes.empty()) {
        return result;
    }
    
    // Find dominant volume (highest priority with influence)
    const GravityVolume* dominantVolume = activeVolumes[0].second;
    float dominantInfluence = activeVolumes[0].first;
    result.dominantVolumeId = dominantVolume->id;
    result.blendFactor = dominantInfluence;
    
    // Calculate blended gravity
    glm::vec3 blendedGravity = defaultGravity_;
    float totalWeight = 1.0f - dominantInfluence;  // Weight of default gravity
    
    // Apply volumes in priority order
    for (const auto& [influence, volume] : activeVolumes) {
        glm::vec3 volumeGravity = calculateVolumeGravity(*volume, position);
        
        // Blend based on influence and priority
        // Higher priority volumes can fully override lower ones
        blendedGravity = glm::mix(blendedGravity, volumeGravity, influence);
    }
    
    result.gravity = blendedGravity;
    result.strength = glm::length(blendedGravity);
    if (result.strength > 0.0001f) {
        result.direction = blendedGravity / result.strength;
    }
    
    return result;
}

glm::vec3 GravitySystem::getGravityDirection(const glm::vec3& position) const {
    return queryGravity(position).direction;
}

float GravitySystem::getGravityStrength(const glm::vec3& position) const {
    return queryGravity(position).strength;
}

// ============================================================================
// INFLUENCE CALCULATION
// ============================================================================

float GravitySystem::calculateInfluence(const GravityVolume& volume, const glm::vec3& pos) const {
    if (!volume.enabled) return 0.0f;
    
    // Handle infinite volumes
    if (volume.shape == GravityVolumeShape::Infinite) {
        return 1.0f;
    }
    
    float distance = getDistanceToVolume(volume, pos);
    
    // Outside volume + blend radius
    if (distance > volume.blendRadius) {
        return 0.0f;
    }
    
    // Fully inside volume
    if (distance <= 0.0f) {
        return 1.0f;
    }
    
    // In blend zone - smooth interpolation
    float t = distance / volume.blendRadius;
    
    // Smooth step for nice falloff
    float smoothT = t * t * (3.0f - 2.0f * t);
    
    return 1.0f - smoothT;
}

float GravitySystem::getDistanceToVolume(const GravityVolume& volume, const glm::vec3& pos) const {
    // Transform position to volume local space
    glm::vec3 localPos = pos - volume.center;
    localPos = glm::inverse(volume.rotation) * localPos;
    
    switch (volume.shape) {
        case GravityVolumeShape::Sphere: {
            float dist = glm::length(localPos) - volume.radius;
            return dist;
        }
        
        case GravityVolumeShape::Box: {
            glm::vec3 d = glm::abs(localPos) - volume.halfExtents;
            float outside = glm::length(glm::max(d, glm::vec3(0.0f)));
            float inside = std::min(std::max(d.x, std::max(d.y, d.z)), 0.0f);
            return outside + inside;
        }
        
        case GravityVolumeShape::Capsule: {
            // Capsule along Y axis
            float halfHeight = volume.height * 0.5f - volume.radius;
            localPos.y = std::max(std::abs(localPos.y) - halfHeight, 0.0f);
            return glm::length(localPos) - volume.radius;
        }
        
        case GravityVolumeShape::Infinite:
            return -1.0f;  // Always inside
        
        default:
            return 0.0f;
    }
}

bool GravitySystem::isInsideVolume(const GravityVolume& volume, const glm::vec3& pos) const {
    return getDistanceToVolume(volume, pos) <= 0.0f;
}

// ============================================================================
// GRAVITY CALCULATION
// ============================================================================

glm::vec3 GravitySystem::calculateVolumeGravity(const GravityVolume& volume, const glm::vec3& pos) const {
    glm::vec3 gravityDir;
    float strength = volume.strength;
    
    switch (volume.type) {
        case GravityVolumeType::Directional: {
            // Constant direction in volume's local space
            gravityDir = volume.rotation * volume.direction;
            break;
        }
        
        case GravityVolumeType::Spherical: {
            // Gravity toward center
            glm::vec3 attractPoint = volume.attractionPoint;
            if (glm::length(attractPoint) < 0.0001f) {
                attractPoint = volume.center;
            }
            glm::vec3 toCenter = attractPoint - pos;
            float dist = glm::length(toCenter);
            
            if (dist > 0.0001f) {
                gravityDir = toCenter / dist;
                
                // Apply falloff based on distance
                if (volume.falloff != GravityVolume::FalloffType::None) {
                    float normalizedDist = dist / volume.radius;
                    switch (volume.falloff) {
                        case GravityVolume::FalloffType::Linear:
                            strength *= 1.0f - glm::clamp(normalizedDist - volume.falloffStart, 0.0f, 1.0f) / 
                                       (volume.falloffEnd - volume.falloffStart);
                            break;
                        case GravityVolume::FalloffType::InverseSquare:
                            strength /= (1.0f + normalizedDist * normalizedDist);
                            break;
                        case GravityVolume::FalloffType::Smooth: {
                            float t = glm::clamp((normalizedDist - volume.falloffStart) / 
                                                (volume.falloffEnd - volume.falloffStart), 0.0f, 1.0f);
                            t = t * t * (3.0f - 2.0f * t);
                            strength = glm::mix(volume.strength, volume.minimumStrength, t);
                            break;
                        }
                        default:
                            break;
                    }
                }
            } else {
                gravityDir = glm::vec3(0.0f, -1.0f, 0.0f);
            }
            break;
        }
        
        case GravityVolumeType::SplineBased: {
            if (!volume.spline) {
                gravityDir = glm::vec3(0.0f, -1.0f, 0.0f);
                break;
            }
            
            // Gravity perpendicular to spline tangent, toward spline
            float param = volume.spline->findClosestParameter(pos);
            glm::vec3 splinePos = volume.spline->evaluatePosition(param);
            glm::vec3 toSpline = splinePos - pos;
            float dist = glm::length(toSpline);
            
            if (dist > 0.0001f) {
                gravityDir = toSpline / dist;
            } else {
                // On the spline - use spline's up direction
                gravityDir = -volume.spline->evaluateUp(param);
            }
            break;
        }
        
        case GravityVolumeType::Cylindrical: {
            // Gravity toward central axis (Y-axis in local space)
            glm::vec3 localPos = pos - volume.center;
            localPos = glm::inverse(volume.rotation) * localPos;
            
            // Project onto XZ plane
            glm::vec3 toAxis = glm::vec3(-localPos.x, 0.0f, -localPos.z);
            float dist = glm::length(toAxis);
            
            if (dist > 0.0001f) {
                toAxis = toAxis / dist;
                gravityDir = volume.rotation * toAxis;
            } else {
                gravityDir = glm::vec3(0.0f, -1.0f, 0.0f);
            }
            break;
        }
        
        case GravityVolumeType::Point: {
            glm::vec3 toPoint = volume.attractionPoint - pos;
            float dist = glm::length(toPoint);
            
            if (dist > 0.0001f) {
                gravityDir = toPoint / dist;
                
                // Inverse square falloff for point gravity
                if (dist > 1.0f) {
                    strength /= (dist * dist);
                }
            } else {
                gravityDir = glm::vec3(0.0f, -1.0f, 0.0f);
            }
            break;
        }
        
        default:
            gravityDir = glm::vec3(0.0f, -1.0f, 0.0f);
            break;
    }
    
    // Handle inverted gravity
    if (volume.invertGravity) {
        gravityDir = -gravityDir;
    }
    
    return gravityDir * strength;
}

// ============================================================================
// HELPER CREATORS
// ============================================================================

uint32_t GravitySystem::createSphericalVolume(
    const glm::vec3& center,
    float radius,
    float strength,
    int priority)
{
    GravityVolume volume;
    volume.type = GravityVolumeType::Spherical;
    volume.shape = GravityVolumeShape::Sphere;
    volume.center = center;
    volume.attractionPoint = center;
    volume.radius = radius;
    volume.strength = strength;
    volume.priority = priority;
    volume.blendRadius = radius * 0.2f;  // 20% blend zone
    
    return addVolume(volume);
}

uint32_t GravitySystem::createDirectionalVolume(
    const glm::vec3& center,
    const glm::vec3& halfExtents,
    const glm::vec3& direction,
    float strength,
    int priority)
{
    GravityVolume volume;
    volume.type = GravityVolumeType::Directional;
    volume.shape = GravityVolumeShape::Box;
    volume.center = center;
    volume.halfExtents = halfExtents;
    volume.direction = glm::normalize(direction);
    volume.strength = strength;
    volume.priority = priority;
    volume.blendRadius = 1.0f;
    
    return addVolume(volume);
}

uint32_t GravitySystem::createSplineVolume(
    SplineComponent* spline,
    float radius,
    float strength,
    int priority)
{
    if (!spline) return 0;
    
    GravityVolume volume;
    volume.type = GravityVolumeType::SplineBased;
    volume.shape = GravityVolumeShape::Infinite;  // Calculated per-point
    volume.spline = spline;
    volume.radius = radius;
    volume.strength = strength;
    volume.priority = priority;
    volume.blendRadius = radius * 0.3f;
    
    return addVolume(volume);
}

} // namespace Sanic
