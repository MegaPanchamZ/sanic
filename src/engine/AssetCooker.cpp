/**
 * AssetCooker.cpp
 * 
 * Implementation of the offline asset cooking pipeline.
 * Generates .sanic_mesh files with pre-computed Nanite/Lumen data.
 */

#include "AssetCooker.h"
#include "SanicAssetFormat.h"

#include <fstream>
#include <iostream>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <unordered_map>
#include <queue>

// External libraries
#include <meshoptimizer.h>

// For OBJ loading (using our existing tiny_obj_loader - implementation is in tiny_obj_loader_impl.cpp)
#include "../../external/tiny_obj_loader.h"

namespace Sanic {

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

uint64_t calculateSourceHash(const void* data, size_t size) {
    // FNV-1a hash
    uint64_t hash = 14695981039346656037ULL;
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; i++) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static double getCurrentTimeMs() {
    using namespace std::chrono;
    return duration_cast<duration<double, std::milli>>(
        high_resolution_clock::now().time_since_epoch()
    ).count();
}

// ============================================================================
// ASSET COOKER IMPLEMENTATION
// ============================================================================

AssetCooker::AssetCooker() {
    stats_ = {};
}

AssetCooker::~AssetCooker() = default;

void AssetCooker::setConfig(const CookerConfig& config) {
    config_ = config;
}

void AssetCooker::reportProgress(const std::string& stage, float progress) {
    if (progressCallback_) {
        progressCallback_(stage, progress);
    }
    if (config_.verbose) {
        std::cout << "[" << static_cast<int>(progress * 100) << "%] " << stage << std::endl;
    }
}

bool AssetCooker::loadFromOBJ(const std::string& objPath, InputAsset& outAsset) {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;
    
    // Get directory for material loading
    std::string baseDir = objPath.substr(0, objPath.find_last_of("/\\") + 1);
    
    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, objPath.c_str(), baseDir.c_str())) {
        lastError_ = "Failed to load OBJ: " + err;
        return false;
    }
    
    if (!warn.empty() && config_.verbose) {
        std::cout << "OBJ Warning: " << warn << std::endl;
    }
    
    // Convert to InputMesh format
    outAsset.mesh.name = objPath.substr(objPath.find_last_of("/\\") + 1);
    outAsset.mesh.sourcePath = objPath;
    
    // Compute vertex format
    outAsset.mesh.vertexFormat = GeometryHeader::HAS_POSITION;
    if (!attrib.normals.empty()) outAsset.mesh.vertexFormat |= GeometryHeader::HAS_NORMAL;
    if (!attrib.texcoords.empty()) outAsset.mesh.vertexFormat |= GeometryHeader::HAS_UV0;
    
    // Build unique vertices with index deduplication
    std::unordered_map<std::string, uint32_t> uniqueVertices;
    
    outAsset.mesh.boundsMin = glm::vec3(std::numeric_limits<float>::max());
    outAsset.mesh.boundsMax = glm::vec3(std::numeric_limits<float>::lowest());
    
    for (const auto& shape : shapes) {
        size_t indexOffset = 0;
        
        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {
            int fv = shape.mesh.num_face_vertices[f];
            
            // Get material for this face
            int materialId = shape.mesh.material_ids.empty() ? 0 : shape.mesh.material_ids[f];
            
            for (int v = 0; v < fv; v++) {
                tinyobj::index_t idx = shape.mesh.indices[indexOffset + v];
                
                InputVertex vertex{};
                
                // Position
                vertex.position = glm::vec3(
                    attrib.vertices[3 * idx.vertex_index + 0],
                    attrib.vertices[3 * idx.vertex_index + 1],
                    attrib.vertices[3 * idx.vertex_index + 2]
                );
                
                // Update bounds
                outAsset.mesh.boundsMin = glm::min(outAsset.mesh.boundsMin, vertex.position);
                outAsset.mesh.boundsMax = glm::max(outAsset.mesh.boundsMax, vertex.position);
                
                // Normal
                if (idx.normal_index >= 0) {
                    vertex.normal = glm::vec3(
                        attrib.normals[3 * idx.normal_index + 0],
                        attrib.normals[3 * idx.normal_index + 1],
                        attrib.normals[3 * idx.normal_index + 2]
                    );
                }
                
                // UV
                if (idx.texcoord_index >= 0) {
                    vertex.uv0 = glm::vec2(
                        attrib.texcoords[2 * idx.texcoord_index + 0],
                        1.0f - attrib.texcoords[2 * idx.texcoord_index + 1]
                    );
                }
                
                // Default values
                vertex.color = glm::vec4(1.0f);
                vertex.tangent = glm::vec4(1, 0, 0, 1);
                
                // Create unique key for deduplication
                std::string key;
                key.resize(sizeof(InputVertex));
                memcpy(key.data(), &vertex, sizeof(InputVertex));
                
                auto it = uniqueVertices.find(key);
                if (it != uniqueVertices.end()) {
                    outAsset.mesh.indices.push_back(it->second);
                } else {
                    uint32_t newIndex = static_cast<uint32_t>(outAsset.mesh.vertices.size());
                    uniqueVertices[key] = newIndex;
                    outAsset.mesh.vertices.push_back(vertex);
                    outAsset.mesh.indices.push_back(newIndex);
                }
            }
            
            // Material per triangle
            outAsset.mesh.materialIndices.push_back(materialId);
            
            indexOffset += fv;
        }
    }
    
    // Convert materials
    for (const auto& mat : materials) {
        InputMaterial inMat;
        inMat.name = mat.name;
        inMat.albedoTexture = mat.diffuse_texname.empty() ? "" : baseDir + mat.diffuse_texname;
        inMat.normalTexture = mat.bump_texname.empty() ? "" : baseDir + mat.bump_texname;
        inMat.roughnessMetallicTexture = mat.specular_texname.empty() ? "" : baseDir + mat.specular_texname;
        inMat.baseColor = glm::vec4(mat.diffuse[0], mat.diffuse[1], mat.diffuse[2], 1.0f);
        inMat.roughness = 1.0f - mat.shininess / 1000.0f;  // Rough conversion
        inMat.metallic = mat.metallic;
        outAsset.materials.push_back(inMat);
    }
    
    // If no materials, add a default
    if (outAsset.materials.empty()) {
        InputMaterial defaultMat;
        defaultMat.name = "DefaultMaterial";
        defaultMat.baseColor = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
        outAsset.materials.push_back(defaultMat);
    }
    
    // Calculate source hash
    outAsset.mesh.sourceHash = calculateSourceHash(
        outAsset.mesh.vertices.data(),
        outAsset.mesh.vertices.size() * sizeof(InputVertex)
    );
    
    if (config_.verbose) {
        std::cout << "Loaded OBJ: " << outAsset.mesh.vertices.size() << " vertices, "
                  << outAsset.mesh.indices.size() / 3 << " triangles, "
                  << outAsset.materials.size() << " materials" << std::endl;
    }
    
    return true;
}

