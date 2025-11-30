# Developer 2: Shader System & Material Editor

## Assigned Tasks
- **Task 14 (Main)**: Shader System - Caching, Includes, Permutations
- **Task 15**: Material System - Visual Editor, Node Graph, Shader Generation

## Overview
Your responsibility is to build a production-ready shader compilation system with caching and hot-reload, then create a node-based material editor that generates optimized shaders.

---

## Task 14: Advanced Shader System

### Current State Analysis

The existing `ShaderCompiler` in Sanic has these limitations:
1. Spawns `glslc.exe` as subprocess (slow)
2. No caching (recompiles every run)
3. No `#include` support
4. No shader permutations
5. No reflection
6. No hot-reload

### Target Architecture

```
src/engine/shaders/
├── ShaderCache.h              # Disk/memory cache
├── ShaderCache.cpp
├── ShaderCompiler.h           # Enhanced compiler (use shaderc lib)
├── ShaderCompiler.cpp
├── ShaderIncluder.h           # Virtual file system for includes
├── ShaderIncluder.cpp
├── ShaderPermutation.h        # Variant system
├── ShaderPermutation.cpp
├── ShaderReflection.h         # SPIRV-Reflect integration
├── ShaderReflection.cpp
├── ShaderHotReload.h          # File watcher + reload
├── ShaderHotReload.cpp
└── ShaderLibrary.h            # Shader asset management
```

---

### Step 1: Shader Cache System

Create `src/engine/shaders/ShaderCache.h`:

```cpp
#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <filesystem>
#include <optional>

namespace Sanic {

// Unique identifier for a compiled shader variant
struct ShaderCacheKey {
    uint64_t sourceHash;           // Hash of source code + includes
    uint64_t definesHash;          // Hash of preprocessor defines
    uint32_t shaderStage;          // Vertex, Fragment, etc.
    uint32_t compilerVersion;      // For invalidation on compiler updates
    
    bool operator==(const ShaderCacheKey& other) const;
};

struct ShaderCacheKeyHash {
    size_t operator()(const ShaderCacheKey& key) const;
};

// Cached shader entry
struct ShaderCacheEntry {
    std::vector<uint32_t> spirv;
    uint64_t timestamp;            // When compiled
    std::string entryPoint;
    
    // Reflection data (cached)
    struct ReflectedBinding {
        uint32_t set;
        uint32_t binding;
        uint32_t descriptorType;   // VkDescriptorType equivalent
        uint32_t count;
        std::string name;
    };
    std::vector<ReflectedBinding> bindings;
    
    struct ReflectedPushConstant {
        uint32_t offset;
        uint32_t size;
    };
    std::vector<ReflectedPushConstant> pushConstants;
};

class ShaderCache {
public:
    ShaderCache();
    ~ShaderCache();
    
    // Initialize with cache directory
    bool initialize(const std::filesystem::path& cacheDir);
    
    // Lookup compiled shader
    std::optional<ShaderCacheEntry> lookup(const ShaderCacheKey& key);
    
    // Store compiled shader
    void store(const ShaderCacheKey& key, const ShaderCacheEntry& entry);
    
    // Invalidate entries (e.g., when source file changes)
    void invalidate(uint64_t sourceHash);
    void invalidateAll();
    
    // Persistence
    bool loadFromDisk();
    bool saveToDisk();
    
    // Statistics
    struct Stats {
        uint32_t hits;
        uint32_t misses;
        uint32_t entriesInMemory;
        uint64_t totalSpirvBytes;
    };
    Stats getStats() const;
    
private:
    std::filesystem::path cacheDir_;
    std::unordered_map<ShaderCacheKey, ShaderCacheEntry, ShaderCacheKeyHash> cache_;
    mutable std::mutex mutex_;
    
    Stats stats_;
    
    // File format helpers
    bool readCacheFile(const std::filesystem::path& path, ShaderCacheEntry& entry);
    bool writeCacheFile(const std::filesystem::path& path, const ShaderCacheEntry& entry);
    std::filesystem::path getCacheFilePath(const ShaderCacheKey& key);
};

// Global shader cache instance
ShaderCache& GetShaderCache();

} // namespace Sanic
```

### Step 2: Include Handler

Create `src/engine/shaders/ShaderIncluder.h`:

