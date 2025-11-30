/**
 * DestructionSystem.cpp
 * 
 * Implementation of Chaos-style destruction system.
 * Uses Voronoi tessellation with strain-based fracturing.
 */

#include "DestructionSystem.h"
#include "AsyncPhysics.h"
#include <algorithm>
#include <random>
#include <queue>
#include <numeric>
#include <cmath>

// Jolt headers
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>

// Constants
namespace {
    constexpr float kEpsilon = 1e-6f;
    constexpr uint32_t kMaxIterations = 100;
}

DestructionSystem::DestructionSystem() = default;

DestructionSystem::~DestructionSystem() {
    shutdown();
}

bool DestructionSystem::initialize(AsyncPhysics* physics, VulkanContext* context) {
    if (initialized_) return true;
    
    physics_ = physics;
    context_ = context;
    
    initialized_ = true;
    return true;
}

void DestructionSystem::shutdown() {
    if (!initialized_) return;
    
    // Cleanup all instances
    for (auto& [id, instance] : instances_) {
        for (auto& piece : instance.pieces) {
            if (piece.bodyId && physics_) {
                // Body cleanup handled by physics system
            }
        }
    }
    instances_.clear();
    fractureData_.clear();
    pendingBreaks_.clear();
    debris_.clear();
    
    initialized_ = false;
}

std::vector<glm::vec3> DestructionSystem::generateVoronoiSites(
    const glm::vec3& boundsMin,
    const glm::vec3& boundsMax,
    uint32_t count,
    bool clustered) {
    
    std::vector<glm::vec3> sites;
    sites.reserve(count);
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> distX(boundsMin.x, boundsMax.x);
    std::uniform_real_distribution<float> distY(boundsMin.y, boundsMax.y);
    std::uniform_real_distribution<float> distZ(boundsMin.z, boundsMax.z);
    
    if (clustered) {
        // Generate cluster centers first
        uint32_t clusterCount = std::max(1u, count / 8);
        std::vector<glm::vec3> clusterCenters;
        clusterCenters.reserve(clusterCount);
        
        for (uint32_t i = 0; i < clusterCount; ++i) {
            clusterCenters.push_back({distX(gen), distY(gen), distZ(gen)});
        }
        
        // Generate sites around cluster centers
        std::uniform_int_distribution<uint32_t> clusterDist(0, clusterCount - 1);
        std::normal_distribution<float> offsetDist(0.0f, 0.1f);
        
        glm::vec3 boundsSize = boundsMax - boundsMin;
        float maxOffset = glm::length(boundsSize) * 0.15f;
        
        for (uint32_t i = 0; i < count; ++i) {
            glm::vec3 center = clusterCenters[clusterDist(gen)];
            glm::vec3 offset(
                offsetDist(gen) * boundsSize.x,
                offsetDist(gen) * boundsSize.y,
                offsetDist(gen) * boundsSize.z
            );
            offset = glm::clamp(offset, -glm::vec3(maxOffset), glm::vec3(maxOffset));
            
            glm::vec3 site = glm::clamp(center + offset, boundsMin, boundsMax);
            sites.push_back(site);
        }
    } else {
        // Uniform random distribution with Lloyd relaxation
        for (uint32_t i = 0; i < count; ++i) {
            sites.push_back({distX(gen), distY(gen), distZ(gen)});
        }
        
        // Lloyd relaxation for better distribution
        for (uint32_t iter = 0; iter < 5; ++iter) {
            // Build simple grid for Voronoi approximation
            const int gridRes = 32;
            glm::vec3 cellSize = (boundsMax - boundsMin) / float(gridRes);
            
            std::vector<glm::vec3> centroids(count, glm::vec3(0));
            std::vector<uint32_t> centroidCounts(count, 0);
            
            // Assign grid points to nearest site
            for (int x = 0; x < gridRes; ++x) {
                for (int y = 0; y < gridRes; ++y) {
                    for (int z = 0; z < gridRes; ++z) {
                        glm::vec3 point = boundsMin + cellSize * glm::vec3(x + 0.5f, y + 0.5f, z + 0.5f);
                        
                        float minDist = std::numeric_limits<float>::max();
                        uint32_t nearest = 0;
                        
                        for (uint32_t s = 0; s < count; ++s) {
                            float dist = glm::distance(point, sites[s]);
                            if (dist < minDist) {
                                minDist = dist;
                                nearest = s;
                            }
                        }
                        
                        centroids[nearest] += point;
                        centroidCounts[nearest]++;
                    }
                }
            }
            
            // Move sites to centroids
            for (uint32_t s = 0; s < count; ++s) {
                if (centroidCounts[s] > 0) {
                    sites[s] = glm::clamp(
                        centroids[s] / float(centroidCounts[s]),
                        boundsMin, boundsMax
                    );
                }
            }
        }
    }
    
    return sites;
}