bool AssetCooker::loadFromGLTF(const std::string& gltfPath, InputAsset& outAsset) {
    // TODO: Implement GLTF loading using tinygltf or similar
    lastError_ = "GLTF loading not yet implemented";
    return false;
}

bool AssetCooker::cook(const InputAsset& input, const std::string& outputPath) {
    double startTime = getCurrentTimeMs();
    stats_ = {};
    
    stats_.inputVertices = static_cast<uint32_t>(input.mesh.vertices.size());
    stats_.inputTriangles = static_cast<uint32_t>(input.mesh.indices.size() / 3);
    stats_.inputMaterials = static_cast<uint32_t>(input.materials.size());
    
    if (config_.dryRun) {
        std::cout << "Dry run - would cook " << input.mesh.name << std::endl;
        return true;
    }
    
    reportProgress("Starting cook", 0.0f);
    
    // ========================================================================
    // STAGE 1: Build Meshlets
    // ========================================================================
    reportProgress("Building meshlets", 0.1f);
    double meshletStart = getCurrentTimeMs();
    
    std::vector<CookedMeshlet> meshlets;
    std::vector<uint32_t> meshletVertices;
    std::vector<uint8_t> meshletTriangles;
    
    if (!buildMeshlets(input.mesh, meshlets, meshletVertices, meshletTriangles)) {
        return false;
    }
    
    stats_.meshletGenerationTime = getCurrentTimeMs() - meshletStart;
    stats_.outputMeshlets = static_cast<uint32_t>(meshlets.size());
    
    // ========================================================================
    // STAGE 2: Build Cluster Hierarchy
    // ========================================================================
    reportProgress("Building cluster hierarchy", 0.25f);
    double clusterStart = getCurrentTimeMs();
    
    std::vector<CookedCluster> clusters;
    std::vector<CookedHierarchyNode> hierarchyNodes;
    
    if (!buildClusterHierarchy(input.mesh, meshlets, clusters, hierarchyNodes)) {
        return false;
    }
    
    stats_.clusterHierarchyTime = getCurrentTimeMs() - clusterStart;
    stats_.outputClusters = static_cast<uint32_t>(clusters.size());
    stats_.outputHierarchyNodes = static_cast<uint32_t>(hierarchyNodes.size());
    
    // ========================================================================
    // STAGE 3: Build Cluster Pages
    // ========================================================================
    reportProgress("Building cluster pages", 0.35f);
    
    std::vector<PageTableEntry> pages;
    if (!buildClusterPages(clusters, pages)) {
        return false;
    }
    stats_.outputPages = static_cast<uint32_t>(pages.size());
    
    // ========================================================================
    // STAGE 4: Generate SDF
    // ========================================================================
    reportProgress("Generating SDF", 0.45f);
    double sdfStart = getCurrentTimeMs();
    
    std::vector<float> sdfVolume;
    glm::ivec3 sdfResolution;
    float sdfVoxelSize;
    
    if (!generateSDF(input.mesh, sdfVolume, sdfResolution, sdfVoxelSize)) {
        return false;
    }
    
    stats_.sdfGenerationTime = getCurrentTimeMs() - sdfStart;
    stats_.sdfVoxels = sdfResolution.x * sdfResolution.y * sdfResolution.z;
    
    // ========================================================================
    // STAGE 5: Generate Surface Cards
    // ========================================================================
    reportProgress("Generating surface cards", 0.6f);
    double cardStart = getCurrentTimeMs();
    
    std::vector<CookedSurfaceCard> surfaceCards;
    if (!generateSurfaceCards(input.mesh, surfaceCards)) {
        return false;
    }
    
    stats_.surfaceCardTime = getCurrentTimeMs() - cardStart;
    stats_.surfaceCards = static_cast<uint32_t>(surfaceCards.size());
    
    // ========================================================================
    // STAGE 6: Generate Physics Data
    // ========================================================================
    reportProgress("Generating physics data", 0.75f);
    double physicsStart = getCurrentTimeMs();
    
    std::vector<uint8_t> joltData;
    std::vector<uint8_t> simpleShapes;
    
    if (!generatePhysicsData(input.mesh, joltData, simpleShapes)) {
        // Physics is optional - continue without it
        if (config_.verbose) {
            std::cout << "Warning: Physics generation failed" << std::endl;
        }
    }
    
    stats_.physicsTime = getCurrentTimeMs() - physicsStart;
    
    // ========================================================================
    // STAGE 7: Assemble Sections
    // ========================================================================
    reportProgress("Assembling sections", 0.85f);
    
    // Geometry section
    std::vector<uint8_t> geometryData;
    {
        GeometryHeader geoHeader{};
        geoHeader.vertexCount = static_cast<uint32_t>(input.mesh.vertices.size());
        geoHeader.indexCount = static_cast<uint32_t>(input.mesh.indices.size());
        geoHeader.vertexStride = sizeof(InputVertex);
        geoHeader.vertexFormat = input.mesh.vertexFormat;
        geoHeader.vertexBufferOffset = sizeof(GeometryHeader);
        geoHeader.vertexBufferSize = geoHeader.vertexCount * sizeof(InputVertex);
        geoHeader.indexBufferOffset = geoHeader.vertexBufferOffset + geoHeader.vertexBufferSize;
        geoHeader.indexBufferSize = geoHeader.indexCount * sizeof(uint32_t);
        
        geometryData.resize(sizeof(GeometryHeader) + geoHeader.vertexBufferSize + geoHeader.indexBufferSize);
        memcpy(geometryData.data(), &geoHeader, sizeof(GeometryHeader));
        memcpy(geometryData.data() + geoHeader.vertexBufferOffset, 
               input.mesh.vertices.data(), geoHeader.vertexBufferSize);
        memcpy(geometryData.data() + geoHeader.indexBufferOffset,
               input.mesh.indices.data(), geoHeader.indexBufferSize);
    }
    stats_.geometrySize = geometryData.size();
    
    // Nanite section
    std::vector<uint8_t> naniteData;
    {
        NaniteHeader naniteHeader{};
        naniteHeader.clusterCount = static_cast<uint32_t>(clusters.size());
        naniteHeader.hierarchyNodeCount = static_cast<uint32_t>(hierarchyNodes.size());
        naniteHeader.totalMeshletCount = static_cast<uint32_t>(meshlets.size());
        naniteHeader.lodLevelCount = stats_.outputLodLevels;
        naniteHeader.pageCount = static_cast<uint32_t>(pages.size());
        naniteHeader.rootPageIndex = 0;
        naniteHeader.maxPageDepth = 0;
        naniteHeader.clusterPageSize = CLUSTER_PAGE_SIZE;
        
        uint64_t offset = sizeof(NaniteHeader);
        
        naniteHeader.clusterBufferOffset = offset;
        naniteHeader.clusterBufferSize = clusters.size() * sizeof(CookedCluster);
        offset += naniteHeader.clusterBufferSize;
        
        naniteHeader.hierarchyBufferOffset = offset;
        naniteHeader.hierarchyBufferSize = hierarchyNodes.size() * sizeof(CookedHierarchyNode);
        offset += naniteHeader.hierarchyBufferSize;
        
        naniteHeader.meshletBufferOffset = offset;
        naniteHeader.meshletBufferSize = meshlets.size() * sizeof(CookedMeshlet);
        offset += naniteHeader.meshletBufferSize;
        
        naniteHeader.meshletVerticesOffset = offset;
        naniteHeader.meshletVerticesSize = meshletVertices.size() * sizeof(uint32_t);
        offset += naniteHeader.meshletVerticesSize;
        
        naniteHeader.meshletTrianglesOffset = offset;
        naniteHeader.meshletTrianglesSize = meshletTriangles.size() * sizeof(uint8_t);
        offset += naniteHeader.meshletTrianglesSize;
        
        naniteHeader.pageTableOffset = offset;
        naniteHeader.pageTableSize = pages.size() * sizeof(PageTableEntry);
        offset += naniteHeader.pageTableSize;
        
        naniteData.resize(offset);
        memcpy(naniteData.data(), &naniteHeader, sizeof(NaniteHeader));
        memcpy(naniteData.data() + naniteHeader.clusterBufferOffset, clusters.data(), naniteHeader.clusterBufferSize);
        memcpy(naniteData.data() + naniteHeader.hierarchyBufferOffset, hierarchyNodes.data(), naniteHeader.hierarchyBufferSize);
        memcpy(naniteData.data() + naniteHeader.meshletBufferOffset, meshlets.data(), naniteHeader.meshletBufferSize);
        memcpy(naniteData.data() + naniteHeader.meshletVerticesOffset, meshletVertices.data(), naniteHeader.meshletVerticesSize);
        memcpy(naniteData.data() + naniteHeader.meshletTrianglesOffset, meshletTriangles.data(), naniteHeader.meshletTrianglesSize);
        memcpy(naniteData.data() + naniteHeader.pageTableOffset, pages.data(), naniteHeader.pageTableSize);
    }
    stats_.naniteSize = naniteData.size();
    
    // Lumen section
    std::vector<uint8_t> lumenData;
    {
        LumenHeader lumenHeader{};
        lumenHeader.sdfResolution = sdfResolution;
        lumenHeader.sdfVoxelSize = sdfVoxelSize;
        lumenHeader.sdfBoundsMin = input.mesh.boundsMin - glm::vec3(config_.sdfPadding);
        lumenHeader.sdfBoundsMax = input.mesh.boundsMax + glm::vec3(config_.sdfPadding);
        lumenHeader.sdfMaxDistance = glm::length(input.mesh.boundsMax - input.mesh.boundsMin);
        lumenHeader.cardCount = static_cast<uint32_t>(surfaceCards.size());
        lumenHeader.cardAtlasWidth = 0;
        lumenHeader.cardAtlasHeight = 0;
        lumenHeader.cardMipLevels = 0;
        
        uint64_t offset = sizeof(LumenHeader);
        
        lumenHeader.sdfVolumeOffset = offset;
        lumenHeader.sdfVolumeSize = sdfVolume.size() * sizeof(float);
        offset += lumenHeader.sdfVolumeSize;
        
        lumenHeader.cardDefinitionsOffset = offset;
        lumenHeader.cardDefinitionsSize = surfaceCards.size() * sizeof(CookedSurfaceCard);
        offset += lumenHeader.cardDefinitionsSize;
        
        lumenHeader.cardAtlasOffset = 0;
        lumenHeader.cardAtlasSize = 0;
        
        lumenData.resize(offset);
        memcpy(lumenData.data(), &lumenHeader, sizeof(LumenHeader));
        memcpy(lumenData.data() + lumenHeader.sdfVolumeOffset, sdfVolume.data(), lumenHeader.sdfVolumeSize);
        if (!surfaceCards.empty()) {
            memcpy(lumenData.data() + lumenHeader.cardDefinitionsOffset, surfaceCards.data(), lumenHeader.cardDefinitionsSize);
        }
    }
    stats_.lumenSize = lumenData.size();
    
    // Physics section
    std::vector<uint8_t> physicsData;
    {
        PhysicsHeader physicsHeader{};
        physicsHeader.collisionType = joltData.empty() ? 0 : 1;
        physicsHeader.joltDataOffset = sizeof(PhysicsHeader);
        physicsHeader.joltDataSize = static_cast<uint32_t>(joltData.size());
        physicsHeader.simpleShapesOffset = physicsHeader.joltDataOffset + physicsHeader.joltDataSize;
        physicsHeader.simpleShapesSize = static_cast<uint32_t>(simpleShapes.size());
        
        physicsData.resize(sizeof(PhysicsHeader) + joltData.size() + simpleShapes.size());
        memcpy(physicsData.data(), &physicsHeader, sizeof(PhysicsHeader));
        if (!joltData.empty()) {
            memcpy(physicsData.data() + physicsHeader.joltDataOffset, joltData.data(), joltData.size());
        }
        if (!simpleShapes.empty()) {
            memcpy(physicsData.data() + physicsHeader.simpleShapesOffset, simpleShapes.data(), simpleShapes.size());
        }
    }
    stats_.physicsSize = physicsData.size();
    
    // Material section
    std::vector<uint8_t> materialData;
    {
        MaterialHeader matHeader{};
        matHeader.materialCount = static_cast<uint32_t>(input.materials.size());
        matHeader.materialDefsOffset = sizeof(MaterialHeader);
        
        size_t matDefsSize = input.materials.size() * sizeof(CookedMaterialDef);
        materialData.resize(sizeof(MaterialHeader) + matDefsSize);
        memcpy(materialData.data(), &matHeader, sizeof(MaterialHeader));
        
        for (size_t i = 0; i < input.materials.size(); i++) {
            CookedMaterialDef matDef{};
            strncpy(matDef.materialName, input.materials[i].name.c_str(), 63);
            matDef.baseColor = input.materials[i].baseColor;
            matDef.roughness = input.materials[i].roughness;
            matDef.metallic = input.materials[i].metallic;
            matDef.albedoTextureIndex = -1;
            matDef.normalTextureIndex = -1;
            matDef.roughnessMetallicIndex = -1;
            matDef.emissiveTextureIndex = -1;
            matDef.aoTextureIndex = -1;
            
            memcpy(materialData.data() + sizeof(MaterialHeader) + i * sizeof(CookedMaterialDef),
                   &matDef, sizeof(CookedMaterialDef));
        }
    }
    
    // ========================================================================
    // STAGE 8: Write File
    // ========================================================================
    reportProgress("Writing output file", 0.95f);
    
    AssetHeader header{};
    header.magic = SANIC_MAGIC;
    header.version = SANIC_VERSION;
    header.flags = static_cast<uint32_t>(AssetFlags::HasNanite) |
                   static_cast<uint32_t>(AssetFlags::HasLumen);
    if (!joltData.empty()) {
        header.flags |= static_cast<uint32_t>(AssetFlags::HasPhysics);
    }
    if (!input.materials.empty()) {
        header.flags |= static_cast<uint32_t>(AssetFlags::HasMaterials);
    }
    
    header.boundsMin = input.mesh.boundsMin;
    header.boundsMax = input.mesh.boundsMax;
    
    strncpy(header.assetName, input.mesh.name.c_str(), 63);
    header.sourceHash = input.mesh.sourceHash;
    header.cookTimestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    if (!writeAssetFile(outputPath, header, geometryData, naniteData, lumenData, physicsData, materialData)) {
        return false;
    }
    
    stats_.totalSize = stats_.geometrySize + stats_.naniteSize + stats_.lumenSize + 
                       stats_.physicsSize + materialData.size() + sizeof(AssetHeader);
    stats_.totalTime = getCurrentTimeMs() - startTime;
    
    reportProgress("Complete", 1.0f);
    
    if (config_.verbose) {
        std::cout << "\nCooking complete: " << outputPath << std::endl;
        std::cout << "  Meshlets: " << stats_.outputMeshlets << std::endl;
        std::cout << "  Clusters: " << stats_.outputClusters << std::endl;
        std::cout << "  Hierarchy nodes: " << stats_.outputHierarchyNodes << std::endl;
        std::cout << "  SDF voxels: " << stats_.sdfVoxels << std::endl;
        std::cout << "  Surface cards: " << stats_.surfaceCards << std::endl;
        std::cout << "  Total size: " << stats_.totalSize / 1024 << " KB" << std::endl;
        std::cout << "  Total time: " << stats_.totalTime << " ms" << std::endl;
    }
    
    return true;
}

