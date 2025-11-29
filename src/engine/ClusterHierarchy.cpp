/**
 * ClusterHierarchy.cpp
 * 
 * Implementation of Nanite-style hierarchical cluster system.
 * Uses meshoptimizer for LOD generation and cluster building.
 */

#include "ClusterHierarchy.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <cstring>
#include <iostream>
#include <meshoptimizer.h>

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

namespace {

/**
 * Compute bounding sphere using Ritter's algorithm.
 * More accurate than AABB-derived sphere.
 */
void computeBoundingSphere(
    const std::vector<glm::vec3>& positions,
    glm::vec3& outCenter,
    float& outRadius
) {
    if (positions.empty()) {
        outCenter = glm::vec3(0.0f);
        outRadius = 0.0f;
        return;
    }
    
    // Find initial sphere from min/max points on each axis
    glm::vec3 minP = positions[0], maxP = positions[0];
    for (const auto& p : positions) {
        minP = glm::min(minP, p);
        maxP = glm::max(maxP, p);
    }
    
    // Initial sphere
    outCenter = (minP + maxP) * 0.5f;
    outRadius = glm::length(maxP - outCenter);
    
    // Expand to include all points
    for (const auto& p : positions) {
        float dist = glm::length(p - outCenter);
        if (dist > outRadius) {
            // Expand sphere to include this point
            float newRadius = (outRadius + dist) * 0.5f;
            outCenter = outCenter + (p - outCenter) * ((newRadius - outRadius) / dist);
            outRadius = newRadius;
        }
    }
}

/**
 * Compute AABB from positions.
 */
void computeAABB(
    const std::vector<glm::vec3>& positions,
    glm::vec3& outCenter,
    glm::vec3& outExtent
) {
    if (positions.empty()) {
        outCenter = glm::vec3(0.0f);
        outExtent = glm::vec3(0.0f);
        return;
    }
    
    glm::vec3 minP = positions[0], maxP = positions[0];
    for (const auto& p : positions) {
        minP = glm::min(minP, p);
        maxP = glm::max(maxP, p);
    }
    
    outCenter = (minP + maxP) * 0.5f;
    outExtent = (maxP - minP) * 0.5f;
}

/**
 * Calculate geometric error for LOD based on simplification ratio.
 * Uses edge length as proxy for geometric detail.
 */
float calculateLODError(
    const std::vector<glm::vec3>& positions,
    const std::vector<uint32_t>& indices,
    float simplificationRatio
) {
    if (indices.size() < 3) return 0.0f;
    
    // Calculate average edge length
    float totalEdgeLength = 0.0f;
    uint32_t edgeCount = 0;
    
    for (size_t i = 0; i < indices.size(); i += 3) {
        const glm::vec3& v0 = positions[indices[i]];
        const glm::vec3& v1 = positions[indices[i + 1]];
        const glm::vec3& v2 = positions[indices[i + 2]];
        
        totalEdgeLength += glm::length(v1 - v0);
        totalEdgeLength += glm::length(v2 - v1);
        totalEdgeLength += glm::length(v0 - v2);
        edgeCount += 3;
    }
    
    float avgEdgeLength = edgeCount > 0 ? totalEdgeLength / edgeCount : 0.0f;
    
    // Error increases with simplification (inverse of detail)
    // At LOD 0 (full detail), error approaches 0
    // At high simplification, error approaches average edge length
    return avgEdgeLength * (1.0f - simplificationRatio);
}

} // anonymous namespace

// ============================================================================
// CLUSTER HIERARCHY IMPLEMENTATION
// ============================================================================

ClusterHierarchy::ClusterHierarchy(VulkanContext& context)
    : context(context) {
}

ClusterHierarchy::~ClusterHierarchy() {
    cleanupGPUBuffers();
}

