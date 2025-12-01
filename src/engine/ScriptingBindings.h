/**
 * ScriptingBindings.h
 * 
 * C API bindings for C# interop with physics and movement systems.
 * These functions are exported for use by the managed scripting runtime.
 * 
 * Follows the pattern established in DEV2_PHYSICS_MOVEMENT.md section 1.5
 */

#pragma once

#include <cstdint>

#ifdef _WIN32
    #define SANIC_API extern "C" __declspec(dllexport)
#else
    #define SANIC_API extern "C" __attribute__((visibility("default")))
#endif

// ============================================================================
// Kinetic Character Controller API
// ============================================================================

/**
 * Set the gravity vector for a character controller
 * @param entityId Entity with KineticCharacterController component
 * @param x, y, z Gravity direction (normalized) times magnitude
 */
SANIC_API void KineticController_SetGravityVector(uint32_t entityId, float x, float y, float z);

/**
 * Get the current gravity vector
 */
SANIC_API void KineticController_GetGravityVector(uint32_t entityId, float* outX, float* outY, float* outZ);

/**
 * Set surface adhesion strength (0-1 range, higher = stickier to surfaces)
 */
SANIC_API void KineticController_SetSurfaceAdhesion(uint32_t entityId, float strength);

/**
 * Get current surface adhesion strength
 */
SANIC_API float KineticController_GetSurfaceAdhesion(uint32_t entityId);

/**
 * Apply an impulse to the character
 */
SANIC_API void KineticController_ApplyImpulse(uint32_t entityId, float x, float y, float z);

/**
 * Apply a force (continuous) to the character
 */
SANIC_API void KineticController_ApplyForce(uint32_t entityId, float x, float y, float z, int forceMode);

/**
 * Set velocity directly
 */
SANIC_API void KineticController_SetVelocity(uint32_t entityId, float x, float y, float z);

/**
 * Get current velocity
 */
SANIC_API void KineticController_GetVelocity(uint32_t entityId, float* outX, float* outY, float* outZ);

/**
 * Get current speed (magnitude of velocity)
 */
SANIC_API float KineticController_GetSpeed(uint32_t entityId);

/**
 * Check if character is on ground
 */
SANIC_API bool KineticController_IsGrounded(uint32_t entityId);

/**
 * Get ground normal (valid only if grounded)
 */
SANIC_API void KineticController_GetGroundNormal(uint32_t entityId, float* outX, float* outY, float* outZ);

/**
 * Lock character to a spline
 * @param entityId Character entity
 * @param splineEntityId Entity with SplineComponent
 * @param startDistance Starting distance along spline
 */
SANIC_API void KineticController_LockToSpline(uint32_t entityId, uint32_t splineEntityId, float startDistance);

/**
 * Unlock character from current spline
 */
SANIC_API void KineticController_UnlockFromSpline(uint32_t entityId);

/**
 * Check if currently locked to a spline
 */
SANIC_API bool KineticController_IsLockedToSpline(uint32_t entityId);

/**
 * Get movement state (0=Walking, 1=Falling, 2=SplineLocked, etc.)
 */
SANIC_API int KineticController_GetMovementState(uint32_t entityId);

// ============================================================================
// Spline Component API
// ============================================================================

/**
 * Get total length of spline in world units
 */
SANIC_API float Spline_GetTotalLength(uint32_t entityId);

/**
 * Check if spline is a closed loop
 */
SANIC_API bool Spline_IsLoop(uint32_t entityId);

/**
 * Set whether spline is a loop
 */
SANIC_API void Spline_SetIsLoop(uint32_t entityId, bool isLoop);

/**
 * Get position at distance along spline
 */
SANIC_API void Spline_GetPositionAtDistance(uint32_t entityId, float distance, float* outX, float* outY, float* outZ);

/**
 * Get tangent at distance along spline
 */
SANIC_API void Spline_GetTangentAtDistance(uint32_t entityId, float distance, float* outX, float* outY, float* outZ);

/**
 * Get up vector at distance along spline
 */
SANIC_API void Spline_GetUpAtDistance(uint32_t entityId, float distance, float* outX, float* outY, float* outZ);

/**
 * Get rotation (as quaternion) at distance along spline
 */