bool AssetCooker::cookFile(const std::string& inputPath, const std::string& outputPath) {
    InputAsset asset;
    
    // Determine format from extension
    std::string ext = inputPath.substr(inputPath.find_last_of('.') + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    if (ext == "obj") {
        if (!loadFromOBJ(inputPath, asset)) {
            return false;
        }
    } else if (ext == "gltf" || ext == "glb") {
        if (!loadFromGLTF(inputPath, asset)) {
            return false;
        }
    } else {
        lastError_ = "Unsupported format: " + ext;
        return false;
    }
    
    return cook(asset, outputPath);
}

bool AssetCooker::cookBatch(const std::vector<std::pair<std::string, std::string>>& files) {
    bool allSuccess = true;
    
    for (size_t i = 0; i < files.size(); i++) {
        if (config_.verbose) {
            std::cout << "\n[" << (i + 1) << "/" << files.size() << "] Cooking: " 
                      << files[i].first << std::endl;
        }
        
        if (!cookFile(files[i].first, files[i].second)) {
            std::cerr << "Failed to cook: " << files[i].first << " - " << lastError_ << std::endl;
            allSuccess = false;
        }
    }
    
    return allSuccess;
}

// ============================================================================
// MESHLET BUILDING
// ============================================================================

bool AssetCooker::buildMeshlets(const InputMesh& mesh,
                                 std::vector<CookedMeshlet>& outMeshlets,
                                 std::vector<uint32_t>& outMeshletVertices,
                                 std::vector<uint8_t>& outMeshletTriangles) {
    const size_t maxMeshlets = meshopt_buildMeshletsBound(
        mesh.indices.size(), 
        config_.maxVerticesPerMeshlet, 
        config_.maxTrianglesPerMeshlet
    );
    
    std::vector<meshopt_Meshlet> meshlets(maxMeshlets);
    std::vector<unsigned int> meshletVertices(maxMeshlets * config_.maxVerticesPerMeshlet);
    std::vector<unsigned char> meshletTriangles(maxMeshlets * config_.maxTrianglesPerMeshlet * 3);
    
    // Build meshlets using meshoptimizer
    size_t meshletCount = meshopt_buildMeshlets(
        meshlets.data(),
        meshletVertices.data(),
        meshletTriangles.data(),
        mesh.indices.data(),
        mesh.indices.size(),
        &mesh.vertices[0].position.x,
        mesh.vertices.size(),
        sizeof(InputVertex),
        config_.maxVerticesPerMeshlet,
        config_.maxTrianglesPerMeshlet,
        config_.clusterGroupingFactor
    );
    
    if (meshletCount == 0) {
        lastError_ = "Failed to build meshlets";
        return false;
    }
    
    // Trim to actual size
    const meshopt_Meshlet& last = meshlets[meshletCount - 1];
    meshletVertices.resize(last.vertex_offset + last.vertex_count);
    meshletTriangles.resize(last.triangle_offset + ((last.triangle_count * 3 + 3) & ~3));
    meshlets.resize(meshletCount);
    
    // Convert to our format and compute bounds
    outMeshlets.resize(meshletCount);
    
    for (size_t i = 0; i < meshletCount; i++) {
        const auto& m = meshlets[i];
        auto& out = outMeshlets[i];
        
        out.vertexOffset = m.vertex_offset;
        out.triangleOffset = m.triangle_offset;
        out.vertexCount = m.vertex_count;
        out.triangleCount = m.triangle_count;
        
        // Compute meshlet bounds
        meshopt_Bounds bounds = meshopt_computeMeshletBounds(
            &meshletVertices[m.vertex_offset],
            &meshletTriangles[m.triangle_offset],
            m.triangle_count,
            &mesh.vertices[0].position.x,
            mesh.vertices.size(),
            sizeof(InputVertex)
        );
        
        out.center[0] = bounds.center[0];
        out.center[1] = bounds.center[1];
        out.center[2] = bounds.center[2];
        out.radius = bounds.radius;
        out.coneAxis[0] = bounds.cone_axis_s8[0];
        out.coneAxis[1] = bounds.cone_axis_s8[1];
        out.coneAxis[2] = bounds.cone_axis_s8[2];
        out.coneCutoff = bounds.cone_cutoff_s8;
    }
    
    outMeshletVertices = meshletVertices;
    outMeshletTriangles = meshletTriangles;
    
    return true;
}

// ============================================================================
// CLUSTER HIERARCHY BUILDING
// ============================================================================

bool AssetCooker::buildClusterHierarchy(const InputMesh& mesh,
                                         const std::vector<CookedMeshlet>& meshlets,
                                         std::vector<CookedCluster>& outClusters,
                                         std::vector<CookedHierarchyNode>& outNodes) {
    // Group meshlets into clusters
    const uint32_t meshletsPerCluster = config_.maxMeshletsPerCluster;
    uint32_t clusterCount = (static_cast<uint32_t>(meshlets.size()) + meshletsPerCluster - 1) / meshletsPerCluster;
    
    outClusters.resize(clusterCount);
    
    // Create leaf clusters from meshlets
    for (uint32_t c = 0; c < clusterCount; c++) {
        auto& cluster = outClusters[c];
        
        uint32_t startMeshlet = c * meshletsPerCluster;
        uint32_t endMeshlet = std::min(startMeshlet + meshletsPerCluster, static_cast<uint32_t>(meshlets.size()));
        
        cluster.meshletOffset = startMeshlet;
        cluster.meshletCount = endMeshlet - startMeshlet;
        
        // Compute cluster bounds from meshlets
        glm::vec3 minBounds(std::numeric_limits<float>::max());
        glm::vec3 maxBounds(std::numeric_limits<float>::lowest());
        
        uint32_t totalTriangles = 0;
        for (uint32_t m = startMeshlet; m < endMeshlet; m++) {
            const auto& meshlet = meshlets[m];
            glm::vec3 center(meshlet.center[0], meshlet.center[1], meshlet.center[2]);
            float radius = meshlet.radius;
            
            minBounds = glm::min(minBounds, center - glm::vec3(radius));
            maxBounds = glm::max(maxBounds, center + glm::vec3(radius));
            
            totalTriangles += meshlet.triangleCount;
        }
        
        cluster.sphereCenter = (minBounds + maxBounds) * 0.5f;
        cluster.sphereRadius = glm::length(maxBounds - minBounds) * 0.5f;
        
        cluster.boxCenter = cluster.sphereCenter;
        glm::vec3 extents = (maxBounds - minBounds) * 0.5f;
        cluster.boxExtentX = extents.x;
        cluster.boxExtentY = extents.y;
        cluster.boxExtentZ = extents.z;
        
        cluster.triangleCount = totalTriangles;
        cluster.lodError = 0.0f;  // LOD 0
        cluster.parentLodError = config_.lodErrorThreshold;
        
        cluster.materialId = 0;  // TODO: Determine from triangles
        cluster.flags = 0;
        cluster.pageIndex = c / 8;  // 8 clusters per page
    }
    
    // Build hierarchy nodes bottom-up
    std::vector<uint32_t> currentLevel;
    for (uint32_t i = 0; i < clusterCount; i++) {
        currentLevel.push_back(i);
    }
    
    uint32_t nodeOffset = 0;
    float currentLodError = config_.lodErrorThreshold;
    
    while (currentLevel.size() > 1) {
        std::vector<uint32_t> nextLevel;
        
        // Group into parent nodes (4 children per node)
        const uint32_t childrenPerNode = 4;
        
        for (size_t i = 0; i < currentLevel.size(); i += childrenPerNode) {
            CookedHierarchyNode node{};
            
            uint32_t childCount = std::min(childrenPerNode, static_cast<uint32_t>(currentLevel.size() - i));
            
            // Compute bounds from children
            glm::vec3 minBounds(std::numeric_limits<float>::max());
            glm::vec3 maxBounds(std::numeric_limits<float>::lowest());
            
            for (uint32_t c = 0; c < childCount; c++) {
                uint32_t childIdx = currentLevel[i + c];
                if (outNodes.empty()) {
                    // Children are clusters
                    const auto& cluster = outClusters[childIdx];
                    minBounds = glm::min(minBounds, cluster.sphereCenter - glm::vec3(cluster.sphereRadius));
                    maxBounds = glm::max(maxBounds, cluster.sphereCenter + glm::vec3(cluster.sphereRadius));
                } else {
                    // Children are nodes
                    const auto& childNode = outNodes[childIdx];
                    glm::vec3 center(childNode.boxCenter);
                    glm::vec3 extents(childNode.boxExtentX, childNode.boxExtentY, childNode.boxExtentZ);
                    minBounds = glm::min(minBounds, center - extents);
                    maxBounds = glm::max(maxBounds, center + extents);
                }
            }
            
            node.boxCenter = (minBounds + maxBounds) * 0.5f;
            glm::vec3 extents = (maxBounds - minBounds) * 0.5f;
            node.boxExtentX = extents.x;
            node.boxExtentY = extents.y;
            node.boxExtentZ = extents.z;
            
            node.lodError = currentLodError;
            node.minLodError = currentLodError * 0.5f;
            
            node.childOffset = currentLevel[i];
            node.childCount = childCount;
            node.flags = outNodes.empty() ? 0x1 : 0;  // NODE_FLAG_LEAF if pointing to clusters
            node.level = static_cast<uint32_t>(outNodes.size() / 100);  // Rough level estimate
            
            nextLevel.push_back(static_cast<uint32_t>(outNodes.size()));
            outNodes.push_back(node);
        }
        
        currentLevel = nextLevel;
        currentLodError *= 2.0f;
    }
    
    stats_.outputLodLevels = 1;  // For now, single LOD
    
    return true;
}

// ============================================================================
// CLUSTER PAGES
// ============================================================================

bool AssetCooker::buildClusterPages(const std::vector<CookedCluster>& clusters,
                                     std::vector<PageTableEntry>& outPages) {
    const uint32_t clustersPerPage = CLUSTER_PAGE_SIZE / sizeof(CookedCluster);
    uint32_t pageCount = (static_cast<uint32_t>(clusters.size()) + clustersPerPage - 1) / clustersPerPage;
    
    outPages.resize(pageCount);
    
    uint32_t fileOffset = 0;  // Will be updated when writing
    
    for (uint32_t p = 0; p < pageCount; p++) {
        auto& page = outPages[p];
        
        page.clusterOffset = p * clustersPerPage;
        page.clusterCount = std::min(clustersPerPage, 
                                      static_cast<uint32_t>(clusters.size()) - page.clusterOffset);
        page.uncompressedSize = page.clusterCount * sizeof(CookedCluster);
        page.compressedSize = 0;  // Will be set if compression is enabled
        page.fileOffset = fileOffset;
        page.flags = 0;
        page.dependencyMask = (p > 0) ? (1u << (p - 1)) : 0;  // Simple linear dependency
        
        fileOffset += page.uncompressedSize;
    }
    
    return true;
}

// ============================================================================
// SDF GENERATION
// ============================================================================

float AssetCooker::pointTriangleDistance(const glm::vec3& p,
                                          const glm::vec3& a,
                                          const glm::vec3& b,
                                          const glm::vec3& c) {
    glm::vec3 ab = b - a;
    glm::vec3 ac = c - a;
    glm::vec3 ap = p - a;
    
    float d1 = glm::dot(ab, ap);
    float d2 = glm::dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) return glm::length(ap);
    
    glm::vec3 bp = p - b;
    float d3 = glm::dot(ab, bp);
    float d4 = glm::dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) return glm::length(bp);
    
    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        float v = d1 / (d1 - d3);
        return glm::length(p - (a + v * ab));
    }
    
    glm::vec3 cp = p - c;
    float d5 = glm::dot(ab, cp);
    float d6 = glm::dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) return glm::length(cp);
    
    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        float w = d2 / (d2 - d6);
        return glm::length(p - (a + w * ac));
    }
    
    float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return glm::length(p - (b + w * (c - b)));
    }
    
    float denom = 1.0f / (va + vb + vc);
    float v = vb * denom;
    float w = vc * denom;
    return glm::length(p - (a + ab * v + ac * w));
}

