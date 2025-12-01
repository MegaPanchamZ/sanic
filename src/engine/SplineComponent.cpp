/**
 * SplineComponent.cpp
 * 
 * Implementation of the spline component system.
 */

#include "SplineComponent.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace Sanic {

// ============================================================================
// CONSTRUCTOR
// ============================================================================

SplineComponent::SplineComponent() {
    distanceTable_.reserve(DISTANCE_TABLE_SAMPLES + 1);
}

// ============================================================================
// CONTROL POINTS
// ============================================================================

void SplineComponent::addControlPoint(const glm::vec3& position) {
    SplineControlPoint point;
    point.position = position;
    addControlPoint(point);
}

void SplineComponent::addControlPoint(const SplineControlPoint& point) {
    controlPoints_.push_back(point);
    rebuildDistanceTable();
}

void SplineComponent::insertControlPoint(size_t index, const SplineControlPoint& point) {
    if (index > controlPoints_.size()) {
        index = controlPoints_.size();
    }
    controlPoints_.insert(controlPoints_.begin() + index, point);
    rebuildDistanceTable();
}

void SplineComponent::removeControlPoint(size_t index) {
    if (index >= controlPoints_.size()) return;
    controlPoints_.erase(controlPoints_.begin() + index);
    rebuildDistanceTable();
}

void SplineComponent::clearControlPoints() {
    controlPoints_.clear();
    distanceTable_.clear();
    totalLength_ = 0.0f;
}

void SplineComponent::setControlPoint(size_t index, const SplineControlPoint& point) {
    if (index >= controlPoints_.size()) return;
    controlPoints_[index] = point;
    rebuildDistanceTable();
}

void SplineComponent::setLoop(bool loop) {
    isLoop_ = loop;
    rebuildDistanceTable();
}

void SplineComponent::setType(SplineType type) {
    splineType_ = type;
    rebuildDistanceTable();
}

void SplineComponent::setWorldTransform(const glm::mat4& transform) {
    worldTransform_ = transform;
}

// ============================================================================
// CATMULL-ROM INTERPOLATION
// ============================================================================