void ClusterHierarchy::buildFromMeshlets(
    const std::vector<glm::vec3>& vertices,
    const std::vector<uint32_t>& indices,
    const void* meshletData,
    uint32_t meshletCount
) {
    clusters.clear();
    hierarchyNodes.clear();
    
    if (meshletCount == 0) return;
    
    // Cast meshlet data (matches Meshlet struct from Mesh.h)
    struct MeshletLayout {
        float center[3];
        float radius;
        int8_t cone_axis[3];
        int8_t cone_cutoff;
        uint32_t vertex_offset;
        uint32_t triangle_offset;
        uint8_t vertex_count;
        uint8_t triangle_count;
        uint8_t padding[2];
    };
    
    const MeshletLayout* meshlets = static_cast<const MeshletLayout*>(meshletData);
    
    // Create one cluster per meshlet (base LOD level)
    clusters.reserve(meshletCount);
    
    for (uint32_t i = 0; i < meshletCount; ++i) {
        const MeshletLayout& m = meshlets[i];
        
        Cluster cluster{};
        
        // Copy sphere bounds from meshlet
        cluster.bounds.sphereCenter[0] = m.center[0];
        cluster.bounds.sphereCenter[1] = m.center[1];
        cluster.bounds.sphereCenter[2] = m.center[2];
        cluster.bounds.sphereRadius = m.radius;
        
        // Compute AABB from sphere (conservative)
        cluster.bounds.boxCenter[0] = m.center[0];
        cluster.bounds.boxCenter[1] = m.center[1];
        cluster.bounds.boxCenter[2] = m.center[2];
        cluster.bounds.boxExtentX = m.radius;
        cluster.bounds.boxExtentY = m.radius;
        cluster.bounds.boxExtentZ = m.radius;
        
        // LOD error - base level has 0 error
        cluster.bounds.lodError = 0.0f;
        cluster.bounds.parentLodError = std::numeric_limits<float>::max();
        
        // Geometry references
        cluster.geometry.meshletOffset = i;
        cluster.geometry.meshletCount = 1;
        cluster.geometry.vertexOffset = m.vertex_offset;
        cluster.geometry.triangleOffset = m.triangle_offset;
        cluster.geometry.materialId = 0;
        cluster.geometry.flags = 0;
        cluster.geometry.instanceDataOffset = 0;
        
        clusters.push_back(cluster);
    }
    
    // Build BVH over clusters
    buildBVH();
    
    // Compute LOD error metrics
    computeLodErrors();
    
    // Upload to GPU
    uploadToGPU();
    
    lodLevelCount = 1;
}

