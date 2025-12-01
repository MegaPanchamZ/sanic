/**
 * ScriptingBindings.cpp
 * 
 * Implementation of C API bindings for C# interop.
 * Bridges managed code to native physics and movement systems.
 */

#include "ScriptingBindings.h"
#include "KineticCharacterController.h"
#include "SplineComponent.h"
#include "SplineMovement.h"
#include "GravitySystem.h"
#include "AbilitySystem.h"
#include "DestructionSystem.h"
#include "SplineMeshGenerator.h"
#include "ECS.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <unordered_map>
#include <vector>

// Global system references (set during engine initialization)
namespace {
    Sanic::World* g_world = nullptr;
    GravitySystem* g_gravitySystem = nullptr;
    DestructionSystem* g_destructionSystem = nullptr;
    
    // Component cache for faster lookups
    std::unordered_map<uint32_t, KineticCharacterController*> g_controllerCache;
    std::unordered_map<uint32_t, SplineComponent*> g_splineCache;
    std::unordered_map<uint32_t, SplineMovementComponent*> g_splineMovementCache;
    std::unordered_map<uint32_t, AbilityComponent*> g_abilityCache;
    
    // Scratch buffer for returning transform data
    std::vector<float> g_transformBuffer;
}

// ============================================================================
// Engine Integration
// ============================================================================

// Called from engine initialization
extern "C" void ScriptingBindings_Initialize(Sanic::World* world, 
                                              GravitySystem* gravity,
                                              DestructionSystem* destruction) {
    g_world = world;
    g_gravitySystem = gravity;
    g_destructionSystem = destruction;
}

extern "C" void ScriptingBindings_Shutdown() {
    g_controllerCache.clear();
    g_splineCache.clear();
    g_splineMovementCache.clear();
    g_abilityCache.clear();
    g_transformBuffer.clear();
    g_world = nullptr;
    g_gravitySystem = nullptr;
    g_destructionSystem = nullptr;
}

// Helper to get/cache components
template<typename T, typename Cache>
T* getComponent(uint32_t entityId, Cache& cache) {
    auto it = cache.find(entityId);
    if (it != cache.end()) return it->second;
    
    if (!g_world) return nullptr;
    
    // Get from ECS (implementation depends on your ECS)
    // This is a placeholder - replace with actual component lookup
    T* component = nullptr;
    // component = g_world->getComponent<T>(entityId);
    
    if (component) {
        cache[entityId] = component;
    }
    return component;
}

// ============================================================================
// Kinetic Character Controller Implementation
// ============================================================================

SANIC_API void KineticController_SetGravityVector(uint32_t entityId, float x, float y, float z) {
    auto* controller = getComponent<KineticCharacterController>(entityId, g_controllerCache);
    if (controller) {
        controller->setLocalGravity(glm::vec3(x, y, z));
    }
}

SANIC_API void KineticController_GetGravityVector(uint32_t entityId, float* outX, float* outY, float* outZ) {
    auto* controller = getComponent<KineticCharacterController>(entityId, g_controllerCache);
    if (controller) {
        glm::vec3 gravity = controller->getLocalGravity();
        if (outX) *outX = gravity.x;
        if (outY) *outY = gravity.y;
        if (outZ) *outZ = gravity.z;
    }
}

SANIC_API void KineticController_SetSurfaceAdhesion(uint32_t entityId, float strength) {
    auto* controller = getComponent<KineticCharacterController>(entityId, g_controllerCache);
    if (controller) {
        CharacterConfig config = controller->getConfig();
        config.adhesionStrength = strength;
        controller->setConfig(config);
    }
}

SANIC_API float KineticController_GetSurfaceAdhesion(uint32_t entityId) {
    auto* controller = getComponent<KineticCharacterController>(entityId, g_controllerCache);
    if (controller) {
        return controller->getConfig().adhesionStrength;
    }
    return 0.0f;
}

