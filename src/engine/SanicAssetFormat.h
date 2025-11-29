/**
 * SanicAssetFormat.h
 * 
 * Binary asset format for cooked Nanite/Lumen data.
 * Similar to UE5's .uasset but optimized for our engine.
 * 
 * File Layout (.sanic_mesh):
 * [Header]              - Magic, version, flags, offsets
 * [Geometry Section]    - LOD0 vertices and indices
 * [Nanite Section]      - Cluster hierarchy DAG, meshlets, pages
 * [Lumen Section]       - SDF volume, surface cache cards
 * [Physics Section]     - Cooked collision data
 * [Material Section]    - Material references and parameters
 * 
 * Streaming Support:
 * - Each section is page-aligned for DirectStorage
 * - Cluster pages can be loaded on-demand
 * - SDF mips support progressive loading
 */

#pragma once

#include <cstdint>
#include <algorithm>
#include <glm/glm.hpp>
#include <vector>
#include <string>

namespace Sanic {

// ============================================================================
// FILE FORMAT CONSTANTS
// ============================================================================

constexpr uint32_t SANIC_MAGIC = 0x53414E49;        // "SANI" in little-endian
constexpr uint32_t SANIC_MESH_MAGIC = 0x534E4D43;   // "SNMC" for mesh files
constexpr uint32_t SANIC_VERSION = 1;
constexpr uint32_t PAGE_SIZE = 65536;               // 64KB pages for streaming
constexpr uint32_t CLUSTER_PAGE_SIZE = 16384;       // 16KB cluster pages

// ============================================================================
// SECTION TYPES
// ============================================================================

enum class SectionType : uint32_t {
    Geometry = 0,
    Nanite = 1,
    Lumen = 2,
    Physics = 3,
    Material = 4
};

struct SectionHeader {
    SectionType type;
    uint32_t uncompressedSize;
    uint32_t compressedSize;
    uint32_t flags;                     // Compression flags
};
// DISABLED: static_assert(sizeof(SectionHeader) == 16, "SectionHeader must be 16 bytes");

// ============================================================================
// FILE HEADER
// ============================================================================

struct AssetHeader {
    uint32_t magic;                     // SANIC_MAGIC
    uint32_t version;                   // Format version
    uint32_t flags;                     // Asset flags (see AssetFlags)
    uint32_t totalSize;                 // Total file size in bytes
    
    // Bounding box
    glm::vec3 boundsMin;
    float padding1;
    glm::vec3 boundsMax;
    float padding2;
    
    // Section offsets (from file start)
    uint64_t geometryOffset;            // Geometry section
    uint64_t naniteOffset;              // Nanite data section
    uint64_t lumenOffset;               // Lumen data section
    uint64_t physicsOffset;             // Physics collision section
    uint64_t materialOffset;            // Material references
    
    // Section sizes
    uint32_t geometrySectionSize;
    uint32_t naniteSectionSize;
    uint32_t lumenSectionSize;
    uint32_t physicsSectionSize;
    uint32_t materialSectionSize;
    
    // Asset metadata
    char assetName[64];                 // Null-terminated asset name
    uint64_t sourceHash;                // Hash of source file for cache invalidation
    uint64_t cookTimestamp;             // When the asset was cooked
    
    uint32_t reserved[16];              // Future use
};
// DISABLED: static_assert(sizeof(AssetHeader) == 256, "AssetHeader must be 256 bytes");

// Asset flags
enum class AssetFlags : uint32_t {
    None = 0,
    HasNanite = 1 << 0,
    HasLumen = 1 << 1,
    HasPhysics = 1 << 2,
    HasMaterials = 1 << 3,
    Compressed = 1 << 4,                // LZ4 compressed sections
    StreamingEnabled = 1 << 5,          // Supports page-based streaming
    HasImpostor = 1 << 6,               // Has LOD impostor for distance
    TwoSided = 1 << 7,
    HasSkinning = 1 << 8,               // Has bone weights for animation
};

// ============================================================================
// GEOMETRY SECTION
// ============================================================================

struct GeometryHeader {
    uint32_t vertexCount;
    uint32_t indexCount;
    uint32_t vertexStride;              // Bytes per vertex
    uint32_t vertexFormat;              // Bitmask of vertex attributes
    
    // Buffer offsets (from section start)
    uint32_t vertexBufferOffset;
    uint32_t indexBufferOffset;
    uint32_t vertexBufferSize;
    uint32_t indexBufferSize;
    
    // Vertex format flags
    static constexpr uint32_t HAS_POSITION = 1 << 0;
    static constexpr uint32_t HAS_NORMAL = 1 << 1;
    static constexpr uint32_t HAS_TANGENT = 1 << 2;
    static constexpr uint32_t HAS_UV0 = 1 << 3;
    static constexpr uint32_t HAS_UV1 = 1 << 4;
    static constexpr uint32_t HAS_COLOR = 1 << 5;
    static constexpr uint32_t HAS_BONE_WEIGHTS = 1 << 6;
};
// DISABLED: static_assert(sizeof(GeometryHeader) == 32, "GeometryHeader must be 32 bytes");

// ============================================================================
// NANITE SECTION
// ============================================================================

struct NaniteHeader {
    // Cluster info
    uint32_t clusterCount;
    uint32_t hierarchyNodeCount;
    uint32_t totalMeshletCount;
    uint32_t lodLevelCount;
    