void ClusterHierarchy::buildWithLOD(
    const std::vector<glm::vec3>& vertices,
    const std::vector<uint32_t>& indices,
    uint32_t maxLodLevels,
    float lodErrorThreshold
) {
    clusters.clear();
    hierarchyNodes.clear();
    lodLevels.clear();
    
    if (indices.empty() || vertices.empty()) return;
    
    std::cout << "Building cluster hierarchy with LOD..." << std::endl;
    std::cout << "  Input: " << vertices.size() << " vertices, " << indices.size() / 3 << " triangles" << std::endl;
    
    // Convert vertices to float array for meshoptimizer
    std::vector<float> vertexPositions(vertices.size() * 3);
    for (size_t i = 0; i < vertices.size(); ++i) {
        vertexPositions[i * 3 + 0] = vertices[i].x;
        vertexPositions[i * 3 + 1] = vertices[i].y;
        vertexPositions[i * 3 + 2] = vertices[i].z;
    }
    
    // Track all LOD levels
    struct LODData {
        std::vector<uint32_t> indices;
        float error;
        float targetRatio;
    };
    std::vector<LODData> allLods;
    
    // LOD 0 = original mesh
    LODData lod0;
    lod0.indices = indices;
    lod0.error = 0.0f;
    lod0.targetRatio = 1.0f;
    allLods.push_back(std::move(lod0));
    
    // Generate simplified LODs
    std::vector<uint32_t> currentIndices = indices;
    float targetRatios[] = { 0.5f, 0.25f, 0.125f, 0.0625f, 0.03125f, 0.015625f, 0.0078125f };
    
    for (uint32_t lodLevel = 1; lodLevel < maxLodLevels && !currentIndices.empty(); ++lodLevel) {
        float targetRatio = (lodLevel - 1 < 7) ? targetRatios[lodLevel - 1] : 0.0078125f;
        size_t targetIndexCount = std::max(size_t(36), size_t(indices.size() * targetRatio)); // At least 12 triangles
        
        // Round to multiple of 3
        targetIndexCount = (targetIndexCount / 3) * 3;
        
        if (targetIndexCount >= currentIndices.size()) {
            break; // Can't simplify further
        }
        
        // Simplify mesh
        std::vector<uint32_t> simplifiedIndices(currentIndices.size());
        float simplifyError = 0.0f;
        
        size_t resultIndexCount = meshopt_simplify(
            simplifiedIndices.data(),
            currentIndices.data(),
            currentIndices.size(),
            vertexPositions.data(),
            vertices.size(),
            sizeof(float) * 3,
            targetIndexCount,
            0.02f,      // Target error
            0,          // Options
            &simplifyError
        );
        
        if (resultIndexCount < 36 || resultIndexCount >= currentIndices.size() * 0.95f) {
            break; // Simplification failed or minimal reduction
        }
        
        simplifiedIndices.resize(resultIndexCount);
        
        // Calculate LOD error based on simplification
        float ratio = float(resultIndexCount) / float(indices.size());
        float lodError = calculateLODError(vertices, simplifiedIndices, ratio);
        
        LODData lod;
        lod.indices = std::move(simplifiedIndices);
        lod.error = lodError;
        lod.targetRatio = ratio;
        allLods.push_back(std::move(lod));
        
        currentIndices = allLods.back().indices;
        
        std::cout << "  LOD " << lodLevel << ": " << currentIndices.size() / 3 
                  << " triangles (ratio: " << ratio << ", error: " << lodError << ")" << std::endl;
    }
    
    lodLevelCount = static_cast<uint32_t>(allLods.size());
    std::cout << "  Generated " << lodLevelCount << " LOD levels" << std::endl;
    
    // Build meshlets and clusters for each LOD level
    for (uint32_t lodIdx = 0; lodIdx < allLods.size(); ++lodIdx) {
        const auto& lod = allLods[lodIdx];
        
        LODLevelInfo levelInfo{};
        levelInfo.clusterOffset = static_cast<uint32_t>(clusters.size());
        levelInfo.triangleCount = static_cast<uint32_t>(lod.indices.size() / 3);
        levelInfo.lodError = lod.error;
        levelInfo.reductionRatio = lod.targetRatio;
        
        // Build meshlets for this LOD
        size_t maxMeshlets = meshopt_buildMeshletsBound(lod.indices.size(), 64, 124);
        std::vector<meshopt_Meshlet> meshlets(maxMeshlets);
        std::vector<uint32_t> meshletVertices(maxMeshlets * 64);
        std::vector<uint8_t> meshletTriangles(maxMeshlets * 124 * 3);
        
        size_t meshletCount = meshopt_buildMeshlets(
            meshlets.data(),
            meshletVertices.data(),
            meshletTriangles.data(),
            lod.indices.data(),
            lod.indices.size(),
            vertexPositions.data(),
            vertices.size(),
            sizeof(float) * 3,
            64,     // max vertices per meshlet
            124,    // max triangles per meshlet
            0.5f    // cone weight
        );
        
        meshlets.resize(meshletCount);
        
        // Create clusters from meshlets
        for (size_t i = 0; i < meshletCount; ++i) {
            const auto& m = meshlets[i];
            
            // Compute bounds for this meshlet
            meshopt_Bounds bounds = meshopt_computeMeshletBounds(
                &meshletVertices[m.vertex_offset],
                &meshletTriangles[m.triangle_offset],
                m.triangle_count,
                vertexPositions.data(),
                vertices.size(),
                sizeof(float) * 3
            );
            
            Cluster cluster{};
            
            // Sphere bounds
            cluster.bounds.sphereCenter[0] = bounds.center[0];
            cluster.bounds.sphereCenter[1] = bounds.center[1];
            cluster.bounds.sphereCenter[2] = bounds.center[2];
            cluster.bounds.sphereRadius = bounds.radius;
            
            // AABB (derived from sphere for now - could compute exact AABB)
            cluster.bounds.boxCenter[0] = bounds.center[0];
            cluster.bounds.boxCenter[1] = bounds.center[1];
            cluster.bounds.boxCenter[2] = bounds.center[2];
            cluster.bounds.boxExtentX = bounds.radius;
            cluster.bounds.boxExtentY = bounds.radius;
            cluster.bounds.boxExtentZ = bounds.radius;
            
            // LOD error - inherit from LOD level
            cluster.bounds.lodError = lod.error;
            // Parent error is the error of the next coarser LOD
            cluster.bounds.parentLodError = (lodIdx + 1 < allLods.size()) 
                ? allLods[lodIdx + 1].error 
                : std::numeric_limits<float>::max();
            
            // Geometry references
            cluster.geometry.meshletOffset = static_cast<uint32_t>(i);
            cluster.geometry.meshletCount = 1;
            cluster.geometry.vertexOffset = m.vertex_offset;
            cluster.geometry.triangleOffset = m.triangle_offset;
            cluster.geometry.materialId = 0;
            cluster.geometry.flags = (lodIdx << 16); // Store LOD level in flags
            cluster.geometry.instanceDataOffset = 0;
            
            clusters.push_back(cluster);
        }
        
        levelInfo.clusterCount = static_cast<uint32_t>(clusters.size()) - levelInfo.clusterOffset;
        lodLevels.push_back(levelInfo);
    }
    
    maxLodError = lodLevels.empty() ? 0.0f : lodLevels.back().lodError;
    
    std::cout << "  Total clusters: " << clusters.size() << std::endl;
    
    // Build BVH over all clusters
    buildBVH();
    
    // Upload to GPU
    uploadToGPU();
}