```cpp
#pragma once
#include <shaderc/shaderc.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>

namespace Sanic {

// Virtual file system for shader includes
class ShaderIncluder : public shaderc::CompileOptions::IncluderInterface {
public:
    ShaderIncluder();
    ~ShaderIncluder() override;
    
    // Add search paths for includes
    void addIncludePath(const std::filesystem::path& path);
    
    // Register virtual files (for generated code)
    void registerVirtualFile(const std::string& name, const std::string& content);
    
    // Get all files that were included (for cache invalidation)
    const std::unordered_set<std::string>& getIncludedFiles() const { return includedFiles_; }
    
    // Compute hash of all included files
    uint64_t computeIncludesHash() const;
    
    // Clear included files tracking (call before new compilation)
    void resetTracking();
    
    // shaderc interface
    shaderc_include_result* GetInclude(
        const char* requested_source,
        shaderc_include_type type,
        const char* requesting_source,
        size_t include_depth) override;
    
    void ReleaseInclude(shaderc_include_result* data) override;
    
private:
    std::vector<std::filesystem::path> includePaths_;
    std::unordered_map<std::string, std::string> virtualFiles_;
    std::unordered_set<std::string> includedFiles_;
    
    // Memory management for include results
    struct IncludeData {
        std::string content;
        std::string sourceName;
    };
    std::vector<std::unique_ptr<IncludeData>> includeDataPool_;
    
    std::optional<std::string> resolveInclude(
        const std::string& requested,
        const std::string& requesting,
        shaderc_include_type type);
};

} // namespace Sanic
```

### Step 3: Shader Permutation System

Create `src/engine/shaders/ShaderPermutation.h`:

```cpp
#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <variant>
#include <functional>

namespace Sanic {

// A single permutation dimension
struct PermutationDimension {
    std::string name;              // e.g., "USE_NORMAL_MAP"
    
    // Values this dimension can take
    std::variant<
        std::vector<bool>,         // Boolean: true/false
        std::vector<int32_t>,      // Integer: 0, 1, 2, 3...
        std::vector<std::string>   // String: "LOW", "MEDIUM", "HIGH"
    > values;
    
    // Default value index
    uint32_t defaultIndex = 0;
};

// A specific permutation (combination of dimension values)
struct PermutationKey {
    std::unordered_map<std::string, uint32_t> dimensionValues;  // dimension name → value index
    
    bool operator==(const PermutationKey& other) const;
    uint64_t hash() const;
};

struct PermutationKeyHash {
    size_t operator()(const PermutationKey& key) const;
};

// Shader with permutations
class ShaderPermutationSet {
public:
    ShaderPermutationSet(const std::string& name);
    
    // Define permutation dimensions
    void addBoolDimension(const std::string& name, bool defaultValue = false);
    void addIntDimension(const std::string& name, const std::vector<int32_t>& values, int32_t defaultValue = 0);
    void addEnumDimension(const std::string& name, const std::vector<std::string>& values, const std::string& defaultValue);
    
    // Set source code
    void setSource(const std::string& source);
    void setSourceFile(const std::string& path);
    
    // Generate defines for a permutation
    std::vector<std::pair<std::string, std::string>> getDefines(const PermutationKey& key) const;
    
    // Get all valid permutation keys
    std::vector<PermutationKey> getAllPermutations() const;
    
    // Get count of total permutations
    uint32_t getPermutationCount() const;
    
    // Filter out invalid permutations (e.g., mutually exclusive options)
    using PermutationFilter = std::function<bool(const PermutationKey&)>;
    void setFilter(PermutationFilter filter);
    
    // Compile all permutations (for pre-warming cache)
    void compileAll(class ShaderCompiler& compiler);
    
    const std::string& getName() const { return name_; }
    const std::string& getSource() const { return source_; }
    
private:
    std::string name_;
    std::string source_;
    std::vector<PermutationDimension> dimensions_;
    PermutationFilter filter_;
};

// Shader permutation manager
class ShaderPermutationManager {
public:
    static ShaderPermutationManager& getInstance();
    
    // Register a permutation set
    void registerShader(std::shared_ptr<ShaderPermutationSet> shader);
    
    // Get a shader permutation set by name
    ShaderPermutationSet* getShader(const std::string& name);
    
    // Pre-compile all registered shaders
    void precompileAll(class ShaderCompiler& compiler);
    
private:
    ShaderPermutationManager() = default;
    std::unordered_map<std::string, std::shared_ptr<ShaderPermutationSet>> shaders_;
};

} // namespace Sanic
```

### Step 4: Enhanced Shader Compiler

Update `src/engine/shaders/ShaderCompiler.h`:

