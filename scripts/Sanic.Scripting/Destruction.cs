using System;
using System.Numerics;
using System.Runtime.InteropServices;

namespace Sanic;

/// <summary>
/// Destruction system for breaking objects at high speed.
/// Supports Sonic-style smashing through obstacles.
/// </summary>
public static class Destruction
{
    [DllImport("sanic_native")]
    private static extern bool Destruction_ApplyDamage(uint entityId, float pointX, float pointY, float pointZ,
        float dirX, float dirY, float dirZ, float magnitude);
    
    [DllImport("sanic_native")]
    private static extern bool Destruction_ApplyHighSpeedCollision(uint entityId,
        float posX, float posY, float posZ,
        float velX, float velY, float velZ);
    
    [DllImport("sanic_native")]
    private static extern void Destruction_ApplyExplosion(float centerX, float centerY, float centerZ,
        float radius, float force);
    
    [DllImport("sanic_native")]
    private static extern bool Destruction_IsIntact(uint entityId);
    
    [DllImport("sanic_native")]
    private static extern void Destruction_SetHighSpeedSettings(float minVelocity, float velocityMultiplier,
        float impactRadius, float characterMass);
    
    [DllImport("sanic_native")]
    private static extern void Destruction_SetDebrisSettings(float lifetime, float despawnDistance,
        uint maxActiveDebris);

    /// <summary>
    /// Apply damage at a specific point
    /// </summary>
    /// <param name="entityId">Destructible entity</param>
    /// <param name="point">Impact point in world space</param>
    /// <param name="direction">Impact direction</param>
    /// <param name="magnitude">Force magnitude</param>
    /// <returns>True if any pieces broke off</returns>
    public static bool ApplyDamage(uint entityId, Vector3 point, Vector3 direction, float magnitude)
    {
        return Destruction_ApplyDamage(entityId, point.X, point.Y, point.Z,
            direction.X, direction.Y, direction.Z, magnitude);
    }

    /// <summary>
    /// Apply high-speed character collision damage.
    /// Used for Sonic-style smashing through objects.
    /// </summary>
    /// <param name="entityId">Destructible entity</param>
    /// <param name="characterPosition">Character world position</param>
    /// <param name="characterVelocity">Character velocity vector</param>
    /// <returns>True if any pieces broke off</returns>
    public static bool ApplyHighSpeedCollision(uint entityId, Vector3 characterPosition, Vector3 characterVelocity)
    {
        return Destruction_ApplyHighSpeedCollision(entityId,
            characterPosition.X, characterPosition.Y, characterPosition.Z,
            characterVelocity.X, characterVelocity.Y, characterVelocity.Z);
    }

    /// <summary>
    /// Apply explosion damage to all destructibles in radius
    /// </summary>
    public static void ApplyExplosion(Vector3 center, float radius, float force)
    {
        Destruction_ApplyExplosion(center.X, center.Y, center.Z, radius, force);
    }

    /// <summary>
    /// Check if a destructible object is still intact
    /// </summary>
    public static bool IsIntact(uint entityId)
    {
        return Destruction_IsIntact(entityId);
    }

    /// <summary>
    /// Configure high-speed collision settings
    /// </summary>
    /// <param name="minVelocity">Minimum velocity to trigger breaking (m/s)</param>
    /// <param name="velocityMultiplier">Velocity to force conversion multiplier</param>
    /// <param name="impactRadius">Radius affected by high-speed impact</param>
    /// <param name="characterMass">Character mass for impulse calculation</param>
    public static void SetHighSpeedSettings(float minVelocity = 50f, float velocityMultiplier = 20f,
        float impactRadius = 2f, float characterMass = 80f)
    {
        Destruction_SetHighSpeedSettings(minVelocity, velocityMultiplier, impactRadius, characterMass);
    }

    /// <summary>
    /// Configure debris cleanup settings
    /// </summary>
    /// <param name="lifetime">Base debris lifetime in seconds</param>
    /// <param name="despawnDistance">Distance from player to despawn debris</param>
    /// <param name="maxActiveDebris">Maximum number of active debris pieces</param>
    public static void SetDebrisSettings(float lifetime = 10f, float despawnDistance = 100f,
        uint maxActiveDebris = 256)
    {
        Destruction_SetDebrisSettings(lifetime, despawnDistance, maxActiveDebris);
    }
}

/// <summary>
/// Component for destructible mesh entities
/// </summary>
public class DestructibleMesh : Component
{
    /// <summary>
    /// Whether this object is still intact
    /// </summary>
    public bool IsIntact => Destruction.IsIntact(EntityId);

    /// <summary>
    /// Whether this object has been destroyed
    /// </summary>
    public bool IsDestroyed => !IsIntact;

    /// <summary>
    /// Apply damage at a point
    /// </summary>
    public bool ApplyDamage(Vector3 point, Vector3 direction, float magnitude)
    {
        return Destruction.ApplyDamage(EntityId, point, direction, magnitude);
    }

    /// <summary>
    /// Apply high-speed collision damage
    /// </summary>
    public bool ApplyHighSpeedCollision(Vector3 characterPosition, Vector3 characterVelocity)
    {
        return Destruction.ApplyHighSpeedCollision(EntityId, characterPosition, characterVelocity);
    }
}