SANIC_API void KineticController_ApplyImpulse(uint32_t entityId, float x, float y, float z) {
    auto* controller = getComponent<KineticCharacterController>(entityId, g_controllerCache);
    if (controller) {
        controller->applyImpulse(glm::vec3(x, y, z));
    }
}

SANIC_API void KineticController_ApplyForce(uint32_t entityId, float x, float y, float z, int forceMode) {
    auto* controller = getComponent<KineticCharacterController>(entityId, g_controllerCache);
    if (controller) {
        // ForceMode: 0=Force, 1=Impulse, 2=VelocityChange, 3=Acceleration
        glm::vec3 force(x, y, z);
        switch (forceMode) {
            case 0: // Force
                controller->applyForce(force);
                break;
            case 1: // Impulse
                controller->applyImpulse(force);
                break;
            case 2: // VelocityChange
                controller->applyImpulse(force);
                break;
            case 3: // Acceleration
                controller->applyForce(force * controller->getMass());
                break;
        }
    }
}

SANIC_API void KineticController_SetVelocity(uint32_t entityId, float x, float y, float z) {
    auto* controller = getComponent<KineticCharacterController>(entityId, g_controllerCache);
    if (controller) {
        controller->setVelocity(glm::vec3(x, y, z));
    }
}

SANIC_API void KineticController_GetVelocity(uint32_t entityId, float* outX, float* outY, float* outZ) {
    auto* controller = getComponent<KineticCharacterController>(entityId, g_controllerCache);
    if (controller) {
        glm::vec3 velocity = controller->getVelocity();
        if (outX) *outX = velocity.x;
        if (outY) *outY = velocity.y;
        if (outZ) *outZ = velocity.z;
    }
}

SANIC_API float KineticController_GetSpeed(uint32_t entityId) {
    auto* controller = getComponent<KineticCharacterController>(entityId, g_controllerCache);
    if (controller) {
        return glm::length(controller->getVelocity());
    }
    return 0.0f;
}

SANIC_API bool KineticController_IsGrounded(uint32_t entityId) {
    auto* controller = getComponent<KineticCharacterController>(entityId, g_controllerCache);
    if (controller) {
        return controller->isGrounded();
    }
    return false;
}

SANIC_API void KineticController_GetGroundNormal(uint32_t entityId, float* outX, float* outY, float* outZ) {
    auto* controller = getComponent<KineticCharacterController>(entityId, g_controllerCache);
    if (controller && controller->isGrounded()) {
        glm::vec3 normal = controller->getGroundNormal();
        if (outX) *outX = normal.x;
        if (outY) *outY = normal.y;
        if (outZ) *outZ = normal.z;
    }
}

SANIC_API void KineticController_LockToSpline(uint32_t entityId, uint32_t splineEntityId, float startDistance) {
    auto* controller = getComponent<KineticCharacterController>(entityId, g_controllerCache);
    auto* spline = getComponent<SplineComponent>(splineEntityId, g_splineCache);
    if (controller && spline) {
        controller->lockToSpline(spline, startDistance);
    }
}

SANIC_API void KineticController_UnlockFromSpline(uint32_t entityId) {
    auto* controller = getComponent<KineticCharacterController>(entityId, g_controllerCache);
    if (controller) {
        controller->unlockFromSpline();
    }
}

SANIC_API bool KineticController_IsLockedToSpline(uint32_t entityId) {
    auto* controller = getComponent<KineticCharacterController>(entityId, g_controllerCache);
    if (controller) {
        return controller->isLockedToSpline();
    }
    return false;
}

SANIC_API int KineticController_GetMovementState(uint32_t entityId) {
    auto* controller = getComponent<KineticCharacterController>(entityId, g_controllerCache);
    if (controller) {
        return static_cast<int>(controller->getState().movementMode);
    }
    return 0;
}

// ============================================================================
// Spline Component Implementation
// ============================================================================

SANIC_API float Spline_GetTotalLength(uint32_t entityId) {
    auto* spline = getComponent<SplineComponent>(entityId, g_splineCache);
    if (spline) {
        return spline->getTotalLength();
    }
    return 0.0f;
}