```cpp
#pragma once
#include <shaderc/shaderc.hpp>
#include "ShaderCache.h"
#include "ShaderIncluder.h"
#include "ShaderPermutation.h"
#include <string>
#include <vector>
#include <memory>
#include <optional>

namespace Sanic {

enum class ShaderStage {
    Vertex,
    Fragment,
    Compute,
    Geometry,
    TessControl,
    TessEvaluation,
    Task,
    Mesh,
    RayGen,
    Miss,
    ClosestHit,
    AnyHit,
    Intersection
};

struct ShaderCompileOptions {
    ShaderStage stage;
    std::string entryPoint = "main";
    std::string sourceName = "shader";
    
    // Preprocessor defines
    std::vector<std::pair<std::string, std::string>> defines;
    
    // Include paths (in addition to defaults)
    std::vector<std::string> includePaths;
    
    // Optimization
    enum class OptLevel { None, Size, Performance };
    OptLevel optimization = OptLevel::Performance;
    
    // Debug info
    bool generateDebugInfo = false;
    
    // Target environment
    uint32_t vulkanVersion = VK_API_VERSION_1_3;
    uint32_t spirvVersion = 0x10600;  // SPIR-V 1.6
};

struct ShaderCompileResult {
    bool success = false;
    std::vector<uint32_t> spirv;
    std::string errors;
    std::string warnings;
    
    // Reflection data
    struct Binding {
        uint32_t set;
        uint32_t binding;
        uint32_t descriptorType;
        uint32_t count;
        std::string name;
        uint32_t size;  // For uniform buffers
    };
    std::vector<Binding> bindings;
    
    struct PushConstant {
        uint32_t offset;
        uint32_t size;
        std::string name;
    };
    std::vector<PushConstant> pushConstants;
    
    struct VertexInput {
        uint32_t location;
        uint32_t format;  // VkFormat
        std::string name;
    };
    std::vector<VertexInput> vertexInputs;
    
    // Workgroup size (for compute shaders)
    uint32_t workgroupSize[3] = {1, 1, 1};
};

class ShaderCompiler {
public:
    ShaderCompiler();
    ~ShaderCompiler();
    
    // Initialize with default include paths
    bool initialize(const std::vector<std::string>& defaultIncludePaths = {});
    
    // Compile shader from source
    ShaderCompileResult compile(const std::string& source, const ShaderCompileOptions& options);
    
    // Compile shader from file
    ShaderCompileResult compileFile(const std::string& path, const ShaderCompileOptions& options);
    
    // Compile with permutation
    ShaderCompileResult compilePermutation(
        ShaderPermutationSet& permSet,
        const PermutationKey& permKey,
        const ShaderCompileOptions& baseOptions);
    
    // Get includer for custom configuration
    ShaderIncluder& getIncluder() { return *includer_; }
    
    // Cache control
    void enableCache(bool enable) { cacheEnabled_ = enable; }
    bool isCacheEnabled() const { return cacheEnabled_; }
    
    // Register virtual include file
    void registerVirtualFile(const std::string& name, const std::string& content);
    
private:
    shaderc::Compiler compiler_;
    std::unique_ptr<ShaderIncluder> includer_;
    bool cacheEnabled_ = true;
    
    shaderc_shader_kind toShadercKind(ShaderStage stage);
    void performReflection(const std::vector<uint32_t>& spirv, ShaderCompileResult& result);
};

} // namespace Sanic
```

### Step 5: Hot Reload System

Create `src/engine/shaders/ShaderHotReload.h`:

```cpp
#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <filesystem>

namespace Sanic {

class ShaderHotReload {
public:
    using ReloadCallback = std::function<void(const std::string& shaderPath)>;
    
    ShaderHotReload();
    ~ShaderHotReload();
    
    // Start watching directories
    bool start(const std::vector<std::filesystem::path>& watchPaths);
    void stop();
    
    // Register callback for shader reloads
    void onReload(ReloadCallback callback);
    
    // Manually trigger reload check
    void checkForChanges();
    
    // Get list of modified shaders since last check
    std::vector<std::string> getModifiedShaders();
    
private:
    void watchThread();
    
    std::vector<std::filesystem::path> watchPaths_;
    std::unordered_map<std::string, std::filesystem::file_time_type> fileTimestamps_;
    std::vector<ReloadCallback> callbacks_;
    
    std::thread watchThread_;
    std::atomic<bool> running_{false};
    std::mutex mutex_;
    
    std::vector<std::string> pendingReloads_;
};

// Global instance
ShaderHotReload& GetShaderHotReload();

} // namespace Sanic
```

