/**
 * PhysicsMovementSystem.h
 * 
 * ECS integration for the physics and movement systems.
 * Ties together:
 * - KineticCharacterController
 * - SplineComponent / SplineMovementComponent
 * - GravitySystem
 * - AbilitySystem
 * - DestructionSystem
 * 
 * This system processes entities with physics/movement components
 * and coordinates between the various subsystems.
 */

#pragma once

#include "ECS.h"
#include "KineticCharacterController.h"
#include "SplineComponent.h"
#include "SplineMovement.h"
#include "GravitySystem.h"
#include "AbilitySystem.h"
#include "DestructionSystem.h"
#include "AsyncPhysics.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <unordered_map>

namespace Sanic {

// ============================================================================
// Physics/Movement Components for ECS
// ============================================================================

/**
 * Component for kinetic character controller
 */
struct KineticControllerComponent {
    std::unique_ptr<KineticCharacterController> controller;
    CharacterConfig config;
    bool isInitialized = false;
    
    // Input state (set by gameplay systems)
    CharacterInput input;
};

/**
 * Component for spline entities
 */
struct SplineEntityComponent {
    std::unique_ptr<SplineComponent> spline;
    bool isDirty = true;  // Needs distance table rebuild
    
    // Tags for gameplay (rail, zipline, camera, etc.)
    std::string splineType = "generic";
};

/**
 * Component for entities that can move along splines
 */
struct SplineMovementEntityComponent {
    std::unique_ptr<SplineMovementComponent> movement;
    Entity lockedSplineEntity = INVALID_ENTITY;
};

/**
 * Component for gravity volume entities
 */
struct GravityVolumeComponent {
    uint32_t volumeId = 0;
    GravityVolumeType type = GravityVolumeType::Directional;
    GravityVolumeShape shape = GravityVolumeShape::Box;
    float strength = 9.81f;
    glm::vec3 direction = glm::vec3(0.0f, -1.0f, 0.0f);
    glm::vec3 halfExtents = glm::vec3(5.0f);
    float radius = 5.0f;
    float blendRadius = 2.0f;
    int priority = 0;
    Entity splineEntity = INVALID_ENTITY;  // For spline-based gravity
};

/**
 * Component for entities with abilities
 */
struct AbilityOwnerComponent {
    std::unique_ptr<AbilityComponent> abilities;
    float resource = 100.0f;      // Energy/stamina pool
    float maxResource = 100.0f;
    float resourceRegen = 10.0f;  // Per second
};

/**
 * Component for destructible entities
 */
struct DestructibleComponent {
    uint32_t fractureDataId = 0;
    uint32_t instanceId = 0;
    bool isIntact = true;
    float breakForceThreshold = 1000.0f;
};

/**
 * Component for spline-generated meshes
 */
struct SplineMeshComponent {
    Entity splineEntity = INVALID_ENTITY;
    float tileLength = 1.0f;
    glm::vec2 scale = glm::vec2(1.0f);
    bool needsUpdate = true;
    std::vector<glm::mat4> instanceTransforms;
};

// ============================================================================
// Physics Movement System
// ============================================================================

/**
 * Main system that coordinates physics and movement
 */
class PhysicsMovementSystem : public System {
public:
    PhysicsMovementSystem();
    ~PhysicsMovementSystem();
    
    // System lifecycle
    void init(World& world) override;
    void update(World& world, float deltaTime) override;
    void fixedUpdate(World& world, float fixedDeltaTime) override;
    void lateUpdate(World& world, float deltaTime) override;
    void shutdown(World& world) override;
    
    // Subsystem access
    GravitySystem* getGravitySystem() { return gravitySystem_.get(); }
    DestructionSystem* getDestructionSystem() { return destructionSystem_.get(); }
    AsyncPhysics* getPhysicsWorld() { return physicsWorld_; }
    
    // Entity helpers
    Entity findNearestSpline(World& world, const glm::vec3& position, 
                              float maxDistance, const std::string& splineType = "");
    
    // Character controller helpers
    void applyImpulse(World& world, Entity entity, const glm::vec3& impulse);
    void lockToSpline(World& world, Entity characterEntity, Entity splineEntity, 
                      SplineLockMode mode, float startDistance = 0.0f);
    void unlockFromSpline(World& world, Entity entity);
    
    // Ability helpers
    bool activateAbility(World& world, Entity entity, uint32_t abilityId);
    uint32_t grantAbility(World& world, Entity entity, AbilityType type);
    
    // Destruction helpers
    bool applyHighSpeedDamage(World& world, Entity destructibleEntity, 
                               const glm::vec3& characterPos, 
                               const glm::vec3& characterVelocity);
    
private:
    // Initialization
    void initializePhysicsWorld();
    void initializeGravitySystem();
    void initializeDestructionSystem();
    
    // Per-frame updates
    void updateKineticControllers(World& world, float deltaTime);
    void updateSplineMovement(World& world, float deltaTime);
    void updateGravityVolumes(World& world);
    void updateAbilities(World& world, float deltaTime);
    void processDestructibles(World& world);
    void updateSplineMeshes(World& world);
    
    // Component creation helpers
    void ensureControllerInitialized(World& world, Entity entity, 
                                      KineticControllerComponent& comp);
    void ensureSplineInitialized(World& world, Entity entity,
                                  SplineEntityComponent& comp);
    void ensureGravityVolumeInitialized(World& world, Entity entity,
                                         GravityVolumeComponent& comp);
    