DestructionSystem::VoronoiDiagram DestructionSystem::generateVoronoi(
    const std::vector<glm::vec3>& vertices,
    const std::vector<uint32_t>& indices,
    const DestructibleConfig& config) {
    
    VoronoiDiagram diagram;
    
    // Compute bounds
    diagram.boundsMin = glm::vec3(std::numeric_limits<float>::max());
    diagram.boundsMax = glm::vec3(std::numeric_limits<float>::lowest());
    
    for (const auto& v : vertices) {
        diagram.boundsMin = glm::min(diagram.boundsMin, v);
        diagram.boundsMax = glm::max(diagram.boundsMax, v);
    }
    
    // Expand bounds slightly to ensure all geometry is contained
    glm::vec3 padding = (diagram.boundsMax - diagram.boundsMin) * 0.05f;
    diagram.boundsMin -= padding;
    diagram.boundsMax += padding;
    
    // Generate Voronoi sites
    std::vector<glm::vec3> sites = generateVoronoiSites(
        diagram.boundsMin, diagram.boundsMax,
        config.voronoiCellCount,
        config.useClusteredSites
    );
    
    // Create cells for each site
    diagram.cells.resize(sites.size());
    
    for (uint32_t i = 0; i < sites.size(); ++i) {
        VoronoiCell& cell = diagram.cells[i];
        cell.id = i;
        cell.center = sites[i];
        
        // Initialize cell as the full bounding box
        cell.vertices = {
            diagram.boundsMin,
            {diagram.boundsMax.x, diagram.boundsMin.y, diagram.boundsMin.z},
            {diagram.boundsMax.x, diagram.boundsMax.y, diagram.boundsMin.z},
            {diagram.boundsMin.x, diagram.boundsMax.y, diagram.boundsMin.z},
            {diagram.boundsMin.x, diagram.boundsMin.y, diagram.boundsMax.z},
            {diagram.boundsMax.x, diagram.boundsMin.y, diagram.boundsMax.z},
            diagram.boundsMax,
            {diagram.boundsMin.x, diagram.boundsMax.y, diagram.boundsMax.z}
        };
        
        // Clip cell by half-planes from other sites
        for (uint32_t j = 0; j < sites.size(); ++j) {
            if (i == j) continue;
            
            // Half-plane between sites i and j
            glm::vec3 midpoint = (sites[i] + sites[j]) * 0.5f;
            glm::vec3 normal = glm::normalize(sites[j] - sites[i]);
            
            // Clip cell vertices by this half-plane
            std::vector<glm::vec3> newVertices;
            
            for (size_t v = 0; v < cell.vertices.size(); ++v) {
                const glm::vec3& curr = cell.vertices[v];
                const glm::vec3& next = cell.vertices[(v + 1) % cell.vertices.size()];
                
                float currDist = glm::dot(curr - midpoint, normal);
                float nextDist = glm::dot(next - midpoint, normal);
                
                if (currDist <= 0) {
                    newVertices.push_back(curr);
                }
                
                if ((currDist > 0) != (nextDist > 0)) {
                    // Edge crosses plane, compute intersection
                    float t = currDist / (currDist - nextDist);
                    glm::vec3 intersection = curr + t * (next - curr);
                    newVertices.push_back(intersection);
                }
            }
            
            cell.vertices = std::move(newVertices);
            
            if (cell.vertices.size() < 4) {
                // Degenerate cell, skip
                break;
            }
        }
        
        // Compute cell properties
        if (cell.vertices.size() >= 4) {
            cell.centroid = glm::vec3(0);
            for (const auto& v : cell.vertices) {
                cell.centroid += v;
            }
            cell.centroid /= float(cell.vertices.size());
            
            // Approximate volume using convex hull decomposition
            cell.volume = 0.0f;
            for (size_t f = 1; f < cell.vertices.size() - 1; ++f) {
                glm::vec3 v0 = cell.vertices[0] - cell.centroid;
                glm::vec3 v1 = cell.vertices[f] - cell.centroid;
                glm::vec3 v2 = cell.vertices[f + 1] - cell.centroid;
                cell.volume += std::abs(glm::dot(v0, glm::cross(v1, v2))) / 6.0f;
            }
            
            // Default mass based on volume (density = 1)
            cell.mass = cell.volume;
        }
    }
    
    // Clip cells to mesh geometry
    for (auto& cell : diagram.cells) {
        clipMeshToCell(vertices, indices, cell);
    }
    
    // Build connectivity
    buildConnectivityGraph(diagram, config.useDelaunayConnectivity);
    
    return diagram;
}

