#pragma once
#include "RHITypes.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>

namespace Sanic {

//=============================================================================
// Shader Bytecode
//=============================================================================

// Shader bytecode that can be used with either Vulkan or D3D12
struct RHIShaderBytecode {
    std::vector<uint32_t> spirv;         // SPIR-V bytecode (always present, source of truth)
    std::vector<uint8_t> dxil;           // DXIL bytecode (generated for D3D12)
    RHIShaderStage stage = RHIShaderStage::None;
    std::string entryPoint = "main";
    std::string debugName;
    
    bool hasSpirv() const { return !spirv.empty(); }
    bool hasDxil() const { return !dxil.empty(); }
};

//=============================================================================
// Shader Reflection
//=============================================================================

// Reflected binding information
struct RHIShaderBinding {
    uint32_t set = 0;                    // Descriptor set (Vulkan) / register space (D3D12)
    uint32_t binding = 0;                // Binding index (Vulkan) / register index (D3D12)
    RHIDescriptorType type = RHIDescriptorType::UniformBuffer;
    uint32_t count = 1;                  // Array size (0 = unbounded/bindless)
    RHIShaderStage stages = RHIShaderStage::None;
    std::string name;
    
    // For uniform/storage buffers
    uint32_t blockSize = 0;              // Size of the buffer block
    
    // For textures
    RHITextureDimension textureDimension = RHITextureDimension::Texture2D;
    bool isMultisampled = false;
    
    // Unique key for hashing
    uint64_t getKey() const {
        return (static_cast<uint64_t>(set) << 32) | binding;
    }
};

// Push constant range
struct RHIShaderPushConstant {
    uint32_t offset = 0;
    uint32_t size = 0;
    RHIShaderStage stages = RHIShaderStage::None;
    std::string name;
    
    // Member information for debugging
    struct Member {
        std::string name;
        uint32_t offset;
        uint32_t size;
    };
    std::vector<Member> members;
};

// Vertex input attribute (from vertex shader reflection)
struct RHIShaderVertexInput {
    uint32_t location = 0;
    RHIFormat format = RHIFormat::Unknown;
    std::string name;
};

// Specialization constant
struct RHIShaderSpecConstant {
    uint32_t id = 0;
    uint32_t size = 0;                   // 4 for int/float, 8 for int64/double, etc.
    std::string name;
    
    union DefaultValue {
        int32_t i32;
        uint32_t u32;
        float f32;
        int64_t i64;
        uint64_t u64;
        double f64;
    } defaultValue = {0};
};

// Workgroup size for compute shaders
struct RHIShaderWorkgroupSize {
    uint32_t x = 1;
    uint32_t y = 1;
    uint32_t z = 1;
    
    // Whether size is set via specialization constant
    bool xSpecId = false;
    bool ySpecId = false;
    bool zSpecId = false;
    uint32_t xSpecConstId = 0;
    uint32_t ySpecConstId = 0;
    uint32_t zSpecConstId = 0;
};

// Complete shader reflection data
struct RHIShaderReflection {
    RHIShaderStage stage = RHIShaderStage::None;
    std::string entryPoint = "main";
    
    // Resource bindings
    std::vector<RHIShaderBinding> bindings;
    
    // Push constants
    std::vector<RHIShaderPushConstant> pushConstants;
    
    // Vertex inputs (for vertex shaders)
    std::vector<RHIShaderVertexInput> vertexInputs;
    
    // Specialization constants
    std::vector<RHIShaderSpecConstant> specConstants;
    
    // Compute workgroup size
    RHIShaderWorkgroupSize workgroupSize;
    
    // For mesh shaders
    uint32_t maxOutputVertices = 0;
    uint32_t maxOutputPrimitives = 0;
    
    // Helper to find binding
    const RHIShaderBinding* findBinding(uint32_t set, uint32_t binding) const {
        for (const auto& b : bindings) {
            if (b.set == set && b.binding == binding) {
                return &b;
            }
        }
        return nullptr;
    }
    
    // Helper to find binding by name
    const RHIShaderBinding* findBinding(const std::string& name) const {
        for (const auto& b : bindings) {
            if (b.name == name) {
                return &b;
            }
        }
        return nullptr;
    }
    
    // Get total push constant size
    uint32_t getTotalPushConstantSize() const {
        uint32_t maxEnd = 0;
        for (const auto& pc : pushConstants) {
            maxEnd = std::max(maxEnd, pc.offset + pc.size);
        }
        return maxEnd;
    }
};

// Combined reflection for all stages in a pipeline
struct RHIPipelineReflection {
    std::unordered_map<RHIShaderStage, RHIShaderReflection> stages;
    