SANIC_API bool Spline_IsLoop(uint32_t entityId) {
    auto* spline = getComponent<SplineComponent>(entityId, g_splineCache);
    if (spline) {
        return spline->isLoop();
    }
    return false;
}

SANIC_API void Spline_SetIsLoop(uint32_t entityId, bool isLoop) {
    auto* spline = getComponent<SplineComponent>(entityId, g_splineCache);
    if (spline) {
        spline->setIsLoop(isLoop);
    }
}

SANIC_API void Spline_GetPositionAtDistance(uint32_t entityId, float distance, 
                                             float* outX, float* outY, float* outZ) {
    auto* spline = getComponent<SplineComponent>(entityId, g_splineCache);
    if (spline) {
        glm::vec3 pos = spline->getPositionAtDistance(distance);
        if (outX) *outX = pos.x;
        if (outY) *outY = pos.y;
        if (outZ) *outZ = pos.z;
    }
}

SANIC_API void Spline_GetTangentAtDistance(uint32_t entityId, float distance,
                                            float* outX, float* outY, float* outZ) {
    auto* spline = getComponent<SplineComponent>(entityId, g_splineCache);
    if (spline) {
        glm::vec3 tangent = spline->getTangentAtDistance(distance);
        if (outX) *outX = tangent.x;
        if (outY) *outY = tangent.y;
        if (outZ) *outZ = tangent.z;
    }
}

SANIC_API void Spline_GetUpAtDistance(uint32_t entityId, float distance,
                                       float* outX, float* outY, float* outZ) {
    auto* spline = getComponent<SplineComponent>(entityId, g_splineCache);
    if (spline) {
        glm::vec3 up = spline->getUpAtDistance(distance);
        if (outX) *outX = up.x;
        if (outY) *outY = up.y;
        if (outZ) *outZ = up.z;
    }
}

SANIC_API void Spline_GetRotationAtDistance(uint32_t entityId, float distance,
                                             float* outX, float* outY, float* outZ, float* outW) {
    auto* spline = getComponent<SplineComponent>(entityId, g_splineCache);
    if (spline) {
        glm::quat rot = spline->getRotationAtDistance(distance);
        if (outX) *outX = rot.x;
        if (outY) *outY = rot.y;
        if (outZ) *outZ = rot.z;
        if (outW) *outW = rot.w;
    }
}

SANIC_API float Spline_GetClosestDistance(uint32_t entityId, float worldX, float worldY, float worldZ) {
    auto* spline = getComponent<SplineComponent>(entityId, g_splineCache);
    if (spline) {
        float param = spline->findClosestParameter(glm::vec3(worldX, worldY, worldZ));
        return spline->parameterToDistance(param);
    }
    return 0.0f;
}

SANIC_API float Spline_GetRollAtDistance(uint32_t entityId, float distance) {
    auto* spline = getComponent<SplineComponent>(entityId, g_splineCache);
    if (spline) {
        return spline->getRollAtDistance(distance);
    }
    return 0.0f;
}

SANIC_API uint32_t Spline_GetControlPointCount(uint32_t entityId) {
    auto* spline = getComponent<SplineComponent>(entityId, g_splineCache);
    if (spline) {
        return static_cast<uint32_t>(spline->getControlPointCount());
    }
    return 0;
}

SANIC_API void Spline_AddControlPoint(uint32_t entityId, float x, float y, float z, int index) {
    auto* spline = getComponent<SplineComponent>(entityId, g_splineCache);
    if (spline) {
        SplineControlPoint point;
        point.position = glm::vec3(x, y, z);
        spline->addControlPoint(point, index);
    }
}

SANIC_API void Spline_RemoveControlPoint(uint32_t entityId, uint32_t index) {
    auto* spline = getComponent<SplineComponent>(entityId, g_splineCache);
    if (spline) {
        spline->removeControlPoint(index);
    }
}