SANIC_API void Spline_GetRotationAtDistance(uint32_t entityId, float distance, 
                                             float* outX, float* outY, float* outZ, float* outW);

/**
 * Get distance along spline of closest point to world position
 */
SANIC_API float Spline_GetClosestDistance(uint32_t entityId, float worldX, float worldY, float worldZ);

/**
 * Get roll angle at distance (in radians)
 */
SANIC_API float Spline_GetRollAtDistance(uint32_t entityId, float distance);

/**
 * Get number of control points
 */
SANIC_API uint32_t Spline_GetControlPointCount(uint32_t entityId);

/**
 * Add a control point
 * @param index Position to insert (-1 for end)
 */
SANIC_API void Spline_AddControlPoint(uint32_t entityId, float x, float y, float z, int index);

/**
 * Remove a control point
 */
SANIC_API void Spline_RemoveControlPoint(uint32_t entityId, uint32_t index);

/**
 * Set control point position
 */
SANIC_API void Spline_SetControlPointPosition(uint32_t entityId, uint32_t index, float x, float y, float z);

/**
 * Get control point position
 */
SANIC_API void Spline_GetControlPointPosition(uint32_t entityId, uint32_t index, float* outX, float* outY, float* outZ);

// ============================================================================
// Spline Movement API
// ============================================================================

/**
 * Lock entity to spline with specified mode
 * @param mode 0=FullLock, 1=LateralLock, 2=Velocity
 */
SANIC_API void SplineMovement_LockToSpline(uint32_t entityId, uint32_t splineEntityId, int mode);

/**
 * Unlock from current spline
 */
SANIC_API void SplineMovement_UnlockFromSpline(uint32_t entityId);

/**
 * Get current distance along locked spline
 */
SANIC_API float SplineMovement_GetCurrentDistance(uint32_t entityId);

/**
 * Set current distance along locked spline
 */
SANIC_API void SplineMovement_SetCurrentDistance(uint32_t entityId, float distance);

/**
 * Get current movement speed along spline
 */
SANIC_API float SplineMovement_GetSpeed(uint32_t entityId);

/**
 * Set movement speed along spline
 */
SANIC_API void SplineMovement_SetSpeed(uint32_t entityId, float speed);

/**
 * Get current lock mode
 */
SANIC_API int SplineMovement_GetLockMode(uint32_t entityId);

/**
 * Set hang offset (for ziplines)
 */
SANIC_API void SplineMovement_SetHangOffset(uint32_t entityId, float x, float y, float z);

// ============================================================================
// Gravity Volume API
// ============================================================================

/**
 * Create a new gravity volume
 * @param type 0=Directional, 1=Spherical, 2=SplineBased, 3=Cylindrical, 4=Point
 * @return Volume ID
 */
SANIC_API uint32_t GravityVolume_Create(int type);

/**
 * Destroy a gravity volume
 */
SANIC_API void GravityVolume_Destroy(uint32_t volumeId);

/**
 * Set volume position (center)
 */
SANIC_API void GravityVolume_SetPosition(uint32_t volumeId, float x, float y, float z);

/**
 * Set volume shape as box
 */
SANIC_API void GravityVolume_SetShapeBox(uint32_t volumeId, float halfX, float halfY, float halfZ);

/**
 * Set volume shape as sphere
 */
SANIC_API void GravityVolume_SetShapeSphere(uint32_t volumeId, float radius);

/**
 * Set gravity strength
 */
SANIC_API void GravityVolume_SetStrength(uint32_t volumeId, float strength);

/**
 * Set gravity direction (for Directional type)
 */
SANIC_API void GravityVolume_SetDirection(uint32_t volumeId, float x, float y, float z);

/**
 * Set blend/falloff radius
 */
SANIC_API void GravityVolume_SetBlendRadius(uint32_t volumeId, float radius);

/**
 * Set priority (higher = takes precedence)
 */
SANIC_API void GravityVolume_SetPriority(uint32_t volumeId, int priority);

/**
 * Associate spline with spline-based gravity volume
 */
SANIC_API void GravityVolume_SetSpline(uint32_t volumeId, uint32_t splineEntityId);

/**
 * Query gravity at world position
 */
SANIC_API void GravitySystem_GetGravityAtPosition(float x, float y, float z, 
                                                   float* outGravX, float* outGravY, float* outGravZ);

