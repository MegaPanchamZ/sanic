using System;
using System.Numerics;
using System.Runtime.InteropServices;

namespace Sanic;

/// <summary>
/// Force application mode for physics
/// </summary>
public enum ForceMode
{
    Force = 0,          // Continuous force, affected by mass
    Impulse = 1,        // Instant velocity change, affected by mass
    VelocityChange = 2, // Instant velocity change, ignores mass
    Acceleration = 3    // Continuous force, ignores mass
}

/// <summary>
/// Character movement state
/// </summary>
public enum MovementState
{
    Walking = 0,
    Falling = 1,
    SplineLocked = 2,
    Swimming = 3,
    Flying = 4
}

/// <summary>
/// Custom kinetic character controller for high-speed traversal.
/// Handles surface adhesion, variable gravity, and spline locking.
/// </summary>
public class KineticController : Component
{
    // Native imports
    [DllImport("sanic_native")]
    private static extern void KineticController_SetGravityVector(uint entityId, float x, float y, float z);
    
    [DllImport("sanic_native")]
    private static extern void KineticController_GetGravityVector(uint entityId, out float x, out float y, out float z);
    
    [DllImport("sanic_native")]
    private static extern void KineticController_SetSurfaceAdhesion(uint entityId, float strength);
    
    [DllImport("sanic_native")]
    private static extern float KineticController_GetSurfaceAdhesion(uint entityId);
    
    [DllImport("sanic_native")]
    private static extern void KineticController_ApplyImpulse(uint entityId, float x, float y, float z);
    
    [DllImport("sanic_native")]
    private static extern void KineticController_ApplyForce(uint entityId, float x, float y, float z, int forceMode);
    
    [DllImport("sanic_native")]
    private static extern void KineticController_SetVelocity(uint entityId, float x, float y, float z);
    
    [DllImport("sanic_native")]
    private static extern void KineticController_GetVelocity(uint entityId, out float x, out float y, out float z);
    
    [DllImport("sanic_native")]
    private static extern float KineticController_GetSpeed(uint entityId);
    
    [DllImport("sanic_native")]
    private static extern bool KineticController_IsGrounded(uint entityId);
    
    [DllImport("sanic_native")]
    private static extern void KineticController_GetGroundNormal(uint entityId, out float x, out float y, out float z);
    
    [DllImport("sanic_native")]
    private static extern void KineticController_LockToSpline(uint entityId, uint splineEntityId, float startDistance);
    
    [DllImport("sanic_native")]
    private static extern void KineticController_UnlockFromSpline(uint entityId);
    
    [DllImport("sanic_native")]
    private static extern bool KineticController_IsLockedToSpline(uint entityId);
    
    [DllImport("sanic_native")]
    private static extern int KineticController_GetMovementState(uint entityId);

    /// <summary>
    /// Current velocity in world space
    /// </summary>
    public Vector3 Velocity
    {
        get
        {
            KineticController_GetVelocity(EntityId, out float x, out float y, out float z);
            return new Vector3(x, y, z);
        }
        set => KineticController_SetVelocity(EntityId, value.X, value.Y, value.Z);
    }

    /// <summary>
    /// Current speed (magnitude of velocity) in m/s
    /// </summary>
    public float Speed => KineticController_GetSpeed(EntityId);
    
    /// <summary>
    /// Current speed in mph
    /// </summary>
    public float SpeedMph => Speed * 2.23694f;

    /// <summary>
    /// Gravity direction and magnitude
    /// </summary>
    public Vector3 GravityVector
    {
        get
        {
            KineticController_GetGravityVector(EntityId, out float x, out float y, out float z);
            return new Vector3(x, y, z);
        }
        set => KineticController_SetGravityVector(EntityId, value.X, value.Y, value.Z);
    }

    /// <summary>
    /// Gravity strength (magnitude only)
    /// </summary>
    public float GravityStrength
    {
        get => GravityVector.Length();
        set
        {
            var dir = Vector3.Normalize(GravityVector);
            GravityVector = dir * value;
        }
    }

    /// <summary>
    /// Gravity direction (normalized)
    /// </summary>
    public Vector3 GravityDirection
    {
        get => Vector3.Normalize(GravityVector);
        set => GravityVector = Vector3.Normalize(value) * GravityStrength;
    }

    /// <summary>
    /// Surface adhesion strength (0-1, higher = stickier to surfaces)
    /// </summary>
    public float SurfaceAdhesion
    {
        get => KineticController_GetSurfaceAdhesion(EntityId);
        set => KineticController_SetSurfaceAdhesion(EntityId, value);
    }

    /// <summary>
    /// Whether the character is on the ground
    /// </summary>
    public bool IsGrounded => KineticController_IsGrounded(EntityId);

    /// <summary>
    /// Ground surface normal (only valid when IsGrounded is true)
    /// </summary>
    public Vector3 GroundNormal
    {
        get
        {
            KineticController_GetGroundNormal(EntityId, out float x, out float y, out float z);
            return new Vector3(x, y, z);
        }
    }

    /// <summary>
    /// Whether character is locked to a spline
    /// </summary>
    public bool IsLockedToSpline => KineticController_IsLockedToSpline(EntityId);

    /// <summary>
    /// Current movement state
    /// </summary>
    public MovementState MovementState => (MovementState)KineticController_GetMovementState(EntityId);

    /// <summary>
    /// Apply an instantaneous impulse
    /// </summary>
    public void ApplyImpulse(Vector3 impulse)
    {
        KineticController_ApplyImpulse(EntityId, impulse.X, impulse.Y, impulse.Z);
    }

    /// <summary>
    /// Apply a force with specified mode
    /// </summary>
    public void ApplyForce(Vector3 force, ForceMode mode = ForceMode.Force)
    {
        KineticController_ApplyForce(EntityId, force.X, force.Y, force.Z, (int)mode);
    }

    /// <summary>
    /// Lock to a spline for rail grinding, ziplines, etc.
    /// </summary>
    public void LockToSpline(Spline spline, float startDistance = 0)
    {
        KineticController_LockToSpline(EntityId, spline.EntityId, startDistance);
    }

    /// <summary>
    /// Unlock from current spline
    /// </summary>
    public void UnlockFromSpline()
    {
        KineticController_UnlockFromSpline(EntityId);
    }

    /// <summary>
    /// Apply a forward boost
    /// </summary>
    public void Boost(float power)
    {
        ApplyImpulse(Transform.Forward * power);
    }

    /// <summary>
    /// Perform a jump
    /// </summary>
    public void Jump(float height)
    {
        // v = sqrt(2 * g * h)
        float jumpVelocity = MathF.Sqrt(2f * GravityStrength * height);
        ApplyImpulse(-GravityDirection * jumpVelocity);
    }
}

/// <summary>
/// Component base class for all Sanic components
/// </summary>
public abstract class Component
{
    public uint EntityId { get; internal set; }
    public Transform Transform { get; internal set; } = new();
}