void DestructionSystem::clipMeshToCell(
    const std::vector<glm::vec3>& vertices,
    const std::vector<uint32_t>& indices,
    VoronoiCell& cell) {
    
    // For each triangle in mesh, check if it's inside this cell
    // and clip to cell boundaries
    // (Simplified - full implementation would do CSG intersection)
    
    if (cell.vertices.size() < 4) return;
    
    // Compute cell bounding box
    glm::vec3 cellMin(std::numeric_limits<float>::max());
    glm::vec3 cellMax(std::numeric_limits<float>::lowest());
    
    for (const auto& v : cell.vertices) {
        cellMin = glm::min(cellMin, v);
        cellMax = glm::max(cellMax, v);
    }
    
    // Quick check: if mesh triangles fall outside cell bounds, skip
    // Full implementation would do proper mesh-cell intersection
    
    // For now, just refine the cell vertices to match mesh boundary
    // This is a simplified approach - production code would use
    // proper CSG boolean operations
}

void DestructionSystem::buildConnectivityGraph(VoronoiDiagram& diagram, bool useDelaunay) {
    // Build edges between adjacent cells
    for (size_t i = 0; i < diagram.cells.size(); ++i) {
        for (size_t j = i + 1; j < diagram.cells.size(); ++j) {
            const VoronoiCell& cellA = diagram.cells[i];
            const VoronoiCell& cellB = diagram.cells[j];
            
            // Check if cells share a face (are neighbors in Voronoi diagram)
            float dist = glm::distance(cellA.center, cellB.center);
            
            // Cells are neighbors if they share the bisecting plane
            // Approximate by checking if distance is reasonable
            glm::vec3 boundsSize = diagram.boundsMax - diagram.boundsMin;
            float avgSize = (boundsSize.x + boundsSize.y + boundsSize.z) / 3.0f;
            float neighborThreshold = avgSize / std::sqrt(float(diagram.cells.size())) * 2.5f;
            
            if (dist < neighborThreshold) {
                // Compute approximate shared face area
                glm::vec3 midpoint = (cellA.center + cellB.center) * 0.5f;
                float contactRadius = std::min(cellA.volume, cellB.volume);
                contactRadius = std::pow(contactRadius, 1.0f / 3.0f) * 0.5f;
                float area = 3.14159f * contactRadius * contactRadius;
                
                ConnectivityEdge edge;
                edge.pieceA = static_cast<uint32_t>(i);
                edge.pieceB = static_cast<uint32_t>(j);
                edge.strength = 1.0f;  // Will be scaled by config
                edge.area = area;
                edge.contactPoint = midpoint;
                edge.contactNormal = glm::normalize(cellB.center - cellA.center);
                edge.isBroken = false;
                
                diagram.edges.push_back(edge);
                
                // Track neighbors
                diagram.cells[i].neighbors.push_back(static_cast<uint32_t>(j));
                diagram.cells[j].neighbors.push_back(static_cast<uint32_t>(i));
                diagram.cells[i].connectionStrengths.push_back(edge.strength);
                diagram.cells[j].connectionStrengths.push_back(edge.strength);
            }
        }
    }
}