bool AssetCooker::generateSDF(const InputMesh& mesh,
                               std::vector<float>& outSdfVolume,
                               glm::ivec3& outResolution,
                               float& outVoxelSize) {
    glm::vec3 boundsMin = mesh.boundsMin - glm::vec3(config_.sdfPadding);
    glm::vec3 boundsMax = mesh.boundsMax + glm::vec3(config_.sdfPadding);
    glm::vec3 boundsSize = boundsMax - boundsMin;
    
    float maxExtent = std::max({boundsSize.x, boundsSize.y, boundsSize.z});
    outVoxelSize = maxExtent / static_cast<float>(config_.sdfResolution);
    
    outResolution = glm::ivec3(
        static_cast<int>(std::ceil(boundsSize.x / outVoxelSize)),
        static_cast<int>(std::ceil(boundsSize.y / outVoxelSize)),
        static_cast<int>(std::ceil(boundsSize.z / outVoxelSize))
    );
    
    // Limit resolution
    outResolution = glm::min(outResolution, glm::ivec3(config_.sdfResolution));
    
    size_t totalVoxels = outResolution.x * outResolution.y * outResolution.z;
    outSdfVolume.resize(totalVoxels);
    
    // Parallel SDF computation
    #pragma omp parallel for
    for (int z = 0; z < outResolution.z; z++) {
        for (int y = 0; y < outResolution.y; y++) {
            for (int x = 0; x < outResolution.x; x++) {
                glm::vec3 voxelPos = boundsMin + glm::vec3(
                    (x + 0.5f) * outVoxelSize,
                    (y + 0.5f) * outVoxelSize,
                    (z + 0.5f) * outVoxelSize
                );
                
                float minDist = std::numeric_limits<float>::max();
                
                // Find minimum distance to all triangles
                for (size_t t = 0; t < mesh.indices.size(); t += 3) {
                    const glm::vec3& a = mesh.vertices[mesh.indices[t + 0]].position;
                    const glm::vec3& b = mesh.vertices[mesh.indices[t + 1]].position;
                    const glm::vec3& c = mesh.vertices[mesh.indices[t + 2]].position;
                    
                    float dist = pointTriangleDistance(voxelPos, a, b, c);
                    minDist = std::min(minDist, dist);
                }
                
                // Determine sign using winding number
                // (simplified - assumes manifold mesh)
                float sign = 1.0f;
                // TODO: Implement proper inside/outside detection
                
                size_t idx = x + y * outResolution.x + z * outResolution.x * outResolution.y;
                outSdfVolume[idx] = sign * minDist;
            }
        }
    }
    
    return true;
}

