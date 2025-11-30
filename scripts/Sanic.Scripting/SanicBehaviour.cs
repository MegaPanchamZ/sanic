using System;
using System.Numerics;
using System.Runtime.InteropServices;
using System.Runtime.CompilerServices;

namespace Sanic;

/// <summary>
/// Base class for all gameplay scripts.
/// Scripts inherit from this to get lifecycle callbacks and engine access.
/// </summary>
public abstract class SanicBehaviour
{
    /// <summary>Entity ID in the ECS</summary>
    public uint EntityId { get; internal set; }
    
    /// <summary>Cached transform component</summary>
    public Transform Transform { get; internal set; } = new();
    
    /// <summary>Is this component enabled?</summary>
    public bool Enabled { get; set; } = true;
    
    /// <summary>Has Awake been called?</summary>
    internal bool HasAwoken { get; set; }
    
    /// <summary>Has Start been called?</summary>
    internal bool HasStarted { get; set; }

    // Lifecycle methods - override these in your scripts
    
    /// <summary>Called once when script is first loaded, before Start</summary>
    protected virtual void Awake() { }
    
    /// <summary>Called once before the first Update</summary>
    protected virtual void Start() { }
    
    /// <summary>Called every frame</summary>
    protected virtual void Update() { }
    
    /// <summary>Called at fixed time intervals (physics)</summary>
    protected virtual void FixedUpdate() { }
    
    /// <summary>Called after all Update calls</summary>
    protected virtual void LateUpdate() { }
    
    /// <summary>Called when the script is destroyed</summary>
    protected virtual void OnDestroy() { }
    
    // Collision callbacks
    
    /// <summary>Called when collision starts</summary>
    protected virtual void OnCollisionEnter(Collision collision) { }
    
    /// <summary>Called when collision ends</summary>
    protected virtual void OnCollisionExit(Collision collision) { }
    
    /// <summary>Called when trigger starts</summary>
    protected virtual void OnTriggerEnter(Collider other) { }
    
    /// <summary>Called when trigger ends</summary>
    protected virtual void OnTriggerExit(Collider other) { }
    
    // Internal invocation methods (called from native)
    
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal void InvokeAwake()
    {
        if (!HasAwoken)
        {
            HasAwoken = true;
            Awake();
        }
    }
    
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal void InvokeStart()
    {
        if (!HasStarted)
        {
            HasStarted = true;
            Start();
        }
    }
    
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal void InvokeUpdate() => Update();
    
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal void InvokeFixedUpdate() => FixedUpdate();
    
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal void InvokeLateUpdate() => LateUpdate();
    
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal void InvokeOnDestroy() => OnDestroy();
}
