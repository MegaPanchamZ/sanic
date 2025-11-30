using System.Numerics;
using System.Runtime.InteropServices;

namespace Sanic;

/// <summary>
/// Static class for accessing engine time values
/// </summary>
public static class Time
{
    /// <summary>Time since last frame in seconds</summary>
    public static float DeltaTime { get; internal set; }
    
    /// <summary>Fixed timestep for physics in seconds</summary>
    public static float FixedDeltaTime { get; internal set; } = 1f / 60f;
    
    /// <summary>Time since engine start in seconds</summary>
    public static float TimeSinceStart { get; internal set; }
    
    /// <summary>Total frames rendered</summary>
    public static ulong FrameCount { get; internal set; }
    
    /// <summary>Time scale (1.0 = normal, 0.5 = half speed, 0 = paused)</summary>
    public static float TimeScale { get; set; } = 1f;
    
    /// <summary>Unscaled delta time (ignores TimeScale)</summary>
    public static float UnscaledDeltaTime { get; internal set; }
}

/// <summary>
/// Input system for keyboard, mouse, and gamepad
/// </summary>
public static class Input
{
    // Keyboard
    [DllImport("sanic_native", EntryPoint = "Input_GetKey")]
    private static extern bool GetKeyNative(int keyCode);
    
    [DllImport("sanic_native", EntryPoint = "Input_GetKeyDown")]
    private static extern bool GetKeyDownNative(int keyCode);
    
    [DllImport("sanic_native", EntryPoint = "Input_GetKeyUp")]
    private static extern bool GetKeyUpNative(int keyCode);
    
    public static bool GetKey(KeyCode key) => GetKeyNative((int)key);
    public static bool GetKeyDown(KeyCode key) => GetKeyDownNative((int)key);
    public static bool GetKeyUp(KeyCode key) => GetKeyUpNative((int)key);
    
    // Mouse
    [DllImport("sanic_native", EntryPoint = "Input_GetMouseButton")]
    private static extern bool GetMouseButtonNative(int button);
    
    [DllImport("sanic_native", EntryPoint = "Input_GetMouseButtonDown")]
    private static extern bool GetMouseButtonDownNative(int button);
    
    [DllImport("sanic_native", EntryPoint = "Input_GetMousePosition")]
    private static extern void GetMousePositionNative(out float x, out float y);
    
    [DllImport("sanic_native", EntryPoint = "Input_GetMouseDelta")]
    private static extern void GetMouseDeltaNative(out float x, out float y);
    
    public static bool GetMouseButton(int button) => GetMouseButtonNative(button);
    public static bool GetMouseButtonDown(int button) => GetMouseButtonDownNative(button);
    
    public static Vector2 MousePosition
    {
        get { GetMousePositionNative(out float x, out float y); return new(x, y); }
    }
    
    public static Vector2 MouseDelta
    {
        get { GetMouseDeltaNative(out float x, out float y); return new(x, y); }
    }
    
    // Axis input (returns -1 to 1)
    [DllImport("sanic_native", EntryPoint = "Input_GetAxis")]
    private static extern float GetAxisNative([MarshalAs(UnmanagedType.LPStr)] string axisName);
    
    public static float GetAxis(string axisName) => GetAxisNative(axisName);
    
    // Common axes
    public static float Horizontal => GetAxis("Horizontal");
    public static float Vertical => GetAxis("Vertical");
    public static float MouseX => GetAxis("Mouse X");
    public static float MouseY => GetAxis("Mouse Y");
}

/// <summary>
/// Key codes matching GLFW
/// </summary>
public enum KeyCode
{
    Space = 32,
    Apostrophe = 39,
    Comma = 44,
    Minus = 45,
    Period = 46,
    Slash = 47,
    Alpha0 = 48, Alpha1, Alpha2, Alpha3, Alpha4, Alpha5, Alpha6, Alpha7, Alpha8, Alpha9,
    Semicolon = 59,
    Equal = 61,
    A = 65, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    LeftBracket = 91,
    Backslash = 92,
    RightBracket = 93,
    GraveAccent = 96,
    Escape = 256,
    Enter = 257,
    Tab = 258,
    Backspace = 259,
    Insert = 260,
    Delete = 261,
    Right = 262,
    Left = 263,
    Down = 264,
    Up = 265,
    PageUp = 266,
    PageDown = 267,
    Home = 268,
    End = 269,
    CapsLock = 280,
    ScrollLock = 281,
    NumLock = 282,
    PrintScreen = 283,
    Pause = 284,
    F1 = 290, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    Keypad0 = 320, Keypad1, Keypad2, Keypad3, Keypad4, Keypad5, Keypad6, Keypad7, Keypad8, Keypad9,
    KeypadDecimal = 330,
    KeypadDivide = 331,
    KeypadMultiply = 332,
    KeypadSubtract = 333,
    KeypadAdd = 334,
    KeypadEnter = 335,
    KeypadEqual = 336,
    LeftShift = 340,
    LeftControl = 341,
    LeftAlt = 342,
    LeftSuper = 343,
    RightShift = 344,
    RightControl = 345,
    RightAlt = 346,
    RightSuper = 347,
    Menu = 348
}
