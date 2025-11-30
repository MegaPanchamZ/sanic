/**
 * ScriptingSystem.cpp
 * 
 * C# scripting integration using .NET CoreCLR hosting.
 * Embeds the .NET 8 runtime for compiled C# script execution.
 * 
 * References:
 * - https://learn.microsoft.com/en-us/dotnet/core/tutorials/netcore-hosting
 * - https://github.com/dotnet/runtime/blob/main/docs/design/features/native-hosting.md
 */

#include "ScriptingSystem.h"
#include "ECS.h"
// #include "PhysicsSystem.h"
// #include "Renderer.h"

#include <fstream>
#include <sstream>
#include <filesystem>
#include <chrono>
#include <cstring>

#ifdef _WIN32
#include <Windows.h>
#define HOSTFXR_CALLTYPE __cdecl
typedef wchar_t char_t;
#define STR(s) L##s
#else
#include <dlfcn.h>
#define HOSTFXR_CALLTYPE
typedef char char_t;
#define STR(s) s
#endif

// hostfxr types (hostfxr_handle defined in header)
enum hostfxr_delegate_type {
    hdt_com_activation,
    hdt_load_in_memory_assembly,
    hdt_winrt_activation,
    hdt_com_register,
    hdt_com_unregister,
    hdt_load_assembly_and_get_function_pointer,
    hdt_get_function_pointer
};

struct hostfxr_initialize_parameters {
    size_t size;
    const char_t* host_path;
    const char_t* dotnet_root;
};

typedef void (HOSTFXR_CALLTYPE *hostfxr_error_writer_fn)(const char_t* message);

// .NET hosting types
typedef int32_t (HOSTFXR_CALLTYPE *hostfxr_initialize_for_runtime_config_fn)(
    const char_t* runtime_config_path,
    const hostfxr_initialize_parameters* parameters,
    hostfxr_handle* host_context_handle);

typedef int32_t (HOSTFXR_CALLTYPE *hostfxr_get_runtime_delegate_fn)(
    const hostfxr_handle host_context_handle,
    hostfxr_delegate_type type,
    void** delegate);

typedef int32_t (HOSTFXR_CALLTYPE *hostfxr_close_fn)(const hostfxr_handle host_context_handle);

typedef int32_t (HOSTFXR_CALLTYPE *hostfxr_set_error_writer_fn)(hostfxr_error_writer_fn error_writer);

// Delegate types for managed interop
typedef void* (HOSTFXR_CALLTYPE *create_instance_fn)(const char_t* assembly_path, const char_t* type_name, uint32_t entity_id, void** method_ptrs);
typedef void (HOSTFXR_CALLTYPE *destroy_instance_fn)(void* gc_handle);
typedef void (HOSTFXR_CALLTYPE *invoke_void_fn)(void* gc_handle, void* method_ptr);
typedef void (HOSTFXR_CALLTYPE *invoke_float_fn)(void* gc_handle, void* method_ptr, float value);
typedef void (HOSTFXR_CALLTYPE *invoke_collision_fn)(void* gc_handle, void* method_ptr, uint32_t other_entity, const float* contact, const float* normal);
typedef size_t (HOSTFXR_CALLTYPE *get_memory_usage_fn)();
typedef void (HOSTFXR_CALLTYPE *force_gc_fn)(int generation);