void ClusterHierarchy::buildBVH() {
    if (clusters.empty()) return;
    
    hierarchyNodes.clear();
    
    // Simple top-down BVH construction
    // Group clusters into nodes of ~32 clusters each (matching GPU workgroup size)
    constexpr uint32_t CLUSTERS_PER_NODE = 32;
    
    // First, create leaf nodes for all clusters
    uint32_t numLeafNodes = (static_cast<uint32_t>(clusters.size()) + CLUSTERS_PER_NODE - 1) / CLUSTERS_PER_NODE;
    hierarchyNodes.reserve(numLeafNodes * 2); // Approximate upper bound
    
    // Create leaf nodes
    std::vector<uint32_t> currentLevel;
    for (uint32_t i = 0; i < clusters.size(); i += CLUSTERS_PER_NODE) {
        HierarchyNode node{};
        
        uint32_t clusterEnd = std::min(i + CLUSTERS_PER_NODE, static_cast<uint32_t>(clusters.size()));
        uint32_t childCount = clusterEnd - i;
        
        // Compute bounds encompassing all child clusters
        glm::vec3 minBounds(std::numeric_limits<float>::max());
        glm::vec3 maxBounds(std::numeric_limits<float>::lowest());
        float maxError = 0.0f;
        float minError = std::numeric_limits<float>::max();
        
        for (uint32_t j = i; j < clusterEnd; ++j) {
            const Cluster& c = clusters[j];
            glm::vec3 center(c.bounds.boxCenter[0], c.bounds.boxCenter[1], c.bounds.boxCenter[2]);
            glm::vec3 extent(c.bounds.boxExtentX, c.bounds.boxExtentY, c.bounds.boxExtentZ);
            
            minBounds = glm::min(minBounds, center - extent);
            maxBounds = glm::max(maxBounds, center + extent);
            maxError = std::max(maxError, c.bounds.lodError);
            minError = std::min(minError, c.bounds.lodError);
        }
        
        glm::vec3 nodeCenter = (minBounds + maxBounds) * 0.5f;
        glm::vec3 nodeExtent = (maxBounds - minBounds) * 0.5f;
        
        node.boxCenter[0] = nodeCenter.x;
        node.boxCenter[1] = nodeCenter.y;
        node.boxCenter[2] = nodeCenter.z;
        node.boxExtentX = nodeExtent.x;
        node.boxExtentY = nodeExtent.y;
        node.boxExtentZ = nodeExtent.z;
        node.lodError = maxError;
        node.minLodError = minError;
        node.childOffset = i;           // Index into cluster array
        node.childCount = childCount;
        node.flags = NODE_FLAG_LEAF;    // These children are clusters
        node.level = 0;
        
        currentLevel.push_back(static_cast<uint32_t>(hierarchyNodes.size()));
        hierarchyNodes.push_back(node);
    }
    
    // Build upper levels until we have a single root
    uint32_t level = 1;
    while (currentLevel.size() > 1) {
        std::vector<uint32_t> nextLevel;
        
        for (uint32_t i = 0; i < currentLevel.size(); i += CLUSTERS_PER_NODE) {
            HierarchyNode node{};
            
            uint32_t nodeEnd = std::min(i + CLUSTERS_PER_NODE, static_cast<uint32_t>(currentLevel.size()));
            uint32_t childCount = nodeEnd - i;
            
            // Compute bounds encompassing all child nodes
            glm::vec3 minBounds(std::numeric_limits<float>::max());
            glm::vec3 maxBounds(std::numeric_limits<float>::lowest());
            float maxError = 0.0f;
            float minError = std::numeric_limits<float>::max();
            
            for (uint32_t j = i; j < nodeEnd; ++j) {
                const HierarchyNode& child = hierarchyNodes[currentLevel[j]];
                glm::vec3 center(child.boxCenter[0], child.boxCenter[1], child.boxCenter[2]);
                glm::vec3 extent(child.boxExtentX, child.boxExtentY, child.boxExtentZ);
                
                minBounds = glm::min(minBounds, center - extent);
                maxBounds = glm::max(maxBounds, center + extent);
                maxError = std::max(maxError, child.lodError);
                minError = std::min(minError, child.minLodError);
            }
            
            glm::vec3 nodeCenter = (minBounds + maxBounds) * 0.5f;
            glm::vec3 nodeExtent = (maxBounds - minBounds) * 0.5f;
            
            node.boxCenter[0] = nodeCenter.x;
            node.boxCenter[1] = nodeCenter.y;
            node.boxCenter[2] = nodeCenter.z;
            node.boxExtentX = nodeExtent.x;
            node.boxExtentY = nodeExtent.y;
            node.boxExtentZ = nodeExtent.z;
            node.lodError = maxError;
            node.minLodError = minError;
            node.childOffset = currentLevel[i]; // Index into node array (for non-leaf)
            node.childCount = childCount;
            node.flags = 0;                      // Children are nodes, not clusters
            node.level = level;
            
            nextLevel.push_back(static_cast<uint32_t>(hierarchyNodes.size()));
            hierarchyNodes.push_back(node);
        }
        
        currentLevel = nextLevel;
        level++;
    }
    
    // Store root node index
    rootNodeIndex = currentLevel.empty() ? 0 : currentLevel[0];
}