glm::vec3 SplineComponent::catmullRom(
    const glm::vec3& p0, const glm::vec3& p1,
    const glm::vec3& p2, const glm::vec3& p3, float t)
{
    float t2 = t * t;
    float t3 = t2 * t;
    
    return 0.5f * (
        (2.0f * p1) +
        (-p0 + p2) * t +
        (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
        (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3
    );
}

glm::vec3 SplineComponent::catmullRomDerivative(
    const glm::vec3& p0, const glm::vec3& p1,
    const glm::vec3& p2, const glm::vec3& p3, float t)
{
    float t2 = t * t;
    
    return 0.5f * (
        (-p0 + p2) +
        (4.0f * p0 - 10.0f * p1 + 8.0f * p2 - 2.0f * p3) * t +
        (-3.0f * p0 + 9.0f * p1 - 9.0f * p2 + 3.0f * p3) * t2
    );
}

// ============================================================================
// BEZIER INTERPOLATION
// ============================================================================

glm::vec3 SplineComponent::bezier(
    const glm::vec3& p0, const glm::vec3& t0,
    const glm::vec3& t1, const glm::vec3& p1, float t)
{
    float u = 1.0f - t;
    float u2 = u * u;
    float u3 = u2 * u;
    float t2 = t * t;
    float t3 = t2 * t;
    
    // Cubic Bezier: B(t) = (1-t)³P0 + 3(1-t)²t·T0 + 3(1-t)t²·T1 + t³·P1
    return u3 * p0 + 3.0f * u2 * t * (p0 + t0) + 3.0f * u * t2 * (p1 + t1) + t3 * p1;
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

size_t SplineComponent::wrapIndex(int index) const {
    if (controlPoints_.empty()) return 0;
    
    int count = static_cast<int>(controlPoints_.size());
    
    if (isLoop_) {
        index = ((index % count) + count) % count;
    } else {
        index = std::clamp(index, 0, count - 1);
    }
    
    return static_cast<size_t>(index);
}

void SplineComponent::getSegmentInfo(float t, int& segment, float& localT) const {
    if (controlPoints_.size() < 2) {
        segment = 0;
        localT = 0;
        return;
    }
    
    int numSegments = isLoop_ ? 
        static_cast<int>(controlPoints_.size()) : 
        static_cast<int>(controlPoints_.size()) - 1;
    
    t = std::clamp(t, 0.0f, 1.0f);
    
    float segmentFloat = t * numSegments;
    segment = static_cast<int>(segmentFloat);
    
    // Handle edge case at t = 1
    if (segment >= numSegments) {
        segment = numSegments - 1;
        localT = 1.0f;
    } else {
        localT = segmentFloat - segment;
    }
}

// ============================================================================
// EVALUATION AT PARAMETER T
// ============================================================================

glm::vec3 SplineComponent::evaluatePosition(float t) const {
    if (controlPoints_.size() < 2) {
        return controlPoints_.empty() ? glm::vec3(0.0f) : controlPoints_[0].position;
    }
    
    int segment;
    float localT;
    getSegmentInfo(t, segment, localT);
    
    switch (splineType_) {
        case SplineType::Linear: {
            const auto& p0 = controlPoints_[wrapIndex(segment)].position;
            const auto& p1 = controlPoints_[wrapIndex(segment + 1)].position;
            return glm::mix(p0, p1, localT);
        }
        
        case SplineType::CatmullRom: {
            const auto& p0 = controlPoints_[wrapIndex(segment - 1)].position;
            const auto& p1 = controlPoints_[wrapIndex(segment)].position;
            const auto& p2 = controlPoints_[wrapIndex(segment + 1)].position;
            const auto& p3 = controlPoints_[wrapIndex(segment + 2)].position;
            return catmullRom(p0, p1, p2, p3, localT);
        }
        
        case SplineType::Bezier: {
            const auto& cp0 = controlPoints_[wrapIndex(segment)];
            const auto& cp1 = controlPoints_[wrapIndex(segment + 1)];
            return bezier(cp0.position, cp0.tangentOut, cp1.tangentIn, cp1.position, localT);
        }
        
        case SplineType::Hermite:
        default:
            // Fallback to Catmull-Rom
            return evaluatePosition(t);
    }
}

glm::vec3 SplineComponent::evaluateTangent(float t) const {
    if (controlPoints_.size() < 2) {
        return glm::vec3(0.0f, 0.0f, 1.0f);
    }
    
    // Numerical derivative
    constexpr float epsilon = 0.001f;
    float t0 = std::max(0.0f, t - epsilon);
    float t1 = std::min(1.0f, t + epsilon);
    
    glm::vec3 p0 = evaluatePosition(t0);
    glm::vec3 p1 = evaluatePosition(t1);
    
    glm::vec3 tangent = p1 - p0;
    float len = glm::length(tangent);
    
    if (len > 0.0001f) {
        return tangent / len;
    }
    
    return glm::vec3(0.0f, 0.0f, 1.0f);
}

glm::vec3 SplineComponent::evaluateUp(float t) const {
    glm::vec3 tangent = evaluateTangent(t);
    glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f);
    
    // Calculate base up (perpendicular to tangent)
    glm::vec3 right = glm::cross(worldUp, tangent);
    float rightLen = glm::length(right);
    
    if (rightLen < 0.0001f) {
        // Tangent is parallel to world up, use different reference
        right = glm::cross(glm::vec3(1.0f, 0.0f, 0.0f), tangent);
        rightLen = glm::length(right);
    }
    
    if (rightLen > 0.0001f) {
        right = right / rightLen;
    }
    
    glm::vec3 up = glm::cross(tangent, right);
    
    // Apply roll
    float roll = evaluateRoll(t);
    if (std::abs(roll) > 0.0001f) {
        glm::mat4 rollMatrix = glm::rotate(glm::mat4(1.0f), roll, tangent);
        up = glm::vec3(rollMatrix * glm::vec4(up, 0.0f));
    }
    
    return glm::normalize(up);
}

glm::vec3 SplineComponent::evaluateRight(float t) const {
    return glm::cross(evaluateUp(t), evaluateTangent(t));
}

glm::quat SplineComponent::evaluateRotation(float t) const {
    glm::vec3 forward = evaluateTangent(t);
    glm::vec3 up = evaluateUp(t);
    
    return glm::quatLookAt(-forward, up);
}

glm::vec3 SplineComponent::evaluateScale(float t) const {
    if (controlPoints_.size() < 2) {
        return controlPoints_.empty() ? glm::vec3(1.0f) : controlPoints_[0].scale;
    }
    
    int segment;
    float localT;
    getSegmentInfo(t, segment, localT);
    
    const auto& s0 = controlPoints_[wrapIndex(segment)].scale;
    const auto& s1 = controlPoints_[wrapIndex(segment + 1)].scale;
    
    return glm::mix(s0, s1, localT);
}

float SplineComponent::evaluateRoll(float t) const {
    if (controlPoints_.size() < 2) {
        return controlPoints_.empty() ? 0.0f : controlPoints_[0].roll;
    }
    
    int segment;
    float localT;
    getSegmentInfo(t, segment, localT);
    
    float r0 = controlPoints_[wrapIndex(segment)].roll;
    float r1 = controlPoints_[wrapIndex(segment + 1)].roll;
    
    return glm::mix(r0, r1, localT);
}

glm::mat4 SplineComponent::evaluateTransform(float t) const {
    glm::vec3 position = evaluatePosition(t);
    glm::quat rotation = evaluateRotation(t);
    glm::vec3 scale = evaluateScale(t);
    
    glm::mat4 T = glm::translate(glm::mat4(1.0f), position);
    glm::mat4 R = glm::mat4_cast(rotation);
    glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);
    
    return worldTransform_ * T * R * S;
}

// ============================================================================
// DISTANCE CONVERSION
// ============================================================================

void SplineComponent::rebuildDistanceTable() {
    distanceTable_.clear();
    
    if (controlPoints_.size() < 2) {
        totalLength_ = 0.0f;
        return;
    }
    
    float totalDist = 0.0f;
    glm::vec3 prevPos = evaluatePosition(0.0f);
    distanceTable_.push_back({0.0f, 0.0f});
    
    for (int i = 1; i <= DISTANCE_TABLE_SAMPLES; i++) {
        float t = static_cast<float>(i) / DISTANCE_TABLE_SAMPLES;
        glm::vec3 pos = evaluatePosition(t);
        totalDist += glm::length(pos - prevPos);
        distanceTable_.push_back({totalDist, t});
        prevPos = pos;
    }
    
    totalLength_ = totalDist;
}

float SplineComponent::distanceToParameter(float distance) const {
    if (distanceTable_.empty() || totalLength_ <= 0.0f) {
        return 0.0f;
    }
    
    // Handle looping
    if (isLoop_) {
        distance = std::fmod(distance, totalLength_);
        if (distance < 0.0f) {
            distance += totalLength_;
        }
    } else {
        distance = std::clamp(distance, 0.0f, totalLength_);
    }
    
    // Binary search
    auto it = std::lower_bound(
        distanceTable_.begin(), distanceTable_.end(), distance,
        [](const SplineDistanceEntry& e, float d) { return e.distance < d; }
    );
    
    if (it == distanceTable_.begin()) return 0.0f;
    if (it == distanceTable_.end()) return 1.0f;
    
    // Interpolate between entries
    auto prev = it - 1;
    float denom = it->distance - prev->distance;
    if (denom < 0.0001f) {
        return prev->parameter;
    }
    
    float alpha = (distance - prev->distance) / denom;
    return glm::mix(prev->parameter, it->parameter, alpha);
}

float SplineComponent::parameterToDistance(float t) const {
    if (distanceTable_.empty()) {
        return 0.0f;
    }
    
    t = std::clamp(t, 0.0f, 1.0f);
    
    // Find entries around this parameter
    size_t index = static_cast<size_t>(t * DISTANCE_TABLE_SAMPLES);
    index = std::min(index, distanceTable_.size() - 1);
    
    if (index == 0) return 0.0f;
    if (index >= distanceTable_.size() - 1) return totalLength_;
    
    const auto& prev = distanceTable_[index];
    const auto& next = distanceTable_[index + 1];
    
    float denom = next.parameter - prev.parameter;
    if (denom < 0.0001f) {
        return prev.distance;
    }
    
    float alpha = (t - prev.parameter) / denom;
    return glm::mix(prev.distance, next.distance, alpha);
}

// ============================================================================
// EVALUATION AT DISTANCE
// ============================================================================

glm::vec3 SplineComponent::getPositionAtDistance(float distance) const {
    return evaluatePosition(distanceToParameter(distance));
}

glm::vec3 SplineComponent::getTangentAtDistance(float distance) const {
    return evaluateTangent(distanceToParameter(distance));
}

glm::vec3 SplineComponent::getUpAtDistance(float distance) const {
    return evaluateUp(distanceToParameter(distance));
}

glm::quat SplineComponent::getRotationAtDistance(float distance) const {
    return evaluateRotation(distanceToParameter(distance));
}

glm::mat4 SplineComponent::getTransformAtDistance(float distance) const {
    return evaluateTransform(distanceToParameter(distance));
}

// ============================================================================
// CLOSEST POINT
// ============================================================================

float SplineComponent::findClosestParameter(const glm::vec3& worldPos) const {
    if (controlPoints_.size() < 2) {
        return 0.0f;
    }
    
    float bestT = 0.0f;
    float bestDist = std::numeric_limits<float>::max();
    
    // Coarse search
    constexpr int COARSE_SAMPLES = 32;
    for (int i = 0; i <= COARSE_SAMPLES; i++) {
        float t = static_cast<float>(i) / COARSE_SAMPLES;
        float dist = glm::length(evaluatePosition(t) - worldPos);
        if (dist < bestDist) {
            bestDist = dist;
            bestT = t;
        }
    }
    
    // Newton-Raphson refinement
    for (int i = 0; i < 5; i++) {
        glm::vec3 pos = evaluatePosition(bestT);
        glm::vec3 tangent = evaluateTangent(bestT);
        glm::vec3 toPoint = worldPos - pos;
        
        float tangentLen = glm::length(tangent);
        if (tangentLen < 0.0001f) break;
        
        float correction = glm::dot(toPoint, tangent) / (tangentLen * tangentLen);
        
        // Damped update
        bestT += correction * 0.1f;
        bestT = std::clamp(bestT, 0.0f, 1.0f);
    }
    
    return bestT;
}

float SplineComponent::findClosestDistance(const glm::vec3& worldPos) const {
    return parameterToDistance(findClosestParameter(worldPos));
}

glm::vec3 SplineComponent::findClosestPoint(const glm::vec3& worldPos) const {
    return evaluatePosition(findClosestParameter(worldPos));
}

// ============================================================================
// TAGS
// ============================================================================

bool SplineComponent::hasTag(const std::string& tag) const {
    return std::find(tags_.begin(), tags_.end(), tag) != tags_.end();
}

// ============================================================================
// AUTO COMPUTE TANGENTS
// ============================================================================

void SplineComponent::autoComputeTangents() {
    if (controlPoints_.size() < 2) return;
    
    for (size_t i = 0; i < controlPoints_.size(); i++) {
        auto& cp = controlPoints_[i];
        
        glm::vec3 prev = controlPoints_[wrapIndex(static_cast<int>(i) - 1)].position;
        glm::vec3 next = controlPoints_[wrapIndex(static_cast<int>(i) + 1)].position;
        
        // Use Catmull-Rom style tangent calculation
        glm::vec3 tangent = (next - prev) * 0.5f;
        
        // Scale tangent to 1/3 of segment length for nice bezier curves
        cp.tangentIn = -tangent * 0.333f;
        cp.tangentOut = tangent * 0.333f;
    }
    
    rebuildDistanceTable();
}

} // namespace Sanic