void DestructionSystem::buildHierarchy(uint32_t fractureDataId, const DestructibleConfig& config) {
    auto it = fractureData_.find(fractureDataId);
    if (it == fractureData_.end()) return;
    
    FractureData& data = it->second;
    std::vector<ClusterNode>& hierarchy = data.hierarchy;
    
    // Start with each cell as a leaf node
    std::vector<uint32_t> currentLevel;
    
    for (const auto& cell : data.voronoi.cells) {
        ClusterNode leaf;
        leaf.id = static_cast<uint32_t>(hierarchy.size());
        leaf.parentId = UINT32_MAX;
        leaf.center = cell.centroid;
        leaf.radius = std::pow(cell.volume, 1.0f / 3.0f);
        leaf.breakThreshold = config.baseStrainThreshold * 
            (1.0f + (std::rand() / float(RAND_MAX) - 0.5f) * config.strainVariance);
        leaf.totalStrain = 0.0f;
        leaf.isLeaf = true;
        leaf.isBroken = false;
        leaf.childIds.push_back(cell.id);
        
        currentLevel.push_back(leaf.id);
        hierarchy.push_back(leaf);
    }
    
    // Build hierarchy levels bottom-up
    float clusterRadius = config.clusterRadius;
    
    for (uint32_t level = 0; level < config.hierarchyLevels && currentLevel.size() > 1; ++level) {
        std::vector<uint32_t> nextLevel;
        std::vector<bool> assigned(currentLevel.size(), false);
        
        for (size_t i = 0; i < currentLevel.size(); ++i) {
            if (assigned[i]) continue;
            
            ClusterNode& nodeA = hierarchy[currentLevel[i]];
            
            // Find nearby nodes to cluster with
            std::vector<uint32_t> clusterMembers;
            clusterMembers.push_back(currentLevel[i]);
            assigned[i] = true;
            
            for (size_t j = i + 1; j < currentLevel.size(); ++j) {
                if (assigned[j]) continue;
                
                ClusterNode& nodeB = hierarchy[currentLevel[j]];
                float dist = glm::distance(nodeA.center, nodeB.center);
                
                if (dist < clusterRadius) {
                    clusterMembers.push_back(currentLevel[j]);
                    assigned[j] = true;
                }
            }
            
            if (clusterMembers.size() > 1) {
                // Create parent cluster
                ClusterNode parent;
                parent.id = static_cast<uint32_t>(hierarchy.size());
                parent.parentId = UINT32_MAX;
                parent.childIds = clusterMembers;
                parent.isLeaf = false;
                parent.isBroken = false;
                parent.totalStrain = 0.0f;
                
                // Compute center as average
                parent.center = glm::vec3(0);
                parent.radius = 0.0f;
                float maxDist = 0.0f;
                
                for (uint32_t childId : clusterMembers) {
                    parent.center += hierarchy[childId].center;
                    hierarchy[childId].parentId = parent.id;
                }
                parent.center /= float(clusterMembers.size());
                
                // Compute radius
                for (uint32_t childId : clusterMembers) {
                    float dist = glm::distance(parent.center, hierarchy[childId].center);
                    dist += hierarchy[childId].radius;
                    maxDist = std::max(maxDist, dist);
                }
                parent.radius = maxDist;
                
                // Break threshold scales with cluster size
                parent.breakThreshold = config.baseStrainThreshold * 
                    float(clusterMembers.size()) * 0.7f;
                
                nextLevel.push_back(parent.id);
                hierarchy.push_back(parent);
            } else {
                // Single node, promote directly
                nextLevel.push_back(clusterMembers[0]);
            }
        }
        
        currentLevel = std::move(nextLevel);
        clusterRadius *= 2.0f;  // Increase radius for next level
    }
}

uint32_t DestructionSystem::preFracture(
    uint32_t meshId,
    const std::vector<glm::vec3>& vertices,
    const std::vector<uint32_t>& indices,
    const DestructibleConfig& config) {
    
    uint32_t fractureId = nextFractureId_++;
    
    FractureData data;
    data.config = config;
    data.voronoi = generateVoronoi(vertices, indices, config);
    
    // Store mesh data for each cell
    data.cellVertices.resize(data.voronoi.cells.size());
    data.cellIndices.resize(data.voronoi.cells.size());
    
    for (size_t i = 0; i < data.voronoi.cells.size(); ++i) {
        const VoronoiCell& cell = data.voronoi.cells[i];
        data.cellVertices[i] = cell.vertices;
        
        // Triangulate convex cell (fan triangulation)
        if (cell.vertices.size() >= 3) {
            for (size_t v = 1; v < cell.vertices.size() - 1; ++v) {
                data.cellIndices[i].push_back(0);
                data.cellIndices[i].push_back(static_cast<uint32_t>(v));
                data.cellIndices[i].push_back(static_cast<uint32_t>(v + 1));
            }
        }
    }
    
    // Build cluster hierarchy
    fractureData_[fractureId] = std::move(data);
    buildHierarchy(fractureId, config);
    
    return fractureId;
}

