using System;
using System.Numerics;
using System.Runtime.InteropServices;
using System.Runtime.CompilerServices;
using System.Collections.Generic;
using System.Reflection;

namespace Sanic;

/// <summary>
/// Internal bridge between native C++ and managed C# scripts.
/// This class is called by the native ScriptingSystem.
/// </summary>
public static class ScriptHost
{
    private static readonly Dictionary<IntPtr, SanicBehaviour> _instances = new();
    private static readonly object _lock = new();
    
    /// <summary>
    /// Creates a new script instance.
    /// Called from native: CreateScriptInstance(assemblyPath, typeName, entityId, out methodPtrs)
    /// </summary>
    [UnmanagedCallersOnly(EntryPoint = "CreateScriptInstance")]
    public static unsafe IntPtr CreateScriptInstance(
        IntPtr assemblyPathPtr,
        IntPtr typeNamePtr,
        uint entityId,
        IntPtr* methodPtrs)
    {
        try
        {
            string? assemblyPath = Marshal.PtrToStringUni(assemblyPathPtr);
            string? typeName = Marshal.PtrToStringUni(typeNamePtr);
            
            if (string.IsNullOrEmpty(typeName))
                return IntPtr.Zero;
            
            // Load assembly and find type
            Type? scriptType = null;
            
            if (!string.IsNullOrEmpty(assemblyPath))
            {
                var assembly = Assembly.LoadFrom(assemblyPath);
                scriptType = assembly.GetType(typeName);
            }
            else
            {
                // Search loaded assemblies
                foreach (var asm in AppDomain.CurrentDomain.GetAssemblies())
                {
                    scriptType = asm.GetType(typeName);
                    if (scriptType != null) break;
                }
            }
            
            if (scriptType == null || !typeof(SanicBehaviour).IsAssignableFrom(scriptType))
                return IntPtr.Zero;
            
            // Create instance
            var instance = (SanicBehaviour?)Activator.CreateInstance(scriptType);
            if (instance == null) return IntPtr.Zero;
            
            instance.EntityId = entityId;
            
            // Pin the instance with a GCHandle
            var handle = GCHandle.Alloc(instance);
            var ptr = GCHandle.ToIntPtr(handle);
            
            lock (_lock)
            {
                _instances[ptr] = instance;
            }
            
            // Return method pointers (for direct invocation)
            // methodPtrs[0] = Awake, [1] = Start, [2] = Update, [3] = FixedUpdate, [4] = LateUpdate, [5] = OnDestroy
            // For now we use the wrapper methods below, so these can be null
            
            return ptr;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"[Sanic] CreateScriptInstance error: {ex}");
            return IntPtr.Zero;
        }
    }
    
    /// <summary>
    /// Destroys a script instance.
    /// </summary>
    [UnmanagedCallersOnly(EntryPoint = "DestroyScriptInstance")]
    public static void DestroyScriptInstance(IntPtr gcHandle)
    {
        try
        {
            lock (_lock)
            {
                if (_instances.TryGetValue(gcHandle, out var instance))
                {
                    instance.InvokeOnDestroy();
                    _instances.Remove(gcHandle);
                }
            }
            
            var handle = GCHandle.FromIntPtr(gcHandle);
            handle.Free();
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"[Sanic] DestroyScriptInstance error: {ex}");
        }
    }
    
    [UnmanagedCallersOnly(EntryPoint = "InvokeAwake")]
    public static void InvokeAwake(IntPtr gcHandle)
    {
        if (TryGetInstance(gcHandle, out var instance))
            instance.InvokeAwake();
    }
    
    [UnmanagedCallersOnly(EntryPoint = "InvokeStart")]
    public static void InvokeStart(IntPtr gcHandle)
    {
        if (TryGetInstance(gcHandle, out var instance))
            instance.InvokeStart();
    }
    
    [UnmanagedCallersOnly(EntryPoint = "InvokeUpdate")]
    public static void InvokeUpdate(IntPtr gcHandle)
    {
        if (TryGetInstance(gcHandle, out var instance) && instance.Enabled)
            instance.InvokeUpdate();
    }
    
    [UnmanagedCallersOnly(EntryPoint = "InvokeFixedUpdate")]
    public static void InvokeFixedUpdate(IntPtr gcHandle)
    {
        if (TryGetInstance(gcHandle, out var instance) && instance.Enabled)
            instance.InvokeFixedUpdate();
    }
    
    [UnmanagedCallersOnly(EntryPoint = "InvokeLateUpdate")]
    public static void InvokeLateUpdate(IntPtr gcHandle)
    {
        if (TryGetInstance(gcHandle, out var instance) && instance.Enabled)
            instance.InvokeLateUpdate();
    }
    
    [UnmanagedCallersOnly(EntryPoint = "UpdateTime")]
    public static void UpdateTime(float deltaTime, float timeSinceStart, ulong frameCount)
    {
        Time.UnscaledDeltaTime = deltaTime;
        Time.DeltaTime = deltaTime * Time.TimeScale;
        Time.TimeSinceStart = timeSinceStart;
        Time.FrameCount = frameCount;
    }
    
    [UnmanagedCallersOnly(EntryPoint = "OnCollisionEnter")]
    public static unsafe void OnCollisionEnter(IntPtr gcHandle, uint otherEntityId, float* contact, float* normal)
    {
        if (TryGetInstance(gcHandle, out var instance))
        {
            var collision = new Collision
            {
                OtherEntityId = otherEntityId,
                ContactPoint = new Vector3(contact[0], contact[1], contact[2]),
                Normal = new Vector3(normal[0], normal[1], normal[2])
            };
            
            // Use reflection to call protected method
            var method = instance.GetType().GetMethod("OnCollisionEnter", 
                BindingFlags.Instance | BindingFlags.NonPublic);
            method?.Invoke(instance, new object[] { collision });
        }
    }
    
    [UnmanagedCallersOnly(EntryPoint = "OnCollisionExit")]
    public static unsafe void OnCollisionExit(IntPtr gcHandle, uint otherEntityId, float* contact, float* normal)
    {
        if (TryGetInstance(gcHandle, out var instance))
        {
            var collision = new Collision
            {
                OtherEntityId = otherEntityId,
                ContactPoint = new Vector3(contact[0], contact[1], contact[2]),
                Normal = new Vector3(normal[0], normal[1], normal[2])
            };
            
            var method = instance.GetType().GetMethod("OnCollisionExit", 
                BindingFlags.Instance | BindingFlags.NonPublic);
            method?.Invoke(instance, new object[] { collision });
        }
    }
    
    [UnmanagedCallersOnly(EntryPoint = "OnTriggerEnter")]
    public static void OnTriggerEnter(IntPtr gcHandle, uint otherEntityId)
    {
        if (TryGetInstance(gcHandle, out var instance))
        {
            var collider = new Collider { EntityId = otherEntityId };
            var method = instance.GetType().GetMethod("OnTriggerEnter", 
                BindingFlags.Instance | BindingFlags.NonPublic);
            method?.Invoke(instance, new object[] { collider });
        }
    }
    
    [UnmanagedCallersOnly(EntryPoint = "OnTriggerExit")]
    public static void OnTriggerExit(IntPtr gcHandle, uint otherEntityId)
    {
        if (TryGetInstance(gcHandle, out var instance))
        {
            var collider = new Collider { EntityId = otherEntityId };
            var method = instance.GetType().GetMethod("OnTriggerExit", 
                BindingFlags.Instance | BindingFlags.NonPublic);
            method?.Invoke(instance, new object[] { collider });
        }
    }
    
    [UnmanagedCallersOnly(EntryPoint = "GetMemoryUsage")]
    public static long GetMemoryUsage()
    {
        return GC.GetTotalMemory(false);
    }
    
    [UnmanagedCallersOnly(EntryPoint = "ForceGC")]
    public static void ForceGC(int generation)
    {
        if (generation < 0)
            GC.Collect();
        else
            GC.Collect(Math.Min(generation, GC.MaxGeneration));
    }
    
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private static bool TryGetInstance(IntPtr gcHandle, out SanicBehaviour instance)
    {
        lock (_lock)
        {
            return _instances.TryGetValue(gcHandle, out instance!);
        }
    }
}
