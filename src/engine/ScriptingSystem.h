/**
 * ScriptingSystem.h
 * 
 * C# scripting integration for the Sanic Engine.
 * Provides gameplay logic, behavior scripts, and editor automation.
 * 
 * Features:
 * - .NET 8 CoreCLR embedding for compiled C# execution
 * - Entity component binding via P/Invoke
 * - Hot-reload support with assembly unloading
 * - Async/await pattern support
 * - Safe AppDomain isolation
 * 
 * Architecture:
 * - Each entity can have a ScriptComponent
 * - Scripts inherit from SanicBehaviour base class
 * - Lifecycle methods: Awake, Start, Update, FixedUpdate, OnDestroy
 * - Native interop through generated bindings
 * 
 * C# Script Example:
 *   using Sanic;
 *   public class PlayerController : SanicBehaviour {
 *       public float speed = 5.0f;
 *       void Update() {
 *           var input = Input.GetAxis("Horizontal");
 *           Transform.position += Vec3.Right * input * speed * Time.deltaTime;
 *       }
 *   }
 */

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <atomic>
#include <mutex>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// Forward declarations for .NET hosting
typedef struct hostfxr_handle_t* hostfxr_handle;
typedef void* load_assembly_and_get_function_pointer_fn;

namespace Sanic {

// Forward declarations
class ECS;
class PhysicsSystem;
class Renderer;

/**
 * Script execution priority (matches C# ExecutionOrder attribute)
 */
enum class ScriptPriority : int {
    Early = -100,       // Before standard scripts
    Default = 0,        // Standard priority
    Late = 100,         // After standard scripts
    VeryLate = 1000     // UI and post-processing
};

/**
 * Managed object handle - opaque reference to C# object
 */
struct ManagedHandle {
    void* gcHandle = nullptr;
    bool isValid() const { return gcHandle != nullptr; }
};

/**
 * Script error info (from C# exceptions)
 */
struct ScriptError {
    std::string scriptPath;        // Assembly or source file
    std::string typeName;          // C# type that threw
    int line;                      // Source line if available
    std::string message;           // Exception message
    std::string stackTrace;        // Full managed stack trace
    std::string innerException;    // Inner exception if present
};

/**
 * Script component attached to entities
 */
struct ScriptComponent {
    std::string assemblyPath;            // Path to .dll assembly
    std::string typeName;                // Fully qualified C# type name (e.g., "Game.PlayerController")
    bool enabled = true;
    ScriptPriority priority = ScriptPriority::Default;
    
    // Runtime state (managed by ScriptingSystem)
    int scriptInstanceId = -1;           // Internal instance ID
    ManagedHandle managedInstance;       // GC handle to C# object
    bool hasAwoken = false;
    bool hasStarted = false;
    
    // Serialized properties (set from editor, synced to C# fields)
    std::unordered_map<std::string, float> floatProperties;
    std::unordered_map<std::string, int> intProperties;
    std::unordered_map<std::string, bool> boolProperties;
    std::unordered_map<std::string, std::string> stringProperties;
    std::unordered_map<std::string, glm::vec3> vec3Properties;
    std::unordered_map<std::string, glm::quat> quatProperties;
    std::unordered_map<std::string, uint32_t> entityRefProperties;  // References to other entities
};

/**
 * Script instance runtime data
 */
struct ScriptInstance {
    int instanceId;
    uint32_t entityId;
    std::string assemblyPath;
    std::string typeName;
    
    // Managed object handle
    ManagedHandle managedObject;         // GCHandle to C# SanicBehaviour instance
    
    // Cached method pointers for fast invocation
    void* awakePtr = nullptr;
    void* startPtr = nullptr;
    void* updatePtr = nullptr;
    void* fixedUpdatePtr = nullptr;
    void* lateUpdatePtr = nullptr;
    void* onDestroyPtr = nullptr;
    void* onEnablePtr = nullptr;
    void* onDisablePtr = nullptr;
    void* onCollisionEnterPtr = nullptr;
    void* onCollisionExitPtr = nullptr;
    void* onTriggerEnterPtr = nullptr;
    void* onTriggerExitPtr = nullptr;
    
