using System.Numerics;
using System.Runtime.InteropServices;

namespace Sanic;

/// <summary>
/// Transform component - position, rotation, scale
/// </summary>
public sealed class Transform
{
    private Vector3 _position;
    private Quaternion _rotation = Quaternion.Identity;
    private Vector3 _scale = Vector3.One;
    private Vector3 _eulerAngles;
    private bool _dirty = true;
    
    /// <summary>World position</summary>
    public Vector3 Position
    {
        get => _position;
        set { _position = value; _dirty = true; }
    }
    
    /// <summary>World rotation as quaternion</summary>
    public Quaternion Rotation
    {
        get => _rotation;
        set { _rotation = value; _eulerAngles = QuaternionToEuler(value); _dirty = true; }
    }
    
    /// <summary>World rotation as Euler angles (degrees)</summary>
    public Vector3 EulerAngles
    {
        get => _eulerAngles;
        set { _eulerAngles = value; _rotation = EulerToQuaternion(value); _dirty = true; }
    }
    
    /// <summary>Local scale</summary>
    public Vector3 Scale
    {
        get => _scale;
        set { _scale = value; _dirty = true; }
    }
    
    /// <summary>Forward direction (negative Z)</summary>
    public Vector3 Forward => Vector3.Transform(-Vector3.UnitZ, _rotation);
    
    /// <summary>Right direction (positive X)</summary>
    public Vector3 Right => Vector3.Transform(Vector3.UnitX, _rotation);
    
    /// <summary>Up direction (positive Y)</summary>
    public Vector3 Up => Vector3.Transform(Vector3.UnitY, _rotation);
    
    /// <summary>Translate by offset</summary>
    public void Translate(Vector3 offset) => Position += offset;
    
    /// <summary>Translate in local space</summary>
    public void TranslateLocal(Vector3 offset) => Position += Vector3.Transform(offset, _rotation);
    
    /// <summary>Rotate by Euler angles (degrees)</summary>
    public void Rotate(Vector3 eulerAngles)
    {
        _eulerAngles += eulerAngles;
        _rotation = EulerToQuaternion(_eulerAngles);
        _dirty = true;
    }
    
    /// <summary>Rotate around axis by angle (degrees)</summary>
    public void RotateAround(Vector3 axis, float angle)
    {
        var rotation = Quaternion.CreateFromAxisAngle(Vector3.Normalize(axis), angle * MathF.PI / 180f);
        _rotation = Quaternion.Normalize(rotation * _rotation);
        _eulerAngles = QuaternionToEuler(_rotation);
        _dirty = true;
    }
    
    /// <summary>Look at target position</summary>
    public void LookAt(Vector3 target, Vector3 up = default)
    {
        if (up == default) up = Vector3.UnitY;
        var direction = Vector3.Normalize(target - _position);
        _rotation = CreateLookRotation(direction, up);
        _eulerAngles = QuaternionToEuler(_rotation);
        _dirty = true;
    }
    
    private static Quaternion EulerToQuaternion(Vector3 euler)
    {
        float pitch = euler.X * MathF.PI / 180f;
        float yaw = euler.Y * MathF.PI / 180f;
        float roll = euler.Z * MathF.PI / 180f;
        return Quaternion.CreateFromYawPitchRoll(yaw, pitch, roll);
    }
    
    private static Vector3 QuaternionToEuler(Quaternion q)
    {
        // Extract Euler angles from quaternion
        float sinr_cosp = 2f * (q.W * q.X + q.Y * q.Z);
        float cosr_cosp = 1f - 2f * (q.X * q.X + q.Y * q.Y);
        float roll = MathF.Atan2(sinr_cosp, cosr_cosp);
        
        float sinp = 2f * (q.W * q.Y - q.Z * q.X);
        float pitch = MathF.Abs(sinp) >= 1f ? MathF.CopySign(MathF.PI / 2f, sinp) : MathF.Asin(sinp);
        
        float siny_cosp = 2f * (q.W * q.Z + q.X * q.Y);
        float cosy_cosp = 1f - 2f * (q.Y * q.Y + q.Z * q.Z);
        float yaw = MathF.Atan2(siny_cosp, cosy_cosp);
        
        return new Vector3(pitch * 180f / MathF.PI, yaw * 180f / MathF.PI, roll * 180f / MathF.PI);
    }
    
    private static Quaternion CreateLookRotation(Vector3 forward, Vector3 up)
    {
        forward = Vector3.Normalize(forward);
        var right = Vector3.Normalize(Vector3.Cross(up, forward));
        up = Vector3.Cross(forward, right);
        
        float m00 = right.X, m01 = up.X, m02 = forward.X;
        float m10 = right.Y, m11 = up.Y, m12 = forward.Y;
        float m20 = right.Z, m21 = up.Z, m22 = forward.Z;
        
        float trace = m00 + m11 + m22;
        Quaternion q;
        
        if (trace > 0)
        {
            float s = 0.5f / MathF.Sqrt(trace + 1f);
            q = new Quaternion((m21 - m12) * s, (m02 - m20) * s, (m10 - m01) * s, 0.25f / s);
        }
        else if (m00 > m11 && m00 > m22)
        {
            float s = 2f * MathF.Sqrt(1f + m00 - m11 - m22);
            q = new Quaternion(0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s);
        }
        else if (m11 > m22)
        {
            float s = 2f * MathF.Sqrt(1f + m11 - m00 - m22);
            q = new Quaternion((m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s);
        }
        else
        {
            float s = 2f * MathF.Sqrt(1f + m22 - m00 - m11);
            q = new Quaternion((m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s);
        }
        
        return Quaternion.Normalize(q);
    }
}

/// <summary>
/// Collision information
/// </summary>
public readonly struct Collision
{
    public uint OtherEntityId { get; init; }
    public Vector3 ContactPoint { get; init; }
    public Vector3 Normal { get; init; }
    public float ImpulseStrength { get; init; }
}

/// <summary>
/// Collider reference for triggers
/// </summary>
public readonly struct Collider
{
    public uint EntityId { get; init; }
}
