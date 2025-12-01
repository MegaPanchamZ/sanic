/**
 * terrain_clipmap.vert
 * 
 * Clipmap-based terrain vertex shader with seamless LOD morphing.
 * Based on UE5 Landscape rendering approach.
 * 
 * Features:
 * - Continuous LOD with morphing to prevent popping
 * - Neighbor LOD stitching to prevent seams
 * - Heightmap sampling with bilinear interpolation
 * - Normal reconstruction from heightmap
 */

#version 450
#extension GL_GOOGLE_include_directive : enable

// Per-vertex input (grid position)
layout(location = 0) in vec2 inPosition;  // XZ position in clipmap level grid

// Per-instance data
layout(location = 1) in vec4 instanceData0;  // xy: level center, z: cell size, w: level index
layout(location = 2) in vec4 instanceData1;  // x: morph factor, y: outer radius, z: heightScale, w: heightOffset

// Output to fragment shader
layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec2 outUV;
layout(location = 2) out vec3 outNormal;
layout(location = 3) out vec3 outTangent;
layout(location = 4) out float outMorphFactor;
layout(location = 5) out flat uint outLOD;

// Uniforms
layout(set = 0, binding = 0) uniform TerrainUniforms {
    mat4 viewProjection;
    vec3 cameraPos;
    float time;
    vec3 worldOrigin;          // Terrain world origin
    float pad0;
    vec2 worldSize;            // Total terrain world size
    vec2 texelSize;            // 1 / heightmap resolution
    ivec4 neighborLODs;        // LOD levels of neighbors (N, E, S, W)
    float morphRange;          // Distance over which to morph
    uint numLevels;
    vec2 pad1;
} ubo;

// Heightmap texture
layout(set = 0, binding = 1) uniform sampler2D heightmap;

// Sample height with bilinear interpolation
float sampleHeight(vec2 uv) {
    // Clamp UV to valid range
    uv = clamp(uv, vec2(0.0), vec2(1.0));
    
    float h = texture(heightmap, uv).r;
    return h * instanceData1.z + instanceData1.w;  // heightScale + heightOffset
}

// Sample normal using central differences
vec3 sampleNormal(vec2 uv) {
    vec2 eps = ubo.texelSize;
    
    float hL = sampleHeight(uv - vec2(eps.x, 0.0));
    float hR = sampleHeight(uv + vec2(eps.x, 0.0));
    float hD = sampleHeight(uv - vec2(0.0, eps.y));
    float hU = sampleHeight(uv + vec2(0.0, eps.y));
    
    // Calculate normal from height differences
    vec3 dx = vec3(2.0 * eps.x * ubo.worldSize.x, hR - hL, 0.0);
    vec3 dy = vec3(0.0, hU - hD, 2.0 * eps.y * ubo.worldSize.y);
    
    return normalize(cross(dy, dx));
}

// Calculate morph factor based on distance
float calculateMorphFactor(vec2 localPos, float outerRadius) {
    float distFromCenter = length(localPos);
    float morphStart = outerRadius - ubo.morphRange;
    
    if (distFromCenter > morphStart) {
        float blend = (distFromCenter - morphStart) / ubo.morphRange;
        return smoothstep(0.0, 1.0, blend);
    }
    
    return 0.0;
}

// Morph vertex toward coarser grid position
vec2 morphVertex(vec2 pos, float cellSize, float morphFactor) {
    if (morphFactor <= 0.0) {
        return pos;
    }
    
    // Snap to coarser grid (2x cell size)
    float coarseCellSize = cellSize * 2.0;
    vec2 coarsePos = floor(pos / coarseCellSize) * coarseCellSize;
    
    // Interpolate between fine and coarse position
    return mix(pos, coarsePos, morphFactor);
}

// Handle LOD boundary stitching to prevent seams
vec2 stitchBoundary(vec2 localPos, vec2 gridPos, float cellSize, int level) {
    vec2 result = localPos;
    
    // Check if we're on a boundary
    float levelSize = cellSize * 32.0;  // Assuming 32 cells per level
    float halfSize = levelSize * 0.5;
    
    // North boundary
    if (abs(localPos.y - halfSize) < cellSize * 0.1) {
        if (ubo.neighborLODs.x > level) {
            // Neighbor has coarser LOD, snap to even grid positions
            float coarseCellSize = cellSize * 2.0;
            result.x = floor(result.x / coarseCellSize) * coarseCellSize;
        }
    }
    // East boundary
    if (abs(localPos.x - halfSize) < cellSize * 0.1) {
        if (ubo.neighborLODs.y > level) {
            float coarseCellSize = cellSize * 2.0;
            result.y = floor(result.y / coarseCellSize) * coarseCellSize;
        }
    }
    // South boundary
    if (abs(localPos.y + halfSize) < cellSize * 0.1) {
        if (ubo.neighborLODs.z > level) {
            float coarseCellSize = cellSize * 2.0;
            result.x = floor(result.x / coarseCellSize) * coarseCellSize;
        }
    }
    // West boundary
    if (abs(localPos.x + halfSize) < cellSize * 0.1) {
        if (ubo.neighborLODs.w > level) {
            float coarseCellSize = cellSize * 2.0;
            result.y = floor(result.y / coarseCellSize) * coarseCellSize;
        }
    }
    
    return result;
}

void main() {
    // Extract instance data
    vec2 levelCenter = instanceData0.xy;
    float cellSize = instanceData0.z;
    int level = int(instanceData0.w);
    float providedMorphFactor = instanceData1.x;
    float outerRadius = instanceData1.y;
    
    // Local position relative to level center
    vec2 localPos = inPosition;
    
    // Calculate distance-based morph factor
    float morphFactor = calculateMorphFactor(localPos, outerRadius);
    morphFactor = max(morphFactor, providedMorphFactor);
    
    // Apply LOD morphing
    vec2 morphedLocalPos = morphVertex(localPos, cellSize, morphFactor);
    
    // Handle boundary stitching
    morphedLocalPos = stitchBoundary(morphedLocalPos, inPosition, cellSize, level);
    
    // Calculate world XZ position
    vec2 worldXZ = levelCenter + morphedLocalPos;
    
    // Calculate UV for heightmap sampling
    vec2 uv = (worldXZ - ubo.worldOrigin.xz) / ubo.worldSize;
    
    // Sample height at morphed position
    float height = sampleHeight(uv);
    
    // Build world position
    vec3 worldPos = vec3(worldXZ.x, height, worldXZ.y);
    
    // Sample normal
    vec3 normal = sampleNormal(uv);
    
    // Calculate tangent for normal mapping
    vec3 tangent = normalize(cross(normal, vec3(0.0, 0.0, 1.0)));
    if (length(tangent) < 0.001) {
        tangent = normalize(cross(normal, vec3(1.0, 0.0, 0.0)));
    }
    
    // Output
    outWorldPos = worldPos;
    outUV = uv;
    outNormal = normal;
    outTangent = tangent;
    outMorphFactor = morphFactor;
    outLOD = uint(level);
    
    gl_Position = ubo.viewProjection * vec4(worldPos, 1.0);
}