    // Merged bindings across all stages
    std::vector<RHIShaderBinding> mergedBindings;
    
    // Merged push constants
    std::vector<RHIShaderPushConstant> mergedPushConstants;
    
    // Vertex inputs from vertex shader
    std::vector<RHIShaderVertexInput> vertexInputs;
    
    // Build merged reflection from individual stage reflections
    void buildMerged();
    
    // Get maximum set index
    uint32_t getMaxSet() const {
        uint32_t maxSet = 0;
        for (const auto& b : mergedBindings) {
            maxSet = std::max(maxSet, b.set);
        }
        return maxSet;
    }
};

//=============================================================================
// Shader Compiler
//=============================================================================

// Shader compilation options
struct RHIShaderCompileOptions {
    RHIShaderStage stage = RHIShaderStage::None;
    std::string entryPoint = "main";
    std::string fileName;                 // For error messages
    std::vector<std::string> includePaths;
    std::unordered_map<std::string, std::string> defines;
    
    // Optimization level (0 = none, 1 = minimal, 2 = performance, 3 = size)
    int optimizationLevel = 2;
    
    // Generate debug info
    bool generateDebugInfo = false;
    
    // HLSL-specific options
    int hlslShaderModel = 66;            // 60 = SM 6.0, 66 = SM 6.6
    bool hlslRowMajorMatrices = false;
    
    // GLSL-specific options
    int glslVersion = 460;
    bool glslVulkanSemantics = true;
    
    // Validation
    bool validateSpirv = true;
};

// Shader compilation result
struct RHIShaderCompileResult {
    bool success = false;
    std::string errorMessage;
    std::vector<std::string> warnings;
    
    RHIShaderBytecode bytecode;
    RHIShaderReflection reflection;
};

// Shader compiler interface
class RHIShaderCompiler {
public:
    virtual ~RHIShaderCompiler() = default;
    
    //-------------------------------------------------------------------------
    // GLSL Compilation (to SPIR-V)
    //-------------------------------------------------------------------------
    
    // Compile GLSL source to SPIR-V
    virtual RHIShaderCompileResult compileGLSL(
        const std::string& source,
        const RHIShaderCompileOptions& options) = 0;
    
    // Compile GLSL file to SPIR-V
    virtual RHIShaderCompileResult compileGLSLFile(
        const std::string& filePath,
        const RHIShaderCompileOptions& options) = 0;
    
    //-------------------------------------------------------------------------
    // HLSL Compilation (to SPIR-V or DXIL)
    //-------------------------------------------------------------------------
    
    // Compile HLSL source to SPIR-V (for Vulkan)
    virtual RHIShaderCompileResult compileHLSLToSpirv(
        const std::string& source,
        const RHIShaderCompileOptions& options) = 0;
    
    // Compile HLSL source to DXIL (for D3D12)
    virtual RHIShaderCompileResult compileHLSLToDxil(
        const std::string& source,
        const RHIShaderCompileOptions& options) = 0;
    
    //-------------------------------------------------------------------------
    // Cross-Compilation
    //-------------------------------------------------------------------------
    
    // Cross-compile SPIR-V to HLSL
    virtual bool crossCompileToHLSL(
        const std::vector<uint32_t>& spirv,
        RHIShaderStage stage,
        std::string& outHlsl,
        std::string& outErrors) = 0;
    
    // Cross-compile SPIR-V to DXIL (SPIR-V -> HLSL -> DXIL)
    virtual bool crossCompileToDXIL(
        const std::vector<uint32_t>& spirv,
        RHIShaderStage stage,
        std::vector<uint8_t>& outDxil,
        std::string& outErrors) = 0;
    
    // Cross-compile SPIR-V to GLSL (for debugging/viewing)
    virtual bool crossCompileToGLSL(
        const std::vector<uint32_t>& spirv,
        RHIShaderStage stage,
        std::string& outGlsl,
        std::string& outErrors) = 0;
    
    // Cross-compile SPIR-V to MSL (for Metal, future)
    virtual bool crossCompileToMSL(
        const std::vector<uint32_t>& spirv,
        RHIShaderStage stage,
        std::string& outMsl,
        std::string& outErrors) = 0;
    
    //-------------------------------------------------------------------------
    // Reflection
    //-------------------------------------------------------------------------
    
    // Reflect SPIR-V shader to get binding information
    virtual bool reflectSPIRV(
        const std::vector<uint32_t>& spirv,
        RHIShaderReflection& outReflection) = 0;
    
    // Reflect DXIL shader
    virtual bool reflectDXIL(
        const std::vector<uint8_t>& dxil,
        RHIShaderReflection& outReflection) = 0;
    