namespace Sanic {

// Static member definitions
ECS* ScriptingSystem::sEcs_ = nullptr;
PhysicsSystem* ScriptingSystem::sPhysics_ = nullptr;
Renderer* ScriptingSystem::sRenderer_ = nullptr;
ScriptingSystem* ScriptingSystem::sInstance_ = nullptr;

// hostfxr function pointers
static hostfxr_initialize_for_runtime_config_fn hostfxr_initialize = nullptr;
static hostfxr_get_runtime_delegate_fn hostfxr_get_delegate = nullptr;
static hostfxr_close_fn hostfxr_close = nullptr;
static hostfxr_set_error_writer_fn hostfxr_set_error_writer = nullptr;

// Platform-specific library loading
#ifdef _WIN32
static HMODULE hostfxrLibrary = nullptr;

static bool loadHostFxr(const std::string& dotnetRoot) {
    std::string hostfxrPath = dotnetRoot + "\\host\\fxr\\8.0.0\\hostfxr.dll";
    
    // Try common .NET installation paths
    std::vector<std::string> paths = {
        hostfxrPath,
        "C:\\Program Files\\dotnet\\host\\fxr\\8.0.0\\hostfxr.dll",
        "C:\\Program Files\\dotnet\\host\\fxr\\7.0.0\\hostfxr.dll",
        "C:\\Program Files (x86)\\dotnet\\host\\fxr\\8.0.0\\hostfxr.dll"
    };
    
    for (const auto& path : paths) {
        hostfxrLibrary = LoadLibraryA(path.c_str());
        if (hostfxrLibrary) break;
    }
    
    if (!hostfxrLibrary) {
        // Try to find via registry or PATH
        hostfxrLibrary = LoadLibraryA("hostfxr.dll");
    }
    
    if (!hostfxrLibrary) {
        fprintf(stderr, "[ScriptingSystem] Failed to load hostfxr.dll\n");
        return false;
    }
    
    hostfxr_initialize = (hostfxr_initialize_for_runtime_config_fn)GetProcAddress(hostfxrLibrary, "hostfxr_initialize_for_runtime_config");
    hostfxr_get_delegate = (hostfxr_get_runtime_delegate_fn)GetProcAddress(hostfxrLibrary, "hostfxr_get_runtime_delegate");
    hostfxr_close = (hostfxr_close_fn)GetProcAddress(hostfxrLibrary, "hostfxr_close");
    hostfxr_set_error_writer = (hostfxr_set_error_writer_fn)GetProcAddress(hostfxrLibrary, "hostfxr_set_error_writer");
    
    return hostfxr_initialize && hostfxr_get_delegate && hostfxr_close;
}

static void unloadHostFxr() {
    if (hostfxrLibrary) {
        FreeLibrary(hostfxrLibrary);
        hostfxrLibrary = nullptr;
    }
}
#else
static void* hostfxrLibrary = nullptr;

static bool loadHostFxr(const std::string& dotnetRoot) {
    std::string hostfxrPath = dotnetRoot + "/host/fxr/8.0.0/libhostfxr.so";
    hostfxrLibrary = dlopen(hostfxrPath.c_str(), RTLD_NOW);
    
    if (!hostfxrLibrary) {
        hostfxrLibrary = dlopen("libhostfxr.so", RTLD_NOW);
    }
    
    if (!hostfxrLibrary) {
        fprintf(stderr, "[ScriptingSystem] Failed to load libhostfxr.so: %s\n", dlerror());
        return false;
    }
    
    hostfxr_initialize = (hostfxr_initialize_for_runtime_config_fn)dlsym(hostfxrLibrary, "hostfxr_initialize_for_runtime_config");
    hostfxr_get_delegate = (hostfxr_get_runtime_delegate_fn)dlsym(hostfxrLibrary, "hostfxr_get_runtime_delegate");
    hostfxr_close = (hostfxr_close_fn)dlsym(hostfxrLibrary, "hostfxr_close");
    hostfxr_set_error_writer = (hostfxr_set_error_writer_fn)dlsym(hostfxrLibrary, "hostfxr_set_error_writer");
    
    return hostfxr_initialize && hostfxr_get_delegate && hostfxr_close;
}

static void unloadHostFxr() {
    if (hostfxrLibrary) {
        dlclose(hostfxrLibrary);
        hostfxrLibrary = nullptr;
    }
}
#endif

// Error writer callback
static void hostfxrErrorWriter(const char_t* message) {
#ifdef _WIN32
    fwprintf(stderr, L"[.NET Error] %s\n", message);
#else
    fprintf(stderr, "[.NET Error] %s\n", message);
#endif
}

ScriptingSystem::~ScriptingSystem() {
    shutdown();
}

bool ScriptingSystem::initialize(const ScriptingConfig& config) {
    if (initialized_) {
        return true;
    }
    
    config_ = config;
    sInstance_ = this;
    
    // Initialize hostfxr
    if (!initializeHostFxr()) {
        lastError_.message = "Failed to initialize .NET runtime host";
        return false;
    }
    
    // Load core scripting assembly
    if (!loadCoreAssembly()) {
        lastError_.message = "Failed to load core scripting assembly";
        return false;
    }
    
    // Register native callbacks with managed code
    registerNativeCallbacks();
    
    initialized_ = true;
    printf("[ScriptingSystem] .NET CoreCLR initialized successfully\n");
    return true;
}

void ScriptingSystem::shutdown() {
    if (!initialized_) {
        return;
    }
    
    // Destroy all managed instances
    std::lock_guard<std::mutex> lock(instancesMutex_);
    for (auto& [id, instance] : instances_) {
        destroyManagedInstance(instance);
    }
    instances_.clear();
    entityToInstances_.clear();
    
    // Close hostfxr context
    if (hostfxrHandle_ && hostfxr_close) {
        hostfxr_close(hostfxrHandle_);
        hostfxrHandle_ = nullptr;
    }
    
    // Unload hostfxr library
    unloadHostFxr();
    
    loadAssemblyFn_ = nullptr;
    createInstanceDelegate_ = nullptr;
    destroyInstanceDelegate_ = nullptr;
    
    sInstance_ = nullptr;
    initialized_ = false;
    
    printf("[ScriptingSystem] .NET CoreCLR shutdown complete\n");
}

bool ScriptingSystem::initializeHostFxr() {
    // Load hostfxr library
    std::string dotnetRoot = "";
    
    // Try to find .NET installation
    const char* dotnetRootEnv = std::getenv("DOTNET_ROOT");
    if (dotnetRootEnv) {
        dotnetRoot = dotnetRootEnv;
    }
    
    if (!loadHostFxr(dotnetRoot)) {
        return false;
    }
    
    // Set error writer
    if (hostfxr_set_error_writer) {
        hostfxr_set_error_writer(hostfxrErrorWriter);
    }
    
    // Find or create runtime config
    std::string runtimeConfigPath = config_.runtimeConfigPath;
    if (runtimeConfigPath.empty()) {
        runtimeConfigPath = config_.assembliesPath + "/Sanic.Scripting.runtimeconfig.json";
    }
    
    // Check if config exists, if not create a default one
    if (!std::filesystem::exists(runtimeConfigPath)) {
        // Create default runtime config
        std::filesystem::create_directories(std::filesystem::path(runtimeConfigPath).parent_path());
        std::ofstream configFile(runtimeConfigPath);
        configFile << R"({
  "runtimeOptions": {
    "tfm": "net8.0",
    "rollForward": "LatestMinor",
    "framework": {
      "name": "Microsoft.NETCore.App",
      "version": "8.0.0"
    }
  }
})";
        configFile.close();
    }
    
    // Initialize hostfxr