---

## Task 15: Material System & Editor

### Material Graph Architecture

```
src/engine/materials/
├── MaterialGraph.h            # Node graph data structure
├── MaterialGraph.cpp
├── MaterialNode.h             # Base node class
├── MaterialNodes/             # Node implementations
│   ├── ConstantNodes.cpp      # Scalar, Vector, Color
│   ├── TextureNodes.cpp       # Sample, Coordinates
│   ├── MathNodes.cpp          # Add, Multiply, Lerp, etc.
│   ├── UtilityNodes.cpp       # Time, WorldPos, etc.
│   └── OutputNode.cpp         # PBR outputs
├── MaterialCompiler.h         # Graph → GLSL
├── MaterialCompiler.cpp
├── MaterialInstance.h         # Parameter overrides
├── MaterialInstance.cpp
└── editor/
    ├── MaterialEditor.h       # ImGui node editor
    ├── MaterialEditor.cpp
    ├── MaterialPreview.h      # Real-time preview
    └── MaterialPreview.cpp
```

### Step 1: Material Node System

Create `src/engine/materials/MaterialNode.h`:

```cpp
#pragma once
#include <string>
#include <vector>
#include <variant>
#include <memory>
#include <glm/glm.hpp>

namespace Sanic {

// Data types that can flow through the graph
enum class MaterialValueType {
    Float,
    Float2,
    Float3,
    Float4,
    Texture2D,
    TextureCube,
    Sampler,
    Matrix3,
    Matrix4,
};

// Pin (input/output connection point)
struct MaterialPin {
    std::string name;
    MaterialValueType type;
    bool isOutput;
    
    // Default value (for unconnected inputs)
    std::variant<
        float,
        glm::vec2,
        glm::vec3,
        glm::vec4,
        std::string  // Texture path
    > defaultValue;
    
    // Connection (only for inputs)
    struct Connection {
        uint32_t nodeId;
        uint32_t pinIndex;
    };
    std::optional<Connection> connection;
    
    uint32_t id;  // Unique within node
};

// Base node class
class MaterialNode {
public:
    virtual ~MaterialNode() = default;
    
    // Node info
    virtual std::string getName() const = 0;
    virtual std::string getCategory() const = 0;
    virtual glm::vec4 getColor() const { return glm::vec4(0.5f, 0.5f, 0.5f, 1.0f); }
    
    // Pins
    const std::vector<MaterialPin>& getInputs() const { return inputs_; }
    const std::vector<MaterialPin>& getOutputs() const { return outputs_; }
    
    // Code generation
    virtual std::string generateCode(class MaterialCompiler& compiler) const = 0;
    
    // Get output variable name for a pin
    std::string getOutputVar(uint32_t pinIndex) const;
    
    // Serialization
    virtual void serialize(class MaterialSerializer& s) const;
    virtual void deserialize(class MaterialSerializer& s);
    
    // Editor position
    glm::vec2 position = glm::vec2(0);
    
    // Unique ID
    uint32_t id = 0;
    
protected:
    void addInput(const std::string& name, MaterialValueType type);
    void addOutput(const std::string& name, MaterialValueType type);
    void setInputDefault(uint32_t index, float value);
    void setInputDefault(uint32_t index, const glm::vec4& value);
    
    std::vector<MaterialPin> inputs_;
    std::vector<MaterialPin> outputs_;
};

// Node factory
class MaterialNodeFactory {
public:
    using CreateFunc = std::function<std::unique_ptr<MaterialNode>()>;
    
    static MaterialNodeFactory& getInstance();
    
    void registerNode(const std::string& typeName, CreateFunc creator);
    std::unique_ptr<MaterialNode> create(const std::string& typeName);
    
    std::vector<std::string> getNodeTypes() const;
    std::vector<std::string> getNodeTypesInCategory(const std::string& category) const;
    
private:
    std::unordered_map<std::string, CreateFunc> creators_;
};

// Registration macro
#define REGISTER_MATERIAL_NODE(Type) \
    static bool _reg_##Type = []() { \
        MaterialNodeFactory::getInstance().registerNode(#Type, []() { \
            return std::make_unique<Type>(); \
        }); \
        return true; \
    }()

} // namespace Sanic
```

### Step 2: Common Material Nodes

Create `src/engine/materials/MaterialNodes/CommonNodes.h`:

