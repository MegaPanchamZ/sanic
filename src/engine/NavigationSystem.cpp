/**
 * NavigationSystem.cpp
 * 
 * Implementation of AI navigation using Recast/Detour
 */

#include "NavigationSystem.h"

// Recast/Detour headers would be included here
// #include "Recast.h"
// #include "DetourNavMesh.h"
// #include "DetourNavMeshBuilder.h"
// #include "DetourNavMeshQuery.h"
// #include "DetourCrowd.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Sanic {

// ============================================================================
// NAV MESH INPUT GEOMETRY
// ============================================================================

void NavMeshInputGeometry::addTriangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    uint32_t baseIdx = static_cast<uint32_t>(vertices.size());
    
    vertices.push_back(a);
    vertices.push_back(b);
    vertices.push_back(c);
    
    indices.push_back(baseIdx);
    indices.push_back(baseIdx + 1);
    indices.push_back(baseIdx + 2);
    
    // Update bounds
    boundsMin = glm::min(boundsMin, a);
    boundsMin = glm::min(boundsMin, b);
    boundsMin = glm::min(boundsMin, c);
    
    boundsMax = glm::max(boundsMax, a);
    boundsMax = glm::max(boundsMax, b);
    boundsMax = glm::max(boundsMax, c);
}

void NavMeshInputGeometry::calculateBounds() {
    boundsMin = glm::vec3(FLT_MAX);
    boundsMax = glm::vec3(-FLT_MAX);
    
    for (const auto& v : vertices) {
        boundsMin = glm::min(boundsMin, v);
        boundsMax = glm::max(boundsMax, v);
    }
}

// ============================================================================
// NAVIGATION MESH
// ============================================================================

NavigationMesh::NavigationMesh() = default;

NavigationMesh::~NavigationMesh() {
    if (navMesh_) {
        // dtFreeNavMesh(navMesh_);
        navMesh_ = nullptr;
    }
}

bool NavigationMesh::build(const NavMeshInputGeometry& geometry, const NavMeshSettings& settings) {
    settings_ = settings;
    boundsMin_ = geometry.boundsMin;
    boundsMax_ = geometry.boundsMax;
    
    if (settings.useTiles) {
        return buildTiledMesh(geometry);
    } else {
        return buildSingleTile(geometry);
    }
}

bool NavigationMesh::buildSingleTile(const NavMeshInputGeometry& geometry) {
    // This would use Recast to build a single-tile NavMesh
    // 
    // 1. Create Recast config from settings
    // 2. Create heightfield (rcCreateHeightfield)
    // 3. Rasterize triangles (rcRasterizeTriangles)
    // 4. Filter walkables (rcFilterLowHangingWalkableObstacles, etc.)
    // 5. Build compact heightfield (rcBuildCompactHeightfield)
    // 6. Build distance field (rcBuildDistanceField)
    // 7. Build regions (rcBuildRegions)
    // 8. Build contours (rcBuildContours)
    // 9. Build polygon mesh (rcBuildPolyMesh)
    // 10. Build detail mesh (rcBuildPolyMeshDetail)
    // 11. Create Detour NavMesh data (dtCreateNavMeshData)
    // 12. Initialize Detour NavMesh (dtAllocNavMesh, init)
    
    // Placeholder - actual implementation would use Recast library
    return true;
}

bool NavigationMesh::buildTiledMesh(const NavMeshInputGeometry& geometry) {
    // Similar to single tile but splits into grid of tiles
    // Each tile is built independently and added to the NavMesh
    
    float tileWidth = settings_.tileSize * settings_.cellSize;
    float tileHeight = settings_.tileSize * settings_.cellSize;
    
    int numTilesX = static_cast<int>(std::ceil((boundsMax_.x - boundsMin_.x) / tileWidth));
    int numTilesY = static_cast<int>(std::ceil((boundsMax_.z - boundsMin_.z) / tileHeight));
    
    // Create empty tiled NavMesh
    // dtNavMeshParams params;
    // params.orig[0] = boundsMin_.x;
    // params.orig[1] = boundsMin_.y;
    // params.orig[2] = boundsMin_.z;
    // params.tileWidth = tileWidth;
    // params.tileHeight = tileHeight;
    // params.maxTiles = numTilesX * numTilesY;
    // params.maxPolys = 1 << 20;
    // 
    // navMesh_ = dtAllocNavMesh();
    // navMesh_->init(&params);
    
    // Build each tile
    for (int y = 0; y < numTilesY; ++y) {
        for (int x = 0; x < numTilesX; ++x) {
            buildTile(x, y, geometry);
        }
    }
    
    return true;
}