uint32_t DestructionSystem::createInstance(
    uint32_t fractureDataId,
    const glm::vec3& position,
    const glm::quat& rotation,
    const glm::vec3& scale) {
    
    auto dataIt = fractureData_.find(fractureDataId);
    if (dataIt == fractureData_.end()) return 0;
    
    const FractureData& data = dataIt->second;
    uint32_t instanceId = nextInstanceId_++;
    
    DestructibleInstance instance;
    instance.fractureDataId = fractureDataId;
    instance.transform = glm::translate(glm::mat4(1.0f), position) *
                         glm::mat4_cast(rotation) *
                         glm::scale(glm::mat4(1.0f), scale);
    instance.isDestroyed = false;
    instance.totalStrain = 0.0f;
    
    // Create pieces from Voronoi cells
    instance.pieces.reserve(data.voronoi.cells.size());
    
    for (const auto& cell : data.voronoi.cells) {
        FracturePiece piece;
        piece.id = cell.id;
        piece.cellIds.push_back(cell.id);
        
        // Transform to world space
        glm::vec4 worldPos = instance.transform * glm::vec4(cell.centroid, 1.0f);
        piece.position = glm::vec3(worldPos);
        piece.rotation = rotation;
        piece.velocity = glm::vec3(0);
        piece.angularVelocity = glm::vec3(0);
        
        piece.totalMass = cell.mass * scale.x * scale.y * scale.z;
        piece.inertia = cell.inertia;
        
        piece.isReleased = false;
        piece.isActive = true;
        piece.strain = 0.0f;
        piece.strainThreshold = data.config.baseStrainThreshold *
            (1.0f + (std::rand() / float(RAND_MAX) - 0.5f) * data.config.strainVariance);
        
        instance.pieces.push_back(std::move(piece));
    }
    
    // Copy connectivity edges
    instance.edges = data.voronoi.edges;
    for (auto& edge : instance.edges) {
        edge.strength *= data.config.connectionStrength;
    }
    
    // Copy hierarchy
    instance.clusters = data.hierarchy;
    
    instances_[instanceId] = std::move(instance);
    return instanceId;
}

bool DestructionSystem::applyDamage(
    uint32_t objectId,
    const glm::vec3& point,
    const glm::vec3& direction,
    float magnitude) {
    
    auto it = instances_.find(objectId);
    if (it == instances_.end()) return false;
    
    DestructibleInstance& instance = it->second;
    if (instance.isDestroyed) return false;
    
    const FractureData& data = fractureData_[instance.fractureDataId];
    bool anyBroke = false;
    
    // Find affected pieces by distance to impact point
    float impactRadius = std::sqrt(magnitude) * 0.1f;
    
    for (auto& piece : instance.pieces) {
        if (piece.isReleased) continue;
        
        float dist = glm::distance(piece.position, point);
        if (dist < impactRadius) {
            // Apply strain based on distance
            float falloff = 1.0f - (dist / impactRadius);
            float strainAmount = magnitude * falloff * data.config.impactMultiplier;
            
            piece.strain += strainAmount;
            instance.totalStrain += strainAmount;
            
            // Check if piece should break
            if (piece.strain >= piece.strainThreshold) {
                pendingBreaks_.push_back({objectId, piece.id, piece.strain});
            }
        }
    }
    
    // Process any immediate breaks
    if (!pendingBreaks_.empty()) {
        processBreaking(objectId);
        anyBroke = true;
    }
    
    return anyBroke;
}