    // Subsystems
    std::unique_ptr<GravitySystem> gravitySystem_;
    std::unique_ptr<DestructionSystem> destructionSystem_;
    AsyncPhysics* physicsWorld_ = nullptr;
    
    // Cache for quick lookups
    std::unordered_map<Entity, uint32_t> entityToGravityVolume_;
    
    // Player tracking for debris distance calculations
    Entity playerEntity_ = INVALID_ENTITY;
    glm::vec3 playerPosition_ = glm::vec3(0.0f);
    
    bool initialized_ = false;
};

// ============================================================================
// Spline System (Manages spline-specific updates)
// ============================================================================

class SplineSystem : public System {
public:
    SplineSystem() {
        setPriority(-10);  // Run before physics
    }
    
    void init(World& world) override;
    void update(World& world, float deltaTime) override;
    
    // Get spline from entity
    SplineComponent* getSpline(World& world, Entity entity);
    
    // Find splines
    std::vector<Entity> findSplinesInRadius(World& world, const glm::vec3& center, 
                                             float radius, const std::string& type = "");
    Entity findClosestSpline(World& world, const glm::vec3& position,
                              const std::string& type = "", float maxDistance = 100.0f);
    
private:
    void rebuildDirtySplines(World& world);
};

// ============================================================================
// Gravity Volume System (Updates gravity volumes from components)
// ============================================================================

class GravityVolumeSystem : public System {
public:
    GravityVolumeSystem() {
        setPriority(-5);  // Run before main physics
    }
    
    void init(World& world) override;
    void update(World& world, float deltaTime) override;
    void shutdown(World& world) override;
    
    // Access to underlying gravity system
    GravitySystem* getGravitySystem() { return gravitySystem_; }
    void setGravitySystem(GravitySystem* system) { gravitySystem_ = system; }
    
    // Query gravity
    glm::vec3 getGravityAtPosition(const glm::vec3& position);
    glm::vec3 getUpAtPosition(const glm::vec3& position);
    
private:
    void syncVolumesFromComponents(World& world);
    
    GravitySystem* gravitySystem_ = nullptr;
    std::unordered_map<Entity, uint32_t> entityToVolumeId_;
};

// ============================================================================
// Ability System (Processes ability components)
// ============================================================================

class AbilitySystemECS : public System {
public:
    AbilitySystemECS() {
        setPriority(10);  // Run after physics
    }
    
    void init(World& world) override;
    void update(World& world, float deltaTime) override;
    
    // Ability management
    uint32_t grantAbility(World& world, Entity entity, AbilityType type);
    void revokeAbility(World& world, Entity entity, uint32_t abilityId);
    bool activateAbility(World& world, Entity entity, uint32_t abilityId);
    
    // Resource management
    void consumeResource(World& world, Entity entity, float amount);
    void regenerateResource(World& world, Entity entity, float deltaTime);
    
private:
    void updateAbilityComponents(World& world, float deltaTime);
    void processAbilityEvents(World& world);
};

// ============================================================================
// Destruction System ECS (Processes destructible components)
// ============================================================================

class DestructionSystemECS : public System {
public:
    DestructionSystemECS() {
        setPriority(20);  // Run after abilities
    }
    
    void init(World& world) override;
    void update(World& world, float deltaTime) override;
    void shutdown(World& world) override;
    
    void setDestructionSystem(DestructionSystem* system) { destructionSystem_ = system; }
    
    // Damage application
    bool applyDamage(World& world, Entity entity, const glm::vec3& point,
                     const glm::vec3& direction, float magnitude);
    bool applyHighSpeedDamage(World& world, Entity entity,
                               const glm::vec3& characterPos,
                               const glm::vec3& characterVelocity);
    void applyExplosion(World& world, const glm::vec3& center, float radius, float force);
    
    // Player tracking for debris
    void setPlayerPosition(const glm::vec3& position);
    
private:
    void processDestructibles(World& world);
    void handleDestructionEvents(World& world);
    
    DestructionSystem* destructionSystem_ = nullptr;
};

// ============================================================================
// Helper: Register all physics/movement systems
// ============================================================================

/**
 * Register all physics and movement related systems with a world
 * Returns the main PhysicsMovementSystem for further configuration
 */
inline PhysicsMovementSystem& registerPhysicsMovementSystems(World& world) {
    world.registerSystem<SplineSystem>();
    world.registerSystem<GravityVolumeSystem>();
    auto& physicsSystem = world.registerSystem<PhysicsMovementSystem>();
    world.registerSystem<AbilitySystemECS>();
    world.registerSystem<DestructionSystemECS>();
    
    // Link subsystems
    auto* gravityVolumeSystem = world.getSystem<GravityVolumeSystem>();
    auto* destructionSystemECS = world.getSystem<DestructionSystemECS>();
    
    if (gravityVolumeSystem) {
        gravityVolumeSystem->setGravitySystem(physicsSystem.getGravitySystem());
    }
    if (destructionSystemECS) {
        destructionSystemECS->setDestructionSystem(physicsSystem.getDestructionSystem());
    }
    
    return physicsSystem;
}

} // namespace Sanic
