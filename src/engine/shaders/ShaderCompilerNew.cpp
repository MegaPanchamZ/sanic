/**
 * ShaderCompilerNew.cpp
 * 
 * Implementation of the enhanced shader compiler.
 */

#include "ShaderCompilerNew.h"
#include <fstream>
#include <sstream>
#include <chrono>
#include <iostream>

namespace Sanic {

ShaderCompilerEnhanced::ShaderCompilerEnhanced() = default;

ShaderCompilerEnhanced::~ShaderCompilerEnhanced() {
    shutdown();
}

bool ShaderCompilerEnhanced::initialize(const std::vector<std::string>& defaultIncludePaths,
                                         const std::string& cacheDir) {
    if (initialized_) {
        return true;
    }
    
    includer_ = std::make_unique<ShaderIncluder>();
    
    // Add default include paths
    defaultIncludePaths_ = defaultIncludePaths;
    for (const auto& path : defaultIncludePaths) {
        includer_->addIncludePath(path);
    }
    
    // Initialize cache
    if (!cacheDir.empty()) {
        if (!GetShaderCache().initialize(cacheDir)) {
            std::cerr << "ShaderCompilerEnhanced: Failed to initialize shader cache" << std::endl;
            // Continue without cache
            cacheEnabled_ = false;
        }
    } else {
        cacheEnabled_ = false;
    }
    
    initialized_ = true;
    std::cout << "ShaderCompilerEnhanced: Initialized with " 
              << defaultIncludePaths.size() << " include paths" << std::endl;
    
    return true;
}

void ShaderCompilerEnhanced::shutdown() {
    if (!initialized_) {
        return;
    }
    
    GetShaderCache().shutdown();
    includer_.reset();
    initialized_ = false;
}

ShaderCompileResult ShaderCompilerEnhanced::compile(const std::string& source,
                                                     const ShaderCompileOptions& options) {
    ShaderCompileResult result;
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Compute hashes for cache lookup
    result.sourceHash = ShaderCache::hashSource(source);
    result.definesHash = ShaderCache::hashDefines(options.defines);
    
    // Try cache lookup
    if (cacheEnabled_ && options.useCache) {
        ShaderCacheKey key;
        key.sourceHash = result.sourceHash;
        key.definesHash = result.definesHash;
        key.shaderStage = static_cast<uint32_t>(options.stage);
        key.compilerVersion = ShaderCache::getCompilerVersion();
        
        auto cached = GetShaderCache().lookup(key);
        if (cached) {
            result.success = true;
            result.spirv = cached->spirv;
            result.wasCached = true;
            
            // Reconstruct reflection data from cache
            if (options.performReflection && !result.spirv.empty()) {
                result.reflection = ShaderReflection::reflect(result.spirv, options.entryPoint);
            }
            
            stats_.cacheHits++;
            
            auto endTime = std::chrono::high_resolution_clock::now();
            result.compilationTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
            
            return result;
        }
        
        stats_.cacheMisses++;
    }
    
    // Reset includer tracking
    includer_->resetTracking();
    
    // Add additional include paths
    for (const auto& path : options.includePaths) {
        includer_->addIncludePath(path);
    }
    
    // Configure compile options
    shaderc::CompileOptions shadercOptions;
    configureOptions(shadercOptions, options);
    
    // Set includer
    shadercOptions.SetIncluder(std::unique_ptr<shaderc::CompileOptions::IncluderInterface>(
        new ShaderIncluder(*includer_)));
    
    // Compile
    shaderc_shader_kind kind = toShadercKind(options.stage);
    
    shaderc::SpvCompilationResult compileResult = compiler_.CompileGlslToSpv(
        source, kind, options.sourceName.c_str(), options.entryPoint.c_str(), shadercOptions);
    
    if (compileResult.GetCompilationStatus() != shaderc_compilation_status_success) {
        result.success = false;
        result.errors = compileResult.GetErrorMessage();
        
        auto endTime = std::chrono::high_resolution_clock::now();
        result.compilationTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
        
        return result;
    }
    
    // Get SPIR-V output
    result.spirv = std::vector<uint32_t>(compileResult.cbegin(), compileResult.cend());
    result.success = true;
    result.warnings = compileResult.GetErrorMessage(); // Warnings are also in error message
    
    // Perform reflection
    if (options.performReflection && !result.spirv.empty()) {
        result.reflection = ShaderReflection::reflect(result.spirv, options.entryPoint);
    }
    
    // Store in cache
    if (cacheEnabled_ && options.useCache) {
        ShaderCacheKey key;
        key.sourceHash = result.sourceHash;
        key.definesHash = result.definesHash;
        key.shaderStage = static_cast<uint32_t>(options.stage);
        key.compilerVersion = ShaderCache::getCompilerVersion();
        
        ShaderCacheEntry entry;
        entry.spirv = result.spirv;
        entry.timestamp = static_cast<uint64_t>(
            std::chrono::system_clock::now().time_since_epoch().count());
        entry.entryPoint = options.entryPoint;
        
        if (result.reflection) {
            ShaderReflection::toCacheEntry(*result.reflection, entry);
        }
        
        GetShaderCache().store(key, entry);
    }
    
    stats_.compilations++;
    
    auto endTime = std::chrono::high_resolution_clock::now();
    result.compilationTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    stats_.totalCompilationTimeMs += result.compilationTimeMs;
    
    return result;
}

ShaderCompileResult ShaderCompilerEnhanced::compileFile(const std::string& path,
                                                         const ShaderCompileOptions& options) {
    std::ifstream file(path);
    if (!file.is_open()) {
        ShaderCompileResult result;
        result.success = false;
        result.errors = "Failed to open file: " + path;
        return result;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    
    // Update options with file info
    ShaderCompileOptions opts = options;
    if (opts.sourceName == "shader") {
        opts.sourceName = path;
    }
    
    // Add file directory to include paths
    std::filesystem::path filePath(path);
    if (filePath.has_parent_path()) {
        includer_->addIncludePath(filePath.parent_path());
    }
    
    return compile(buffer.str(), opts);
}

ShaderCompileResult ShaderCompilerEnhanced::compilePermutation(
    ShaderPermutationSet& permSet,
    const PermutationKey& permKey,
    const ShaderCompileOptions& baseOptions) {
    
    // Get defines for this permutation
    auto permDefines = permSet.getDefines(permKey);
    
    // Merge with base options
    ShaderCompileOptions options = baseOptions;
    for (const auto& [name, value] : permDefines) {
        options.defines.emplace_back(name, value);
    }
    
    // Update source name
    options.sourceName = permSet.getName() + "_perm" + std::to_string(permKey.hash());
    
    return compile(permSet.getSource(), options);
}

uint32_t ShaderCompilerEnhanced::precompileAllPermutations(
    ShaderPermutationSet& permSet,
    const ShaderCompileOptions& baseOptions) {
    
    auto permutations = permSet.getAllPermutations();
    uint32_t successCount = 0;
    
    std::cout << "Pre-compiling " << permutations.size() << " permutations of " 
              << permSet.getName() << std::endl;
    
    for (const auto& permKey : permutations) {
        auto result = compilePermutation(permSet, permKey, baseOptions);
        if (result.success) {
            successCount++;
        } else {
            std::cerr << "  Failed permutation " << permKey.hash() << ": " 
                      << result.errors << std::endl;
        }
    }
    
    std::cout << "  Compiled " << successCount << "/" << permutations.size() 
              << " permutations" << std::endl;
    
    return successCount;
}

void ShaderCompilerEnhanced::registerVirtualFile(const std::string& name, const std::string& content) {
    if (includer_) {
        includer_->registerVirtualFile(name, content);
    }
}

void ShaderCompilerEnhanced::clearCache() {
    GetShaderCache().invalidateAll();
}

shaderc_shader_kind ShaderCompilerEnhanced::toShadercKind(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::Vertex:
            return shaderc_vertex_shader;
        case ShaderStage::Fragment:
            return shaderc_fragment_shader;
        case ShaderStage::Compute:
            return shaderc_compute_shader;
        case ShaderStage::Geometry:
            return shaderc_geometry_shader;
        case ShaderStage::TessControl:
            return shaderc_tess_control_shader;
        case ShaderStage::TessEvaluation:
            return shaderc_tess_evaluation_shader;
        case ShaderStage::Task:
            return shaderc_task_shader;
        case ShaderStage::Mesh:
            return shaderc_mesh_shader;
        case ShaderStage::RayGen:
            return shaderc_raygen_shader;
        case ShaderStage::Miss:
            return shaderc_miss_shader;
        case ShaderStage::ClosestHit:
            return shaderc_closesthit_shader;
        case ShaderStage::AnyHit:
            return shaderc_anyhit_shader;
        case ShaderStage::Intersection:
            return shaderc_intersection_shader;
        case ShaderStage::Callable:
            return shaderc_callable_shader;
        default:
            return shaderc_glsl_infer_from_source;
    }
}

void ShaderCompilerEnhanced::configureOptions(shaderc::CompileOptions& opts,
                                               const ShaderCompileOptions& options) {
    // Target environment
    shaderc_env_version envVersion;
    switch (options.vulkanVersion) {
        case VK_API_VERSION_1_0:
            envVersion = shaderc_env_version_vulkan_1_0;
            break;
        case VK_API_VERSION_1_1:
            envVersion = shaderc_env_version_vulkan_1_1;
            break;
        case VK_API_VERSION_1_2:
            envVersion = shaderc_env_version_vulkan_1_2;
            break;
        case VK_API_VERSION_1_3:
        default:
            envVersion = shaderc_env_version_vulkan_1_3;
            break;
    }
    opts.SetTargetEnvironment(shaderc_target_env_vulkan, envVersion);
    
    // SPIR-V version
    if (options.spirvVersion != 0) {
        shaderc_spirv_version spvVersion;
        switch (options.spirvVersion) {
            case 0x10000: spvVersion = shaderc_spirv_version_1_0; break;
            case 0x10100: spvVersion = shaderc_spirv_version_1_1; break;
            case 0x10200: spvVersion = shaderc_spirv_version_1_2; break;
            case 0x10300: spvVersion = shaderc_spirv_version_1_3; break;
            case 0x10400: spvVersion = shaderc_spirv_version_1_4; break;
            case 0x10500: spvVersion = shaderc_spirv_version_1_5; break;
            case 0x10600:
            default:
                spvVersion = shaderc_spirv_version_1_6;
                break;
        }
        opts.SetTargetSpirv(spvVersion);
    }
    
    // Optimization level
    switch (options.optimization) {
        case ShaderOptLevel::None:
            opts.SetOptimizationLevel(shaderc_optimization_level_zero);
            break;
        case ShaderOptLevel::Size:
            opts.SetOptimizationLevel(shaderc_optimization_level_size);
            break;
        case ShaderOptLevel::Performance:
        default:
            opts.SetOptimizationLevel(shaderc_optimization_level_performance);
            break;
    }
    
    // Debug info
    if (options.generateDebugInfo) {
        opts.SetGenerateDebugInfo();
    }
    
    // Preprocessor defines
    for (const auto& [name, value] : options.defines) {
        if (value.empty()) {
            opts.AddMacroDefinition(name);
        } else {
            opts.AddMacroDefinition(name, value);
        }
    }
    
    // Enable extensions
    if (options.enable16BitTypes) {
        opts.AddMacroDefinition("ENABLE_16BIT_TYPES", "1");
    }
    
    if (options.enableBindless) {
        opts.AddMacroDefinition("ENABLE_BINDLESS", "1");
    }
    
    // Warnings as errors (optional, for stricter compilation)
    // opts.SetWarningsAsErrors();
}

// Global instance
static ShaderCompilerEnhanced g_shaderCompiler;

ShaderCompilerEnhanced& GetShaderCompiler() {
    return g_shaderCompiler;
}

ShaderCompileResult LoadShader(const std::string& path, ShaderStage stage) {
    ShaderCompileOptions options;
    options.stage = stage;
    return GetShaderCompiler().compileFile(path, options);
}

ShaderCompileResult LoadShader(const std::string& path, ShaderStage stage,
                               const std::vector<std::pair<std::string, std::string>>& defines) {
    ShaderCompileOptions options;
    options.stage = stage;
    options.defines = defines;
    return GetShaderCompiler().compileFile(path, options);
}

} // namespace Sanic
