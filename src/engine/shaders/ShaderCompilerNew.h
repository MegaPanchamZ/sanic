/**
 * ShaderCompilerNew.h
 * 
 * Enhanced shader compiler using shaderc library directly.
 * Replaces the old subprocess-based approach with integrated compilation.
 * 
 * Features:
 * - Direct shaderc library usage (no subprocess)
 * - Include handling via ShaderIncluder
 * - Caching via ShaderCache
 * - Permutation support
 * - SPIR-V reflection integration
 * - Hot-reload integration
 */

#pragma once

#include <vulkan/vulkan.h>
#include <shaderc/shaderc.hpp>
#include "ShaderCache.h"
#include "ShaderIncluder.h"
#include "ShaderPermutation.h"
#include "ShaderReflection.h"
#include <string>
#include <vector>
#include <memory>
#include <optional>

namespace Sanic {

/**
 * Shader stages
 */
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
    Intersection,
    Callable
};

/**
 * Shader optimization level
 */
enum class ShaderOptLevel {
    None,          // No optimization (debug)
    Size,          // Optimize for code size
    Performance    // Optimize for performance (default)
};

/**
 * Shader compilation options
 */
struct ShaderCompileOptions {
    ShaderStage stage = ShaderStage::Fragment;
    std::string entryPoint = "main";
    std::string sourceName = "shader";
    
    // Preprocessor defines
    std::vector<std::pair<std::string, std::string>> defines;
    
    // Additional include paths (beyond defaults)
    std::vector<std::string> includePaths;
    
    // Optimization level
    ShaderOptLevel optimization = ShaderOptLevel::Performance;
    
    // Generate debug info (for RenderDoc, etc.)
    bool generateDebugInfo = false;
    
    // Target Vulkan version
    uint32_t vulkanVersion = VK_API_VERSION_1_3;
    
    // Target SPIR-V version (0 = auto based on Vulkan version)
    uint32_t spirvVersion = 0;
    
    // Enable bindless (affects some optimizations)
    bool enableBindless = true;
    
    // Enable 16-bit types
    bool enable16BitTypes = true;
    
    // Use cache
    bool useCache = true;
    
    // Perform reflection
    bool performReflection = true;
};

/**
 * Shader compilation result
 */
struct ShaderCompileResult {
    bool success = false;
    std::vector<uint32_t> spirv;
    std::string errors;
    std::string warnings;
    
    // Reflection data (if requested)
    std::optional<ShaderReflectionData> reflection;
    
    // Cache info
    bool wasCached = false;
    uint64_t sourceHash = 0;
    uint64_t definesHash = 0;
    
    // Compilation time (for profiling)
    double compilationTimeMs = 0.0;
};

/**
 * Enhanced shader compiler using shaderc library
 */
class ShaderCompilerEnhanced {
public:
    ShaderCompilerEnhanced();
    ~ShaderCompilerEnhanced();
    
    // Non-copyable
    ShaderCompilerEnhanced(const ShaderCompilerEnhanced&) = delete;
    ShaderCompilerEnhanced& operator=(const ShaderCompilerEnhanced&) = delete;
    
    /**
     * Initialize the compiler
     * @param defaultIncludePaths Default directories to search for includes
     * @param cacheDir Directory for shader cache (empty = disable cache)
     * @return true if initialization successful
     */
    bool initialize(const std::vector<std::string>& defaultIncludePaths = {},
                   const std::string& cacheDir = "shader_cache");
    
    /**
     * Shutdown the compiler
     */
    void shutdown();
    
    /**
     * Compile shader from source
     * @param source GLSL source code
     * @param options Compilation options
     * @return Compilation result
     */
    ShaderCompileResult compile(const std::string& source, const ShaderCompileOptions& options);
    
    /**
     * Compile shader from file
     * @param path Path to shader source file
     * @param options Compilation options
     * @return Compilation result
     */
    ShaderCompileResult compileFile(const std::string& path, const ShaderCompileOptions& options);
    
    /**
     * Compile a specific permutation
     * @param permSet Permutation set
     * @param permKey Permutation key (which variant)
     * @param options Base compilation options
     * @return Compilation result
     */
    ShaderCompileResult compilePermutation(
        ShaderPermutationSet& permSet,
        const PermutationKey& permKey,
        const ShaderCompileOptions& options);
    
    /**
     * Pre-compile all permutations of a shader
     * @param permSet Permutation set
     * @param options Base compilation options
     * @return Number of successfully compiled permutations
     */
    uint32_t precompileAllPermutations(
        ShaderPermutationSet& permSet,
        const ShaderCompileOptions& options);
    
    /**
     * Access the includer for configuration
     */
    ShaderIncluder& getIncluder() { return *includer_; }
    
    /**
     * Register a virtual include file
     */
    void registerVirtualFile(const std::string& name, const std::string& content);
    
    /**
     * Enable/disable caching
     */
    void enableCache(bool enable) { cacheEnabled_ = enable; }
    bool isCacheEnabled() const { return cacheEnabled_; }
    
    /**
     * Clear the shader cache
     */
    void clearCache();
    
    /**
     * Get compilation statistics
     */
    struct Stats {
        uint32_t compilations = 0;
        uint32_t cacheHits = 0;
        uint32_t cacheMisses = 0;
        double totalCompilationTimeMs = 0.0;
    };
    Stats getStats() const { return stats_; }
    
private:
    shaderc_shader_kind toShadercKind(ShaderStage stage);
    void configureOptions(shaderc::CompileOptions& opts, const ShaderCompileOptions& options);
    
    shaderc::Compiler compiler_;
    std::unique_ptr<ShaderIncluder> includer_;
    
    bool initialized_ = false;
    bool cacheEnabled_ = true;
    
    std::vector<std::string> defaultIncludePaths_;
    
    Stats stats_;
};

/**
 * Get the global shader compiler instance
 */
ShaderCompilerEnhanced& GetShaderCompiler();

/**
 * Utility: Load shader from file (uses global compiler)
 */
ShaderCompileResult LoadShader(const std::string& path, ShaderStage stage);

/**
 * Utility: Load shader with defines
 */
ShaderCompileResult LoadShader(const std::string& path, ShaderStage stage,
                               const std::vector<std::pair<std::string, std::string>>& defines);

} // namespace Sanic