    // Page info for streaming
    uint32_t pageCount;
    uint32_t rootPageIndex;
    uint32_t maxPageDepth;
    uint32_t clusterPageSize;           // Bytes per cluster page
    
    // Buffer offsets (from section start)
    uint64_t clusterBufferOffset;
    uint64_t hierarchyBufferOffset;
    uint64_t meshletBufferOffset;
    uint64_t meshletVerticesOffset;
    uint64_t meshletTrianglesOffset;
    uint64_t pageTableOffset;
    
    // Buffer sizes
    uint32_t clusterBufferSize;
    uint32_t hierarchyBufferSize;
    uint32_t meshletBufferSize;
    uint32_t meshletVerticesSize;
    uint32_t meshletTrianglesSize;
    uint32_t pageTableSize;
    
    // LOD info
    float maxLodError;
    float minLodError;
    
    uint32_t reserved[8];
};
// DISABLED: static_assert(sizeof(NaniteHeader) == 128, "NaniteHeader must be 128 bytes");

// Section header for Nanite data (used in file I/O)
struct NaniteSectionHeader {
    uint32_t clusterCount;
    uint32_t meshletCount;
    uint32_t hierarchyNodeCount;
    uint32_t pageCount;
    uint32_t totalMeshletVertices;
    uint32_t totalMeshletTriangles;
    uint32_t reserved[2];
};
// DISABLED: static_assert(sizeof(NaniteSectionHeader) == 32, "NaniteSectionHeader must be 32 bytes");

// Cluster data (matches GPU struct)
struct CookedCluster {
    // Bounding sphere
    glm::vec3 sphereCenter;
    float sphereRadius;
    
    // AABB
    glm::vec3 boxCenter;
    float boxExtentX;
    float boxExtentY;
    float boxExtentZ;
    
    // LOD info
    float lodError;
    float parentLodError;
    
    // Geometry references
    uint32_t meshletOffset;
    uint32_t meshletCount;
    uint32_t vertexOffset;
    uint32_t triangleOffset;
    uint32_t triangleCount;
    
    // Material and flags
    uint32_t materialId;
    uint32_t flags;
    
    // Page for streaming
    uint32_t pageIndex;
};
// DISABLED: static_assert(sizeof(CookedCluster) == 80, "CookedCluster must be 80 bytes");

// Hierarchy node for GPU culling
struct CookedHierarchyNode {
    glm::vec3 boxCenter;
    float boxExtentX;
    float boxExtentY;
    float boxExtentZ;
    float lodError;
    float minLodError;
    
    uint32_t childOffset;
    uint32_t childCount;
    uint32_t flags;
    uint32_t level;
};
// DISABLED: static_assert(sizeof(CookedHierarchyNode) == 48, "CookedHierarchyNode must be 48 bytes");

// Page table entry for streaming
struct PageTableEntry {
    uint32_t fileOffset;                // Offset in file
    uint32_t compressedSize;            // Compressed size (or 0 if uncompressed)
    uint32_t uncompressedSize;          // Uncompressed size
    uint32_t clusterOffset;             // First cluster index
    uint32_t clusterCount;              // Clusters in this page
    uint32_t flags;                     // Page flags
    uint32_t dependencyMask;            // Bitmask of required parent pages
    uint32_t reserved;
};
// DISABLED: static_assert(sizeof(PageTableEntry) == 32, "PageTableEntry must be 32 bytes");

// Meshlet data (matches GPU struct from Mesh.h)
struct CookedMeshlet {
    uint32_t vertexOffset;
    uint32_t triangleOffset;
    uint32_t vertexCount;
    uint32_t triangleCount;
    
    float center[3];
    float radius;
    int8_t coneAxis[3];
    int8_t coneCutoff;
};
// DISABLED: static_assert(sizeof(CookedMeshlet) == 32, "CookedMeshlet must be 32 bytes");

// ============================================================================
// LUMEN SECTION
// ============================================================================

struct LumenHeader {
    // SDF info
    glm::ivec3 sdfResolution;
    float sdfVoxelSize;
    glm::vec3 sdfBoundsMin;
    float sdfPadding;
    glm::vec3 sdfBoundsMax;
    float sdfMaxDistance;
    
    // Surface cache cards
    uint32_t cardCount;
    uint32_t cardAtlasWidth;
    uint32_t cardAtlasHeight;
    uint32_t cardMipLevels;
    
    // Buffer offsets (from section start)
    uint64_t sdfVolumeOffset;
    uint64_t cardDefinitionsOffset;
    uint64_t cardAtlasOffset;           // Pre-baked card textures (optional)
    