bool DestructionSystem::applyExplosion(
    const glm::vec3& center,
    float radius,
    float force) {
    
    bool anyBroke = false;
    
    for (auto& [instanceId, instance] : instances_) {
        if (instance.isDestroyed) continue;
        
        for (auto& piece : instance.pieces) {
            float dist = glm::distance(piece.position, center);
            
            if (dist < radius) {
                float falloff = 1.0f - (dist / radius);
                falloff = falloff * falloff;  // Quadratic falloff
                
                float strainAmount = force * falloff;
                piece.strain += strainAmount;
                
                if (piece.strain >= piece.strainThreshold && !piece.isReleased) {
                    pendingBreaks_.push_back({instanceId, piece.id, piece.strain});
                    
                    // Apply explosion impulse
                    glm::vec3 dir = glm::normalize(piece.position - center);
                    piece.velocity += dir * strainAmount * 0.01f;
                }
            }
        }
        
        if (!pendingBreaks_.empty()) {
            processBreaking(instanceId);
            anyBroke = true;
        }
    }
    
    return anyBroke;
}

void DestructionSystem::processBreaking(uint32_t objectId) {
    auto it = instances_.find(objectId);
    if (it == instances_.end()) return;
    
    DestructibleInstance& instance = it->second;
    std::vector<uint32_t> brokenPieceIds;
    
    // Sort by strain (break highest strain first)
    std::sort(pendingBreaks_.begin(), pendingBreaks_.end(),
              [](const PendingBreak& a, const PendingBreak& b) {
                  return a.strain > b.strain;
              });
    
    for (const auto& pending : pendingBreaks_) {
        if (pending.objectId != objectId) continue;
        
        FracturePiece& piece = instance.pieces[pending.pieceId];
        if (piece.isReleased) continue;
        
        // Break connections to this piece
        for (size_t i = 0; i < instance.edges.size(); ++i) {
            auto& edge = instance.edges[i];
            if (edge.isBroken) continue;
            
            if (edge.pieceA == pending.pieceId || edge.pieceB == pending.pieceId) {
                breakConnection(objectId, static_cast<uint32_t>(i));
            }
        }
        
        // Release the piece
        releasePiece(objectId, pending.pieceId);
        brokenPieceIds.push_back(pending.pieceId);
    }
    
    // Clear pending breaks for this object
    pendingBreaks_.erase(
        std::remove_if(pendingBreaks_.begin(), pendingBreaks_.end(),
                       [objectId](const PendingBreak& b) { return b.objectId == objectId; }),
        pendingBreaks_.end()
    );
    
    // Fire callback
    if (callback_ && !brokenPieceIds.empty()) {
        callback_(objectId, brokenPieceIds);
    }
    
    // Check if object is fully destroyed
    bool allReleased = true;
    for (const auto& piece : instance.pieces) {
        if (!piece.isReleased) {
            allReleased = false;
            break;
        }
    }
    instance.isDestroyed = allReleased;
}

void DestructionSystem::breakConnection(uint32_t objectId, uint32_t edgeIndex) {
    auto it = instances_.find(objectId);
    if (it == instances_.end()) return;
    
    DestructibleInstance& instance = it->second;
    if (edgeIndex >= instance.edges.size()) return;
    
    instance.edges[edgeIndex].isBroken = true;
}

void DestructionSystem::releasePiece(uint32_t objectId, uint32_t pieceId) {
    auto it = instances_.find(objectId);
    if (it == instances_.end()) return;
    
    DestructibleInstance& instance = it->second;
    if (pieceId >= instance.pieces.size()) return;
    
    FracturePiece& piece = instance.pieces[pieceId];
    if (piece.isReleased) return;
    
    piece.isReleased = true;
    
    // Create physics body for this piece
    createPieceBody(objectId, pieceId);
    
    // Track as debris
    const FractureData& data = fractureData_[instance.fractureDataId];
    debris_.push_back({objectId, pieceId, data.config.debrisLifetime});
}

void DestructionSystem::createPieceBody(uint32_t objectId, uint32_t pieceId) {
    if (!physics_) return;
    
    auto it = instances_.find(objectId);
    if (it == instances_.end()) return;
    
    DestructibleInstance& instance = it->second;
    FracturePiece& piece = instance.pieces[pieceId];
    
    const FractureData& data = fractureData_[instance.fractureDataId];
    
    // Get vertices for this piece
    const auto& vertices = data.cellVertices[pieceId];
    if (vertices.size() < 4) return;
    
    // Create convex hull shape in Jolt
    // (This would integrate with your existing physics body creation)
    
    // Register with async physics
    PhysicsBodyHandle handle;
    handle.position = piece.position;
    handle.rotation = piece.rotation;
    handle.linearVelocity = piece.velocity;
    handle.angularVelocity = piece.angularVelocity;
    handle.isDynamic = true;
    
    // In a full implementation, you'd create the Jolt body here
    // and store the body ID in piece.bodyId
}