SANIC_API void Spline_SetControlPointPosition(uint32_t entityId, uint32_t index, float x, float y, float z) {
    auto* spline = getComponent<SplineComponent>(entityId, g_splineCache);
    if (spline && index < spline->getControlPointCount()) {
        SplineControlPoint point = spline->getControlPoint(index);
        point.position = glm::vec3(x, y, z);
        spline->setControlPoint(index, point);
    }
}

SANIC_API void Spline_GetControlPointPosition(uint32_t entityId, uint32_t index, 
                                               float* outX, float* outY, float* outZ) {
    auto* spline = getComponent<SplineComponent>(entityId, g_splineCache);
    if (spline && index < spline->getControlPointCount()) {
        glm::vec3 pos = spline->getControlPoint(index).position;
        if (outX) *outX = pos.x;
        if (outY) *outY = pos.y;
        if (outZ) *outZ = pos.z;
    }
}

// ============================================================================
// Spline Movement Implementation
// ============================================================================

SANIC_API void SplineMovement_LockToSpline(uint32_t entityId, uint32_t splineEntityId, int mode) {
    auto* movement = getComponent<SplineMovementComponent>(entityId, g_splineMovementCache);
    auto* spline = getComponent<SplineComponent>(splineEntityId, g_splineCache);
    if (movement && spline) {
        movement->lockToSpline(spline, static_cast<SplineLockMode>(mode), 0.0f);
    }
}

SANIC_API void SplineMovement_UnlockFromSpline(uint32_t entityId) {
    auto* movement = getComponent<SplineMovementComponent>(entityId, g_splineMovementCache);
    if (movement) {
        movement->unlockFromSpline();
    }
}

SANIC_API float SplineMovement_GetCurrentDistance(uint32_t entityId) {
    auto* movement = getComponent<SplineMovementComponent>(entityId, g_splineMovementCache);
    if (movement) {
        return movement->getCurrentDistance();
    }
    return 0.0f;
}

SANIC_API void SplineMovement_SetCurrentDistance(uint32_t entityId, float distance) {
    auto* movement = getComponent<SplineMovementComponent>(entityId, g_splineMovementCache);
    if (movement) {
        movement->setCurrentDistance(distance);
    }
}

SANIC_API float SplineMovement_GetSpeed(uint32_t entityId) {
    auto* movement = getComponent<SplineMovementComponent>(entityId, g_splineMovementCache);
    if (movement) {
        return movement->getSpeed();
    }
    return 0.0f;
}

SANIC_API void SplineMovement_SetSpeed(uint32_t entityId, float speed) {
    auto* movement = getComponent<SplineMovementComponent>(entityId, g_splineMovementCache);
    if (movement) {
        movement->setSpeed(speed);
    }
}

SANIC_API int SplineMovement_GetLockMode(uint32_t entityId) {
    auto* movement = getComponent<SplineMovementComponent>(entityId, g_splineMovementCache);
    if (movement) {
        return static_cast<int>(movement->getLockMode());
    }
    return 0;
}

SANIC_API void SplineMovement_SetHangOffset(uint32_t entityId, float x, float y, float z) {
    auto* movement = getComponent<SplineMovementComponent>(entityId, g_splineMovementCache);
    if (movement) {
        movement->setHangOffset(glm::vec3(x, y, z));
    }
}

// ============================================================================
// Gravity System Implementation
// ============================================================================

SANIC_API uint32_t GravityVolume_Create(int type) {
    if (!g_gravitySystem) return 0;
    return g_gravitySystem->createVolume(static_cast<GravityVolumeType>(type));
}

SANIC_API void GravityVolume_Destroy(uint32_t volumeId) {
    if (g_gravitySystem) {
        g_gravitySystem->removeVolume(volumeId);
    }
}

SANIC_API void GravityVolume_SetPosition(uint32_t volumeId, float x, float y, float z) {
    if (g_gravitySystem) {
        GravityVolume* volume = g_gravitySystem->getVolume(volumeId);
        if (volume) {
            volume->center = glm::vec3(x, y, z);
        }
    }
}

