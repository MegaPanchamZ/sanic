/**
 * cluster_common.glsl
 * 
 * Shared cluster hierarchy data structures for GPU shaders.
 * Must match ClusterHierarchy.h CPU structures exactly.
 * 
 * Based on Unreal Nanite:
 * - NaniteDataDecode.ush
 * - NaniteCulling.ush
 */

#ifndef CLUSTER_COMMON_GLSL
#define CLUSTER_COMMON_GLSL

// ============================================================================
// CLUSTER DATA STRUCTURES
// ============================================================================

/**
 * Cluster bounding data for culling and LOD selection.
 * 48 bytes, matching ClusterBounds in ClusterHierarchy.h
 */
struct ClusterBounds {
    // Bounding sphere for LOD calculation (16 bytes)
    vec3 sphereCenter;          // World-space center
    float sphereRadius;         // Bounding sphere radius
    
    // AABB for frustum/occlusion culling (16 bytes)
    vec3 boxCenter;             // AABB center
    float boxExtentX;           // AABB half-extent X
    
    // More AABB + LOD (16 bytes)
    float boxExtentY;           // AABB half-extent Y
    float boxExtentZ;           // AABB half-extent Z
    float lodError;             // Screen-space error when this cluster is used
    float parentLodError;       // Parent's error (for LOD selection)
};

/**
 * Cluster geometry data - references into meshlet buffers.
 * 32 bytes, matching ClusterGeometry in ClusterHierarchy.h
 */
struct ClusterGeometry {
    uint meshletOffset;         // First meshlet index in the meshlet buffer
    uint meshletCount;          // Number of meshlets in this cluster
    uint vertexOffset;          // Offset into vertex buffer
    uint triangleOffset;        // Offset into triangle buffer
    
    uint triangleCount;         // Number of triangles in this cluster
    uint flags;                 // Cluster flags (two-sided, masked, etc.)
    uint materialId;            // Material index for shading
    uint padding;               // Padding to 32 bytes
};

/**
 * Complete cluster data.
 * 80 bytes, matching Cluster in ClusterHierarchy.h
 */
struct Cluster {
    ClusterBounds bounds;       // 48 bytes
    ClusterGeometry geometry;   // 32 bytes
};

// ============================================================================
// HIERARCHY NODE STRUCTURES
// ============================================================================

/**
 * BVH hierarchy node for cluster DAG traversal.
 * 48 bytes, matching HierarchyNode in ClusterHierarchy.h
 */
struct HierarchyNode {
    // Bounding box for this node - 16 bytes
    vec3 boxCenter;
    float boxExtentX;
    
    // More bounds + LOD - 16 bytes
    float boxExtentY;
    float boxExtentZ;
    float lodError;             // Max LOD error of all children
    float minLodError;          // Min LOD error (for early-out)
    
    // Child references - 16 bytes
    uint childOffset;           // Index of first child
    uint childCount;            // Number of children
    uint flags;                 // NODE_FLAG_* bits
    uint level;                 // Hierarchy level
};

// Node flags
#define NODE_FLAG_LEAF          0x1
#define NODE_FLAG_STREAMING     0x2
#define NODE_FLAG_HAS_IMPOSTOR  0x4

/**
 * Candidate node for GPU culling queue.
 * 16 bytes, matching CandidateNode in ClusterHierarchy.h
 */
struct CandidateNode {
    uint nodeIndex;             // Index into hierarchy node array
    uint instanceId;            // Instance this node belongs to
    uint flags;                 // Culling pass flags
    uint padding;
};

/**
 * Visible cluster output from culling.
 * 16 bytes, matching VisibleCluster in ClusterHierarchy.h
 */
struct VisibleCluster {
    uint clusterIndex;          // Index into cluster array
    uint instanceId;            // Instance ID for transforms
    uint flags;                 // Rasterization flags (SW/HW, etc.)
    uint pageIndex;             // For streaming/caching
};

// Visible cluster flags
#define VISIBLE_FLAG_HW_RASTER  0x1     // Use hardware rasterizer
#define VISIBLE_FLAG_SW_RASTER  0x2     // Use software rasterizer
#define VISIBLE_FLAG_NEEDS_CLIP 0x4     // Needs triangle clipping

// ============================================================================
// QUEUE STATE
// ============================================================================

/**
 * Queue state for persistent thread hierarchy traversal.
 * 64 bytes, matching QueueState in ClusterHierarchy.h
 */
struct QueueState {
    // Candidate queue state (ping-pong)
    uint candidateCountA;       // Candidates in queue A
    uint candidateCountB;       // Candidates in queue B
    uint currentIteration;      // Current iteration (determines which queue to read)
    uint maxCandidates;         // Maximum candidates per queue
    
    // Output state
    uint visibleClusterCount;   // Number of visible clusters written
    uint maxVisibleClusters;    // Maximum visible clusters
    uint totalNodesProcessed;   // Stats: nodes processed
    uint totalClustersTested;   // Stats: clusters tested
    
    // Per-frame state
    uint frameIndex;            // Current frame
    uint passIndex;             // Current culling pass
    uint flags;                 // State flags
    uint padding;               // Alignment
    
    // Reserved for future
    uint reserved[4];
};

// ============================================================================
// CULLING PASS DEFINITIONS
// ============================================================================

#define CULLING_PASS_NO_OCCLUSION   0
#define CULLING_PASS_OCCLUSION_MAIN 1
#define CULLING_PASS_OCCLUSION_POST 2

#endif // CLUSTER_COMMON_GLSL
