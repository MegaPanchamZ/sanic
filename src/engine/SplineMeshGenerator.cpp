/**
 * SplineMeshGenerator.cpp
 * 
 * Implementation of spline mesh generation.
 */

#include "SplineMeshGenerator.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace Sanic {

// ============================================================================
// CONSTRUCTOR
// ============================================================================

SplineMeshGenerator::SplineMeshGenerator() = default;

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

glm::vec3 SplineMeshGenerator::axisToVector(SplineMeshSettings::MeshAxis axis) const {
    switch (axis) {
        case SplineMeshSettings::MeshAxis::X:    return glm::vec3(1.0f, 0.0f, 0.0f);
        case SplineMeshSettings::MeshAxis::Y:    return glm::vec3(0.0f, 1.0f, 0.0f);
        case SplineMeshSettings::MeshAxis::Z:    return glm::vec3(0.0f, 0.0f, 1.0f);
        case SplineMeshSettings::MeshAxis::NegX: return glm::vec3(-1.0f, 0.0f, 0.0f);
        case SplineMeshSettings::MeshAxis::NegY: return glm::vec3(0.0f, -1.0f, 0.0f);
        case SplineMeshSettings::MeshAxis::NegZ: return glm::vec3(0.0f, 0.0f, -1.0f);
    }
    return glm::vec3(0.0f, 0.0f, 1.0f);
}

void SplineMeshGenerator::computeMeshBounds(
    const std::vector<Vertex>& vertices,
    glm::vec3& outMin, glm::vec3& outMax) const
{
    if (vertices.empty()) {
        outMin = outMax = glm::vec3(0.0f);
        return;
    }
    
    outMin = outMax = vertices[0].pos;
    for (const auto& v : vertices) {
        outMin = glm::min(outMin, v.pos);
        outMax = glm::max(outMax, v.pos);
    }
}

float SplineMeshGenerator::computeAutoRoll(
    const SplineComponent& spline,
    float t,
    float multiplier,
    float maxRoll) const
{
    // Compute curvature by checking tangent change
    constexpr float epsilon = 0.01f;
    float t0 = std::max(0.0f, t - epsilon);
    float t1 = std::min(1.0f, t + epsilon);
    
    glm::vec3 tangent0 = spline.evaluateTangent(t0);
    glm::vec3 tangent1 = spline.evaluateTangent(t1);
    
    // Cross product gives turning direction
    glm::vec3 turnAxis = glm::cross(tangent0, tangent1);
    float turnMagnitude = glm::length(turnAxis);
    
    if (turnMagnitude < 0.0001f) {
        return 0.0f;  // Going straight
    }
    
    // Determine roll direction based on turn axis
    glm::vec3 up = spline.evaluateUp(t);
    float rollSign = glm::sign(glm::dot(turnAxis, up));
    
    // Scale by curvature and multiplier
    float curvature = turnMagnitude / (2.0f * epsilon);
    float roll = curvature * multiplier * rollSign * 50.0f;  // Arbitrary scale factor
    
    return glm::clamp(roll, -maxRoll, maxRoll);
}

// ============================================================================
// INSTANCE GENERATION
// ============================================================================

std::vector<glm::mat4> SplineMeshGenerator::generateInstanceTransforms(
    const SplineComponent& spline,
    const SplineMeshSettings& settings)
{
    std::vector<glm::mat4> transforms;
    
    float totalLength = spline.getTotalLength();
    if (totalLength < 0.001f || settings.tileLength < 0.001f) {
        return transforms;
    }
    
    int tileCount = static_cast<int>(totalLength / settings.tileLength);
    if (tileCount == 0) tileCount = 1;
    
    transforms.reserve(tileCount);
    
    for (int i = 0; i < tileCount; i++) {
        float distance = i * settings.tileLength + settings.tileLength * 0.5f;
        float param = spline.distanceToParameter(distance);
        
        glm::vec3 position = spline.evaluatePosition(param);
        glm::vec3 tangent = spline.evaluateTangent(param);
        glm::vec3 up = spline.evaluateUp(param);
        glm::vec3 right = glm::cross(tangent, up);
        
        // Apply auto roll if enabled
        if (settings.autoComputeRoll) {
            float autoRoll = computeAutoRoll(spline, param, settings.rollMultiplier, settings.maxAutoRoll);
            glm::mat4 rollMatrix = glm::rotate(glm::mat4(1.0f), glm::radians(autoRoll), tangent);
            up = glm::vec3(rollMatrix * glm::vec4(up, 0.0f));
            right = glm::cross(tangent, up);
        }
        
        // Build rotation matrix
        glm::mat4 rotation(1.0f);
        rotation[0] = glm::vec4(right * settings.scale.x, 0.0f);
        rotation[1] = glm::vec4(up * settings.scale.y, 0.0f);
        rotation[2] = glm::vec4(tangent, 0.0f);
        rotation[3] = glm::vec4(position, 1.0f);
        
        transforms.push_back(rotation);
    }
    
    return transforms;
}

