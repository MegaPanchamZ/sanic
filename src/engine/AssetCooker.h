/**
 * AssetCooker.h
 * 
 * Offline asset processing tool that generates cooked .sanic_mesh files.
 * Moves expensive computations (meshlet generation, SDF voxelization,
 * cluster hierarchy building) from runtime to cook time.
 * 
 * This is the equivalent of UE5's NaniteBuilder and Lumen baking systems.
 * 
 * Usage (command line):
 *   sanic_cooker.exe --input model.obj --output model.sanic_mesh
 *   sanic_cooker.exe --batch assets_list.txt --output-dir cooked/
 * 
 * Usage (API):
 *   AssetCooker cooker;
 *   cooker.setConfig(config);
 *   cooker.cook("model.obj", "model.sanic_mesh");
 */

#pragma once

#include "SanicAssetFormat.h"
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <glm/glm.hpp>

namespace Sanic {

// ============================================================================
// COOKER CONFIGURATION
// ============================================================================

struct CookerConfig {
    // Nanite settings
    uint32_t maxMeshletsPerCluster = 8;
    uint32_t maxVerticesPerMeshlet = 64;
    uint32_t maxTrianglesPerMeshlet = 124;
    uint32_t maxLodLevels = 8;
    float lodErrorThreshold = 1.0f;         // Screen-space pixels
    float clusterGroupingFactor = 0.5f;     // For meshopt cone culling
    bool generateImpostors = false;
    
    // Lumen settings
    uint32_t sdfResolution = 64;            // Per-mesh SDF resolution
    float sdfPadding = 0.1f;                // Padding around mesh bounds
    uint32_t maxSurfaceCards = 32;
    float cardMinArea = 0.01f;              // Min surface area for card
    float cardTexelDensity = 64.0f;         // Texels per world unit
    bool bakeSurfaceCardTextures = false;   // Pre-bake albedo into cards
    
    // Physics settings
    bool generateConvexHulls = true;
    uint32_t maxConvexHulls = 16;
    uint32_t maxConvexVertices = 64;
    bool generateTriangleMesh = true;
    float physicsMeshSimplification = 0.8f; // Keep 80% of triangles
    
    // Compression
    bool compressPages = true;
    int compressionLevel = 6;               // 1-12 for LZ4HC
    
    // Output
    bool verbose = true;
    bool dryRun = false;
};

// ============================================================================
// INPUT MESH DATA
// ============================================================================

struct InputVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec4 tangent;                      // w = handedness
    glm::vec2 uv0;
    glm::vec2 uv1;
    glm::vec4 color;
    glm::ivec4 boneIndices;
    glm::vec4 boneWeights;
};

struct InputMesh {
    std::vector<InputVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<int32_t> materialIndices;   // Per-triangle material
    
    // Bounds (computed if not provided)
    glm::vec3 boundsMin;
    glm::vec3 boundsMax;
    
    // Metadata
    std::string name;
    std::string sourcePath;
    uint64_t sourceHash;
    
    // Vertex format flags
    uint32_t vertexFormat;
};

struct InputMaterial {
    std::string name;
    std::string albedoTexture;
    std::string normalTexture;
    std::string roughnessMetallicTexture;
    std::string emissiveTexture;
    
    glm::vec4 baseColor = glm::vec4(1.0f);
    float roughness = 0.5f;
    float metallic = 0.0f;
    float emissiveIntensity = 0.0f;
    
    uint32_t flags = 0;
};

struct InputAsset {
    InputMesh mesh;
    std::vector<InputMaterial> materials;
};

// ============================================================================
// COOKING RESULTS
// ============================================================================

struct CookingStats {
    // Input
    uint32_t inputVertices;
    uint32_t inputTriangles;
    uint32_t inputMaterials;
    
    // Nanite output
    uint32_t outputClusters;
    uint32_t outputMeshlets;
    uint32_t outputHierarchyNodes;
    uint32_t outputPages;
    uint32_t outputLodLevels;
    
    // Lumen output
    uint32_t sdfVoxels;
    uint32_t surfaceCards;
    
    // Sizes (bytes)
    uint64_t geometrySize;
    uint64_t naniteSize;
    uint64_t lumenSize;
    uint64_t physicsSize;
    uint64_t totalSize;
    uint64_t compressedSize;
    