#ifdef _WIN32
    std::wstring configPathW(runtimeConfigPath.begin(), runtimeConfigPath.end());
    int32_t result = hostfxr_initialize(configPathW.c_str(), nullptr, &hostfxrHandle_);
#else
    int32_t result = hostfxr_initialize(runtimeConfigPath.c_str(), nullptr, &hostfxrHandle_);
#endif
    
    if (result != 0 || !hostfxrHandle_) {
        fprintf(stderr, "[ScriptingSystem] hostfxr_initialize failed: 0x%x\n", result);
        return false;
    }
    
    // Get load_assembly_and_get_function_pointer delegate
    result = hostfxr_get_delegate(hostfxrHandle_, hdt_load_assembly_and_get_function_pointer, &loadAssemblyFn_);
    if (result != 0 || !loadAssemblyFn_) {
        fprintf(stderr, "[ScriptingSystem] Failed to get load_assembly delegate: 0x%x\n", result);
        return false;
    }
    
    return true;
}

bool ScriptingSystem::loadCoreAssembly() {
    std::string coreAssemblyPath = config_.assembliesPath + "/" + config_.coreAssemblyName;
    
    // Check if assembly exists
    if (!std::filesystem::exists(coreAssemblyPath)) {
        fprintf(stderr, "[ScriptingSystem] Core assembly not found: %s\n", coreAssemblyPath.c_str());
        fprintf(stderr, "[ScriptingSystem] Note: You need to build Sanic.Scripting.dll from the C# project\n");
        // Don't fail - we can still initialize without scripts
        return true;
    }
    
    return loadAssembly(coreAssemblyPath);
}