std::vector<SplineMeshInstance> SplineMeshGenerator::generateInstances(
    const SplineComponent& spline,
    const SplineMeshSettings& settings)
{
    std::vector<SplineMeshInstance> instances;
    
    float totalLength = spline.getTotalLength();
    if (totalLength < 0.001f || settings.tileLength < 0.001f) {
        return instances;
    }
    
    int tileCount = static_cast<int>(totalLength / settings.tileLength);
    if (tileCount == 0) tileCount = 1;
    
    instances.reserve(tileCount);
    
    for (int i = 0; i < tileCount; i++) {
        SplineMeshInstance instance;
        
        instance.startDistance = i * settings.tileLength;
        instance.endDistance = instance.startDistance + settings.tileLength;
        instance.lodLevel = 0;
        
        float midDistance = (instance.startDistance + instance.endDistance) * 0.5f;
        float param = spline.distanceToParameter(midDistance);
        
        glm::vec3 position = spline.evaluatePosition(param);
        glm::vec3 tangent = spline.evaluateTangent(param);
        glm::vec3 up = spline.evaluateUp(param);
        glm::vec3 right = glm::cross(tangent, up);
        
        if (settings.autoComputeRoll) {
            float autoRoll = computeAutoRoll(spline, param, settings.rollMultiplier, settings.maxAutoRoll);
            glm::mat4 rollMatrix = glm::rotate(glm::mat4(1.0f), glm::radians(autoRoll), tangent);
            up = glm::vec3(rollMatrix * glm::vec4(up, 0.0f));
            right = glm::cross(tangent, up);
        }
        
        glm::mat4 transform(1.0f);
        transform[0] = glm::vec4(right * settings.scale.x, 0.0f);
        transform[1] = glm::vec4(up * settings.scale.y, 0.0f);
        transform[2] = glm::vec4(tangent, 0.0f);
        transform[3] = glm::vec4(position, 1.0f);
        
        instance.transform = transform;
        instances.push_back(instance);
    }
    
    return instances;
}

// ============================================================================
// MESH DEFORMATION
// ============================================================================

Vertex SplineMeshGenerator::transformVertexToSpline(
    const Vertex& vertex,
    const SplineComponent& spline,
    float t,
    const SplineMeshSettings& settings,
    const glm::vec3& meshBoundsMin,
    const glm::vec3& meshBoundsMax) const
{
    Vertex result = vertex;
    
    // Get spline frame at this parameter
    glm::vec3 splinePos = spline.evaluatePosition(t);
    glm::vec3 tangent = spline.evaluateTangent(t);
    glm::vec3 up = spline.evaluateUp(t);
    glm::vec3 right = glm::cross(tangent, up);
    
    // Get the perpendicular offset from the vertex
    glm::vec3 forwardAxis = axisToVector(settings.forwardAxis);
    glm::vec3 upAxis = axisToVector(settings.upAxis);
    glm::vec3 rightAxis = glm::cross(upAxis, forwardAxis);
    
    // Calculate local offset (perpendicular to spline direction)
    float localRight = glm::dot(vertex.pos, rightAxis);
    float localUp = glm::dot(vertex.pos, upAxis);
    
    // Apply scale
    glm::vec3 splineScale = settings.useSplineScale ? spline.evaluateScale(t) : glm::vec3(1.0f);
    localRight *= settings.scale.x * splineScale.x;
    localUp *= settings.scale.y * splineScale.y;
    
    // Transform position
    result.pos = splinePos + right * localRight + up * localUp;
    
    // Transform normal
    glm::mat3 basis(right, up, tangent);
    glm::vec3 localNormal(
        glm::dot(vertex.normal, rightAxis),
        glm::dot(vertex.normal, upAxis),
        glm::dot(vertex.normal, forwardAxis)
    );
    result.normal = glm::normalize(basis * localNormal);
    
    // UV adjustment based on mode
    if (settings.uvMode == SplineMeshSettings::UVMode::Tile) {
        float splineLength = spline.getTotalLength();
        result.texCoord.y = t * splineLength * settings.uvTileScale;
    }
    
    return result;
}