    // Method availability flags (set during reflection)
    uint32_t methodFlags = 0;
    static constexpr uint32_t HAS_AWAKE = 1 << 0;
    static constexpr uint32_t HAS_START = 1 << 1;
    static constexpr uint32_t HAS_UPDATE = 1 << 2;
    static constexpr uint32_t HAS_FIXED_UPDATE = 1 << 3;
    static constexpr uint32_t HAS_LATE_UPDATE = 1 << 4;
    static constexpr uint32_t HAS_ON_DESTROY = 1 << 5;
    static constexpr uint32_t HAS_ON_ENABLE = 1 << 6;
    static constexpr uint32_t HAS_ON_DISABLE = 1 << 7;
    static constexpr uint32_t HAS_ON_COLLISION_ENTER = 1 << 8;
    static constexpr uint32_t HAS_ON_COLLISION_EXIT = 1 << 9;
    static constexpr uint32_t HAS_ON_TRIGGER_ENTER = 1 << 10;
    static constexpr uint32_t HAS_ON_TRIGGER_EXIT = 1 << 11;
    
    bool valid = false;
    bool hasAwoken = false;
    bool hasStarted = false;
    uint64_t assemblyTimestamp = 0;      // For hot reload detection
};

/**
 * Configuration for the scripting system
 */
struct ScriptingConfig {
    std::string runtimeConfigPath = "";      // Path to .runtimeconfig.json (auto-detected if empty)
    std::string assembliesPath = "scripts/"; // Directory containing C# assemblies
    std::string coreAssemblyName = "Sanic.Scripting.dll";  // Core bindings assembly
    
    bool enableHotReload = true;              // Watch for assembly changes
    bool enableDebugger = false;              // Allow managed debugger attach
    bool enableProfiling = false;             // Enable CLR profiling
    bool enableTieredCompilation = true;      // JIT optimization tiers
    
    size_t gcHeapLimit = 256 * 1024 * 1024;   // 256 MB managed heap limit
    int gcLatencyMode = 1;                    // 0=Batch, 1=Interactive, 2=LowLatency, 3=SustainedLowLatency
};

/**
 * C# Scripting System - .NET CoreCLR Host
 */
class ScriptingSystem {
public:
    ScriptingSystem() = default;
    ~ScriptingSystem();
    
    // Non-copyable
    ScriptingSystem(const ScriptingSystem&) = delete;
    ScriptingSystem& operator=(const ScriptingSystem&) = delete;
    
    /**
     * Initialize the .NET runtime
     */
    bool initialize(const ScriptingConfig& config = {});
    
    /**
     * Shutdown and cleanup
     */
    void shutdown();
    
    /**
     * Register engine systems for script access
     */
    void registerECS(ECS* ecs);
    void registerPhysics(PhysicsSystem* physics);
    void registerRenderer(Renderer* renderer);
    
    /**
     * Create a script instance for an entity
     * @param entityId The entity to attach the script to
     * @param assemblyPath Path to the .dll assembly
     * @param typeName Fully qualified type name (e.g., "Game.PlayerController")
     * Returns instance ID or -1 on failure
     */
    int createScriptInstance(uint32_t entityId, const std::string& assemblyPath, const std::string& typeName);
    
    /**
     * Destroy a script instance
     */
    void destroyScriptInstance(int instanceId);
    
    /**
     * Get a script instance
     */
    ScriptInstance* getScriptInstance(int instanceId);
    
    /**
     * Execute lifecycle hooks
     */
    void awakeAll();
    void startAll();
    void update(float deltaTime);
    void fixedUpdate(float fixedDeltaTime);
    void lateUpdate(float deltaTime);
    
    /**
     * Send events to scripts
     */
    void sendCollisionEnter(uint32_t entityA, uint32_t entityB, const glm::vec3& contactPoint, const glm::vec3& normal);
    void sendCollisionExit(uint32_t entityA, uint32_t entityB);
    void sendTriggerEnter(uint32_t entityA, uint32_t entityB);
    void sendTriggerExit(uint32_t entityA, uint32_t entityB);
    
    /**
     * Hot reload support - unloads and reloads assemblies
     */
    void checkForChanges();
    void reloadAssembly(const std::string& assemblyPath);
    void reloadAll();
    
    /**
     * Compile and run C# code at runtime (for console/editor)
     * Uses Roslyn for dynamic compilation
     */
    bool executeCode(const std::string& csharpCode, std::string& output);
    
    /**
     * Get last error
     */
    const ScriptError& getLastError() const { return lastError_; }
    bool hasError() const { return !lastError_.message.empty(); }
    void clearError() { lastError_ = {}; }
    
    /**
     * Get managed heap memory usage
     */
    size_t getMemoryUsage() const;
    
    /**
     * Force garbage collection (use sparingly!)
     */
    void forceGC(int generation = -1);
    
    /**
     * Enable/disable scripts globally
     */
    void setEnabled(bool enabled) { globalEnabled_ = enabled; }
    bool isEnabled() const { return globalEnabled_; }
    