```cpp
#pragma once
#include "../MaterialNode.h"

namespace Sanic {

// ============================================================================
// CONSTANT NODES
// ============================================================================

class ScalarNode : public MaterialNode {
public:
    ScalarNode();
    std::string getName() const override { return "Scalar"; }
    std::string getCategory() const override { return "Constants"; }
    std::string generateCode(MaterialCompiler& c) const override;
    
    float value = 0.0f;
};

class VectorNode : public MaterialNode {
public:
    VectorNode();
    std::string getName() const override { return "Vector"; }
    std::string getCategory() const override { return "Constants"; }
    std::string generateCode(MaterialCompiler& c) const override;
    
    glm::vec4 value = glm::vec4(0);
};

class ColorNode : public MaterialNode {
public:
    ColorNode();
    std::string getName() const override { return "Color"; }
    std::string getCategory() const override { return "Constants"; }
    glm::vec4 getColor() const override { return glm::vec4(0.8f, 0.2f, 0.2f, 1.0f); }
    std::string generateCode(MaterialCompiler& c) const override;
    
    glm::vec3 color = glm::vec3(1);
    float alpha = 1.0f;
};

// ============================================================================
// TEXTURE NODES
// ============================================================================

class TextureSampleNode : public MaterialNode {
public:
    TextureSampleNode();
    std::string getName() const override { return "Texture Sample"; }
    std::string getCategory() const override { return "Textures"; }
    glm::vec4 getColor() const override { return glm::vec4(0.2f, 0.6f, 0.2f, 1.0f); }
    std::string generateCode(MaterialCompiler& c) const override;
    
    std::string texturePath;  // If not connected
    uint32_t textureSlot = 0; // For bindless
};

class TexCoordNode : public MaterialNode {
public:
    TexCoordNode();
    std::string getName() const override { return "Texture Coordinates"; }
    std::string getCategory() const override { return "Textures"; }
    std::string generateCode(MaterialCompiler& c) const override;
    
    uint32_t uvChannel = 0;
};

// ============================================================================
// MATH NODES
// ============================================================================

class AddNode : public MaterialNode {
public:
    AddNode();
    std::string getName() const override { return "Add"; }
    std::string getCategory() const override { return "Math"; }
    std::string generateCode(MaterialCompiler& c) const override;
};

class MultiplyNode : public MaterialNode {
public:
    MultiplyNode();
    std::string getName() const override { return "Multiply"; }
    std::string getCategory() const override { return "Math"; }
    std::string generateCode(MaterialCompiler& c) const override;
};

class LerpNode : public MaterialNode {
public:
    LerpNode();
    std::string getName() const override { return "Lerp"; }
    std::string getCategory() const override { return "Math"; }
    std::string generateCode(MaterialCompiler& c) const override;
};

class ClampNode : public MaterialNode {
public:
    ClampNode();
    std::string getName() const override { return "Clamp"; }
    std::string getCategory() const override { return "Math"; }
    std::string generateCode(MaterialCompiler& c) const override;
};

class NormalizeNode : public MaterialNode {
public:
    NormalizeNode();
    std::string getName() const override { return "Normalize"; }
    std::string getCategory() const override { return "Math"; }
    std::string generateCode(MaterialCompiler& c) const override;
};

class DotProductNode : public MaterialNode {
public:
    DotProductNode();
    std::string getName() const override { return "Dot Product"; }
    std::string getCategory() const override { return "Math"; }
    std::string generateCode(MaterialCompiler& c) const override;
};

class PowerNode : public MaterialNode {
public:
    PowerNode();
    std::string getName() const override { return "Power"; }
    std::string getCategory() const override { return "Math"; }
    std::string generateCode(MaterialCompiler& c) const override;
};

class FresnelNode : public MaterialNode {
public:
    FresnelNode();
    std::string getName() const override { return "Fresnel"; }
    std::string getCategory() const override { return "Math"; }
    std::string generateCode(MaterialCompiler& c) const override;
};

// ============================================================================
// UTILITY NODES
// ============================================================================

class TimeNode : public MaterialNode {
public:
    TimeNode();
    std::string getName() const override { return "Time"; }
    std::string getCategory() const override { return "Utility"; }
    std::string generateCode(MaterialCompiler& c) const override;
};

class WorldPositionNode : public MaterialNode {
public:
    WorldPositionNode();
    std::string getName() const override { return "World Position"; }
    std::string getCategory() const override { return "Utility"; }
    std::string generateCode(MaterialCompiler& c) const override;
};

class WorldNormalNode : public MaterialNode {
public:
    WorldNormalNode();
    std::string getName() const override { return "World Normal"; }
    std::string getCategory() const override { return "Utility"; }
    std::string generateCode(MaterialCompiler& c) const override;
};

class ViewDirectionNode : public MaterialNode {
public:
    ViewDirectionNode();
    std::string getName() const override { return "View Direction"; }
    std::string getCategory() const override { return "Utility"; }
    std::string generateCode(MaterialCompiler& c) const override;
};

// ============================================================================
// OUTPUT NODE (special - one per material)
// ============================================================================

class MaterialOutputNode : public MaterialNode {
public:
    MaterialOutputNode();
    std::string getName() const override { return "Material Output"; }
    std::string getCategory() const override { return "Output"; }
    glm::vec4 getColor() const override { return glm::vec4(0.8f, 0.4f, 0.1f, 1.0f); }
    std::string generateCode(MaterialCompiler& c) const override;
    
    // PBR inputs are defined in constructor
};

} // namespace Sanic
```