GeneratedSplineMesh SplineMeshGenerator::deformMeshAlongSpline(
    const SplineComponent& spline,
    const Mesh& sourceMesh,
    const SplineMeshSettings& settings)
{
    return deformMeshAlongSpline(spline, sourceMesh.getVertices(), sourceMesh.getIndices(), settings);
}

GeneratedSplineMesh SplineMeshGenerator::deformMeshAlongSpline(
    const SplineComponent& spline,
    const std::vector<Vertex>& sourceVertices,
    const std::vector<uint32_t>& sourceIndices,
    const SplineMeshSettings& settings)
{
    GeneratedSplineMesh result;
    
    if (sourceVertices.empty()) {
        return result;
    }
    
    // Get mesh bounds
    glm::vec3 meshMin, meshMax;
    computeMeshBounds(sourceVertices, meshMin, meshMax);
    
    // Determine which axis is "forward" along the mesh
    glm::vec3 forwardAxis = axisToVector(settings.forwardAxis);
    float meshLength = glm::dot(meshMax - meshMin, glm::abs(forwardAxis));
    
    if (meshLength < 0.0001f) {
        return result;
    }
    
    // Reserve space
    result.vertices.reserve(sourceVertices.size());
    result.indices = sourceIndices;
    
    // Transform each vertex
    for (const auto& vertex : sourceVertices) {
        // Normalize position along mesh length to get spline parameter
        float localForward = glm::dot(vertex.pos - meshMin, forwardAxis);
        float t = localForward / meshLength;
        t = glm::clamp(t, 0.0f, 1.0f);
        
        Vertex transformed = transformVertexToSpline(
            vertex, spline, t, settings, meshMin, meshMax);
        
        result.vertices.push_back(transformed);
    }
    
    // Compute bounds
    computeMeshBounds(result.vertices, result.boundsMin, result.boundsMax);
    
    // Generate collision if requested
    if (settings.generateCollision) {
        generateCollisionMesh(result, spline, settings.collisionSimplification);
    }
    
    return result;
}

// ============================================================================
// PROCEDURAL GENERATION
// ============================================================================