    /**
     * Get statistics
     */
    struct Statistics {
        size_t instanceCount;
        size_t managedHeapSize;
        size_t gen0Collections;
        size_t gen1Collections;
        size_t gen2Collections;
        uint64_t totalMethodCalls;
        double averageCallTimeMs;
        size_t loadedAssemblies;
    };
    Statistics getStatistics() const;
    
    /**
     * Get list of loaded assemblies
     */
    std::vector<std::string> getLoadedAssemblies() const;
    
    /**
     * Invoke a static C# method
     */
    template<typename TRet, typename... TArgs>
    TRet invokeStatic(const std::string& assemblyPath, const std::string& typeName, 
                      const std::string& methodName, TArgs... args);
    
private:
    // .NET hosting
    bool initializeHostFxr();
    bool loadCoreAssembly();
    void* getExportedMethod(const std::string& assemblyPath, const std::string& typeName,
                            const std::string& methodName, const std::string& delegateTypeName);
    
    // Assembly loading
    bool loadAssembly(const std::string& path);
    void unloadAssembly(const std::string& path);
    
    // Instance management
    bool createManagedInstance(ScriptInstance& instance);
    void destroyManagedInstance(ScriptInstance& instance);
    void cacheMethodPointers(ScriptInstance& instance);
    
    // Method invocation
    void invokeLifecycleMethod(ScriptInstance& instance, void* methodPtr);
    void invokeLifecycleMethodWithDelta(ScriptInstance& instance, void* methodPtr, float deltaTime);
    void invokeCollisionMethod(ScriptInstance& instance, void* methodPtr, uint32_t otherEntity, 
                               const glm::vec3* contactPoint, const glm::vec3* normal);
    
    // Native callbacks (called from C#)
    static void nativeLog(int level, const char* message);
    static void nativeGetTransform(uint32_t entityId, float* outMatrix);
    static void nativeSetTransform(uint32_t entityId, const float* matrix);
    static bool nativeRaycast(const float* origin, const float* direction, float maxDist, 
                              uint32_t* hitEntity, float* hitPoint, float* hitNormal);
    static bool nativeGetKey(const char* keyName);
    static bool nativeGetKeyDown(const char* keyName);
    static bool nativeGetKeyUp(const char* keyName);
    static void nativeGetMousePosition(float* x, float* y);
    static void nativeGetMouseDelta(float* dx, float* dy);
    static void nativeAddForce(uint32_t entityId, const float* force, int forceMode);
    static void nativeSetVelocity(uint32_t entityId, const float* velocity);
    static void nativeDrawLine(const float* from, const float* to, uint32_t color, float duration);
    static void nativeDrawSphere(const float* center, float radius, uint32_t color, float duration);
    static void nativeInstantiate(const char* prefabPath, const float* position, const float* rotation, uint32_t* outEntity);
    static void nativeDestroy(uint32_t entityId);
    
    // Register native callbacks with managed code
    void registerNativeCallbacks();
    
    // .NET runtime handles
    hostfxr_handle hostfxrHandle_ = nullptr;
    void* hostContextHandle_ = nullptr;
    load_assembly_and_get_function_pointer_fn loadAssemblyFn_ = nullptr;
    
    // Cached managed delegates
    void* createInstanceDelegate_ = nullptr;
    void* destroyInstanceDelegate_ = nullptr;
    void* getMethodPointerDelegate_ = nullptr;
    void* invokeVoidDelegate_ = nullptr;
    void* invokeFloatDelegate_ = nullptr;
    void* getStatisticsDelegate_ = nullptr;
    void* forceGCDelegate_ = nullptr;
    
    ScriptingConfig config_;
    
    // Registered systems (static for callback access)
    static ECS* sEcs_;
    static PhysicsSystem* sPhysics_;
    static Renderer* sRenderer_;
    static ScriptingSystem* sInstance_;
    
    // Script instances
    std::unordered_map<int, ScriptInstance> instances_;
    std::mutex instancesMutex_;
    int nextInstanceId_ = 1;
    
    // Entity to instance mapping
    std::unordered_map<uint32_t, std::vector<int>> entityToInstances_;
    
    // Loaded assemblies
    std::unordered_map<std::string, uint64_t> assemblyTimestamps_;
    std::unordered_map<std::string, void*> loadedAssemblies_;
    
    // Error handling
    ScriptError lastError_;
    
    // Statistics
    std::atomic<uint64_t> totalCalls_{0};
    double totalCallTime_ = 0.0;
    