bool NavigationMesh::buildTile(int tileX, int tileY, const NavMeshInputGeometry& geometry) {
    // Build a single tile and add it to the NavMesh
    // Filters geometry to only include triangles that overlap this tile
    
    return true;
}

void NavigationMesh::removeTile(int tileX, int tileY) {
    if (!navMesh_) return;
    
    // dtTileRef ref = navMesh_->getTileRefAt(tileX, tileY, 0);
    // if (ref) {
    //     navMesh_->removeTile(ref, nullptr, nullptr);
    // }
}

bool NavigationMesh::addOffMeshConnection(const OffMeshConnection& connection) {
    offMeshConnections_.push_back(connection);
    
    // Would rebuild affected tiles to include the connection
    
    return true;
}

void NavigationMesh::removeOffMeshConnection(uint32_t userId) {
    offMeshConnections_.erase(
        std::remove_if(offMeshConnections_.begin(), offMeshConnections_.end(),
            [userId](const OffMeshConnection& c) { return c.userId == userId; }),
        offMeshConnections_.end()
    );
}

bool NavigationMesh::saveToFile(const std::string& path) const {
    // Serialize NavMesh to file
    // Would iterate tiles and save their data
    return true;
}

bool NavigationMesh::loadFromFile(const std::string& path) {
    // Load serialized NavMesh
    return true;
}

// ============================================================================
// NAV QUERY FILTER
// ============================================================================

NavQueryFilter::NavQueryFilter() {
    // filter_ = new dtQueryFilter();
    // 
    // // Set default area costs
    // filter_->setAreaCost(NavArea::WALKABLE, 1.0f);
    // filter_->setAreaCost(NavArea::WATER, 10.0f);
    // filter_->setAreaCost(NavArea::GRASS, 1.5f);
    // filter_->setAreaCost(NavArea::ROAD, 0.5f);
    // filter_->setAreaCost(NavArea::DOOR, 1.0f);
    // 
    // filter_->setIncludeFlags(NavFlag::ALL);
    // filter_->setExcludeFlags(NavFlag::DISABLED);
}

NavQueryFilter::~NavQueryFilter() {
    // delete filter_;
}

void NavQueryFilter::setAreaCost(uint32_t areaId, float cost) {
    // if (filter_) {
    //     filter_->setAreaCost(areaId, cost);
    // }
}

float NavQueryFilter::getAreaCost(uint32_t areaId) const {
    // return filter_ ? filter_->getAreaCost(areaId) : 1.0f;
    return 1.0f;
}

void NavQueryFilter::setIncludeFlags(uint16_t flags) {
    // if (filter_) filter_->setIncludeFlags(flags);
}

void NavQueryFilter::setExcludeFlags(uint16_t flags) {
    // if (filter_) filter_->setExcludeFlags(flags);
}

// ============================================================================
// NAVIGATION QUERY
// ============================================================================

NavigationQuery::NavigationQuery(NavigationMesh& navMesh)
    : navMesh_(navMesh) {
    
    // query_ = dtAllocNavMeshQuery();
    // query_->init(navMesh.getNavMesh(), 2048);
    
    polyPath_.resize(MAX_POLYS);
}

NavigationQuery::~NavigationQuery() {
    // dtFreeNavMeshQuery(query_);
}