    // Timing (ms)
    double meshletGenerationTime;
    double clusterHierarchyTime;
    double sdfGenerationTime;
    double surfaceCardTime;
    double physicsTime;
    double compressionTime;
    double totalTime;
};

// Progress callback
using ProgressCallback = std::function<void(const std::string& stage, float progress)>;

// ============================================================================
// ASSET COOKER
// ============================================================================

class AssetCooker {
public:
    AssetCooker();
    ~AssetCooker();
    
    // Configuration
    void setConfig(const CookerConfig& config);
    const CookerConfig& getConfig() const { return config_; }
    
    // Progress callback
    void setProgressCallback(ProgressCallback callback) { progressCallback_ = callback; }
    
    // Main cooking functions
    bool loadFromOBJ(const std::string& objPath, InputAsset& outAsset);
    bool loadFromGLTF(const std::string& gltfPath, InputAsset& outAsset);
    
    bool cook(const InputAsset& input, const std::string& outputPath);
    bool cookFile(const std::string& inputPath, const std::string& outputPath);
    
    // Batch cooking
    bool cookBatch(const std::vector<std::pair<std::string, std::string>>& files);
    
    // Get last cooking stats
    const CookingStats& getStats() const { return stats_; }
    
    // Get last error message
    const std::string& getLastError() const { return lastError_; }
    
private:
    // Internal cooking stages
    bool buildMeshlets(const InputMesh& mesh,
                       std::vector<CookedMeshlet>& outMeshlets,
                       std::vector<uint32_t>& outMeshletVertices,
                       std::vector<uint8_t>& outMeshletTriangles);
    
    bool buildClusterHierarchy(const InputMesh& mesh,
                               const std::vector<CookedMeshlet>& meshlets,
                               std::vector<CookedCluster>& outClusters,
                               std::vector<CookedHierarchyNode>& outNodes);
    
    bool buildClusterPages(const std::vector<CookedCluster>& clusters,
                           std::vector<PageTableEntry>& outPages);
    
    bool generateSDF(const InputMesh& mesh,
                     std::vector<float>& outSdfVolume,
                     glm::ivec3& outResolution,
                     float& outVoxelSize);
    
    bool generateSurfaceCards(const InputMesh& mesh,
                              std::vector<CookedSurfaceCard>& outCards);
    
    bool generatePhysicsData(const InputMesh& mesh,
                             std::vector<uint8_t>& outJoltData,
                             std::vector<uint8_t>& outSimpleShapes);
    
    // LOD generation
    bool simplifyMesh(const InputMesh& mesh, float targetError,
                      std::vector<InputVertex>& outVertices,
                      std::vector<uint32_t>& outIndices);
    
    // SDF helpers
    float pointTriangleDistance(const glm::vec3& p,
                                const glm::vec3& a,
                                const glm::vec3& b,
                                const glm::vec3& c);
    
    float calculateWindingNumber(const glm::vec3& point,
                                 const std::vector<InputVertex>& vertices,
                                 const std::vector<uint32_t>& indices);
    
    // Surface card generation helpers
    void fitOrientedBoundingBox(const std::vector<glm::vec3>& points,
                                glm::vec3& outCenter,
                                glm::vec3 outAxes[3],
                                glm::vec3& outExtents);
    
    void clusterSurfacePatches(const InputMesh& mesh,
                               std::vector<std::vector<uint32_t>>& outPatches);
    
    // File I/O
    bool writeAssetFile(const std::string& path,
                        const AssetHeader& header,
                        const std::vector<uint8_t>& geometryData,
                        const std::vector<uint8_t>& naniteData,
                        const std::vector<uint8_t>& lumenData,
                        const std::vector<uint8_t>& physicsData,
                        const std::vector<uint8_t>& materialData);
    
    // Compression
    std::vector<uint8_t> compressData(const std::vector<uint8_t>& data);
    
    // Progress reporting
    void reportProgress(const std::string& stage, float progress);
    
    CookerConfig config_;
    CookingStats stats_;
    std::string lastError_;
    ProgressCallback progressCallback_;
};

// ============================================================================
// COMMAND LINE INTERFACE
// ============================================================================

struct CookerCLI {
    static int run(int argc, char* argv[]);
    static void printUsage();
    static void printVersion();
};

} // namespace Sanic
