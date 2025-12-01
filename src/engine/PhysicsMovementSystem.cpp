/**
 * PhysicsMovementSystem.cpp
 * 
 * Implementation of ECS integration for physics and movement systems.
 */

#include "PhysicsMovementSystem.h"
#include <algorithm>
#include <cmath>

namespace Sanic {

// ============================================================================
// PhysicsMovementSystem Implementation
// ============================================================================

PhysicsMovementSystem::PhysicsMovementSystem() {
    setPriority(0);  // Main physics runs at priority 0
    
    // Require Transform for all physics entities
    requireComponent<Transform>();
}

PhysicsMovementSystem::~PhysicsMovementSystem() {
    // Cleanup handled in shutdown
}

void PhysicsMovementSystem::init(World& world) {
    initializePhysicsWorld();
    initializeGravitySystem();
    initializeDestructionSystem();
    
    // Initialize scripting bindings with our systems
    extern void ScriptingBindings_Initialize(World*, GravitySystem*, DestructionSystem*);
    ScriptingBindings_Initialize(&world, gravitySystem_.get(), destructionSystem_.get());
    
    initialized_ = true;
}

void PhysicsMovementSystem::update(World& world, float deltaTime) {
    if (!initialized_) return;
    
    // Update gravity volumes from component transforms
    updateGravityVolumes(world);
    
    // Update abilities (cooldowns, active effects)
    updateAbilities(world, deltaTime);
    
    // Update spline meshes if dirty
    updateSplineMeshes(world);
}

void PhysicsMovementSystem::fixedUpdate(World& world, float fixedDeltaTime) {
    if (!initialized_) return;
    
    // Update kinetic character controllers
    updateKineticControllers(world, fixedDeltaTime);
    
    // Update spline movement
    updateSplineMovement(world, fixedDeltaTime);
    
    // Process destructibles
    processDestructibles(world);
    
    // Update destruction system debris
    if (destructionSystem_) {
        destructionSystem_->setPlayerPosition(playerPosition_);
        destructionSystem_->update(fixedDeltaTime);
    }
}

void PhysicsMovementSystem::lateUpdate(World& world, float deltaTime) {
    // Any post-physics updates (camera follow, etc.)
}

void PhysicsMovementSystem::shutdown(World& world) {
    if (!initialized_) return;
    
    extern void ScriptingBindings_Shutdown();
    ScriptingBindings_Shutdown();
    
    if (destructionSystem_) {
        destructionSystem_->shutdown();
    }
    
    gravitySystem_.reset();
    destructionSystem_.reset();
    
    entityToGravityVolume_.clear();
    initialized_ = false;
}

void PhysicsMovementSystem::initializePhysicsWorld() {
    // Physics world should be passed in or created by the engine
    // For now, we assume it's set externally
}

void PhysicsMovementSystem::initializeGravitySystem() {
    gravitySystem_ = std::make_unique<GravitySystem>();
    
    // Set default world gravity
    gravitySystem_->setDefaultGravity(glm::vec3(0.0f, -9.81f, 0.0f));
}

void PhysicsMovementSystem::initializeDestructionSystem() {
    destructionSystem_ = std::make_unique<DestructionSystem>();
    destructionSystem_->initialize(physicsWorld_, nullptr);
    
    // Configure for high-speed gameplay
    HighSpeedCollisionSettings highSpeedSettings;
    highSpeedSettings.minVelocityToBreak = 22.35f;  // ~50 mph
    highSpeedSettings.velocityToForceMultiplier = 20.0f;
    highSpeedSettings.impactRadius = 2.0f;
    highSpeedSettings.characterMass = 80.0f;
    highSpeedSettings.applyImpulseToDebris = true;
    destructionSystem_->setHighSpeedSettings(highSpeedSettings);
    
    // Debris settings for performance
    DebrisSettings debrisSettings;
    debrisSettings.lifetime = 10.0f;
    debrisSettings.despawnDistance = 100.0f;
    debrisSettings.maxActiveDebris = 256;
    debrisSettings.freezeDistantDebris = true;
    destructionSystem_->setDebrisSettings(debrisSettings);
}

void PhysicsMovementSystem::updateKineticControllers(World& world, float deltaTime) {
    // Query all entities with kinetic controllers
    for (auto& [entity, transform, controller] : 
         world.query<Transform, KineticControllerComponent>()) {
        
        if (!controller.isInitialized) {
            ensureControllerInitialized(world, entity, controller);
        }
        
        if (!controller.controller) continue;
        
        // Query gravity at current position
        GravityQueryResult gravityResult = gravitySystem_->getGravityAtPosition(transform.position);
        controller.controller->setLocalGravity(gravityResult.gravity);
        
        // Update the controller
        controller.controller->update(controller.input, deltaTime);
        
        // Sync transform from controller
        const CharacterState& state = controller.controller->getState();
        transform.position = state.position;
        transform.rotation = state.rotation;
        
        // Track player for debris distance
        if (world.hasComponent<Name>(entity)) {
            const Name& name = world.getComponent<Name>(entity);
            if (name.tag == "Player") {
                playerEntity_ = entity;
                playerPosition_ = transform.position;
            }
        }
    }
}

void PhysicsMovementSystem::updateSplineMovement(World& world, float deltaTime) {
    for (auto& [entity, transform, movement] : 
         world.query<Transform, SplineMovementEntityComponent>()) {
        
        if (!movement.movement) continue;
        if (movement.lockedSplineEntity == INVALID_ENTITY) continue;
        
        // Get input from kinetic controller if present
        glm::vec3 inputDir = glm::vec3(0.0f);
        float inputSpeed = 0.0f;
        
        if (world.hasComponent<KineticControllerComponent>(entity)) {
            auto& controller = world.getComponent<KineticControllerComponent>(entity);
            inputDir = controller.input.moveDirection;
            inputSpeed = controller.input.wantsSprint ? 1.0f : 0.5f;
        }
        
        // Update movement
        movement.movement->update(deltaTime, inputDir, inputSpeed);
        
        // Sync position from spline movement
        if (movement.movement->getLockMode() != SplineLockMode::None) {
            transform.position = movement.movement->getCurrentPosition();
            transform.rotation = movement.movement->getCurrentRotation();
        }
    }
}

void PhysicsMovementSystem::updateGravityVolumes(World& world) {
    for (auto& [entity, transform, volume] : 
         world.query<Transform, GravityVolumeComponent>()) {
        
        ensureGravityVolumeInitialized(world, entity, volume);
        
        if (volume.volumeId == 0) continue;
        
        // Update volume position from transform
        GravityVolume* vol = gravitySystem_->getVolume(volume.volumeId);
        if (vol) {
            vol->center = transform.position;
            
            // Update spline reference if needed
            if (volume.type == GravityVolumeType::SplineBased && 
                volume.splineEntity != INVALID_ENTITY) {
                if (world.hasComponent<SplineEntityComponent>(volume.splineEntity)) {
                    auto& splineComp = world.getComponent<SplineEntityComponent>(volume.splineEntity);
                    vol->spline = splineComp.spline.get();
                }
            }
        }
    }
}

void PhysicsMovementSystem::updateAbilities(World& world, float deltaTime) {
    for (auto& [entity, abilities] : world.query<AbilityOwnerComponent>()) {
        if (!abilities.abilities) continue;
        
        // Regenerate resource
        if (abilities.resource < abilities.maxResource) {
            abilities.resource = std::min(
                abilities.maxResource,
                abilities.resource + abilities.resourceRegen * deltaTime
            );
        }
        
        // Create context for ability updates
        AbilityContext context;
        context.deltaTime = deltaTime;
        context.ownerEntity = entity;
        
        if (world.hasComponent<Transform>(entity)) {
            context.position = world.getComponent<Transform>(entity).position;
        }
        if (world.hasComponent<KineticControllerComponent>(entity)) {
            auto& controller = world.getComponent<KineticControllerComponent>(entity);
            if (controller.controller) {
                context.velocity = controller.controller->getVelocity();
                context.isGrounded = controller.controller->isGrounded();
            }
        }
        
        abilities.abilities->update(context);
    }
}

void PhysicsMovementSystem::processDestructibles(World& world) {
    if (!destructionSystem_) return;
    
    // Check for high-speed collisions with player
    if (playerEntity_ == INVALID_ENTITY) return;
    
    auto* playerController = world.tryGetComponent<KineticControllerComponent>(playerEntity_);
    if (!playerController || !playerController->controller) return;
    
    glm::vec3 playerVel = playerController->controller->getVelocity();
    float playerSpeed = glm::length(playerVel);
    
    // Only check if moving fast enough
    if (playerSpeed < destructionSystem_->getHighSpeedSettings().minVelocityToBreak) {
        return;
    }
    
    // Find nearby destructibles
    float checkRadius = 3.0f;  // Detection radius
    
    for (auto& [entity, transform, destructible] : 
         world.query<Transform, DestructibleComponent>()) {
        
        if (!destructible.isIntact) continue;
        
        float dist = glm::length(transform.position - playerPosition_);
        if (dist > checkRadius) continue;
        
        // Apply high-speed damage
        if (destructionSystem_->applyHighSpeedCollision(
            destructible.instanceId, playerPosition_, playerVel)) {
            
            destructible.isIntact = destructionSystem_->isObjectIntact(destructible.instanceId);
            
            // Emit destruction event
            Event event;
            event.name = "Destruction";
            event.sender = playerEntity_;
            event.target = entity;
            event.data = playerVel;
            world.getEventBus().emit(event);
        }
    }
}

void PhysicsMovementSystem::updateSplineMeshes(World& world) {
    for (auto& [entity, splineMesh] : world.query<SplineMeshComponent>()) {
        if (!splineMesh.needsUpdate) continue;
        if (splineMesh.splineEntity == INVALID_ENTITY) continue;
        
        auto* splineComp = world.tryGetComponent<SplineEntityComponent>(splineMesh.splineEntity);
        if (!splineComp || !splineComp->spline) continue;
        
        // Generate instance transforms
        SplineMeshSettings settings;
        settings.tileLength = splineMesh.tileLength;
        settings.scale = splineMesh.scale;
        
        SplineMeshGenerator generator;
        splineMesh.instanceTransforms = generator.generateInstanceTransforms(
            *splineComp->spline, settings);
        
        splineMesh.needsUpdate = false;
    }
}

void PhysicsMovementSystem::ensureControllerInitialized(
    World& world, Entity entity, KineticControllerComponent& comp) {
    
    if (comp.isInitialized) return;
    
    comp.controller = std::make_unique<KineticCharacterController>();
    
    // Get initial position from transform
    if (world.hasComponent<Transform>(entity)) {
        const Transform& transform = world.getComponent<Transform>(entity);
        comp.controller->setPosition(transform.position);
        comp.controller->setRotation(transform.rotation);
    }
    
    // Apply config
    comp.controller->setConfig(comp.config);
    
    // Set gravity system reference
    comp.controller->setGravitySystem(gravitySystem_.get());
    
    comp.isInitialized = true;
}

void PhysicsMovementSystem::ensureSplineInitialized(
    World& world, Entity entity, SplineEntityComponent& comp) {
    
    if (!comp.spline) {
        comp.spline = std::make_unique<SplineComponent>();
    }
    
    if (comp.isDirty) {
        comp.spline->rebuildDistanceTable();
        comp.isDirty = false;
    }
}

void PhysicsMovementSystem::ensureGravityVolumeInitialized(
    World& world, Entity entity, GravityVolumeComponent& comp) {
    
    if (comp.volumeId != 0) return;
    
    // Create volume in gravity system
    comp.volumeId = gravitySystem_->createVolume(comp.type);
    
    GravityVolume* vol = gravitySystem_->getVolume(comp.volumeId);
    if (vol) {
        vol->shape = comp.shape;
        vol->strength = comp.strength;
        vol->direction = comp.direction;
        vol->halfExtents = comp.halfExtents;
        vol->radius = comp.radius;
        vol->blendRadius = comp.blendRadius;
        vol->priority = comp.priority;
        
        if (world.hasComponent<Transform>(entity)) {
            vol->center = world.getComponent<Transform>(entity).position;
        }
    }
    
    entityToGravityVolume_[entity] = comp.volumeId;
}

Entity PhysicsMovementSystem::findNearestSpline(
    World& world, const glm::vec3& position, 
    float maxDistance, const std::string& splineType) {
    
    Entity nearestEntity = INVALID_ENTITY;
    float nearestDist = maxDistance;
    
    for (auto& [entity, transform, splineComp] : 
         world.query<Transform, SplineEntityComponent>()) {
        
        if (!splineComp.spline) continue;
        if (!splineType.empty() && splineComp.splineType != splineType) continue;
        
        float param = splineComp.spline->findClosestParameter(position);
        glm::vec3 closestPoint = splineComp.spline->evaluatePosition(param);
        float dist = glm::length(closestPoint - position);
        
        if (dist < nearestDist) {
            nearestDist = dist;
            nearestEntity = entity;
        }
    }
    
    return nearestEntity;
}

void PhysicsMovementSystem::applyImpulse(World& world, Entity entity, const glm::vec3& impulse) {
    auto* controller = world.tryGetComponent<KineticControllerComponent>(entity);
    if (controller && controller->controller) {
        controller->controller->applyImpulse(impulse);
    }
}

void PhysicsMovementSystem::lockToSpline(
    World& world, Entity characterEntity, Entity splineEntity,
    SplineLockMode mode, float startDistance) {
    
    auto* movement = world.tryGetComponent<SplineMovementEntityComponent>(characterEntity);
    auto* splineComp = world.tryGetComponent<SplineEntityComponent>(splineEntity);
    
    if (!movement) {
        // Add spline movement component if missing
        auto& newMovement = world.addComponent<SplineMovementEntityComponent>(characterEntity);
        newMovement.movement = std::make_unique<SplineMovementComponent>();
        movement = &newMovement;
    }
    
    if (splineComp && splineComp->spline && movement->movement) {
        movement->movement->lockToSpline(splineComp->spline.get(), mode, startDistance);
        movement->lockedSplineEntity = splineEntity;
    }
}

void PhysicsMovementSystem::unlockFromSpline(World& world, Entity entity) {
    auto* movement = world.tryGetComponent<SplineMovementEntityComponent>(entity);
    if (movement && movement->movement) {
        movement->movement->unlockFromSpline();
        movement->lockedSplineEntity = INVALID_ENTITY;
    }
}

bool PhysicsMovementSystem::activateAbility(World& world, Entity entity, uint32_t abilityId) {
    auto* abilities = world.tryGetComponent<AbilityOwnerComponent>(entity);
    if (abilities && abilities->abilities) {
        return abilities->abilities->activateAbility(abilityId);
    }
    return false;
}

uint32_t PhysicsMovementSystem::grantAbility(World& world, Entity entity, AbilityType type) {
    auto* abilities = world.tryGetComponent<AbilityOwnerComponent>(entity);
    if (!abilities) {
        auto& newAbilities = world.addComponent<AbilityOwnerComponent>(entity);
        newAbilities.abilities = std::make_unique<AbilityComponent>();
        abilities = &newAbilities;
    }
    
    if (abilities->abilities) {
        return abilities->abilities->grantAbility(type);
    }
    return 0;
}

bool PhysicsMovementSystem::applyHighSpeedDamage(
    World& world, Entity destructibleEntity,
    const glm::vec3& characterPos, const glm::vec3& characterVelocity) {
    
    auto* destructible = world.tryGetComponent<DestructibleComponent>(destructibleEntity);
    if (!destructible || !destructible->isIntact) return false;
    
    if (destructionSystem_) {
        bool broke = destructionSystem_->applyHighSpeedCollision(
            destructible->instanceId, characterPos, characterVelocity);
        
        if (broke) {
            destructible->isIntact = destructionSystem_->isObjectIntact(destructible->instanceId);
        }
        return broke;
    }
    return false;
}

// ============================================================================
// SplineSystem Implementation
// ============================================================================

void SplineSystem::init(World& world) {
    // Nothing special needed
}

void SplineSystem::update(World& world, float deltaTime) {
    rebuildDirtySplines(world);
}

SplineComponent* SplineSystem::getSpline(World& world, Entity entity) {
    auto* comp = world.tryGetComponent<SplineEntityComponent>(entity);
    return comp ? comp->spline.get() : nullptr;
}

std::vector<Entity> SplineSystem::findSplinesInRadius(
    World& world, const glm::vec3& center, float radius, const std::string& type) {
    
    std::vector<Entity> result;
    
    for (auto& [entity, transform, splineComp] : 
         world.query<Transform, SplineEntityComponent>()) {
        
        if (!splineComp.spline) continue;
        if (!type.empty() && splineComp.splineType != type) continue;
        
        float param = splineComp.spline->findClosestParameter(center);
        glm::vec3 closestPoint = splineComp.spline->evaluatePosition(param);
        float dist = glm::length(closestPoint - center);
        
        if (dist <= radius) {
            result.push_back(entity);
        }
    }
    
    return result;
}

Entity SplineSystem::findClosestSpline(
    World& world, const glm::vec3& position,
    const std::string& type, float maxDistance) {
    
    Entity closest = INVALID_ENTITY;
    float closestDist = maxDistance;
    
    for (auto& [entity, transform, splineComp] : 
         world.query<Transform, SplineEntityComponent>()) {
        
        if (!splineComp.spline) continue;
        if (!type.empty() && splineComp.splineType != type) continue;
        
        float param = splineComp.spline->findClosestParameter(position);
        glm::vec3 closestPoint = splineComp.spline->evaluatePosition(param);
        float dist = glm::length(closestPoint - position);
        
        if (dist < closestDist) {
            closestDist = dist;
            closest = entity;
        }
    }
    
    return closest;
}

void SplineSystem::rebuildDirtySplines(World& world) {
    for (auto& [entity, splineComp] : world.query<SplineEntityComponent>()) {
        if (splineComp.isDirty && splineComp.spline) {
            splineComp.spline->rebuildDistanceTable();
            splineComp.isDirty = false;
        }
    }
}

// ============================================================================
// GravityVolumeSystem Implementation
// ============================================================================

void GravityVolumeSystem::init(World& world) {
    // GravitySystem is set from PhysicsMovementSystem
}

void GravityVolumeSystem::update(World& world, float deltaTime) {
    syncVolumesFromComponents(world);
}

void GravityVolumeSystem::shutdown(World& world) {
    // Clean up volumes
    if (gravitySystem_) {
        for (auto& [entity, volumeId] : entityToVolumeId_) {
            gravitySystem_->removeVolume(volumeId);
        }
    }
    entityToVolumeId_.clear();
}

glm::vec3 GravityVolumeSystem::getGravityAtPosition(const glm::vec3& position) {
    if (gravitySystem_) {
        return gravitySystem_->getGravityAtPosition(position).gravity;
    }
    return glm::vec3(0.0f, -9.81f, 0.0f);
}

glm::vec3 GravityVolumeSystem::getUpAtPosition(const glm::vec3& position) {
    glm::vec3 gravity = getGravityAtPosition(position);
    float mag = glm::length(gravity);
    if (mag > 0.001f) {
        return -gravity / mag;
    }
    return glm::vec3(0.0f, 1.0f, 0.0f);
}

void GravityVolumeSystem::syncVolumesFromComponents(World& world) {
    if (!gravitySystem_) return;
    
    for (auto& [entity, transform, volumeComp] : 
         world.query<Transform, GravityVolumeComponent>()) {
        
        uint32_t volumeId = volumeComp.volumeId;
        if (volumeId == 0) continue;
        
        GravityVolume* vol = gravitySystem_->getVolume(volumeId);
        if (vol) {
            vol->center = transform.position;
        }
    }
}

// ============================================================================
// AbilitySystemECS Implementation
// ============================================================================

void AbilitySystemECS::init(World& world) {
    // Nothing special needed
}

void AbilitySystemECS::update(World& world, float deltaTime) {
    updateAbilityComponents(world, deltaTime);
    processAbilityEvents(world);
}

uint32_t AbilitySystemECS::grantAbility(World& world, Entity entity, AbilityType type) {
    auto* comp = world.tryGetComponent<AbilityOwnerComponent>(entity);
    if (!comp) {
        auto& newComp = world.addComponent<AbilityOwnerComponent>(entity);
        newComp.abilities = std::make_unique<AbilityComponent>();
        comp = &newComp;
    }
    
    if (comp->abilities) {
        return comp->abilities->grantAbility(type);
    }
    return 0;
}

void AbilitySystemECS::revokeAbility(World& world, Entity entity, uint32_t abilityId) {
    auto* comp = world.tryGetComponent<AbilityOwnerComponent>(entity);
    if (comp && comp->abilities) {
        comp->abilities->revokeAbility(abilityId);
    }
}

bool AbilitySystemECS::activateAbility(World& world, Entity entity, uint32_t abilityId) {
    auto* comp = world.tryGetComponent<AbilityOwnerComponent>(entity);
    if (comp && comp->abilities) {
        return comp->abilities->activateAbility(abilityId);
    }
    return false;
}

void AbilitySystemECS::consumeResource(World& world, Entity entity, float amount) {
    auto* comp = world.tryGetComponent<AbilityOwnerComponent>(entity);
    if (comp) {
        comp->resource = std::max(0.0f, comp->resource - amount);
    }
}

void AbilitySystemECS::regenerateResource(World& world, Entity entity, float deltaTime) {
    auto* comp = world.tryGetComponent<AbilityOwnerComponent>(entity);
    if (comp && comp->resource < comp->maxResource) {
        comp->resource = std::min(comp->maxResource, 
                                   comp->resource + comp->resourceRegen * deltaTime);
    }
}

void AbilitySystemECS::updateAbilityComponents(World& world, float deltaTime) {
    for (auto& [entity, comp] : world.query<AbilityOwnerComponent>()) {
        if (!comp.abilities) continue;
        
        // Build context
        AbilityContext context;
        context.deltaTime = deltaTime;
        context.ownerEntity = entity;
        context.resource = comp.resource;
        context.maxResource = comp.maxResource;
        
        if (world.hasComponent<Transform>(entity)) {
            context.position = world.getComponent<Transform>(entity).position;
        }
        
        // Update abilities
        comp.abilities->update(context);
        
        // Regenerate resource
        regenerateResource(world, entity, deltaTime);
    }
}

void AbilitySystemECS::processAbilityEvents(World& world) {
    // Handle ability activation events, damage, effects, etc.
}

// ============================================================================
// DestructionSystemECS Implementation
// ============================================================================

void DestructionSystemECS::init(World& world) {
    // DestructionSystem is set from PhysicsMovementSystem
}

void DestructionSystemECS::update(World& world, float deltaTime) {
    processDestructibles(world);
    handleDestructionEvents(world);
}

void DestructionSystemECS::shutdown(World& world) {
    // Cleanup handled by DestructionSystem
}

bool DestructionSystemECS::applyDamage(
    World& world, Entity entity, const glm::vec3& point,
    const glm::vec3& direction, float magnitude) {
    
    if (!destructionSystem_) return false;
    
    auto* comp = world.tryGetComponent<DestructibleComponent>(entity);
    if (!comp || !comp->isIntact) return false;
    
    bool broke = destructionSystem_->applyDamage(comp->instanceId, point, direction, magnitude);
    if (broke) {
        comp->isIntact = destructionSystem_->isObjectIntact(comp->instanceId);
    }
    return broke;
}

bool DestructionSystemECS::applyHighSpeedDamage(
    World& world, Entity entity,
    const glm::vec3& characterPos, const glm::vec3& characterVelocity) {
    
    if (!destructionSystem_) return false;
    
    auto* comp = world.tryGetComponent<DestructibleComponent>(entity);
    if (!comp || !comp->isIntact) return false;
    
    bool broke = destructionSystem_->applyHighSpeedCollision(
        comp->instanceId, characterPos, characterVelocity);
    if (broke) {
        comp->isIntact = destructionSystem_->isObjectIntact(comp->instanceId);
    }
    return broke;
}

void DestructionSystemECS::applyExplosion(
    World& world, const glm::vec3& center, float radius, float force) {
    
    if (destructionSystem_) {
        destructionSystem_->applyExplosion(center, radius, force);
    }
    
    // Update all affected destructibles
    for (auto& [entity, comp] : world.query<DestructibleComponent>()) {
        if (comp.isIntact) {
            comp.isIntact = destructionSystem_->isObjectIntact(comp.instanceId);
        }
    }
}

void DestructionSystemECS::setPlayerPosition(const glm::vec3& position) {
    if (destructionSystem_) {
        destructionSystem_->setPlayerPosition(position);
    }
}

void DestructionSystemECS::processDestructibles(World& world) {
    if (!destructionSystem_) return;
    
    // Sync state from destruction system
    for (auto& [entity, comp] : world.query<DestructibleComponent>()) {
        if (comp.isIntact) {
            comp.isIntact = destructionSystem_->isObjectIntact(comp.instanceId);
        }
    }
}

void DestructionSystemECS::handleDestructionEvents(World& world) {
    // Handle destruction callbacks, spawn effects, etc.
}

} // namespace Sanic