### Step 3: Material Graph

Create `src/engine/materials/MaterialGraph.h`:

```cpp
#pragma once
#include "MaterialNode.h"
#include <vector>
#include <memory>
#include <unordered_map>

namespace Sanic {

class MaterialGraph {
public:
    MaterialGraph(const std::string& name = "New Material");
    
    // Node management
    MaterialNode* addNode(const std::string& typeName);
    MaterialNode* addNode(std::unique_ptr<MaterialNode> node);
    void removeNode(uint32_t nodeId);
    MaterialNode* getNode(uint32_t nodeId);
    const std::vector<std::unique_ptr<MaterialNode>>& getNodes() const { return nodes_; }
    
    // Connections
    bool connect(uint32_t srcNodeId, uint32_t srcPinIndex,
                uint32_t dstNodeId, uint32_t dstPinIndex);
    void disconnect(uint32_t nodeId, uint32_t pinIndex);
    void disconnectAll(uint32_t nodeId);
    
    // Output node (always exists)
    MaterialOutputNode* getOutputNode() { return outputNode_; }
    
    // Validation
    struct ValidationResult {
        bool valid = true;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
    };
    ValidationResult validate() const;
    
    // Topological sort (for code generation)
    std::vector<MaterialNode*> getExecutionOrder() const;
    
    // Serialization
    bool save(const std::string& path) const;
    bool load(const std::string& path);
    
    const std::string& getName() const { return name_; }
    void setName(const std::string& name) { name_ = name; }
    
private:
    std::string name_;
    std::vector<std::unique_ptr<MaterialNode>> nodes_;
    MaterialOutputNode* outputNode_ = nullptr;
    uint32_t nextNodeId_ = 1;
    
    void ensureOutputNode();
};

} // namespace Sanic
```

### Step 4: Material Compiler (Graph → GLSL)

Create `src/engine/materials/MaterialCompiler.h`:

```cpp
#pragma once
#include "MaterialGraph.h"
#include <string>
#include <sstream>
#include <unordered_set>

namespace Sanic {

struct CompiledMaterial {
    std::string fragmentShaderCode;
    std::string vertexShaderCode;  // Usually standard, but can have WPO
    
    // Required textures
    struct TextureSlot {
        uint32_t binding;
        std::string name;
        std::string defaultPath;
    };
    std::vector<TextureSlot> textures;
    
    // Uniform buffer layout
    struct UniformField {
        std::string name;
        MaterialValueType type;
        uint32_t offset;
        std::variant<float, glm::vec2, glm::vec3, glm::vec4> defaultValue;
    };
    std::vector<UniformField> uniforms;
    uint32_t uniformBufferSize = 0;
    
    // Material flags
    bool needsWorldPosition = false;
    bool needsWorldNormal = false;
    bool needsTangent = false;
    bool needsVertexColor = false;
    bool needsTime = false;
    bool isTranslucent = false;
    bool isTwoSided = false;
    bool usesWPO = false;  // World Position Offset
};

class MaterialCompiler {
public:
    MaterialCompiler();
    
    // Compile material graph to GLSL
    CompiledMaterial compile(const MaterialGraph& graph);
    
    // For node code generation
    std::string getInputValue(const MaterialNode& node, uint32_t pinIndex);
    std::string declareLocal(const std::string& type, const std::string& value);
    void requireUniform(const std::string& name, const std::string& type);
    void requireTexture(const std::string& name, uint32_t binding);
    void requireFeature(const std::string& feature);
    
    // Get the last compilation errors
    const std::string& getErrors() const { return errors_; }
    
private:
    void reset();
    void buildExecutionOrder(const MaterialGraph& graph);
    void generateCode(const MaterialGraph& graph);
    
    std::string generateVertexShader(const CompiledMaterial& mat);
    std::string generateFragmentShader(const CompiledMaterial& mat);
    
    // Code building
    std::stringstream declarations_;
    std::stringstream mainBody_;
    std::string errors_;
    
    uint32_t localVarCounter_ = 0;
    std::unordered_set<std::string> requiredFeatures_;
    std::vector<std::pair<std::string, std::string>> requiredUniforms_;
    std::vector<std::pair<std::string, uint32_t>> requiredTextures_;
    
    std::vector<MaterialNode*> executionOrder_;
    std::unordered_map<const MaterialNode*, std::vector<std::string>> nodeOutputVars_;
};

} // namespace Sanic
```

