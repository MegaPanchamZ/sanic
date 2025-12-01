/**
 * SplineMeshGenerator.h
 * 
 * Generate meshes that follow splines for:
 * - Grind rails
 * - Vines/pipes
 * - Bridges
 * - Tubes
 * 
 * Based on UE5's SplineMeshComponent concepts.
 */

#pragma once

#include "SplineComponent.h"
#include "Mesh.h"
#include "Vertex.h"
#include <glm/glm.hpp>
#include <vector>
#include <memory>

namespace Sanic {

// Forward declarations
class VulkanContext;

// ============================================================================
// SPLINE MESH SETTINGS
// ============================================================================

struct SplineMeshSettings {
    // Base mesh to deform or tile
    Mesh* baseMesh = nullptr;
    
    // Tiling mode
    enum class TileMode {
        Stretch,      // Stretch mesh along entire spline
        Tile,         // Repeat mesh along spline
        TileToFit,    // Tile and stretch last segment to fit
        Deform        // Deform vertices along spline
    };
    TileMode tileMode = TileMode::Tile;
    
    // Tiling
    float tileLength = 2.0f;         // How often to repeat the mesh (for Tile mode)
    bool alignToSpline = true;       // Rotate tiles to follow spline
    
    // Roll/Banking
    bool autoComputeRoll = false;    // Compute banking from curvature
    float rollMultiplier = 1.0f;     // Exaggerate/reduce banking
    float maxAutoRoll = 45.0f;       // Max auto roll in degrees
    
    // Scale
    glm::vec2 scale = glm::vec2(1.0f);  // Scale perpendicular to spline
    bool useSplineScale = true;          // Use spline control point scales
    
    // UV mapping
    enum class UVMode {
        Stretch,      // Stretch UVs along spline length
        Tile,         // Tile UVs at regular intervals
        KeepOriginal  // Keep original mesh UVs
    };
    UVMode uvMode = UVMode::Tile;
    float uvTileScale = 1.0f;
    
    // Mesh orientation
    enum class MeshAxis { X, Y, Z, NegX, NegY, NegZ };
    MeshAxis forwardAxis = MeshAxis::Z;   // Which mesh axis points along spline
    MeshAxis upAxis = MeshAxis::Y;        // Which mesh axis points up
    
    // Collision
    bool generateCollision = true;
    float collisionSimplification = 0.0f;  // 0 = full detail, 1 = very simplified
    
    // LOD
    uint32_t lodLevels = 1;
    float lodDistances[4] = {50.0f, 100.0f, 200.0f, 400.0f};
};

// ============================================================================
// SPLINE MESH INSTANCE
// ============================================================================

struct SplineMeshInstance {
    glm::mat4 transform;
    float startDistance;
    float endDistance;
    uint32_t lodLevel;
};

// ============================================================================
// GENERATED SPLINE MESH
// ============================================================================

struct GeneratedSplineMesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    
    // For instanced rendering
    std::vector<SplineMeshInstance> instances;
    
    // Bounds
    glm::vec3 boundsMin;
    glm::vec3 boundsMax;
    
    // Collision mesh (simplified)
    std::vector<glm::vec3> collisionVertices;
    std::vector<uint32_t> collisionIndices;
};

// ============================================================================
// SPLINE MESH GENERATOR
// ============================================================================

class SplineMeshGenerator {
public:
    SplineMeshGenerator();
    ~SplineMeshGenerator() = default;
    
    // ========== INSTANCE GENERATION ==========
    
    /**
     * Generate instance transforms for tiled mesh along spline
     * Good for rails, fences, etc.
     */
    std::vector<glm::mat4> generateInstanceTransforms(
        const SplineComponent& spline,
        const SplineMeshSettings& settings);
    
    /**
     * Generate instances with more detail
     */
    std::vector<SplineMeshInstance> generateInstances(
        const SplineComponent& spline,
        const SplineMeshSettings& settings);
    
    // ========== MESH DEFORMATION ==========
    
    /**
     * Deform mesh along spline (seamless geometry)
     * Creates continuous geometry following the spline
     */
    GeneratedSplineMesh deformMeshAlongSpline(
        const SplineComponent& spline,
        const Mesh& sourceMesh,
        const SplineMeshSettings& settings);
    
    /**
     * Deform raw vertex/index data along spline
     */
    GeneratedSplineMesh deformMeshAlongSpline(
        const SplineComponent& spline,
        const std::vector<Vertex>& vertices,
        const std::vector<uint32_t>& indices,
        const SplineMeshSettings& settings);
    
    // ========== PROCEDURAL GENERATION ==========
    
    /**
     * Generate a tube/pipe mesh along spline
     */
    GeneratedSplineMesh generateTube(
        const SplineComponent& spline,
        float radius,
        uint32_t radialSegments = 16,
        uint32_t lengthSegments = 0);  // 0 = auto based on length
    
    /**
     * Generate a flat ribbon/road along spline
     */
    GeneratedSplineMesh generateRibbon(
        const SplineComponent& spline,
        float width,
        float thickness = 0.1f,
        uint32_t widthSegments = 1,
        uint32_t lengthSegments = 0);
    
    /**
     * Generate a rail profile along spline
     */
    GeneratedSplineMesh generateRail(
        const SplineComponent& spline,
        float railRadius = 0.05f,
        float railSpacing = 0.5f,
        uint32_t radialSegments = 8);
    
    // ========== COLLISION GENERATION ==========
    
    /**
     * Generate simplified collision mesh
     */
    void generateCollisionMesh(
        GeneratedSplineMesh& mesh,
        const SplineComponent& spline,
        float simplification = 0.5f);
    
private:
    // Mesh axis to vector
    glm::vec3 axisToVector(SplineMeshSettings::MeshAxis axis) const;
    
    // Get mesh bounds
    void computeMeshBounds(
        const std::vector<Vertex>& vertices,
        glm::vec3& outMin, glm::vec3& outMax) const;
    
    // Transform vertex to spline space
    Vertex transformVertexToSpline(
        const Vertex& vertex,
        const SplineComponent& spline,
        float t,
        const SplineMeshSettings& settings,
        const glm::vec3& meshBoundsMin,
        const glm::vec3& meshBoundsMax) const;
    
    // Compute auto roll from curvature
    float computeAutoRoll(
        const SplineComponent& spline,
        float t,
        float multiplier,
        float maxRoll) const;
};

} // namespace Sanic
