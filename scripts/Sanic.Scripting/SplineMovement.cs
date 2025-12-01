using System;
using System.Numerics;
using System.Runtime.InteropServices;

namespace Sanic;

/// <summary>
/// How movement is constrained to a spline
/// </summary>
public enum SplineLockMode
{
    /// <summary>Not locked to any spline</summary>
    None = 0,
    /// <summary>Fully locked to spline position (grind rails)</summary>
    FullLock = 1,
    /// <summary>Can move perpendicular to spline (2.5D sections)</summary>
    LateralLock = 2,
    /// <summary>Inject velocity along spline tangent (boost rings)</summary>
    Velocity = 3
}

/// <summary>
/// Component for moving entities along splines.
/// Supports rail grinding, ziplines, and 2.5D gameplay sections.
/// </summary>
public class SplineMovement : Component
{
    // Native imports
    [DllImport("sanic_native")]
    private static extern void SplineMovement_LockToSpline(uint entityId, uint splineEntityId, int mode);
    
    [DllImport("sanic_native")]
    private static extern void SplineMovement_UnlockFromSpline(uint entityId);
    
    [DllImport("sanic_native")]
    private static extern float SplineMovement_GetCurrentDistance(uint entityId);
    
    [DllImport("sanic_native")]
    private static extern void SplineMovement_SetCurrentDistance(uint entityId, float distance);
    
    [DllImport("sanic_native")]
    private static extern float SplineMovement_GetSpeed(uint entityId);
    
    [DllImport("sanic_native")]
    private static extern void SplineMovement_SetSpeed(uint entityId, float speed);
    
    [DllImport("sanic_native")]
    private static extern int SplineMovement_GetLockMode(uint entityId);
    
    [DllImport("sanic_native")]
    private static extern void SplineMovement_SetHangOffset(uint entityId, float x, float y, float z);

    /// <summary>
    /// Currently locked spline (null if not locked)
    /// </summary>
    public Spline? LockedSpline { get; private set; }

    /// <summary>
    /// Current lock mode
    /// </summary>
    public SplineLockMode LockMode => (SplineLockMode)SplineMovement_GetLockMode(EntityId);

    /// <summary>
    /// Whether currently locked to a spline
    /// </summary>
    public bool IsLocked => LockMode != SplineLockMode.None;

    /// <summary>
    /// Current distance along the spline
    /// </summary>
    public float CurrentDistance
    {
        get => SplineMovement_GetCurrentDistance(EntityId);
        set => SplineMovement_SetCurrentDistance(EntityId, value);
    }

    /// <summary>
    /// Current movement speed along the spline
    /// </summary>
    public float Speed
    {
        get => SplineMovement_GetSpeed(EntityId);
        set => SplineMovement_SetSpeed(EntityId, value);
    }

    /// <summary>
    /// Progress along spline (0 to 1)
    /// </summary>
    public float Progress
    {
        get
        {
            if (LockedSpline == null) return 0;
            return CurrentDistance / LockedSpline.TotalLength;
        }
        set
        {
            if (LockedSpline != null)
            {
                CurrentDistance = value * LockedSpline.TotalLength;
            }
        }
    }

    /// <summary>
    /// Lock to a spline with specified mode
    /// </summary>
    public void LockToSpline(Spline spline, SplineLockMode mode = SplineLockMode.FullLock)
    {
        SplineMovement_LockToSpline(EntityId, spline.EntityId, (int)mode);
        LockedSpline = spline;
    }

    /// <summary>
    /// Lock to a spline at a specific distance
    /// </summary>
    public void LockToSpline(Spline spline, float startDistance, SplineLockMode mode = SplineLockMode.FullLock)
    {
        LockToSpline(spline, mode);
        CurrentDistance = startDistance;
    }

    /// <summary>
    /// Unlock from current spline
    /// </summary>
    public void UnlockFromSpline()
    {
        SplineMovement_UnlockFromSpline(EntityId);
        LockedSpline = null;
    }

    /// <summary>
    /// Set hang offset (for ziplines - character hangs below the spline)
    /// </summary>
    public void SetHangOffset(Vector3 offset)
    {
        SplineMovement_SetHangOffset(EntityId, offset.X, offset.Y, offset.Z);
    }

    /// <summary>
    /// Set hang offset to hang below spline by specified amount
    /// </summary>
    public void SetHangBelow(float distance)
    {
        SetHangOffset(new Vector3(0, -distance, 0));
    }

    /// <summary>
    /// Get current position on the spline
    /// </summary>
    public Vector3 GetCurrentPosition()
    {
        if (LockedSpline == null) return Vector3.Zero;
        return LockedSpline.GetPositionAtDistance(CurrentDistance);
    }

    /// <summary>
    /// Get current tangent direction on the spline
    /// </summary>
    public Vector3 GetCurrentTangent()
    {
        if (LockedSpline == null) return Vector3.UnitZ;
        return LockedSpline.GetTangentAtDistance(CurrentDistance);
    }

    /// <summary>
    /// Reverse direction on spline
    /// </summary>
    public void Reverse()
    {
        Speed = -Speed;
    }
}