void ClusterHierarchy::computeLodErrors() {
    // Compute screen-space error metric for each cluster/node
    // Based on Nanite's approach: error = projected edge length at threshold distance
    
    // For clusters, we use the LOD error set during buildWithLOD
    // Here we just need to propagate errors through the hierarchy
    
    maxLodError = 0.0f;
    
    // Find max error across all clusters
    for (const Cluster& cluster : clusters) {
        maxLodError = std::max(maxLodError, cluster.bounds.lodError);
    }
    
    // Update hierarchy node error metrics (bottom-up propagation)
    // Process nodes in reverse order (leaves first)
    for (int i = static_cast<int>(hierarchyNodes.size()) - 1; i >= 0; --i) {
        HierarchyNode& node = hierarchyNodes[i];
        
        if (node.flags & NODE_FLAG_LEAF) {
            // Leaf node - compute from child clusters
            float maxErr = 0.0f;
            float minErr = std::numeric_limits<float>::max();
            
            for (uint32_t j = 0; j < node.childCount; ++j) {
                uint32_t clusterIdx = node.childOffset + j;
                if (clusterIdx < clusters.size()) {
                    float err = clusters[clusterIdx].bounds.lodError;
                    maxErr = std::max(maxErr, err);
                    minErr = std::min(minErr, err);
                }
            }
            
            node.lodError = maxErr;
            node.minLodError = minErr;
        } else {
            // Interior node - compute from child nodes
            float maxErr = 0.0f;
            float minErr = std::numeric_limits<float>::max();
            
            for (uint32_t j = 0; j < node.childCount; ++j) {
                uint32_t childIdx = node.childOffset + j;
                if (childIdx < hierarchyNodes.size()) {
                    maxErr = std::max(maxErr, hierarchyNodes[childIdx].lodError);
                    minErr = std::min(minErr, hierarchyNodes[childIdx].minLodError);
                }
            }
            
            node.lodError = maxErr;
            node.minLodError = minErr;
        }
    }
    
    std::cout << "  Max LOD error: " << maxLodError << std::endl;
}