PathResult NavigationQuery::findPath(
    const glm::vec3& start,
    const glm::vec3& end,
    const NavQueryFilter& filter
) {
    PathResult result;
    
    if (!navMesh_.isValid()) {
        result.status = PathResult::Status::NoPath;
        return result;
    }
    
    // Find nearest polys for start and end
    // dtPolyRef startPoly, endPoly;
    // float nearestPt[3];
    // float extents[3] = { 2.0f, 4.0f, 2.0f };
    // 
    // dtStatus status = query_->findNearestPoly(
    //     &start.x, extents, filter.getFilter(), &startPoly, nearestPt);
    // 
    // if (dtStatusFailed(status) || startPoly == 0) {
    //     result.status = PathResult::Status::InvalidStart;
    //     return result;
    // }
    // 
    // status = query_->findNearestPoly(
    //     &end.x, extents, filter.getFilter(), &endPoly, nearestPt);
    // 
    // if (dtStatusFailed(status) || endPoly == 0) {
    //     result.status = PathResult::Status::InvalidEnd;
    //     return result;
    // }
    // 
    // // Find path
    // int polyCount = 0;
    // status = query_->findPath(
    //     startPoly, endPoly,
    //     &start.x, &end.x,
    //     filter.getFilter(),
    //     polyPath_.data(), &polyCount, MAX_POLYS);
    // 
    // if (dtStatusFailed(status) || polyCount == 0) {
    //     result.status = PathResult::Status::NoPath;
    //     return result;
    // }
    // 
    // // Smooth path
    // smoothPath(polyPath_, polyCount, start, end, result.path);
    // 
    // result.success = true;
    // result.partial = (dtStatusDetail(status, DT_PARTIAL_RESULT));
    // result.status = result.partial ? PathResult::Status::PartialPath : PathResult::Status::Success;
    
    // Placeholder implementation
    result.success = true;
    result.path.push_back(start);
    result.path.push_back(end);
    result.status = PathResult::Status::Success;
    
    return result;
}

void NavigationQuery::findPathAsync(
    const glm::vec3& start,
    const glm::vec3& end,
    PathCallback callback,
    const NavQueryFilter& filter
) {
    // Would use a job system to compute path on background thread
    // For now, just compute synchronously
    PathResult result = findPath(start, end, filter);
    callback(result);
}

bool NavigationQuery::findNearestPoint(
    const glm::vec3& point,
    glm::vec3& outNearest,
    float searchRadius
) {
    // dtPolyRef nearestPoly;
    // float extents[3] = { searchRadius, searchRadius * 2, searchRadius };
    // 
    // dtStatus status = query_->findNearestPoly(
    //     &point.x, extents, defaultFilter_.getFilter(),
    //     &nearestPoly, &outNearest.x);
    // 
    // return dtStatusSucceed(status) && nearestPoly != 0;
    
    outNearest = point;
    outNearest.y = 0;  // Project to ground
    return true;
}

bool NavigationQuery::raycast(
    const glm::vec3& start,
    const glm::vec3& end,
    glm::vec3& outHitPoint,
    glm::vec3& outHitNormal
) {
    // Would use dtNavMeshQuery::raycast
    return false;
}

bool NavigationQuery::isPointOnNavMesh(const glm::vec3& point, float tolerance) {
    glm::vec3 nearest;
    if (findNearestPoint(point, nearest, tolerance)) {
        return glm::distance(point, nearest) <= tolerance;
    }
    return false;
}

glm::vec3 NavigationQuery::getRandomPoint() {
    // dtPolyRef randomPoly;
    // float pt[3];
    // 
    // dtStatus status = query_->findRandomPoint(
    //     defaultFilter_.getFilter(), frand, &randomPoly, pt);
    // 
    // if (dtStatusSucceed(status)) {
    //     return glm::vec3(pt[0], pt[1], pt[2]);
    // }
    
    return glm::vec3(0);
}

glm::vec3 NavigationQuery::getRandomPointInRadius(const glm::vec3& center, float radius) {
    // Find start poly
    // dtPolyRef startPoly;
    // float nearestPt[3];
    // float extents[3] = { 2, 4, 2 };
    // 
    // query_->findNearestPoly(&center.x, extents, defaultFilter_.getFilter(),
    //                         &startPoly, nearestPt);
    // 
    // dtPolyRef randomPoly;
    // float pt[3];
    // 
    // query_->findRandomPointAroundCircle(
    //     startPoly, &center.x, radius,
    //     defaultFilter_.getFilter(), frand,
    //     &randomPoly, pt);
    // 
    // return glm::vec3(pt[0], pt[1], pt[2]);
    
    return center;
}

bool NavigationQuery::projectToNavMesh(const glm::vec3& point, glm::vec3& outProjected, float searchHeight) {
    return findNearestPoint(point, outProjected, searchHeight);
}