SANIC_API void GravityVolume_SetShapeBox(uint32_t volumeId, float halfX, float halfY, float halfZ) {
    if (g_gravitySystem) {
        GravityVolume* volume = g_gravitySystem->getVolume(volumeId);
        if (volume) {
            volume->shape = GravityVolumeShape::Box;
            volume->halfExtents = glm::vec3(halfX, halfY, halfZ);
        }
    }
}

SANIC_API void GravityVolume_SetShapeSphere(uint32_t volumeId, float radius) {
    if (g_gravitySystem) {
        GravityVolume* volume = g_gravitySystem->getVolume(volumeId);
        if (volume) {
            volume->shape = GravityVolumeShape::Sphere;
            volume->radius = radius;
        }
    }
}

SANIC_API void GravityVolume_SetStrength(uint32_t volumeId, float strength) {
    if (g_gravitySystem) {
        GravityVolume* volume = g_gravitySystem->getVolume(volumeId);
        if (volume) {
            volume->strength = strength;
        }
    }
}

SANIC_API void GravityVolume_SetDirection(uint32_t volumeId, float x, float y, float z) {
    if (g_gravitySystem) {
        GravityVolume* volume = g_gravitySystem->getVolume(volumeId);
        if (volume) {
            volume->direction = glm::normalize(glm::vec3(x, y, z));
        }
    }
}

SANIC_API void GravityVolume_SetBlendRadius(uint32_t volumeId, float radius) {
    if (g_gravitySystem) {
        GravityVolume* volume = g_gravitySystem->getVolume(volumeId);
        if (volume) {
            volume->blendRadius = radius;
        }
    }
}

SANIC_API void GravityVolume_SetPriority(uint32_t volumeId, int priority) {
    if (g_gravitySystem) {
        GravityVolume* volume = g_gravitySystem->getVolume(volumeId);
        if (volume) {
            volume->priority = priority;
        }
    }
}

SANIC_API void GravityVolume_SetSpline(uint32_t volumeId, uint32_t splineEntityId) {
    if (g_gravitySystem) {
        GravityVolume* volume = g_gravitySystem->getVolume(volumeId);
        auto* spline = getComponent<SplineComponent>(splineEntityId, g_splineCache);
        if (volume && spline) {
            volume->spline = spline;
        }
    }
}

SANIC_API void GravitySystem_GetGravityAtPosition(float x, float y, float z,
                                                   float* outGravX, float* outGravY, float* outGravZ) {
    if (g_gravitySystem) {
        GravityQueryResult result = g_gravitySystem->getGravityAtPosition(glm::vec3(x, y, z));
        if (outGravX) *outGravX = result.gravity.x;
        if (outGravY) *outGravY = result.gravity.y;
        if (outGravZ) *outGravZ = result.gravity.z;
    }
}

// ============================================================================
// Ability System Implementation
// ============================================================================

SANIC_API uint32_t Ability_Grant(uint32_t entityId, int abilityType) {
    auto* abilityComp = getComponent<AbilityComponent>(entityId, g_abilityCache);
    if (abilityComp) {
        return abilityComp->grantAbility(static_cast<AbilityType>(abilityType));
    }
    return 0;
}

SANIC_API void Ability_Revoke(uint32_t entityId, uint32_t abilityId) {
    auto* abilityComp = getComponent<AbilityComponent>(entityId, g_abilityCache);
    if (abilityComp) {
        abilityComp->revokeAbility(abilityId);
    }
}

SANIC_API bool Ability_CanActivate(uint32_t entityId, uint32_t abilityId) {
    auto* abilityComp = getComponent<AbilityComponent>(entityId, g_abilityCache);
    if (abilityComp) {
        Ability* ability = abilityComp->getAbility(abilityId);
        if (ability) {
            return ability->canActivate();
        }
    }
    return false;
}

