using System;
using System.Numerics;
using System.Runtime.InteropServices;

namespace Sanic;

/// <summary>
/// Type of gravity volume
/// </summary>
public enum GravityVolumeType
{
    /// <summary>Constant direction (e.g., ceiling walk area)</summary>
    Directional = 0,
    /// <summary>Toward center (planetoids)</summary>
    Spherical = 1,
    /// <summary>Perpendicular to spline tangent (tubes/loops)</summary>
    SplineBased = 2,
    /// <summary>Toward central axis (pipe interiors)</summary>
    Cylindrical = 3,
    /// <summary>Toward a single point</summary>
    Point = 4
}

/// <summary>
/// Variable gravity volume for loops, planetoids, and tube traversal.
/// </summary>
public class GravityVolume : IDisposable
{
    // Native imports
    [DllImport("sanic_native")]
    private static extern uint GravityVolume_Create(int type);
    
    [DllImport("sanic_native")]
    private static extern void GravityVolume_Destroy(uint volumeId);
    
    [DllImport("sanic_native")]
    private static extern void GravityVolume_SetPosition(uint volumeId, float x, float y, float z);
    
    [DllImport("sanic_native")]
    private static extern void GravityVolume_SetShapeBox(uint volumeId, float halfX, float halfY, float halfZ);
    
    [DllImport("sanic_native")]
    private static extern void GravityVolume_SetShapeSphere(uint volumeId, float radius);
    
    [DllImport("sanic_native")]
    private static extern void GravityVolume_SetStrength(uint volumeId, float strength);
    
    [DllImport("sanic_native")]
    private static extern void GravityVolume_SetDirection(uint volumeId, float x, float y, float z);
    
    [DllImport("sanic_native")]
    private static extern void GravityVolume_SetBlendRadius(uint volumeId, float radius);
    
    [DllImport("sanic_native")]
    private static extern void GravityVolume_SetPriority(uint volumeId, int priority);
    
    [DllImport("sanic_native")]
    private static extern void GravityVolume_SetSpline(uint volumeId, uint splineEntityId);

    private uint _volumeId;
    private bool _disposed;
    private GravityVolumeType _type;
    private Vector3 _position;
    private float _strength = 9.81f;
    private Vector3 _direction = -Vector3.UnitY;
    private float _blendRadius = 2.0f;
    private int _priority;

    /// <summary>
    /// Create a new gravity volume
    /// </summary>
    public GravityVolume(GravityVolumeType type)
    {
        _type = type;
        _volumeId = GravityVolume_Create((int)type);
    }

    /// <summary>
    /// Native volume ID
    /// </summary>
    public uint VolumeId => _volumeId;

    /// <summary>
    /// Type of gravity
    /// </summary>
    public GravityVolumeType Type => _type;

    /// <summary>
    /// Center position of the volume
    /// </summary>
    public Vector3 Position
    {
        get => _position;
        set
        {
            _position = value;
            GravityVolume_SetPosition(_volumeId, value.X, value.Y, value.Z);
        }
    }

    /// <summary>
    /// Gravity strength (m/s²)
    /// </summary>
    public float Strength
    {
        get => _strength;
        set
        {
            _strength = value;
            GravityVolume_SetStrength(_volumeId, value);
        }
    }

    /// <summary>
    /// Gravity direction (for Directional type)
    /// </summary>
    public Vector3 Direction
    {
        get => _direction;
        set
        {
            _direction = Vector3.Normalize(value);
            GravityVolume_SetDirection(_volumeId, _direction.X, _direction.Y, _direction.Z);
        }
    }

    /// <summary>
    /// Blend/falloff radius for smooth transitions
    /// </summary>
    public float BlendRadius
    {
        get => _blendRadius;
        set
        {
            _blendRadius = value;
            GravityVolume_SetBlendRadius(_volumeId, value);
        }
    }

    /// <summary>
    /// Priority (higher values override lower)
    /// </summary>
    public int Priority
    {
        get => _priority;
        set
        {
            _priority = value;
            GravityVolume_SetPriority(_volumeId, value);
        }
    }

    /// <summary>
    /// Set volume shape to a box
    /// </summary>
    public void SetShapeBox(Vector3 halfExtents)
    {
        GravityVolume_SetShapeBox(_volumeId, halfExtents.X, halfExtents.Y, halfExtents.Z);
    }

    /// <summary>
    /// Set volume shape to a sphere
    /// </summary>
    public void SetShapeSphere(float radius)
    {
        GravityVolume_SetShapeSphere(_volumeId, radius);
    }

    /// <summary>
    /// Associate a spline (for SplineBased type)
    /// </summary>
    public void SetSpline(Spline spline)
    {
        GravityVolume_SetSpline(_volumeId, spline.EntityId);
    }

    /// <summary>
    /// Create a spherical gravity volume (like a small planet)
    /// </summary>
    public static GravityVolume CreateSpherical(Vector3 center, float radius, float strength = 9.81f)
    {
        var volume = new GravityVolume(GravityVolumeType.Spherical)
        {
            Position = center,
            Strength = strength
        };
        volume.SetShapeSphere(radius);
        return volume;
    }

    /// <summary>
    /// Create a directional gravity volume (like a ceiling walk area)
    /// </summary>
    public static GravityVolume CreateDirectional(Vector3 center, Vector3 halfExtents, Vector3 direction, float strength = 9.81f)
    {
        var volume = new GravityVolume(GravityVolumeType.Directional)
        {
            Position = center,
            Direction = direction,
            Strength = strength
        };
        volume.SetShapeBox(halfExtents);
        return volume;
    }

    /// <summary>
    /// Create a spline-based gravity volume (for tubes/loops)
    /// </summary>
    public static GravityVolume CreateSplineBased(Spline spline, float radius, float strength = 9.81f)
    {
        var volume = new GravityVolume(GravityVolumeType.SplineBased)
        {
            Strength = strength
        };
        volume.SetSpline(spline);
        volume.SetShapeSphere(radius); // Tube radius around spline
        return volume;
    }

    public void Dispose()
    {
        if (!_disposed)
        {
            GravityVolume_Destroy(_volumeId);
            _disposed = true;
        }
    }
}

/// <summary>
/// Static access to the gravity system
/// </summary>
public static class GravitySystem
{
    [DllImport("sanic_native")]
    private static extern void GravitySystem_GetGravityAtPosition(float x, float y, float z, 
        out float gravX, out float gravY, out float gravZ);

    /// <summary>
    /// Query gravity at a world position
    /// </summary>
    public static Vector3 GetGravityAtPosition(Vector3 position)
    {
        GravitySystem_GetGravityAtPosition(position.X, position.Y, position.Z,
            out float gx, out float gy, out float gz);
        return new Vector3(gx, gy, gz);
    }

    /// <summary>
    /// Get the "up" direction at a world position (opposite of gravity)
    /// </summary>
    public static Vector3 GetUpAtPosition(Vector3 position)
    {
        return -Vector3.Normalize(GetGravityAtPosition(position));
    }
}