// ============================================================================
// Ability System API
// ============================================================================

/**
 * Grant an ability to an entity
 * @param abilityType 0=Boost, 1=SuperJump, 2=ZiplineAttach, 3=Dash, 4=GroundPound
 * @return Ability instance ID
 */
SANIC_API uint32_t Ability_Grant(uint32_t entityId, int abilityType);

/**
 * Revoke an ability
 */
SANIC_API void Ability_Revoke(uint32_t entityId, uint32_t abilityId);

/**
 * Check if ability can be activated
 */
SANIC_API bool Ability_CanActivate(uint32_t entityId, uint32_t abilityId);

/**
 * Activate an ability
 */
SANIC_API void Ability_Activate(uint32_t entityId, uint32_t abilityId);

/**
 * Deactivate an ability
 */
SANIC_API void Ability_Deactivate(uint32_t entityId, uint32_t abilityId);

/**
 * Check if ability is currently active
 */
SANIC_API bool Ability_IsActive(uint32_t entityId, uint32_t abilityId);

/**
 * Get ability state (0=Ready, 1=Active, 2=Cooldown)
 */
SANIC_API int Ability_GetState(uint32_t entityId, uint32_t abilityId);

/**
 * Get remaining cooldown time
 */
SANIC_API float Ability_GetCooldownRemaining(uint32_t entityId, uint32_t abilityId);

/**
 * Set ability cooldown
 */
SANIC_API void Ability_SetCooldown(uint32_t entityId, uint32_t abilityId, float cooldown);

/**
 * Set ability resource cost
 */
SANIC_API void Ability_SetResourceCost(uint32_t entityId, uint32_t abilityId, float cost);

/**
 * Set boost ability parameters
 */
SANIC_API void BoostAbility_SetParameters(uint32_t entityId, uint32_t abilityId, 
                                           float force, float duration);

/**
 * Set super jump ability parameters
 */
SANIC_API void SuperJumpAbility_SetParameters(uint32_t entityId, uint32_t abilityId,
                                               float minForce, float maxForce, float chargeTime);

/**
 * Set dash ability parameters
 */
SANIC_API void DashAbility_SetParameters(uint32_t entityId, uint32_t abilityId,
                                          float distance, float duration, float cooldown);

// ============================================================================
// Destruction System API
// ============================================================================

/**
 * Apply damage at a point
 * @return true if any pieces broke off
 */
SANIC_API bool Destruction_ApplyDamage(uint32_t entityId, float pointX, float pointY, float pointZ,
                                        float dirX, float dirY, float dirZ, float magnitude);

/**
 * Apply high-speed collision damage (for Sonic-style impact)
 * @return true if any pieces broke off
 */
SANIC_API bool Destruction_ApplyHighSpeedCollision(uint32_t entityId, 
                                                    float posX, float posY, float posZ,
                                                    float velX, float velY, float velZ);

/**
 * Apply explosion damage to all destructibles in radius
 */
SANIC_API void Destruction_ApplyExplosion(float centerX, float centerY, float centerZ,
                                           float radius, float force);

/**
 * Check if object is still intact
 */
SANIC_API bool Destruction_IsIntact(uint32_t entityId);

/**
 * Configure high-speed collision settings
 */
SANIC_API void Destruction_SetHighSpeedSettings(float minVelocity, float velocityMultiplier,
                                                 float impactRadius, float characterMass);

/**
 * Configure debris settings
 */
SANIC_API void Destruction_SetDebrisSettings(float lifetime, float despawnDistance,
                                              uint32_t maxActiveDebris);

// ============================================================================
// Spline Mesh Generator API
// ============================================================================

/**
 * Generate instance transforms along a spline
 * @param outTransformCount Output: number of transforms generated
 * @return Pointer to transform data (16 floats per transform, column-major mat4)
 */
SANIC_API const float* SplineMesh_GenerateTransforms(uint32_t splineEntityId, float tileLength,
                                                      float scaleX, float scaleY, 
                                                      uint32_t* outTransformCount);

/**
 * Update mesh deformation for a spline mesh
 */
SANIC_API void SplineMesh_UpdateDeformation(uint32_t meshEntityId, uint32_t splineEntityId);