void* ScriptingSystem::getExportedMethod(const std::string& assemblyPath, const std::string& typeName,
                                          const std::string& methodName, const std::string& delegateTypeName) {
    if (!loadAssemblyFn_) return nullptr;
    
    typedef int (HOSTFXR_CALLTYPE *load_assembly_fn)(
        const char_t* assembly_path,
        const char_t* type_name,
        const char_t* method_name,
        const char_t* delegate_type_name,
        void* reserved,
        void** delegate);
    
    load_assembly_fn loadFn = (load_assembly_fn)loadAssemblyFn_;
    void* methodPtr = nullptr;
    
#ifdef _WIN32
    std::wstring assemblyPathW(assemblyPath.begin(), assemblyPath.end());
    std::wstring typeNameW(typeName.begin(), typeName.end());
    std::wstring methodNameW(methodName.begin(), methodName.end());
    std::wstring delegateTypeNameW(delegateTypeName.begin(), delegateTypeName.end());
    
    int32_t result = loadFn(
        assemblyPathW.c_str(),
        typeNameW.c_str(),
        methodNameW.c_str(),
        delegateTypeName.empty() ? nullptr : delegateTypeNameW.c_str(),
        nullptr,
        &methodPtr);
#else
    int32_t result = loadFn(
        assemblyPath.c_str(),
        typeName.c_str(),
        methodName.c_str(),
        delegateTypeName.empty() ? nullptr : delegateTypeName.c_str(),
        nullptr,
        &methodPtr);
#endif
    
    if (result != 0) {
        fprintf(stderr, "[ScriptingSystem] Failed to get method %s.%s: 0x%x\n", 
                typeName.c_str(), methodName.c_str(), result);
        return nullptr;
    }
    
    return methodPtr;
}

bool ScriptingSystem::loadAssembly(const std::string& path) {
    if (loadedAssemblies_.count(path) > 0) {
        return true; // Already loaded
    }
    
    // Record assembly and timestamp
    try {
        auto modTime = std::filesystem::last_write_time(path);
        assemblyTimestamps_[path] = modTime.time_since_epoch().count();
    } catch (...) {}
    
    loadedAssemblies_[path] = nullptr; // Mark as loaded
    
    printf("[ScriptingSystem] Loaded assembly: %s\n", path.c_str());
    return true;
}

void ScriptingSystem::unloadAssembly(const std::string& path) {
    // Note: Full assembly unloading requires AssemblyLoadContext in .NET Core
    // For now, we just mark it as unloaded
    loadedAssemblies_.erase(path);
    assemblyTimestamps_.erase(path);
}

void ScriptingSystem::registerECS(ECS* ecs) {
    sEcs_ = ecs;
}

void ScriptingSystem::registerPhysics(PhysicsSystem* physics) {
    sPhysics_ = physics;
}

void ScriptingSystem::registerRenderer(Renderer* renderer) {
    sRenderer_ = renderer;
}

int ScriptingSystem::createScriptInstance(uint32_t entityId, const std::string& assemblyPath, const std::string& typeName) {
    if (!initialized_) return -1;
    
    std::lock_guard<std::mutex> lock(instancesMutex_);
    
    ScriptInstance instance;
    instance.instanceId = nextInstanceId_++;
    instance.entityId = entityId;
    instance.assemblyPath = assemblyPath;
    instance.typeName = typeName;
    
    // Load assembly if not already loaded
    if (!loadAssembly(assemblyPath)) {
        lastError_.message = "Failed to load assembly: " + assemblyPath;
        return -1;
    }
    
    // Create managed instance
    if (!createManagedInstance(instance)) {
        lastError_.message = "Failed to create managed instance for: " + typeName;
        return -1;
    }
    
    instances_[instance.instanceId] = std::move(instance);
    entityToInstances_[entityId].push_back(instance.instanceId);
    
    return instance.instanceId;
}