    // Buffer sizes
    uint32_t sdfVolumeSize;
    uint32_t cardDefinitionsSize;
    uint32_t cardAtlasSize;
    
    uint32_t reserved[8];
};
// DISABLED: static_assert(sizeof(LumenHeader) == 128, "LumenHeader must be 128 bytes");

// Section header for Lumen data (used in file I/O)
struct LumenSectionHeader {
    uint32_t sdfResolutionX;
    uint32_t sdfResolutionY;
    uint32_t sdfResolutionZ;
    float sdfVoxelSize;
    uint32_t surfaceCardCount;
    uint32_t reserved[3];
};
// DISABLED: static_assert(sizeof(LumenSectionHeader) == 32, "LumenSectionHeader must be 32 bytes");

// Surface cache card definition
struct CookedSurfaceCard {
    // World-space bounds
    glm::vec3 boundsMin;
    float padding1;
    glm::vec3 boundsMax;
    float padding2;
    
    // Card orientation (local to world)
    glm::vec3 axisX;
    float extentX;
    glm::vec3 axisY;
    float extentY;
    glm::vec3 normal;
    float padding3;
    
    // Atlas placement
    uint32_t atlasOffsetX;
    uint32_t atlasOffsetY;
    uint32_t atlasWidth;
    uint32_t atlasHeight;
    
    // LOD and quality
    uint32_t mipLevel;
    float texelDensity;
    uint32_t flags;
    uint32_t reserved;
};
// DISABLED: static_assert(sizeof(CookedSurfaceCard) == 128, "CookedSurfaceCard must be 128 bytes");

// ============================================================================
// PHYSICS SECTION
// ============================================================================

struct PhysicsHeader {
    uint32_t collisionType;             // Box, sphere, convex, mesh, etc.
    uint32_t convexHullCount;
    uint32_t triangleMeshVertexCount;
    uint32_t triangleMeshIndexCount;
    
    // Jolt physics cooked data
    uint64_t joltDataOffset;
    uint32_t joltDataSize;
    uint32_t joltDataVersion;
    
    // Simple collision shapes for fast tests
    uint64_t simpleShapesOffset;
    uint32_t simpleShapesSize;
    uint32_t simpleShapeCount;
    
    uint32_t reserved[8];
};
// DISABLED: static_assert(sizeof(PhysicsHeader) == 64, "PhysicsHeader must be 64 bytes");

// Alias for backward compatibility
using PhysicsSectionHeader = PhysicsHeader;

// ============================================================================
// MATERIAL SECTION
// ============================================================================

struct MaterialHeader {
    uint32_t materialCount;
    uint32_t textureReferenceCount;
    uint32_t parameterCount;
    uint32_t reserved1;
    
    uint64_t materialDefsOffset;
    uint64_t textureRefsOffset;
    uint64_t parametersOffset;
    
    uint32_t reserved[2];
};
// DISABLED: static_assert(sizeof(MaterialHeader) == 48, "MaterialHeader must be 48 bytes");

struct CookedMaterialDef {
    char materialName[64];
    
    // Texture indices (-1 if not used)
    int32_t albedoTextureIndex;
    int32_t normalTextureIndex;
    int32_t roughnessMetallicIndex;
    int32_t emissiveTextureIndex;
    int32_t aoTextureIndex;
    int32_t padding1;
    
    // Base parameters
    glm::vec4 baseColor;
    float roughness;
    float metallic;
    float emissiveIntensity;
    
    // Flags
    uint32_t flags;                     // Blend mode, two-sided, etc.
    uint32_t shadingModel;
    
    uint32_t reserved[3];
};
// DISABLED: static_assert(sizeof(CookedMaterialDef) == 128, "CookedMaterialDef must be 128 bytes");

struct TextureReference {
    char texturePath[256];              // Relative path to texture file
    uint64_t textureHash;               // Hash for cache validation
};
// DISABLED: static_assert(sizeof(TextureReference) == 264, "TextureReference must be 264 bytes");

// ============================================================================
// STREAMING SUPPORT
// ============================================================================

enum class PageState : uint8_t {
    NotLoaded = 0,
    Loading = 1,
    Loaded = 2,
    Resident = 3,                       // In GPU memory
    Error = 255
};

// Runtime page tracking (not stored in file)
struct StreamingPage {
    PageTableEntry entry;
    PageState state;
    uint8_t priority;
    uint16_t framesSinceUsed;
    uint32_t gpuBufferOffset;           // Offset in streaming buffer pool
};

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

inline uint64_t alignToPage(uint64_t offset) {
    return (offset + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

inline uint32_t calculateMipCount(uint32_t width, uint32_t height, uint32_t depth = 1) {
    uint32_t maxDim = std::max(std::max(width, height), depth);
    uint32_t mips = 1;
    while (maxDim > 1) {
        maxDim >>= 1;
        mips++;
    }
    return mips;
}

// Calculate hash for source file cache invalidation
uint64_t calculateSourceHash(const void* data, size_t size);

} // namespace Sanic