void NavigationQuery::smoothPath(
    const std::vector<uint64_t>& polys,
    int polyCount,
    const glm::vec3& start,
    const glm::vec3& end,
    std::vector<glm::vec3>& outPath
) {
    if (polyCount == 0) return;
    
    // String pulling algorithm (Detour's findStraightPath)
    // This creates a smooth path by finding the shortest path through the portal edges
    
    // float straightPath[MAX_SMOOTH * 3];
    // unsigned char straightPathFlags[MAX_SMOOTH];
    // dtPolyRef straightPathPolys[MAX_SMOOTH];
    // int straightPathCount = 0;
    // 
    // query_->findStraightPath(
    //     &start.x, &end.x,
    //     polys.data(), polyCount,
    //     straightPath, straightPathFlags, straightPathPolys,
    //     &straightPathCount, MAX_SMOOTH);
    // 
    // outPath.reserve(straightPathCount);
    // for (int i = 0; i < straightPathCount; ++i) {
    //     outPath.push_back(glm::vec3(
    //         straightPath[i * 3],
    //         straightPath[i * 3 + 1],
    //         straightPath[i * 3 + 2]
    //     ));
    // }
    
    // Placeholder
    outPath.push_back(start);
    outPath.push_back(end);
}

// ============================================================================
// CROWD MANAGER
// ============================================================================

CrowdManager::CrowdManager(NavigationMesh& navMesh)
    : navMesh_(navMesh) {
}

CrowdManager::~CrowdManager() {
    shutdown();
}

bool CrowdManager::initialize(int maxAgents) {
    if (!navMesh_.isValid()) return false;
    
    maxAgents_ = maxAgents;
    
    // crowd_ = dtAllocCrowd();
    // 
    // if (!crowd_->init(maxAgents, navMesh_.getSettings().agentRadius, navMesh_.getNavMesh())) {
    //     dtFreeCrowd(crowd_);
    //     crowd_ = nullptr;
    //     return false;
    // }
    // 
    // // Setup obstacle avoidance params
    // dtObstacleAvoidanceParams params;
    // memcpy(&params, crowd_->getObstacleAvoidanceParams(0), sizeof(dtObstacleAvoidanceParams));
    // 
    // // Low quality
    // params.velBias = 0.5f;
    // params.adaptiveDivs = 5;
    // params.adaptiveRings = 2;
    // params.adaptiveDepth = 1;
    // crowd_->setObstacleAvoidanceParams(0, &params);
    // 
    // // Medium quality
    // params.velBias = 0.5f;
    // params.adaptiveDivs = 5;
    // params.adaptiveRings = 2;
    // params.adaptiveDepth = 2;
    // crowd_->setObstacleAvoidanceParams(1, &params);
    // 
    // // High quality
    // params.velBias = 0.5f;
    // params.adaptiveDivs = 7;
    // params.adaptiveRings = 2;
    // params.adaptiveDepth = 3;
    // crowd_->setObstacleAvoidanceParams(2, &params);
    // 
    // // Ultra quality
    // params.velBias = 0.5f;
    // params.adaptiveDivs = 7;
    // params.adaptiveRings = 3;
    // params.adaptiveDepth = 3;
    // crowd_->setObstacleAvoidanceParams(3, &params);
    
    return true;
}

void CrowdManager::shutdown() {
    // if (crowd_) {
    //     dtFreeCrowd(crowd_);
    //     crowd_ = nullptr;
    // }
}

int CrowdManager::addAgent(const glm::vec3& position, const CrowdAgentParams& params) {
    // if (!crowd_) return -1;
    // 
    // dtCrowdAgentParams ap;
    // memset(&ap, 0, sizeof(ap));
    // 
    // ap.radius = params.radius;
    // ap.height = params.height;
    // ap.maxAcceleration = params.maxAcceleration;
    // ap.maxSpeed = params.maxSpeed;
    // ap.collisionQueryRange = params.collisionQueryRange;
    // ap.pathOptimizationRange = params.pathOptimizationRange;
    // ap.separationWeight = params.separationWeight;
    // ap.obstacleAvoidanceType = params.obstacleAvoidanceType;
    // 
    // ap.updateFlags = 0;
    // if (params.anticipateTurns) ap.updateFlags |= DT_CROWD_ANTICIPATE_TURNS;
    // if (params.optimizeVisibility) ap.updateFlags |= DT_CROWD_OPTIMIZE_VIS;
    // if (params.optimizeTopology) ap.updateFlags |= DT_CROWD_OPTIMIZE_TOPO;
    // if (params.obstacleAvoidance) ap.updateFlags |= DT_CROWD_OBSTACLE_AVOIDANCE;
    // if (params.separation) ap.updateFlags |= DT_CROWD_SEPARATION;
    // 
    // return crowd_->addAgent(&position.x, &ap);
    
    return 0;
}