void ClusterHierarchy::uploadToGPU() {
    createGPUBuffers();
    
    VkDevice device = context.getDevice();
    
    // Upload cluster data
    if (!clusters.empty()) {
        VkDeviceSize clusterSize = clusters.size() * sizeof(Cluster);
        
        VkBuffer stagingBuffer;
        VkDeviceMemory stagingMemory;
        createBuffer(clusterSize, 
                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     stagingBuffer, stagingMemory);
        
        void* data;
        vkMapMemory(device, stagingMemory, 0, clusterSize, 0, &data);
        memcpy(data, clusters.data(), clusterSize);
        vkUnmapMemory(device, stagingMemory);
        
        // Copy to device buffer
        VkCommandBuffer cmd = context.beginSingleTimeCommands();
        VkBufferCopy copyRegion{};
        copyRegion.size = clusterSize;
        vkCmdCopyBuffer(cmd, stagingBuffer, clusterBuffer, 1, &copyRegion);
        context.endSingleTimeCommands(cmd);
        
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingMemory, nullptr);
    }
    
    // Upload hierarchy node data
    if (!hierarchyNodes.empty()) {
        VkDeviceSize nodeSize = hierarchyNodes.size() * sizeof(HierarchyNode);
        
        VkBuffer stagingBuffer;
        VkDeviceMemory stagingMemory;
        createBuffer(nodeSize,
                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     stagingBuffer, stagingMemory);
        
        void* data;
        vkMapMemory(device, stagingMemory, 0, nodeSize, 0, &data);
        memcpy(data, hierarchyNodes.data(), nodeSize);
        vkUnmapMemory(device, stagingMemory);
        
        VkCommandBuffer cmd = context.beginSingleTimeCommands();
        VkBufferCopy copyRegion{};
        copyRegion.size = nodeSize;
        vkCmdCopyBuffer(cmd, stagingBuffer, hierarchyNodeBuffer, 1, &copyRegion);
        context.endSingleTimeCommands(cmd);
        
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingMemory, nullptr);
    }
}

void ClusterHierarchy::createGPUBuffers() {
    cleanupGPUBuffers();
    
    VkDeviceSize clusterSize = std::max(clusters.size(), size_t(1)) * sizeof(Cluster);
    VkDeviceSize nodeSize = std::max(hierarchyNodes.size(), size_t(1)) * sizeof(HierarchyNode);
    VkDeviceSize queueStateSize = sizeof(QueueState);
    VkDeviceSize candidateSize = MAX_CANDIDATE_NODES * sizeof(CandidateNode);
    VkDeviceSize visibleSize = MAX_VISIBLE_CLUSTERS * sizeof(VisibleCluster);
    
    // Cluster buffer
    createBuffer(clusterSize,
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | 
                 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 clusterBuffer, clusterBufferMemory);
    clusterBufferAddress = getBufferAddress(clusterBuffer);
    
    // Hierarchy node buffer
    createBuffer(nodeSize,
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 hierarchyNodeBuffer, hierarchyNodeBufferMemory);
    hierarchyNodeBufferAddress = getBufferAddress(hierarchyNodeBuffer);
    
    // Queue state buffer (GPU read/write)
    createBuffer(queueStateSize,
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 queueStateBuffer, queueStateBufferMemory);
    
    // Candidate node buffer (persistent thread queue)
    createBuffer(candidateSize,
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 candidateNodeBuffer, candidateNodeBufferMemory);
    
    // Visible cluster buffer (output)
    createBuffer(visibleSize,
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 visibleClusterBuffer, visibleClusterBufferMemory);
}

void ClusterHierarchy::cleanupGPUBuffers() {
    VkDevice device = context.getDevice();
    
    auto destroyBuffer = [device](VkBuffer& buffer, VkDeviceMemory& memory) {
        if (buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, buffer, nullptr);
            buffer = VK_NULL_HANDLE;
        }
        if (memory != VK_NULL_HANDLE) {
            vkFreeMemory(device, memory, nullptr);
            memory = VK_NULL_HANDLE;
        }
    };
    
    destroyBuffer(clusterBuffer, clusterBufferMemory);
    destroyBuffer(hierarchyNodeBuffer, hierarchyNodeBufferMemory);
    destroyBuffer(queueStateBuffer, queueStateBufferMemory);
    destroyBuffer(candidateNodeBuffer, candidateNodeBufferMemory);
    destroyBuffer(visibleClusterBuffer, visibleClusterBufferMemory);
    
    clusterBufferAddress = 0;
    hierarchyNodeBufferAddress = 0;
}

void ClusterHierarchy::createBuffer(
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties,
    VkBuffer& buffer,
    VkDeviceMemory& bufferMemory
) {
    VkDevice device = context.getDevice();
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create buffer!");
    }
    
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer, &memRequirements);
    
    VkMemoryAllocateFlagsInfo allocFlagsInfo{};
    allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
        allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    }
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);
    allocInfo.pNext = (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) ? &allocFlagsInfo : nullptr;
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate buffer memory!");
    }
    
    vkBindBufferMemory(device, buffer, bufferMemory, 0);
}