SANIC_API void Ability_Activate(uint32_t entityId, uint32_t abilityId) {
    auto* abilityComp = getComponent<AbilityComponent>(entityId, g_abilityCache);
    if (abilityComp) {
        abilityComp->activateAbility(abilityId);
    }
}

SANIC_API void Ability_Deactivate(uint32_t entityId, uint32_t abilityId) {
    auto* abilityComp = getComponent<AbilityComponent>(entityId, g_abilityCache);
    if (abilityComp) {
        Ability* ability = abilityComp->getAbility(abilityId);
        if (ability) {
            ability->deactivate();
        }
    }
}

SANIC_API bool Ability_IsActive(uint32_t entityId, uint32_t abilityId) {
    auto* abilityComp = getComponent<AbilityComponent>(entityId, g_abilityCache);
    if (abilityComp) {
        Ability* ability = abilityComp->getAbility(abilityId);
        if (ability) {
            return ability->getState() == AbilityState::Active;
        }
    }
    return false;
}

SANIC_API int Ability_GetState(uint32_t entityId, uint32_t abilityId) {
    auto* abilityComp = getComponent<AbilityComponent>(entityId, g_abilityCache);
    if (abilityComp) {
        Ability* ability = abilityComp->getAbility(abilityId);
        if (ability) {
            return static_cast<int>(ability->getState());
        }
    }
    return 0;
}

SANIC_API float Ability_GetCooldownRemaining(uint32_t entityId, uint32_t abilityId) {
    auto* abilityComp = getComponent<AbilityComponent>(entityId, g_abilityCache);
    if (abilityComp) {
        Ability* ability = abilityComp->getAbility(abilityId);
        if (ability) {
            return ability->getCooldownRemaining();
        }
    }
    return 0.0f;
}

SANIC_API void Ability_SetCooldown(uint32_t entityId, uint32_t abilityId, float cooldown) {
    auto* abilityComp = getComponent<AbilityComponent>(entityId, g_abilityCache);
    if (abilityComp) {
        Ability* ability = abilityComp->getAbility(abilityId);
        if (ability) {
            ability->setCooldown(cooldown);
        }
    }
}

SANIC_API void Ability_SetResourceCost(uint32_t entityId, uint32_t abilityId, float cost) {
    auto* abilityComp = getComponent<AbilityComponent>(entityId, g_abilityCache);
    if (abilityComp) {
        Ability* ability = abilityComp->getAbility(abilityId);
        if (ability) {
            ability->setResourceCost(cost);
        }
    }
}

SANIC_API void BoostAbility_SetParameters(uint32_t entityId, uint32_t abilityId,
                                           float force, float duration) {
    auto* abilityComp = getComponent<AbilityComponent>(entityId, g_abilityCache);
    if (abilityComp) {
        BoostAbility* boost = dynamic_cast<BoostAbility*>(abilityComp->getAbility(abilityId));
        if (boost) {
            boost->setBoostForce(force);
            boost->setBoostDuration(duration);
        }
    }
}

SANIC_API void SuperJumpAbility_SetParameters(uint32_t entityId, uint32_t abilityId,
                                               float minForce, float maxForce, float chargeTime) {
    auto* abilityComp = getComponent<AbilityComponent>(entityId, g_abilityCache);
    if (abilityComp) {
        SuperJumpAbility* jump = dynamic_cast<SuperJumpAbility*>(abilityComp->getAbility(abilityId));
        if (jump) {
            jump->setMinJumpForce(minForce);
            jump->setMaxJumpForce(maxForce);
            jump->setChargeTime(chargeTime);
        }
    }
}

SANIC_API void DashAbility_SetParameters(uint32_t entityId, uint32_t abilityId,
                                          float distance, float duration, float cooldown) {
    auto* abilityComp = getComponent<AbilityComponent>(entityId, g_abilityCache);
    if (abilityComp) {
        DashAbility* dash = dynamic_cast<DashAbility*>(abilityComp->getAbility(abilityId));
        if (dash) {
            dash->setDashDistance(distance);
            dash->setDashDuration(duration);
            dash->setCooldown(cooldown);
        }
    }
}