void CrowdManager::removeAgent(int agentId) {
    // if (crowd_) {
    //     crowd_->removeAgent(agentId);
    // }
}

bool CrowdManager::setAgentTarget(int agentId, const glm::vec3& target) {
    // if (!crowd_) return false;
    // 
    // dtNavMeshQuery* query = crowd_->getNavMeshQuery();
    // const dtQueryFilter* filter = crowd_->getFilter(0);
    // 
    // dtPolyRef targetPoly;
    // float nearestPt[3];
    // float extents[3] = { 2, 4, 2 };
    // 
    // query->findNearestPoly(&target.x, extents, filter, &targetPoly, nearestPt);
    // 
    // if (targetPoly) {
    //     crowd_->requestMoveTarget(agentId, targetPoly, nearestPt);
    //     return true;
    // }
    
    return false;
}

void CrowdManager::clearAgentTarget(int agentId) {
    // if (crowd_) {
    //     crowd_->resetMoveTarget(agentId);
    // }
}

void CrowdManager::setAgentVelocity(int agentId, const glm::vec3& velocity) {
    // if (crowd_) {
    //     crowd_->requestMoveVelocity(agentId, &velocity.x);
    // }
}

void CrowdManager::setAgentParams(int agentId, const CrowdAgentParams& params) {
    // Similar to addAgent but for updating
}

CrowdAgentState CrowdManager::getAgentState(int agentId) const {
    CrowdAgentState state;
    
    // if (!crowd_) return state;
    // 
    // const dtCrowdAgent* agent = crowd_->getAgent(agentId);
    // if (!agent || !agent->active) return state;
    // 
    // state.active = true;
    // state.position = glm::vec3(agent->npos[0], agent->npos[1], agent->npos[2]);
    // state.velocity = glm::vec3(agent->vel[0], agent->vel[1], agent->vel[2]);
    // state.desiredVelocity = glm::vec3(agent->dvel[0], agent->dvel[1], agent->dvel[2]);
    // state.reachedTarget = agent->targetState == DT_CROWDAGENT_TARGET_VALID &&
    //                       glm::length(state.velocity) < 0.01f;
    
    return state;
}

void CrowdManager::update(float deltaTime) {
    // if (crowd_) {
    //     crowd_->update(deltaTime, nullptr);
    // }
}

int CrowdManager::getActiveAgentCount() const {
    // return crowd_ ? crowd_->getAgentCount() : 0;
    return 0;
}

// ============================================================================
// NAVIGATION SYSTEM
// ============================================================================

NavigationSystem::NavigationSystem() {
    requireComponent<NavigationComponent>();
    requireComponent<Transform>();
}

void NavigationSystem::init(World& world) {
    // Initialize if we have a NavMesh
    if (navMesh_ && navMesh_->isValid()) {
        query_ = std::make_unique<NavigationQuery>(*navMesh_);
        crowd_ = std::make_unique<CrowdManager>(*navMesh_);
        crowd_->initialize(128);
    }
}

void NavigationSystem::update(World& world, float deltaTime) {
    processPathRequests(world);
    updatePathFollowing(world, deltaTime);
    
    if (crowd_) {
        crowd_->update(deltaTime);
        updateCrowdAgents(world, deltaTime);
    }
}

void NavigationSystem::shutdown(World& world) {
    query_.reset();
    crowd_.reset();
}

void NavigationSystem::setNavMesh(std::shared_ptr<NavigationMesh> navMesh) {
    navMesh_ = navMesh;
    
    if (navMesh_ && navMesh_->isValid()) {
        query_ = std::make_unique<NavigationQuery>(*navMesh_);
        crowd_ = std::make_unique<CrowdManager>(*navMesh_);
        crowd_->initialize(128);
    }
}

void NavigationSystem::requestPath(Entity entity, const glm::vec3& target) {
    pendingRequests_.push({ entity, target });
}

void NavigationSystem::stopNavigation(Entity entity) {
    // Would clear path and stop movement
}

bool NavigationSystem::hasReachedDestination(Entity entity) const {
    // Would check entity's NavigationComponent
    return false;
}