    bool initialized_ = false;
    bool globalEnabled_ = true;
};

// ============================================================================
// C# Script API - Available in managed assemblies
// ============================================================================

/**
 * C# API documentation (inherit from SanicBehaviour)
 * 
 * using Sanic;
 * using Sanic.Math;
 * 
 * public class PlayerController : SanicBehaviour
 * {
 *     // Serialized fields (editable in engine)
 *     [SerializeField] public float moveSpeed = 5.0f;
 *     [SerializeField] public float jumpForce = 10.0f;
 *     [SerializeField] public Entity target;
 *     
 *     // Lifecycle methods
 *     void Awake() { }                    // Called once when created
 *     void Start() { }                    // Called before first Update
 *     void Update() { }                   // Called every frame
 *     void FixedUpdate() { }              // Called at fixed physics rate
 *     void LateUpdate() { }               // Called after all Update calls
 *     void OnDestroy() { }                // Called when destroyed
 *     void OnEnable() { }                 // Called when enabled
 *     void OnDisable() { }                // Called when disabled
 *     
 *     // Collision callbacks
 *     void OnCollisionEnter(Collision collision) { }
 *     void OnCollisionExit(Collision collision) { }
 *     void OnTriggerEnter(Collider other) { }
 *     void OnTriggerExit(Collider other) { }
 * }
 * 
 * // Transform API
 * Transform.position = new Vec3(1, 2, 3);
 * Transform.rotation = Quat.FromEuler(0, 45, 0);
 * Transform.LookAt(target.Transform.position);
 * Transform.Translate(Vec3.Forward * speed * Time.deltaTime);
 * Transform.Rotate(Vec3.Up, angularSpeed * Time.deltaTime);
 * 
 * // Entity API
 * var health = GetComponent<Health>();
 * var allRenderers = GetComponentsInChildren<MeshRenderer>();
 * var child = Transform.Find("Gun");
 * var parent = Transform.parent;
 * var newEntity = Instantiate(prefab, position, rotation);
 * Destroy(entity);
 * Destroy(entity, delay: 2.0f);
 * 
 * // Input API
 * if (Input.GetKey(KeyCode.W)) { }
 * if (Input.GetKeyDown(KeyCode.Space)) { }
 * if (Input.GetKeyUp(KeyCode.LeftShift)) { }
 * if (Input.GetMouseButton(0)) { }
 * Vec2 mousePos = Input.mousePosition;
 * Vec2 mouseDelta = Input.mouseDelta;
 * float axis = Input.GetAxis("Horizontal");
 * 
 * // Physics API
 * if (Physics.Raycast(origin, direction, out RaycastHit hit, maxDistance))
 * {
 *     Debug.Log($"Hit {hit.entity} at {hit.point}");
 * }
 * var hits = Physics.SphereCastAll(origin, radius, direction, maxDistance);
 * Rigidbody.AddForce(Vec3.Up * jumpForce, ForceMode.Impulse);
 * Rigidbody.velocity = new Vec3(5, 0, 0);
 * 
 * // Debug API
 * Debug.Log("Hello world");
 * Debug.LogWarning("Something odd happened");
 * Debug.LogError("Critical error!");
 * Debug.DrawLine(from, to, Color.Red, duration: 1.0f);
 * Debug.DrawSphere(center, radius, Color.Green);
 * Debug.DrawRay(origin, direction * length, Color.Blue);
 * 
 * // Math helpers
 * Vec3 v = new Vec3(1, 2, 3);
 * Quat q = Quat.FromEuler(pitch, yaw, roll);
 * float t = Mathf.Lerp(a, b, 0.5f);
 * float d = Mathf.Clamp(value, min, max);
 * Vec3 dir = Vec3.Normalize(target - position);
 * float angle = Vec3.Angle(forward, toTarget);
 * 
 * // Async/Coroutines
 * StartCoroutine(MyCoroutine());
 * 
 * IEnumerator MyCoroutine()
 * {
 *     yield return null;                       // Wait one frame
 *     yield return new WaitForSeconds(1.0f);   // Wait for duration
 *     yield return new WaitUntil(() => ready); // Wait for condition
 *     yield return new WaitForFixedUpdate();   // Wait for physics
 * }
 * 
 * // Events
 * public event Action<int> OnScoreChanged;
 * OnScoreChanged?.Invoke(newScore);
 * 
 * // Attributes
 * [RequireComponent(typeof(Rigidbody))]
 * [ExecutionOrder(-100)]  // Run before other scripts
 * [DisallowMultipleComponent]
 * public class MyScript : SanicBehaviour { }
 */

} // namespace Sanic