GeneratedSplineMesh SplineMeshGenerator::generateTube(
    const SplineComponent& spline,
    float radius,
    uint32_t radialSegments,
    uint32_t lengthSegments)
{
    GeneratedSplineMesh result;
    
    float splineLength = spline.getTotalLength();
    if (splineLength < 0.001f) {
        return result;
    }
    
    // Auto determine length segments
    if (lengthSegments == 0) {
        lengthSegments = static_cast<uint32_t>(splineLength / 0.5f);
        lengthSegments = std::max(2u, std::min(lengthSegments, 256u));
    }
    
    uint32_t numRings = lengthSegments + 1;
    result.vertices.reserve(numRings * radialSegments);
    result.indices.reserve(lengthSegments * radialSegments * 6);
    
    // Generate vertices
    for (uint32_t ring = 0; ring < numRings; ring++) {
        float t = static_cast<float>(ring) / lengthSegments;
        
        glm::vec3 center = spline.evaluatePosition(t);
        glm::vec3 tangent = spline.evaluateTangent(t);
        glm::vec3 up = spline.evaluateUp(t);
        glm::vec3 right = glm::cross(tangent, up);
        
        for (uint32_t seg = 0; seg < radialSegments; seg++) {
            float angle = (static_cast<float>(seg) / radialSegments) * glm::two_pi<float>();
            
            glm::vec3 localOffset = glm::cos(angle) * right + glm::sin(angle) * up;
            
            Vertex v;
            v.pos = center + localOffset * radius;
            v.normal = localOffset;
            v.texCoord = glm::vec2(
                static_cast<float>(seg) / radialSegments,
                t * splineLength
            );
            v.color = glm::vec3(1.0f);
            
            result.vertices.push_back(v);
        }
    }
    
    // Generate indices
    for (uint32_t ring = 0; ring < lengthSegments; ring++) {
        for (uint32_t seg = 0; seg < radialSegments; seg++) {
            uint32_t current = ring * radialSegments + seg;
            uint32_t next = ring * radialSegments + (seg + 1) % radialSegments;
            uint32_t currentNext = current + radialSegments;
            uint32_t nextNext = next + radialSegments;
            
            // Two triangles per quad
            result.indices.push_back(current);
            result.indices.push_back(next);
            result.indices.push_back(currentNext);
            
            result.indices.push_back(next);
            result.indices.push_back(nextNext);
            result.indices.push_back(currentNext);
        }
    }
    
    computeMeshBounds(result.vertices, result.boundsMin, result.boundsMax);
    
    return result;
}

GeneratedSplineMesh SplineMeshGenerator::generateRibbon(
    const SplineComponent& spline,
    float width,
    float thickness,
    uint32_t widthSegments,
    uint32_t lengthSegments)
{
    GeneratedSplineMesh result;
    
    float splineLength = spline.getTotalLength();
    if (splineLength < 0.001f) {
        return result;
    }
    
    if (lengthSegments == 0) {
        lengthSegments = static_cast<uint32_t>(splineLength / 0.5f);
        lengthSegments = std::max(2u, std::min(lengthSegments, 256u));
    }
    
    float halfWidth = width * 0.5f;
    uint32_t numLengthVerts = lengthSegments + 1;
    uint32_t numWidthVerts = widthSegments + 1;
    
    result.vertices.reserve(numLengthVerts * numWidthVerts * 2);  // Top and bottom
    
    // Generate top surface
    for (uint32_t l = 0; l < numLengthVerts; l++) {
        float t = static_cast<float>(l) / lengthSegments;
        
        glm::vec3 center = spline.evaluatePosition(t);
        glm::vec3 tangent = spline.evaluateTangent(t);
        glm::vec3 up = spline.evaluateUp(t);
        glm::vec3 right = glm::cross(tangent, up);
        
        for (uint32_t w = 0; w < numWidthVerts; w++) {
            float widthT = static_cast<float>(w) / widthSegments;
            float offset = (widthT - 0.5f) * width;
            
            Vertex v;
            v.pos = center + right * offset + up * (thickness * 0.5f);
            v.normal = up;
            v.texCoord = glm::vec2(widthT, t * splineLength);
            v.color = glm::vec3(1.0f);
            
            result.vertices.push_back(v);
        }
    }
    
    // Generate bottom surface
    for (uint32_t l = 0; l < numLengthVerts; l++) {
        float t = static_cast<float>(l) / lengthSegments;
        
        glm::vec3 center = spline.evaluatePosition(t);
        glm::vec3 tangent = spline.evaluateTangent(t);
        glm::vec3 up = spline.evaluateUp(t);
        glm::vec3 right = glm::cross(tangent, up);
        
        for (uint32_t w = 0; w < numWidthVerts; w++) {
            float widthT = static_cast<float>(w) / widthSegments;
            float offset = (widthT - 0.5f) * width;
            
            Vertex v;
            v.pos = center + right * offset - up * (thickness * 0.5f);
            v.normal = -up;
            v.texCoord = glm::vec2(widthT, t * splineLength);
            v.color = glm::vec3(1.0f);
            
            result.vertices.push_back(v);
        }
    }
    
    // Generate indices for top surface
    uint32_t vertsPerSurface = numLengthVerts * numWidthVerts;
    for (uint32_t l = 0; l < lengthSegments; l++) {
        for (uint32_t w = 0; w < widthSegments; w++) {
            uint32_t current = l * numWidthVerts + w;
            uint32_t next = current + 1;
            uint32_t currentNext = current + numWidthVerts;
            uint32_t nextNext = currentNext + 1;
            
            result.indices.push_back(current);
            result.indices.push_back(next);
            result.indices.push_back(currentNext);
            
            result.indices.push_back(next);
            result.indices.push_back(nextNext);
            result.indices.push_back(currentNext);
        }
    }
    
    // Generate indices for bottom surface (reversed winding)
    for (uint32_t l = 0; l < lengthSegments; l++) {
        for (uint32_t w = 0; w < widthSegments; w++) {
            uint32_t current = vertsPerSurface + l * numWidthVerts + w;
            uint32_t next = current + 1;
            uint32_t currentNext = current + numWidthVerts;
            uint32_t nextNext = currentNext + 1;
            
            result.indices.push_back(current);
            result.indices.push_back(currentNext);
            result.indices.push_back(next);
            
            result.indices.push_back(next);
            result.indices.push_back(currentNext);
            result.indices.push_back(nextNext);
        }
    }
    
    computeMeshBounds(result.vertices, result.boundsMin, result.boundsMax);
    
    return result;
}