void NavigationSystem::processPathRequests(World& world) {
    if (!query_) return;
    
    while (!pendingRequests_.empty()) {
        auto request = pendingRequests_.front();
        pendingRequests_.pop();
        
        auto* nav = world.tryGetComponent<NavigationComponent>(request.entity);
        auto* transform = world.tryGetComponent<Transform>(request.entity);
        
        if (!nav || !transform) continue;
        
        NavQueryFilter filter;
        if (nav->filter) {
            filter = *nav->filter;
        }
        
        PathResult result = query_->findPath(transform->position, request.target, filter);
        
        if (result.success) {
            nav->path = result.path;
            nav->currentWaypoint = 0;
            nav->targetPosition = request.target;
            nav->hasTarget = true;
            nav->isMoving = true;
            nav->reachedDestination = false;
            nav->pathPending = false;
        }
    }
}

void NavigationSystem::updatePathFollowing(World& world, float deltaTime) {
    for (auto& [entity, transform, nav] : world.query<Transform, NavigationComponent>()) {
        // Skip if using crowd navigation
        if (nav.crowdAgentId >= 0) continue;
        
        if (!nav.hasTarget || nav.path.empty()) continue;
        
        // Get current waypoint
        if (nav.currentWaypoint >= static_cast<int>(nav.path.size())) {
            nav.reachedDestination = true;
            nav.isMoving = false;
            continue;
        }
        
        glm::vec3 target = nav.path[nav.currentWaypoint];
        glm::vec3 toTarget = target - transform.position;
        toTarget.y = 0;  // Ignore height for 2D navigation
        
        float distance = glm::length(toTarget);
        
        if (distance < nav.arrivalDistance) {
            // Reached waypoint
            nav.currentWaypoint++;
            
            if (nav.currentWaypoint >= static_cast<int>(nav.path.size())) {
                nav.reachedDestination = true;
                nav.isMoving = false;
            }
            continue;
        }
        
        // Move toward waypoint
        glm::vec3 direction = glm::normalize(toTarget);
        float moveDistance = nav.moveSpeed * deltaTime;
        moveDistance = std::min(moveDistance, distance);
        
        transform.position += direction * moveDistance;
        
        // Rotate to face movement direction
        if (distance > 0.01f) {
            float targetYaw = std::atan2(-direction.x, -direction.z);
            float currentYaw = glm::eulerAngles(transform.rotation).y;
            
            float angleDiff = targetYaw - currentYaw;
            
            // Normalize angle
            while (angleDiff > glm::pi<float>()) angleDiff -= 2.0f * glm::pi<float>();
            while (angleDiff < -glm::pi<float>()) angleDiff += 2.0f * glm::pi<float>();
            
            float maxTurn = glm::radians(nav.turnSpeed) * deltaTime;
            float turn = glm::clamp(angleDiff, -maxTurn, maxTurn);
            
            transform.rotation = glm::angleAxis(currentYaw + turn, glm::vec3(0, 1, 0));
        }
        
        nav.isMoving = true;
    }
}

void NavigationSystem::updateCrowdAgents(World& world, float deltaTime) {
    if (!crowd_) return;
    
    for (auto& [entity, transform, nav] : world.query<Transform, NavigationComponent>()) {
        if (nav.crowdAgentId < 0) continue;
        
        CrowdAgentState state = crowd_->getAgentState(nav.crowdAgentId);
        
        if (state.active) {
            transform.position = state.position;
            
            // Face velocity direction
            if (glm::length(state.velocity) > 0.01f) {
                glm::vec3 dir = glm::normalize(state.velocity);
                float yaw = std::atan2(-dir.x, -dir.z);
                transform.rotation = glm::angleAxis(yaw, glm::vec3(0, 1, 0));
            }
            
            nav.isMoving = glm::length(state.velocity) > 0.1f;
            nav.reachedDestination = state.reachedTarget;
        }
    }
}

bool NavigationSystem::buildNavMeshFromWorld(World& world, const NavMeshSettings& settings) {
    NavMeshInputGeometry geometry;
    
    // Collect geometry from world
    // Would iterate through static meshes and add their triangles
    
    // For now, just create a simple ground plane
    float size = 100.0f;
    geometry.addTriangle(
        glm::vec3(-size, 0, -size),
        glm::vec3(size, 0, -size),
        glm::vec3(-size, 0, size)
    );
    geometry.addTriangle(
        glm::vec3(size, 0, -size),
        glm::vec3(size, 0, size),
        glm::vec3(-size, 0, size)
    );
    
    navMesh_ = std::make_shared<NavigationMesh>();
    return navMesh_->build(geometry, settings);
}

} // namespace Sanic