// ============================================================================
// Destruction System Implementation
// ============================================================================

SANIC_API bool Destruction_ApplyDamage(uint32_t entityId, float pointX, float pointY, float pointZ,
                                        float dirX, float dirY, float dirZ, float magnitude) {
    if (g_destructionSystem) {
        return g_destructionSystem->applyDamage(
            entityId,
            glm::vec3(pointX, pointY, pointZ),
            glm::vec3(dirX, dirY, dirZ),
            magnitude
        );
    }
    return false;
}

SANIC_API bool Destruction_ApplyHighSpeedCollision(uint32_t entityId,
                                                    float posX, float posY, float posZ,
                                                    float velX, float velY, float velZ) {
    if (g_destructionSystem) {
        return g_destructionSystem->applyHighSpeedCollision(
            entityId,
            glm::vec3(posX, posY, posZ),
            glm::vec3(velX, velY, velZ)
        );
    }
    return false;
}

SANIC_API void Destruction_ApplyExplosion(float centerX, float centerY, float centerZ,
                                           float radius, float force) {
    if (g_destructionSystem) {
        g_destructionSystem->applyExplosion(
            glm::vec3(centerX, centerY, centerZ),
            radius,
            force
        );
    }
}

SANIC_API bool Destruction_IsIntact(uint32_t entityId) {
    if (g_destructionSystem) {
        return g_destructionSystem->isObjectIntact(entityId);
    }
    return false;
}

SANIC_API void Destruction_SetHighSpeedSettings(float minVelocity, float velocityMultiplier,
                                                 float impactRadius, float characterMass) {
    if (g_destructionSystem) {
        HighSpeedCollisionSettings settings;
        settings.minVelocityToBreak = minVelocity;
        settings.velocityToForceMultiplier = velocityMultiplier;
        settings.impactRadius = impactRadius;
        settings.characterMass = characterMass;
        g_destructionSystem->setHighSpeedSettings(settings);
    }
}

SANIC_API void Destruction_SetDebrisSettings(float lifetime, float despawnDistance,
                                              uint32_t maxActiveDebris) {
    if (g_destructionSystem) {
        DebrisSettings settings = g_destructionSystem->getDebrisSettings();
        settings.lifetime = lifetime;
        settings.despawnDistance = despawnDistance;
        settings.maxActiveDebris = maxActiveDebris;
        g_destructionSystem->setDebrisSettings(settings);
    }
}

// ============================================================================
// Spline Mesh Generator Implementation
// ============================================================================

SANIC_API const float* SplineMesh_GenerateTransforms(uint32_t splineEntityId, float tileLength,
                                                      float scaleX, float scaleY,
                                                      uint32_t* outTransformCount) {
    auto* spline = getComponent<SplineComponent>(splineEntityId, g_splineCache);
    if (!spline) {
        if (outTransformCount) *outTransformCount = 0;
        return nullptr;
    }
    
    SplineMeshSettings settings;
    settings.tileLength = tileLength;
    settings.scale = glm::vec2(scaleX, scaleY);
    
    SplineMeshGenerator generator;
    std::vector<glm::mat4> transforms = generator.generateInstanceTransforms(*spline, settings);
    
    // Copy to scratch buffer
    g_transformBuffer.resize(transforms.size() * 16);
    for (size_t i = 0; i < transforms.size(); ++i) {
        const float* mat = glm::value_ptr(transforms[i]);
        std::memcpy(&g_transformBuffer[i * 16], mat, sizeof(float) * 16);
    }
    
    if (outTransformCount) *outTransformCount = static_cast<uint32_t>(transforms.size());
    return g_transformBuffer.data();
}

SANIC_API void SplineMesh_UpdateDeformation(uint32_t meshEntityId, uint32_t splineEntityId) {
    // Implementation depends on mesh system
    // Would deform mesh vertices along spline
}
