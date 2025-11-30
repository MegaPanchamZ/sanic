/**
 * ShaderLibrary.h
 * 
 * Centralized shader asset management.
 * Handles loading, caching, and reloading of shader programs.
 * 
 * Features:
 * - Shader program management
 * - Hot-reload integration
 * - Pipeline state caching
 * - Shader program variants
 */

#pragma once

#include "ShaderCompilerNew.h"
#include "ShaderPermutation.h"
#include "ShaderHotReload.h"
#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>

namespace Sanic {

/**
 * Compiled shader module with metadata
 */
struct ShaderModule {
    VkShaderModule module = VK_NULL_HANDLE;
    ShaderStage stage;
    std::string entryPoint;
    std::string sourcePath;
    
    // Reflection data
    std::optional<ShaderReflectionData> reflection;
    
    // For permutations
    uint64_t permutationHash = 0;
    
    bool isValid() const { return module != VK_NULL_HANDLE; }
};

/**
 * A complete shader program (vertex + fragment, or compute, etc.)
 */
struct ShaderProgram {
    std::string name;
    std::vector<std::shared_ptr<ShaderModule>> stages;
    
    // Combined descriptor layout info
    std::vector<ReflectedDescriptor> descriptors;
    std::vector<ReflectedPushConstantBlock> pushConstants;
    
    // For graphics pipelines
    std::vector<ReflectedInputAttribute> vertexInputs;
    
    // Hot-reload tracking
    bool needsReload = false;
    
    ShaderModule* getStage(ShaderStage stage) const {
        for (const auto& s : stages) {
            if (s->stage == stage) return s.get();
        }
        return nullptr;
    }
    
    bool hasStage(ShaderStage stage) const {
        return getStage(stage) != nullptr;
    }
};

/**
 * Shader library configuration
 */
struct ShaderLibraryConfig {
    // Vulkan device for creating shader modules
    VkDevice device = VK_NULL_HANDLE;
    
    // Shader source directories
    std::vector<std::string> shaderDirs = {"shaders"};
    
    // Cache directory
    std::string cacheDir = "shader_cache";
    
    // Enable hot-reload
    bool enableHotReload = true;
    
    // Hot-reload callback (called on main thread)
    std::function<void(const std::string& programName)> onProgramReloaded;
};

/**
 * Shader library - manages all shader assets
 */
class ShaderLibrary {
public:
    ShaderLibrary();
    ~ShaderLibrary();
    
    // Non-copyable
    ShaderLibrary(const ShaderLibrary&) = delete;
    ShaderLibrary& operator=(const ShaderLibrary&) = delete;
    
    /**
     * Initialize the library
     */
    bool initialize(const ShaderLibraryConfig& config);
    
    /**
     * Shutdown and cleanup
     */
    void shutdown();
    
    /**
     * Update (process hot-reloads)
     * Call from main thread each frame
     */
    void update();
    
    /**
     * Load a shader module from file
     */
    std::shared_ptr<ShaderModule> loadModule(const std::string& path, 
                                              ShaderStage stage,
                                              const ShaderCompileOptions& options = {});
    
    /**
     * Load a shader module with permutation
     */
    std::shared_ptr<ShaderModule> loadModule(ShaderPermutationSet& permSet,
                                              const PermutationKey& permKey,
                                              ShaderStage stage,
                                              const ShaderCompileOptions& options = {});
    
    /**
     * Create a shader program from modules
     */
    std::shared_ptr<ShaderProgram> createProgram(const std::string& name,
                                                  const std::vector<std::shared_ptr<ShaderModule>>& stages);
    
    /**
     * Load and create a graphics program (vertex + fragment)
     */
    std::shared_ptr<ShaderProgram> loadGraphicsProgram(const std::string& name,
                                                        const std::string& vertPath,
                                                        const std::string& fragPath,
                                                        const ShaderCompileOptions& options = {});
    
    /**
     * Load and create a compute program
     */
    std::shared_ptr<ShaderProgram> loadComputeProgram(const std::string& name,
                                                       const std::string& compPath,
                                                       const ShaderCompileOptions& options = {});
    
    /**
     * Get a loaded program by name
     */
    std::shared_ptr<ShaderProgram> getProgram(const std::string& name);
    
    /**
     * Check if a program exists
     */
    bool hasProgram(const std::string& name) const;
    
    /**
     * Reload a specific program
     */
    bool reloadProgram(const std::string& name);
    
    /**
     * Reload all programs that use a specific source file
     */
    void reloadProgramsUsing(const std::string& sourcePath);
    
    /**
     * Get all program names
     */
    std::vector<std::string> getProgramNames() const;
    
    /**
     * Register a permutation set for a shader
     */
    void registerPermutationSet(std::shared_ptr<ShaderPermutationSet> permSet);
    
    /**
     * Get a registered permutation set
     */
    ShaderPermutationSet* getPermutationSet(const std::string& name);
    
    /**
     * Pre-compile all registered permutation sets
     */
    void precompileAll();
    
    /**
     * Get statistics
     */
    struct Stats {
        uint32_t modulesLoaded = 0;
        uint32_t programsCreated = 0;
        uint32_t hotReloads = 0;
        uint32_t failedReloads = 0;
    };
    Stats getStats() const { return stats_; }
    
private:
    VkShaderModule createVkShaderModule(const std::vector<uint32_t>& spirv);
    void destroyModule(ShaderModule* module);
    void onFileChanged(const std::string& path);
    void mergeReflection(ShaderProgram& program);
    
    ShaderLibraryConfig config_;
    bool initialized_ = false;
    
    // Loaded modules (keyed by path + stage + permutation hash)
    std::unordered_map<std::string, std::shared_ptr<ShaderModule>> modules_;
    
    // Created programs
    std::unordered_map<std::string, std::shared_ptr<ShaderProgram>> programs_;
    
    // Permutation sets
    std::unordered_map<std::string, std::shared_ptr<ShaderPermutationSet>> permutationSets_;
    
    // Track which files are used by which programs
    std::unordered_map<std::string, std::vector<std::string>> fileToPrograms_;
    
    // Programs needing reload (set by hot-reload, processed in update)
    std::vector<std::string> pendingReloads_;
    std::mutex pendingReloadsMutex_;
    
    // Hot-reload callback ID
    uint32_t hotReloadCallbackId_ = 0;
    
    Stats stats_;
};

/**
 * Get the global shader library instance
 */
ShaderLibrary& GetShaderLibrary();

} // namespace Sanic