bool ScriptingSystem::createManagedInstance(ScriptInstance& instance) {
    if (!createInstanceDelegate_) {
        // Try to get the delegate from core assembly
        std::string coreAssemblyPath = config_.assembliesPath + "/" + config_.coreAssemblyName;
        createInstanceDelegate_ = getExportedMethod(
            coreAssemblyPath,
            "Sanic.Scripting.ScriptHost, Sanic.Scripting",
            "CreateInstance",
            "Sanic.Scripting.CreateInstanceDelegate, Sanic.Scripting"
        );
    }
    
    if (!createInstanceDelegate_) {
        // Can't create managed instances without the core assembly
        fprintf(stderr, "[ScriptingSystem] CreateInstance delegate not available\n");
        return false;
    }
    
    // Method pointers array to be filled by managed code
    void* methodPtrs[12] = {nullptr};
    
    create_instance_fn createFn = (create_instance_fn)createInstanceDelegate_;
    
#ifdef _WIN32
    std::wstring assemblyPathW(instance.assemblyPath.begin(), instance.assemblyPath.end());
    std::wstring typeNameW(instance.typeName.begin(), instance.typeName.end());
    instance.managedObject.gcHandle = createFn(assemblyPathW.c_str(), typeNameW.c_str(), instance.entityId, methodPtrs);
#else
    instance.managedObject.gcHandle = createFn(instance.assemblyPath.c_str(), instance.typeName.c_str(), instance.entityId, methodPtrs);
#endif
    
    if (!instance.managedObject.isValid()) {
        return false;
    }
    
    // Cache method pointers
    instance.awakePtr = methodPtrs[0];
    instance.startPtr = methodPtrs[1];
    instance.updatePtr = methodPtrs[2];
    instance.fixedUpdatePtr = methodPtrs[3];
    instance.lateUpdatePtr = methodPtrs[4];
    instance.onDestroyPtr = methodPtrs[5];
    instance.onEnablePtr = methodPtrs[6];
    instance.onDisablePtr = methodPtrs[7];
    instance.onCollisionEnterPtr = methodPtrs[8];
    instance.onCollisionExitPtr = methodPtrs[9];
    instance.onTriggerEnterPtr = methodPtrs[10];
    instance.onTriggerExitPtr = methodPtrs[11];
    
    // Set method flags
    if (instance.awakePtr) instance.methodFlags |= ScriptInstance::HAS_AWAKE;
    if (instance.startPtr) instance.methodFlags |= ScriptInstance::HAS_START;
    if (instance.updatePtr) instance.methodFlags |= ScriptInstance::HAS_UPDATE;
    if (instance.fixedUpdatePtr) instance.methodFlags |= ScriptInstance::HAS_FIXED_UPDATE;
    if (instance.lateUpdatePtr) instance.methodFlags |= ScriptInstance::HAS_LATE_UPDATE;
    if (instance.onDestroyPtr) instance.methodFlags |= ScriptInstance::HAS_ON_DESTROY;
    if (instance.onEnablePtr) instance.methodFlags |= ScriptInstance::HAS_ON_ENABLE;
    if (instance.onDisablePtr) instance.methodFlags |= ScriptInstance::HAS_ON_DISABLE;
    if (instance.onCollisionEnterPtr) instance.methodFlags |= ScriptInstance::HAS_ON_COLLISION_ENTER;
    if (instance.onCollisionExitPtr) instance.methodFlags |= ScriptInstance::HAS_ON_COLLISION_EXIT;
    if (instance.onTriggerEnterPtr) instance.methodFlags |= ScriptInstance::HAS_ON_TRIGGER_ENTER;
    if (instance.onTriggerExitPtr) instance.methodFlags |= ScriptInstance::HAS_ON_TRIGGER_EXIT;
    
    instance.valid = true;
    
    // Record timestamp for hot reload
    try {
        auto modTime = std::filesystem::last_write_time(instance.assemblyPath);
        instance.assemblyTimestamp = modTime.time_since_epoch().count();
    } catch (...) {}
    
    return true;
}

void ScriptingSystem::destroyManagedInstance(ScriptInstance& instance) {
    if (!instance.managedObject.isValid()) return;
    
    // Call OnDestroy if available
    if (instance.onDestroyPtr && (instance.methodFlags & ScriptInstance::HAS_ON_DESTROY)) {
        invokeLifecycleMethod(instance, instance.onDestroyPtr);
    }
    
    // Release GC handle
    if (destroyInstanceDelegate_) {
        destroy_instance_fn destroyFn = (destroy_instance_fn)destroyInstanceDelegate_;
        destroyFn(instance.managedObject.gcHandle);
    }
    
    instance.managedObject.gcHandle = nullptr;
    instance.valid = false;
}

void ScriptingSystem::destroyScriptInstance(int instanceId) {
    std::lock_guard<std::mutex> lock(instancesMutex_);
    
    auto it = instances_.find(instanceId);
    if (it == instances_.end()) return;
    
    ScriptInstance& instance = it->second;
    
    // Destroy managed instance
    destroyManagedInstance(instance);
    
    // Remove from entity mapping
    auto& entityInstances = entityToInstances_[instance.entityId];
    entityInstances.erase(
        std::remove(entityInstances.begin(), entityInstances.end(), instanceId),
        entityInstances.end()
    );
    
    instances_.erase(it);
}

ScriptInstance* ScriptingSystem::getScriptInstance(int instanceId) {
    auto it = instances_.find(instanceId);
    if (it != instances_.end()) {
        return &it->second;
    }
    return nullptr;
}

