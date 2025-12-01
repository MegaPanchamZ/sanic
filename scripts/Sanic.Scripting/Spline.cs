using System;
using System.Numerics;
using System.Runtime.InteropServices;

namespace Sanic;

/// <summary>
/// Catmull-Rom spline component for rails, ziplines, camera paths, etc.
/// </summary>
public class Spline : Component
{
    // Native imports
    [DllImport("sanic_native")]
    private static extern float Spline_GetTotalLength(uint entityId);
    
    [DllImport("sanic_native")]
    private static extern bool Spline_IsLoop(uint entityId);
    
    [DllImport("sanic_native")]
    private static extern void Spline_SetIsLoop(uint entityId, bool isLoop);
    
    [DllImport("sanic_native")]
    private static extern void Spline_GetPositionAtDistance(uint entityId, float distance, out float x, out float y, out float z);
    
    [DllImport("sanic_native")]
    private static extern void Spline_GetTangentAtDistance(uint entityId, float distance, out float x, out float y, out float z);
    
    [DllImport("sanic_native")]
    private static extern void Spline_GetUpAtDistance(uint entityId, float distance, out float x, out float y, out float z);
    
    [DllImport("sanic_native")]
    private static extern void Spline_GetRotationAtDistance(uint entityId, float distance, out float x, out float y, out float z, out float w);
    
    [DllImport("sanic_native")]
    private static extern float Spline_GetClosestDistance(uint entityId, float worldX, float worldY, float worldZ);
    
    [DllImport("sanic_native")]
    private static extern float Spline_GetRollAtDistance(uint entityId, float distance);
    
    [DllImport("sanic_native")]
    private static extern uint Spline_GetControlPointCount(uint entityId);
    
    [DllImport("sanic_native")]
    private static extern void Spline_AddControlPoint(uint entityId, float x, float y, float z, int index);
    
    [DllImport("sanic_native")]
    private static extern void Spline_RemoveControlPoint(uint entityId, uint index);
    
    [DllImport("sanic_native")]
    private static extern void Spline_SetControlPointPosition(uint entityId, uint index, float x, float y, float z);
    
    [DllImport("sanic_native")]
    private static extern void Spline_GetControlPointPosition(uint entityId, uint index, out float x, out float y, out float z);

    /// <summary>
    /// Total arc length of the spline in world units
    /// </summary>
    public float TotalLength => Spline_GetTotalLength(EntityId);

    /// <summary>
    /// Whether this spline forms a closed loop
    /// </summary>
    public bool IsLoop
    {
        get => Spline_IsLoop(EntityId);
        set => Spline_SetIsLoop(EntityId, value);
    }

    /// <summary>
    /// Number of control points
    /// </summary>
    public uint ControlPointCount => Spline_GetControlPointCount(EntityId);

    /// <summary>
    /// Get world position at distance along spline
    /// </summary>
    public Vector3 GetPositionAtDistance(float distance)
    {
        Spline_GetPositionAtDistance(EntityId, distance, out float x, out float y, out float z);
        return new Vector3(x, y, z);
    }

    /// <summary>
    /// Get tangent direction at distance along spline
    /// </summary>
    public Vector3 GetTangentAtDistance(float distance)
    {
        Spline_GetTangentAtDistance(EntityId, distance, out float x, out float y, out float z);
        return new Vector3(x, y, z);
    }

    /// <summary>
    /// Get up vector at distance along spline
    /// </summary>
    public Vector3 GetUpAtDistance(float distance)
    {
        Spline_GetUpAtDistance(EntityId, distance, out float x, out float y, out float z);
        return new Vector3(x, y, z);
    }

    /// <summary>
    /// Get rotation quaternion at distance along spline
    /// </summary>
    public Quaternion GetRotationAtDistance(float distance)
    {
        Spline_GetRotationAtDistance(EntityId, distance, out float x, out float y, out float z, out float w);
        return new Quaternion(x, y, z, w);
    }

    /// <summary>
    /// Get roll angle at distance (in radians)
    /// </summary>
    public float GetRollAtDistance(float distance) => Spline_GetRollAtDistance(EntityId, distance);

    /// <summary>
    /// Get roll angle at distance (in degrees)
    /// </summary>
    public float GetRollDegreesAtDistance(float distance) => GetRollAtDistance(distance) * 180f / MathF.PI;

    /// <summary>
    /// Find the distance along spline that is closest to a world position
    /// </summary>
    public float GetClosestDistance(Vector3 worldPosition)
    {
        return Spline_GetClosestDistance(EntityId, worldPosition.X, worldPosition.Y, worldPosition.Z);
    }

    /// <summary>
    /// Find the closest point on the spline to a world position
    /// </summary>
    public Vector3 GetClosestPoint(Vector3 worldPosition)
    {
        float distance = GetClosestDistance(worldPosition);
        return GetPositionAtDistance(distance);
    }

    /// <summary>
    /// Add a control point at the specified index (-1 for end)
    /// </summary>
    public void AddControlPoint(Vector3 position, int index = -1)
    {
        Spline_AddControlPoint(EntityId, position.X, position.Y, position.Z, index);
    }

    /// <summary>
    /// Remove a control point at the specified index
    /// </summary>
    public void RemoveControlPoint(uint index)
    {
        Spline_RemoveControlPoint(EntityId, index);
    }

    /// <summary>
    /// Get position of a control point
    /// </summary>
    public Vector3 GetControlPointPosition(uint index)
    {
        Spline_GetControlPointPosition(EntityId, index, out float x, out float y, out float z);
        return new Vector3(x, y, z);
    }

    /// <summary>
    /// Set position of a control point
    /// </summary>
    public void SetControlPointPosition(uint index, Vector3 position)
    {
        Spline_SetControlPointPosition(EntityId, index, position.X, position.Y, position.Z);
    }

    /// <summary>
    /// Sample positions along the spline at regular intervals
    /// </summary>
    public Vector3[] SamplePositions(int sampleCount)
    {
        if (sampleCount < 2) sampleCount = 2;
        
        var positions = new Vector3[sampleCount];
        float length = TotalLength;
        
        for (int i = 0; i < sampleCount; i++)
        {
            float t = (float)i / (sampleCount - 1);
            float distance = t * length;
            positions[i] = GetPositionAtDistance(distance);
        }
        
        return positions;
    }
}