VkDeviceAddress ClusterHierarchy::getBufferAddress(VkBuffer buffer) {
    VkBufferDeviceAddressInfo addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addressInfo.buffer = buffer;
    return vkGetBufferDeviceAddress(context.getDevice(), &addressInfo);
}

uint32_t ClusterHierarchy::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(context.getPhysicalDevice(), &memProperties);
    
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && 
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    
    throw std::runtime_error("Failed to find suitable memory type!");
}

void ClusterHierarchy::computeClusterBounds(
    const std::vector<glm::vec3>& vertices,
    const std::vector<uint32_t>& indices,
    uint32_t startIndex,
    uint32_t triangleCount,
    ClusterBounds& outBounds
) {
    // Gather all positions for this cluster
    std::vector<glm::vec3> clusterPositions;
    clusterPositions.reserve(triangleCount * 3);
    
    for (uint32_t i = 0; i < triangleCount * 3; ++i) {
        uint32_t idx = indices[startIndex + i];
        if (idx < vertices.size()) {
            clusterPositions.push_back(vertices[idx]);
        }
    }
    
    if (clusterPositions.empty()) {
        std::memset(&outBounds, 0, sizeof(ClusterBounds));
        return;
    }
    
    // Compute accurate bounding sphere
    glm::vec3 sphereCenter;
    float sphereRadius;
    computeBoundingSphere(clusterPositions, sphereCenter, sphereRadius);
    
    outBounds.sphereCenter[0] = sphereCenter.x;
    outBounds.sphereCenter[1] = sphereCenter.y;
    outBounds.sphereCenter[2] = sphereCenter.z;
    outBounds.sphereRadius = sphereRadius;
    
    // Compute AABB
    glm::vec3 boxCenter, boxExtent;
    computeAABB(clusterPositions, boxCenter, boxExtent);
    
    outBounds.boxCenter[0] = boxCenter.x;
    outBounds.boxCenter[1] = boxCenter.y;
    outBounds.boxCenter[2] = boxCenter.z;
    outBounds.boxExtentX = boxExtent.x;
    outBounds.boxExtentY = boxExtent.y;
    outBounds.boxExtentZ = boxExtent.z;
}

// ============================================================================
// CLUSTER CULLING SYSTEM IMPLEMENTATION
// ============================================================================

ClusterCullingSystem::ClusterCullingSystem(VulkanContext& context)
    : context(context) {
}

ClusterCullingSystem::~ClusterCullingSystem() {
    VkDevice device = context.getDevice();
    
    if (instanceCullingPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, instanceCullingPipeline, nullptr);
    }
    if (clusterCullingPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, clusterCullingPipeline, nullptr);
    }
    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    }
    if (descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
    }
    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    }
    
    if (visibleClusterBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, visibleClusterBuffer, nullptr);
    }
    if (visibleClusterBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, visibleClusterBufferMemory, nullptr);
    }
    if (queueStateBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, queueStateBuffer, nullptr);
    }
    if (queueStateBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, queueStateBufferMemory, nullptr);
    }
}

void ClusterCullingSystem::initialize(VkImageView hzbImageView) {
    createDescriptorSets();
    createPipelines();
}

void ClusterCullingSystem::beginFrame(VkCommandBuffer cmd) {
    // Reset queue state
    vkCmdFillBuffer(cmd, queueStateBuffer, 0, sizeof(QueueState), 0);
    
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void ClusterCullingSystem::cullInstances(
    VkCommandBuffer cmd,
    const ClusterHierarchy& hierarchy,
    const glm::mat4& viewProj,
    const glm::vec3& cameraPos,
    uint32_t instanceCount
) {
    // TODO: Implement in Turn 4
    // For now, just pass all instances through
}

void ClusterCullingSystem::cullClusters(VkCommandBuffer cmd, CullingPass pass) {
    // TODO: Implement in Turn 5-6
    // For now, placeholder
}

uint32_t ClusterCullingSystem::getVisibleClusterCount() const {
    // TODO: Read from GPU buffer
    return 0;
}

void ClusterCullingSystem::createPipelines() {
    // TODO: Create compute pipelines in Turn 4
}

void ClusterCullingSystem::createDescriptorSets() {
    // TODO: Create descriptor sets in Turn 4
}