    //-------------------------------------------------------------------------
    // Validation
    //-------------------------------------------------------------------------
    
    // Validate SPIR-V bytecode
    virtual bool validateSPIRV(
        const std::vector<uint32_t>& spirv,
        std::string& outErrors) = 0;
    
    // Optimize SPIR-V (for final builds)
    virtual bool optimizeSPIRV(
        const std::vector<uint32_t>& spirv,
        std::vector<uint32_t>& outOptimized,
        int optimizationLevel = 2) = 0;
    
    // Strip debug info from SPIR-V
    virtual bool stripDebugInfo(
        const std::vector<uint32_t>& spirv,
        std::vector<uint32_t>& outStripped) = 0;
    
    //-------------------------------------------------------------------------
    // Utility
    //-------------------------------------------------------------------------
    
    // Get shader stage from file extension
    static RHIShaderStage getStageFromExtension(const std::string& extension) {
        if (extension == ".vert" || extension == ".vs") return RHIShaderStage::Vertex;
        if (extension == ".frag" || extension == ".fs" || extension == ".ps") return RHIShaderStage::Fragment;
        if (extension == ".comp" || extension == ".cs") return RHIShaderStage::Compute;
        if (extension == ".geom" || extension == ".gs") return RHIShaderStage::Geometry;
        if (extension == ".tesc" || extension == ".hs") return RHIShaderStage::Hull;
        if (extension == ".tese" || extension == ".ds") return RHIShaderStage::Domain;
        if (extension == ".task" || extension == ".as") return RHIShaderStage::Task;
        if (extension == ".mesh" || extension == ".ms") return RHIShaderStage::Mesh;
        if (extension == ".rgen") return RHIShaderStage::RayGen;
        if (extension == ".rmiss") return RHIShaderStage::Miss;
        if (extension == ".rchit") return RHIShaderStage::ClosestHit;
        if (extension == ".rahit") return RHIShaderStage::AnyHit;
        if (extension == ".rint") return RHIShaderStage::Intersection;
        if (extension == ".rcall") return RHIShaderStage::Callable;
        return RHIShaderStage::None;
    }
    
    // Get DXC shader profile from stage
    static const char* getDXCProfile(RHIShaderStage stage, int shaderModel = 66) {
        const char* prefix = nullptr;
        switch (stage) {
            case RHIShaderStage::Vertex:      prefix = "vs"; break;
            case RHIShaderStage::Fragment:    prefix = "ps"; break;
            case RHIShaderStage::Compute:     prefix = "cs"; break;
            case RHIShaderStage::Geometry:    prefix = "gs"; break;
            case RHIShaderStage::Hull:        prefix = "hs"; break;
            case RHIShaderStage::Domain:      prefix = "ds"; break;
            case RHIShaderStage::Task:        prefix = "as"; break;  // Amplification
            case RHIShaderStage::Mesh:        prefix = "ms"; break;
            case RHIShaderStage::RayGen:
            case RHIShaderStage::Miss:
            case RHIShaderStage::ClosestHit:
            case RHIShaderStage::AnyHit:
            case RHIShaderStage::Intersection:
            case RHIShaderStage::Callable:
                prefix = "lib"; break;  // Ray tracing uses library profile
            default: return nullptr;
        }
        
        // Format: "xx_6_6" for SM 6.6
        static char profile[16];
        snprintf(profile, sizeof(profile), "%s_%d_%d", prefix, shaderModel / 10, shaderModel % 10);
        return profile;
    }
};

// Create the default shader compiler instance
std::unique_ptr<RHIShaderCompiler> CreateShaderCompiler();

//=============================================================================
// Shader Library (for caching compiled shaders)
//=============================================================================

class RHIShaderLibrary {
public:
    virtual ~RHIShaderLibrary() = default;
    
    // Load shader from cache or compile
    virtual const RHIShaderBytecode* getShader(
        const std::string& name,
        RHIShaderStage stage) = 0;
    
    // Add shader to library
    virtual void addShader(
        const std::string& name,
        const RHIShaderBytecode& bytecode) = 0;
    
    // Check if shader exists
    virtual bool hasShader(const std::string& name, RHIShaderStage stage) const = 0;
    
    // Remove shader from library
    virtual void removeShader(const std::string& name, RHIShaderStage stage) = 0;
    
    // Clear all shaders
    virtual void clear() = 0;
    
    // Save library to disk cache
    virtual bool saveToFile(const std::string& path) = 0;
    
    // Load library from disk cache
    virtual bool loadFromFile(const std::string& path) = 0;
};

std::unique_ptr<RHIShaderLibrary> CreateShaderLibrary();

} // namespace Sanic