// ============================================================================
// SURFACE CARD GENERATION
// ============================================================================

bool AssetCooker::generateSurfaceCards(const InputMesh& mesh,
                                        std::vector<CookedSurfaceCard>& outCards) {
    // Simple approach: Create cards for major axis-aligned faces
    // More sophisticated: Cluster triangles by normal and create cards
    
    // Group triangles by dominant normal direction
    std::vector<std::vector<uint32_t>> normalBuckets(6);  // +X, -X, +Y, -Y, +Z, -Z
    
    for (size_t t = 0; t < mesh.indices.size(); t += 3) {
        const auto& v0 = mesh.vertices[mesh.indices[t + 0]];
        const auto& v1 = mesh.vertices[mesh.indices[t + 1]];
        const auto& v2 = mesh.vertices[mesh.indices[t + 2]];
        
        glm::vec3 faceNormal = glm::normalize(glm::cross(
            v1.position - v0.position,
            v2.position - v0.position
        ));
        
        // Find dominant axis
        int axis = 0;
        float maxAbs = 0.0f;
        for (int i = 0; i < 3; i++) {
            if (std::abs(faceNormal[i]) > maxAbs) {
                maxAbs = std::abs(faceNormal[i]);
                axis = i;
            }
        }
        
        int bucket = axis * 2 + (faceNormal[axis] < 0 ? 1 : 0);
        normalBuckets[bucket].push_back(static_cast<uint32_t>(t / 3));
    }
    
    // Create a card for each non-empty bucket
    for (int bucket = 0; bucket < 6; bucket++) {
        if (normalBuckets[bucket].empty()) continue;
        if (normalBuckets[bucket].size() < 4) continue;  // Too few triangles
        
        CookedSurfaceCard card{};
        
        // Compute bounds of triangles in this bucket
        glm::vec3 minBounds(std::numeric_limits<float>::max());
        glm::vec3 maxBounds(std::numeric_limits<float>::lowest());
        
        for (uint32_t triIdx : normalBuckets[bucket]) {
            for (int v = 0; v < 3; v++) {
                const auto& vert = mesh.vertices[mesh.indices[triIdx * 3 + v]];
                minBounds = glm::min(minBounds, vert.position);
                maxBounds = glm::max(maxBounds, vert.position);
            }
        }
        
        card.boundsMin = minBounds;
        card.boundsMax = maxBounds;
        
        // Set card orientation based on bucket
        int axis = bucket / 2;
        float sign = (bucket % 2 == 0) ? 1.0f : -1.0f;
        
        card.normal = glm::vec3(0.0f);
        card.normal[axis] = sign;
        
        // Set perpendicular axes
        int axisX = (axis + 1) % 3;
        int axisY = (axis + 2) % 3;
        
        card.axisX = glm::vec3(0.0f);
        card.axisX[axisX] = 1.0f;
        card.extentX = (maxBounds[axisX] - minBounds[axisX]) * 0.5f;
        
        card.axisY = glm::vec3(0.0f);
        card.axisY[axisY] = 1.0f;
        card.extentY = (maxBounds[axisY] - minBounds[axisY]) * 0.5f;
        
        // Skip cards that are too small
        if (card.extentX * card.extentY < config_.cardMinArea) continue;
        
        // Atlas placement (simplified - sequential)
        card.atlasOffsetX = 0;
        card.atlasOffsetY = static_cast<uint32_t>(outCards.size()) * 64;
        card.atlasWidth = static_cast<uint32_t>(card.extentX * config_.cardTexelDensity * 2.0f);
        card.atlasHeight = static_cast<uint32_t>(card.extentY * config_.cardTexelDensity * 2.0f);
        card.atlasWidth = std::max(card.atlasWidth, 8u);
        card.atlasHeight = std::max(card.atlasHeight, 8u);
        
        card.mipLevel = 0;
        card.texelDensity = config_.cardTexelDensity;
        card.flags = 0;
        
        outCards.push_back(card);
        
        if (outCards.size() >= config_.maxSurfaceCards) break;
    }
    
    return true;
}