void ScriptingSystem::invokeLifecycleMethod(ScriptInstance& instance, void* methodPtr) {
    if (!methodPtr || !instance.managedObject.isValid()) return;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    if (invokeVoidDelegate_) {
        invoke_void_fn invokeFn = (invoke_void_fn)invokeVoidDelegate_;
        invokeFn(instance.managedObject.gcHandle, methodPtr);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double, std::milli>(end - start).count();
    
    totalCalls_++;
    totalCallTime_ += elapsed;
}

void ScriptingSystem::invokeLifecycleMethodWithDelta(ScriptInstance& instance, void* methodPtr, float deltaTime) {
    if (!methodPtr || !instance.managedObject.isValid()) return;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    if (invokeFloatDelegate_) {
        invoke_float_fn invokeFn = (invoke_float_fn)invokeFloatDelegate_;
        invokeFn(instance.managedObject.gcHandle, methodPtr, deltaTime);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double, std::milli>(end - start).count();
    
    totalCalls_++;
    totalCallTime_ += elapsed;
}

void ScriptingSystem::invokeCollisionMethod(ScriptInstance& instance, void* methodPtr, uint32_t otherEntity,
                                             const glm::vec3* contactPoint, const glm::vec3* normal) {
    if (!methodPtr || !instance.managedObject.isValid()) return;
    
    invoke_collision_fn invokeFn = (invoke_collision_fn)invokeVoidDelegate_;  // Would need separate delegate
    if (invokeFn) {
        const float* contact = contactPoint ? &contactPoint->x : nullptr;
        const float* norm = normal ? &normal->x : nullptr;
        invokeFn(instance.managedObject.gcHandle, methodPtr, otherEntity, contact, norm);
    }
}

void ScriptingSystem::awakeAll() {
    if (!globalEnabled_) return;
    
    std::lock_guard<std::mutex> lock(instancesMutex_);
    
    for (auto& [id, instance] : instances_) {
        if (instance.valid && !instance.hasAwoken && 
            (instance.methodFlags & ScriptInstance::HAS_AWAKE)) {
            invokeLifecycleMethod(instance, instance.awakePtr);
            instance.hasAwoken = true;
        }
    }
}

void ScriptingSystem::startAll() {
    if (!globalEnabled_) return;
    
    std::lock_guard<std::mutex> lock(instancesMutex_);
    
    for (auto& [id, instance] : instances_) {
        if (instance.valid && instance.hasAwoken && !instance.hasStarted && 
            (instance.methodFlags & ScriptInstance::HAS_START)) {
            invokeLifecycleMethod(instance, instance.startPtr);
            instance.hasStarted = true;
        }
    }
}

void ScriptingSystem::update(float deltaTime) {
    if (!globalEnabled_) return;
    
    std::lock_guard<std::mutex> lock(instancesMutex_);
    
    for (auto& [id, instance] : instances_) {
        if (instance.valid && instance.hasStarted && 
            (instance.methodFlags & ScriptInstance::HAS_UPDATE)) {
            invokeLifecycleMethodWithDelta(instance, instance.updatePtr, deltaTime);
        }
    }
}

void ScriptingSystem::fixedUpdate(float fixedDeltaTime) {
    if (!globalEnabled_) return;
    
    std::lock_guard<std::mutex> lock(instancesMutex_);
    
    for (auto& [id, instance] : instances_) {
        if (instance.valid && instance.hasStarted && 
            (instance.methodFlags & ScriptInstance::HAS_FIXED_UPDATE)) {
            invokeLifecycleMethodWithDelta(instance, instance.fixedUpdatePtr, fixedDeltaTime);
        }
    }
}

void ScriptingSystem::lateUpdate(float deltaTime) {
    if (!globalEnabled_) return;
    
    std::lock_guard<std::mutex> lock(instancesMutex_);
    
    for (auto& [id, instance] : instances_) {
        if (instance.valid && instance.hasStarted && 
            (instance.methodFlags & ScriptInstance::HAS_LATE_UPDATE)) {
            invokeLifecycleMethodWithDelta(instance, instance.lateUpdatePtr, deltaTime);
        }
    }
}

void ScriptingSystem::sendCollisionEnter(uint32_t entityA, uint32_t entityB, 
                                          const glm::vec3& contactPoint, const glm::vec3& normal) {
    auto it = entityToInstances_.find(entityA);
    if (it == entityToInstances_.end()) return;
    
    for (int instanceId : it->second) {
        auto instIt = instances_.find(instanceId);
        if (instIt != instances_.end() && 
            (instIt->second.methodFlags & ScriptInstance::HAS_ON_COLLISION_ENTER)) {
            invokeCollisionMethod(instIt->second, instIt->second.onCollisionEnterPtr, 
                                  entityB, &contactPoint, &normal);
        }
    }
}

void ScriptingSystem::sendCollisionExit(uint32_t entityA, uint32_t entityB) {
    auto it = entityToInstances_.find(entityA);
    if (it == entityToInstances_.end()) return;
    
    for (int instanceId : it->second) {
        auto instIt = instances_.find(instanceId);
        if (instIt != instances_.end() && 
            (instIt->second.methodFlags & ScriptInstance::HAS_ON_COLLISION_EXIT)) {
            invokeCollisionMethod(instIt->second, instIt->second.onCollisionExitPtr, entityB, nullptr, nullptr);
        }
    }
}

void ScriptingSystem::sendTriggerEnter(uint32_t entityA, uint32_t entityB) {
    auto it = entityToInstances_.find(entityA);
    if (it == entityToInstances_.end()) return;
    
    for (int instanceId : it->second) {
        auto instIt = instances_.find(instanceId);
        if (instIt != instances_.end() && 
            (instIt->second.methodFlags & ScriptInstance::HAS_ON_TRIGGER_ENTER)) {
            invokeCollisionMethod(instIt->second, instIt->second.onTriggerEnterPtr, entityB, nullptr, nullptr);
        }
    }
}

void ScriptingSystem::sendTriggerExit(uint32_t entityA, uint32_t entityB) {
    auto it = entityToInstances_.find(entityA);
    if (it == entityToInstances_.end()) return;
    
    for (int instanceId : it->second) {
        auto instIt = instances_.find(instanceId);
        if (instIt != instances_.end() && 
            (instIt->second.methodFlags & ScriptInstance::HAS_ON_TRIGGER_EXIT)) {
            invokeCollisionMethod(instIt->second, instIt->second.onTriggerExitPtr, entityB, nullptr, nullptr);
        }
    }
}

void ScriptingSystem::checkForChanges() {
    if (!config_.enableHotReload) return;
    
    for (auto& [path, lastTime] : assemblyTimestamps_) {
        try {
            auto modTime = std::filesystem::last_write_time(path);
            uint64_t newTime = modTime.time_since_epoch().count();
            
            if (newTime > lastTime) {
                reloadAssembly(path);
                assemblyTimestamps_[path] = newTime;
            }
        } catch (...) {}
    }
}

void ScriptingSystem::reloadAssembly(const std::string& assemblyPath) {
    printf("[ScriptingSystem] Hot reloading assembly: %s\n", assemblyPath.c_str());
    
    std::lock_guard<std::mutex> lock(instancesMutex_);
    
    // Find all instances using this assembly
    std::vector<std::pair<uint32_t, std::string>> toReload;
    for (auto& [id, instance] : instances_) {
        if (instance.assemblyPath == assemblyPath) {
            toReload.push_back({instance.entityId, instance.typeName});
        }
    }
    
    // Unload assembly
    unloadAssembly(assemblyPath);
    
    // Reload assembly
    loadAssembly(assemblyPath);
    
    // Recreate instances (simplified - in production would preserve state)
    for (auto& [entityId, typeName] : toReload) {
        createScriptInstance(entityId, assemblyPath, typeName);
    }
}

void ScriptingSystem::reloadAll() {
    std::vector<std::string> assemblies;
    for (auto& [path, time] : assemblyTimestamps_) {
        assemblies.push_back(path);
    }
    for (auto& path : assemblies) {
        reloadAssembly(path);
    }
}

bool ScriptingSystem::executeCode(const std::string& csharpCode, std::string& output) {
    // This would require Roslyn compilation at runtime
    // For now, return false indicating not supported
    output = "Runtime C# compilation not yet implemented. Use compiled assemblies.";
    return false;
}

size_t ScriptingSystem::getMemoryUsage() const {
    if (getStatisticsDelegate_) {
        get_memory_usage_fn getFn = (get_memory_usage_fn)getStatisticsDelegate_;
        return getFn();
    }
    return 0;
}

void ScriptingSystem::forceGC(int generation) {
    if (forceGCDelegate_) {
        force_gc_fn gcFn = (force_gc_fn)forceGCDelegate_;
        gcFn(generation);
    }
}

ScriptingSystem::Statistics ScriptingSystem::getStatistics() const {
    Statistics stats{};
    stats.instanceCount = instances_.size();
    stats.managedHeapSize = getMemoryUsage();
    stats.gen0Collections = 0;  // Would need CLR profiling API
    stats.gen1Collections = 0;
    stats.gen2Collections = 0;
    stats.totalMethodCalls = totalCalls_.load();
    stats.averageCallTimeMs = totalCalls_ > 0 ? totalCallTime_ / totalCalls_ : 0.0;
    stats.loadedAssemblies = loadedAssemblies_.size();
    return stats;
}

std::vector<std::string> ScriptingSystem::getLoadedAssemblies() const {
    std::vector<std::string> result;
    for (auto& [path, handle] : loadedAssemblies_) {
        result.push_back(path);
    }
    return result;
}

void ScriptingSystem::registerNativeCallbacks() {
    // Register native function pointers with the managed side
    // This allows C# code to call back into native code
    
    // Would call into managed code to register these callbacks:
    // - nativeLog
    // - nativeGetTransform / nativeSetTransform
    // - nativeRaycast
    // - nativeGetKey / nativeGetKeyDown / nativeGetKeyUp
    // - nativeGetMousePosition / nativeGetMouseDelta
    // - nativeAddForce / nativeSetVelocity
    // - nativeDrawLine / nativeDrawSphere
    // - nativeInstantiate / nativeDestroy
    
    printf("[ScriptingSystem] Native callbacks registered\n");
}

// Native callback implementations (called from C#)
void ScriptingSystem::nativeLog(int level, const char* message) {
    const char* prefix = "[Script]";
    if (level == 1) prefix = "[Script WARN]";
    else if (level == 2) prefix = "[Script ERROR]";
    
    if (level == 2) {
        fprintf(stderr, "%s %s\n", prefix, message);
    } else {
        printf("%s %s\n", prefix, message);
    }
}

void ScriptingSystem::nativeGetTransform(uint32_t entityId, float* outMatrix) {
    if (!sEcs_ || !outMatrix) return;
    // TODO: Get transform from ECS and fill matrix
    // For now, return identity
    memset(outMatrix, 0, sizeof(float) * 16);
    outMatrix[0] = outMatrix[5] = outMatrix[10] = outMatrix[15] = 1.0f;
}

void ScriptingSystem::nativeSetTransform(uint32_t entityId, const float* matrix) {
    if (!sEcs_ || !matrix) return;
    // TODO: Set transform in ECS
}

bool ScriptingSystem::nativeRaycast(const float* origin, const float* direction, float maxDist,
                                     uint32_t* hitEntity, float* hitPoint, float* hitNormal) {
    if (!sPhysics_) return false;
    // TODO: Perform raycast through physics system
    return false;
}

bool ScriptingSystem::nativeGetKey(const char* keyName) {
    // TODO: Query input system
    return false;
}

bool ScriptingSystem::nativeGetKeyDown(const char* keyName) {
    // TODO: Query input system
    return false;
}

bool ScriptingSystem::nativeGetKeyUp(const char* keyName) {
    // TODO: Query input system
    return false;
}

void ScriptingSystem::nativeGetMousePosition(float* x, float* y) {
    if (x) *x = 0.0f;
    if (y) *y = 0.0f;
    // TODO: Query input system
}

void ScriptingSystem::nativeGetMouseDelta(float* dx, float* dy) {
    if (dx) *dx = 0.0f;
    if (dy) *dy = 0.0f;
    // TODO: Query input system
}

void ScriptingSystem::nativeAddForce(uint32_t entityId, const float* force, int forceMode) {
    if (!sPhysics_) return;
    // TODO: Add force through physics system
}

void ScriptingSystem::nativeSetVelocity(uint32_t entityId, const float* velocity) {
    if (!sPhysics_) return;
    // TODO: Set velocity through physics system
}

void ScriptingSystem::nativeDrawLine(const float* from, const float* to, uint32_t color, float duration) {
    if (!sRenderer_) return;
    // TODO: Add to debug draw queue
}

void ScriptingSystem::nativeDrawSphere(const float* center, float radius, uint32_t color, float duration) {
    if (!sRenderer_) return;
    // TODO: Add to debug draw queue
}

void ScriptingSystem::nativeInstantiate(const char* prefabPath, const float* position, 
                                         const float* rotation, uint32_t* outEntity) {
    if (!sEcs_ || !outEntity) return;
    // TODO: Instantiate prefab through ECS
    *outEntity = 0;
}

void ScriptingSystem::nativeDestroy(uint32_t entityId) {
    if (!sEcs_) return;
    // TODO: Mark entity for destruction in ECS
}

} // namespace Sanic