void DestructionSystem::update(float deltaTime) {
    if (!initialized_) return;
    
    // Process any pending breaks
    for (const auto& pending : pendingBreaks_) {
        processBreaking(pending.objectId);
    }
    pendingBreaks_.clear();
    
    // Update released pieces from physics
    for (auto& [instanceId, instance] : instances_) {
        for (auto& piece : instance.pieces) {
            if (piece.isReleased && piece.isActive && physics_) {
                // Get updated transform from physics system
                // piece.position = physics_->getBodyPosition(piece.bodyId);
                // piece.rotation = physics_->getBodyRotation(piece.bodyId);
            }
        }
    }
    
    // Cleanup old debris
    cleanupDebris(deltaTime);
}

void DestructionSystem::cleanupDebris(float deltaTime) {
    auto it = debris_.begin();
    while (it != debris_.end()) {
        it->lifetime -= deltaTime;
        
        if (it->lifetime <= 0.0f) {
            // Find and deactivate the piece
            auto instanceIt = instances_.find(it->objectId);
            if (instanceIt != instances_.end()) {
                auto& instance = instanceIt->second;
                if (it->pieceId < instance.pieces.size()) {
                    FracturePiece& piece = instance.pieces[it->pieceId];
                    piece.isActive = false;
                    
                    // Remove physics body
                    if (piece.bodyId && physics_) {
                        // physics_->unregisterBody(piece.bodyId);
                        piece.bodyId = nullptr;
                    }
                }
            }
            
            it = debris_.erase(it);
        } else {
            ++it;
        }
    }
}

bool DestructionSystem::getPieceMesh(
    uint32_t objectId,
    uint32_t pieceId,
    std::vector<glm::vec3>& outVertices,
    std::vector<uint32_t>& outIndices) {
    
    auto it = instances_.find(objectId);
    if (it == instances_.end()) return false;
    
    const DestructibleInstance& instance = it->second;
    auto dataIt = fractureData_.find(instance.fractureDataId);
    if (dataIt == fractureData_.end()) return false;
    
    const FractureData& data = dataIt->second;
    if (pieceId >= data.cellVertices.size()) return false;
    
    outVertices = data.cellVertices[pieceId];
    outIndices = data.cellIndices[pieceId];
    
    // Transform vertices to world space
    const FracturePiece& piece = instance.pieces[pieceId];
    glm::mat4 transform = glm::translate(glm::mat4(1.0f), piece.position) *
                          glm::mat4_cast(piece.rotation);
    
    for (auto& v : outVertices) {
        glm::vec4 worldPos = transform * glm::vec4(v, 1.0f);
        v = glm::vec3(worldPos);
    }
    
    return true;
}

void DestructionSystem::getActiveTransforms(
    uint32_t objectId,
    std::vector<glm::mat4>& outTransforms) {
    
    auto it = instances_.find(objectId);
    if (it == instances_.end()) return;
    
    const DestructibleInstance& instance = it->second;
    
    outTransforms.clear();
    outTransforms.reserve(instance.pieces.size());
    
    for (const auto& piece : instance.pieces) {
        if (!piece.isActive) continue;
        
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), piece.position) *
                              glm::mat4_cast(piece.rotation);
        outTransforms.push_back(transform);
    }
}

DestructionSystem::Stats DestructionSystem::getStats() const {
    Stats stats = {};
    stats.totalFracturedObjects = static_cast<uint32_t>(fractureData_.size());
    stats.pendingBreaks = static_cast<uint32_t>(pendingBreaks_.size());
    
    for (const auto& [id, instance] : instances_) {
        for (const auto& piece : instance.pieces) {
            if (piece.isActive) stats.activePieces++;
        }
        stats.totalStrainAccumulated += instance.totalStrain;
    }
    
    return stats;
}