### Step 5: Material Editor UI (ImGui-based)

Create `src/engine/materials/editor/MaterialEditor.h`:

```cpp
#pragma once
#include "../MaterialGraph.h"
#include <imgui.h>
#include <memory>

// Using imnodes for node editor: https://github.com/Nelarius/imnodes
#include <imnodes.h>

namespace Sanic {

class MaterialPreview;

class MaterialEditor {
public:
    MaterialEditor();
    ~MaterialEditor();
    
    // Initialize (call once)
    bool initialize();
    
    // Open material for editing
    void openMaterial(std::shared_ptr<MaterialGraph> material);
    void closeMaterial();
    
    // Create new material
    void newMaterial();
    
    // Save/Load
    bool save(const std::string& path = "");
    bool load(const std::string& path);
    
    // Draw the editor (call each frame)
    void draw();
    
    // Check if editor has unsaved changes
    bool hasUnsavedChanges() const { return dirty_; }
    
private:
    void drawMenuBar();
    void drawNodeGraph();
    void drawNodePalette();
    void drawPropertyPanel();
    void drawPreviewPanel();
    void drawToolbar();
    
    void handleNodeCreation();
    void handleLinkCreation();
    void handleSelection();
    void handleDeletion();
    
    void recompileMaterial();
    
    std::shared_ptr<MaterialGraph> material_;
    std::unique_ptr<MaterialPreview> preview_;
    
    // Editor state
    bool dirty_ = false;
    std::string currentPath_;
    
    // Selected items
    std::vector<uint32_t> selectedNodes_;
    std::vector<std::pair<uint32_t, uint32_t>> selectedLinks_;
    
    // Node palette state
    std::string nodeSearchFilter_;
    std::string selectedCategory_;
    
    // Property panel
    MaterialNode* inspectedNode_ = nullptr;
    
    // ImNodes context
    ImNodesContext* nodesContext_ = nullptr;
    
    // Compilation state
    std::string compilationErrors_;
    bool needsRecompile_ = true;
};

} // namespace Sanic
```

---

## Unreal Engine Reference Files

### Shader Compilation
```
Engine/Source/Runtime/RenderCore/
├── Public/
│   ├── Shader.h                        # FShader base class
│   ├── ShaderCore.h                    # Core shader types
│   ├── ShaderCompilerCore.h            # Compilation interface
│   ├── ShaderParameterStruct.h         # Parameter binding
│   └── ShaderPermutation.h             # Permutation system
└── Private/
    ├── Shader.cpp
    ├── ShaderCompiler.cpp
    └── ShaderDerivedDataCache.cpp      # DDC integration
```

**Key files to study**:

1. **`ShaderPermutation.h`** - Study `TShaderPermutationDomain` and `FShaderPermutationBool`
2. **`ShaderCompiler.cpp`** - Look at `CompileShader()` function
3. **`ShaderDerivedDataCache.cpp`** - Caching implementation

### Material System
```
Engine/Source/Runtime/Engine/
├── Public/
│   ├── Materials/
│   │   ├── Material.h                  # UMaterial
│   │   ├── MaterialInstance.h          # UMaterialInstance
│   │   ├── MaterialExpression.h        # Node base class
│   │   └── MaterialShared.h            # Common types
│   └── Classes/
│       └── Materials/
│           ├── MaterialExpressionAdd.h
│           ├── MaterialExpressionMultiply.h
│           ├── MaterialExpressionTextureSample.h
│           └── ... (100+ expression types)
└── Private/
    ├── Materials/
    │   ├── Material.cpp
    │   ├── MaterialShader.cpp          # Shader generation
    │   └── MaterialInstance.cpp
    └── HLSLMaterialTranslator.cpp      # Graph → HLSL
```