// ============================================================================
// PHYSICS DATA GENERATION
// ============================================================================

bool AssetCooker::generatePhysicsData(const InputMesh& mesh,
                                       std::vector<uint8_t>& outJoltData,
                                       std::vector<uint8_t>& outSimpleShapes) {
    // For now, just store a simple AABB as the collision shape
    // Full Jolt integration would serialize convex hulls and mesh shapes
    
    struct SimpleAABB {
        glm::vec3 min;
        float pad1;
        glm::vec3 max;
        float pad2;
    };
    
    SimpleAABB aabb;
    aabb.min = mesh.boundsMin;
    aabb.max = mesh.boundsMax;
    
    outSimpleShapes.resize(sizeof(SimpleAABB));
    memcpy(outSimpleShapes.data(), &aabb, sizeof(SimpleAABB));
    
    // Jolt data would go here if we integrated Jolt's serialization
    outJoltData.clear();
    
    return true;
}

// ============================================================================
// FILE WRITING
// ============================================================================

bool AssetCooker::writeAssetFile(const std::string& path,
                                  const AssetHeader& header,
                                  const std::vector<uint8_t>& geometryData,
                                  const std::vector<uint8_t>& naniteData,
                                  const std::vector<uint8_t>& lumenData,
                                  const std::vector<uint8_t>& physicsData,
                                  const std::vector<uint8_t>& materialData) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        lastError_ = "Failed to open output file: " + path;
        return false;
    }
    
    // Calculate section offsets
    AssetHeader finalHeader = header;
    uint64_t offset = sizeof(AssetHeader);
    
    finalHeader.geometryOffset = offset;
    finalHeader.geometrySectionSize = static_cast<uint32_t>(geometryData.size());
    offset += geometryData.size();
    
    finalHeader.naniteOffset = offset;
    finalHeader.naniteSectionSize = static_cast<uint32_t>(naniteData.size());
    offset += naniteData.size();
    
    finalHeader.lumenOffset = offset;
    finalHeader.lumenSectionSize = static_cast<uint32_t>(lumenData.size());
    offset += lumenData.size();
    
    finalHeader.physicsOffset = offset;
    finalHeader.physicsSectionSize = static_cast<uint32_t>(physicsData.size());
    offset += physicsData.size();
    
    finalHeader.materialOffset = offset;
    finalHeader.materialSectionSize = static_cast<uint32_t>(materialData.size());
    offset += materialData.size();
    
    finalHeader.totalSize = static_cast<uint32_t>(offset);
    
    // Write header
    file.write(reinterpret_cast<const char*>(&finalHeader), sizeof(AssetHeader));
    
    // Write sections
    file.write(reinterpret_cast<const char*>(geometryData.data()), geometryData.size());
    file.write(reinterpret_cast<const char*>(naniteData.data()), naniteData.size());
    file.write(reinterpret_cast<const char*>(lumenData.data()), lumenData.size());
    file.write(reinterpret_cast<const char*>(physicsData.data()), physicsData.size());
    file.write(reinterpret_cast<const char*>(materialData.data()), materialData.size());
    
    file.close();
    return true;
}