GeneratedSplineMesh SplineMeshGenerator::generateRail(
    const SplineComponent& spline,
    float railRadius,
    float railSpacing,
    uint32_t radialSegments)
{
    GeneratedSplineMesh result;
    
    // Generate two parallel tubes for a rail
    GeneratedSplineMesh leftRail = generateTube(spline, railRadius, radialSegments, 0);
    GeneratedSplineMesh rightRail = generateTube(spline, railRadius, radialSegments, 0);
    
    // Offset the rails
    float halfSpacing = railSpacing * 0.5f;
    
    for (size_t i = 0; i < leftRail.vertices.size(); i++) {
        // Get the right vector at this point
        float t = leftRail.vertices[i].texCoord.y / spline.getTotalLength();
        t = glm::clamp(t, 0.0f, 1.0f);
        
        glm::vec3 tangent = spline.evaluateTangent(t);
        glm::vec3 up = spline.evaluateUp(t);
        glm::vec3 right = glm::cross(tangent, up);
        
        leftRail.vertices[i].pos -= right * halfSpacing;
        rightRail.vertices[i].pos += right * halfSpacing;
    }
    
    // Combine meshes
    result.vertices = leftRail.vertices;
    result.indices = leftRail.indices;
    
    uint32_t indexOffset = static_cast<uint32_t>(result.vertices.size());
    result.vertices.insert(result.vertices.end(), rightRail.vertices.begin(), rightRail.vertices.end());
    
    for (uint32_t idx : rightRail.indices) {
        result.indices.push_back(idx + indexOffset);
    }
    
    computeMeshBounds(result.vertices, result.boundsMin, result.boundsMax);
    
    return result;
}

// ============================================================================
// COLLISION GENERATION
// ============================================================================

void SplineMeshGenerator::generateCollisionMesh(
    GeneratedSplineMesh& mesh,
    const SplineComponent& spline,
    float simplification)
{
    // Simple collision - capsules along spline
    uint32_t numCapsules = static_cast<uint32_t>((1.0f - simplification) * 16.0f) + 2;
    numCapsules = std::min(numCapsules, 32u);
    
    mesh.collisionVertices.clear();
    mesh.collisionIndices.clear();
    
    // For now, just use the visual mesh vertices simplified
    // A proper implementation would generate convex hull segments
    
    float step = 1.0f / (numCapsules - 1);
    mesh.collisionVertices.reserve(numCapsules * 2);
    
    for (uint32_t i = 0; i < numCapsules; i++) {
        float t = i * step;
        glm::vec3 pos = spline.evaluatePosition(t);
        mesh.collisionVertices.push_back(pos);
    }
}

} // namespace Sanic