**Key files to study**:

1. **`HLSLMaterialTranslator.cpp`** - This is the graph-to-code compiler
   - Look at `Translate()` function
   - Study how nodes generate code with `Compiler->` calls

2. **`MaterialExpression.h`** - Base node class
   - `Compile()` virtual function
   - `GetInputs()` / `GetOutputs()`

3. **`MaterialExpressionTextureSample.h`** - Example texture node
   - How texture bindings are declared
   - UV coordinate handling

### Material Editor (Editor module)
```
Engine/Source/Editor/MaterialEditor/
├── Private/
│   ├── MaterialEditor.cpp              # Main editor window
│   ├── MaterialEditorActions.cpp       # Commands/shortcuts
│   ├── MaterialEditorModule.cpp        # Plugin setup
│   └── SMaterialEditorCanvas.cpp       # Node graph widget
└── Public/
    └── MaterialEditor.h
```

**Key patterns**:

1. **Node Graph Rendering** - `SMaterialEditorCanvas.cpp`
   - How nodes are laid out
   - Connection rendering
   - Selection handling

2. **Preview Window** - Material preview uses a dedicated scene
   - Sphere, plane, cube preview meshes
   - Real-time shader compilation

---

## Implementation Checklist

### Week 1-2: Shader Cache & Includes
- [ ] Implement ShaderCache with disk persistence
- [ ] Implement ShaderIncluder using shaderc API
- [ ] Add common includes (`common.glsl`, `math.glsl`, etc.)
- [ ] Test cache hit/miss behavior
- [ ] Profile compilation times

### Week 3-4: Permutations & Reflection
- [ ] Implement ShaderPermutationSet
- [ ] Implement ShaderPermutationManager
- [ ] Add SPIRV-Reflect integration
- [ ] Test with existing engine shaders
- [ ] Add permutation pre-compilation

### Week 5-6: Hot Reload & Integration
- [ ] Implement file watcher
- [ ] Add hot reload callbacks
- [ ] Integrate with Renderer
- [ ] Test live shader editing
- [ ] Add keyboard shortcut (Ctrl+Shift+R)

### Week 7-8: Material Node System
- [ ] Implement MaterialNode base
- [ ] Implement all common nodes
- [ ] Implement MaterialGraph
- [ ] Implement MaterialCompiler
- [ ] Test generated GLSL

### Week 9-10: Material Editor UI
- [ ] Integrate imnodes library
- [ ] Implement node palette
- [ ] Implement property panel
- [ ] Implement preview renderer
- [ ] Save/load materials

---

## Dependencies

### Required Libraries
```cmake
# Shader compilation (use library, not subprocess)
find_package(shaderc CONFIG REQUIRED)

# SPIRV reflection
find_package(spirv_cross_core CONFIG REQUIRED)
find_package(spirv_cross_reflect CONFIG REQUIRED)

# ImGui node editor
# Download from: https://github.com/Nelarius/imnodes
add_subdirectory(external/imnodes)
```

### Integration Points
- Coordinate with **Developer 1** on RHI shader bytecode format
- Coordinate with **Developer 3** on ImGui integration
- Materials need to integrate with existing MaterialSystem.h

---

## Testing Strategy

1. **Shader Cache**: Verify faster second-run startup
2. **Includes**: Test nested includes, circular detection
3. **Permutations**: Generate all variants of a test shader
4. **Hot Reload**: Modify shader file, verify live update
5. **Material Nodes**: Unit test each node's code generation
6. **Material Compiler**: Compare output to hand-written GLSL
7. **Material Editor**: User testing for usability

---

## Resources

### Libraries
- [shaderc](https://github.com/google/shaderc) - Shader compilation library
- [SPIRV-Reflect](https://github.com/KhronosGroup/SPIRV-Reflect) - Shader reflection
- [imnodes](https://github.com/Nelarius/imnodes) - ImGui node editor
- [ImGuiColorTextEdit](https://github.com/BalazsJako/ImGuiColorTextEdit) - Code editor

### Tutorials
- [GPU-Driven Rendering](https://vkguide.dev/docs/gpudriven/) - Bindless patterns
- [Shader Permutations](https://www.reedbeta.com/blog/shader-permutations-part1/)
- [Node-Based Material Editor](https://www.youtube.com/watch?v=y-5O7fLKQN0)