// ============================================================================
// COMMAND LINE INTERFACE
// ============================================================================

void CookerCLI::printUsage() {
    std::cout << R"(
Sanic Asset Cooker - Offline asset processing tool

Usage:
  sanic_cooker [options] <input> <output>
  sanic_cooker --batch <list_file> --output-dir <dir>

Options:
  -h, --help           Show this help message
  -v, --version        Show version information
  -q, --quiet          Suppress output
  --dry-run            Don't write files, just validate
  
  --sdf-resolution N   SDF resolution (default: 64)
  --max-lod N          Maximum LOD levels (default: 8)
  --no-physics         Skip physics generation
  --no-compress        Disable page compression
  
Input formats:
  .obj                 Wavefront OBJ
  .gltf, .glb          GLTF 2.0

Output format:
  .sanic_mesh          Cooked Sanic mesh asset
)";
}

void CookerCLI::printVersion() {
    std::cout << "Sanic Asset Cooker v1.0.0" << std::endl;
    std::cout << "Nanite/Lumen offline processing tool" << std::endl;
}

int CookerCLI::run(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage();
        return 1;
    }
    
    AssetCooker cooker;
    CookerConfig config;
    
    std::string inputPath;
    std::string outputPath;
    std::string batchFile;
    std::string outputDir;
    
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            printUsage();
            return 0;
        } else if (arg == "-v" || arg == "--version") {
            printVersion();
            return 0;
        } else if (arg == "-q" || arg == "--quiet") {
            config.verbose = false;
        } else if (arg == "--dry-run") {
            config.dryRun = true;
        } else if (arg == "--sdf-resolution" && i + 1 < argc) {
            config.sdfResolution = std::stoi(argv[++i]);
        } else if (arg == "--max-lod" && i + 1 < argc) {
            config.maxLodLevels = std::stoi(argv[++i]);
        } else if (arg == "--no-physics") {
            config.generateConvexHulls = false;
            config.generateTriangleMesh = false;
        } else if (arg == "--no-compress") {
            config.compressPages = false;
        } else if (arg == "--batch" && i + 1 < argc) {
            batchFile = argv[++i];
        } else if (arg == "--output-dir" && i + 1 < argc) {
            outputDir = argv[++i];
        } else if (inputPath.empty()) {
            inputPath = arg;
        } else if (outputPath.empty()) {
            outputPath = arg;
        }
    }
    
    cooker.setConfig(config);
    
    // Batch mode
    if (!batchFile.empty()) {
        std::ifstream listFile(batchFile);
        if (!listFile) {
            std::cerr << "Failed to open batch file: " << batchFile << std::endl;
            return 1;
        }
        
        std::vector<std::pair<std::string, std::string>> files;
        std::string line;
        while (std::getline(listFile, line)) {
            if (line.empty() || line[0] == '#') continue;
            
            // Generate output path
            std::string baseName = line.substr(line.find_last_of("/\\") + 1);
            size_t dotPos = baseName.find_last_of('.');
            if (dotPos != std::string::npos) {
                baseName = baseName.substr(0, dotPos);
            }
            
            std::string outPath = outputDir + "/" + baseName + ".sanic_mesh";
            files.push_back({line, outPath});
        }
        
        return cooker.cookBatch(files) ? 0 : 1;
    }
    
    // Single file mode
    if (inputPath.empty()) {
        std::cerr << "No input file specified" << std::endl;
        printUsage();
        return 1;
    }
    
    if (outputPath.empty()) {
        // Generate output path
        size_t dotPos = inputPath.find_last_of('.');
        outputPath = (dotPos != std::string::npos ? inputPath.substr(0, dotPos) : inputPath) + ".sanic_mesh";
    }
    
    return cooker.cookFile(inputPath, outputPath) ? 0 : 1;
}

} // namespace Sanic

// Main entry point for standalone cooker executable
#ifdef SANIC_COOKER_STANDALONE
int main(int argc, char* argv[]) {
    return Sanic::CookerCLI::run(argc, argv);
}
#endif
