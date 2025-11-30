/**
 * ShaderManager.h
 * 
 * Centralized shader management for the Sanic Engine.
 * Provides runtime compilation with caching - no pre-compiled SPV files needed.
 * 
 * Usage:
 *   // Initialize once after VkDevice is created
 *   ShaderManager::initialize(device);
 *   
 *   // Load shaders anywhere in the engine
 *   VkShaderModule vert = ShaderManager::loadShader("shaders/shader.vert", Sanic::ShaderStage::Vertex);
 *   VkShaderModule frag = ShaderManager::loadShader("shaders/shader.frag", Sanic::ShaderStage::Fragment);
 *   
 *   // Or load compute shaders
 *   VkShaderModule comp = ShaderManager::loadShader("shaders/ssr.comp", Sanic::ShaderStage::Compute);
 *   
 *   // Cleanup at shutdown
 *   ShaderManager::shutdown();
 */

#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include "shaders/ShaderCompilerNew.h"
#include "shaders/ShaderLibrary.h"

namespace Sanic {

/**
 * Global shader manager - handles all shader compilation and caching
 */
class ShaderManager {
public:
    /**
     * Initialize the shader manager
     * @param device Vulkan device for creating shader modules
     * @param shaderDir Base directory for shader sources (default: "shaders")
     * @param cacheDir Directory for compiled shader cache (default: "shader_cache")
     * @return true if initialization successful
     */
    static bool initialize(VkDevice device, 
                          const std::string& shaderDir = "shaders",
                          const std::string& cacheDir = "shader_cache");
    
    /**
     * Shutdown and cleanup all resources
     */
    static void shutdown();
    
    /**
     * Check if the shader manager is initialized
     */
    static bool isInitialized();
    
    /**
     * Load a shader module from source file
     * Compiles on first load, uses cache for subsequent loads.
     * 
     * @param path Path to shader source (e.g., "shaders/shader.vert")
     * @param stage Shader stage (Vertex, Fragment, Compute, etc.)
     * @param defines Optional preprocessor defines
     * @return VkShaderModule handle, or VK_NULL_HANDLE on failure
     */
    static VkShaderModule loadShader(const std::string& path, 
                                     ShaderStage stage,
                                     const std::vector<std::pair<std::string, std::string>>& defines = {});
    
    /**
     * Load a shader module from source file (auto-detect stage from extension)
     * Compiles on first load, uses cache for subsequent loads.
     * 
     * @param path Path to shader source (e.g., "shaders/ssr.comp" - .comp = compute)
     * @return VkShaderModule handle, or VK_NULL_HANDLE on failure
     */
    static VkShaderModule loadShader(const std::string& path);
    
    /**
     * Load shader and get SPIR-V bytecode directly
     * Useful for ray tracing pipelines that need the raw bytes
     * 
     * @param path Path to shader source
     * @param stage Shader stage
     * @param defines Optional preprocessor defines
     * @return SPIR-V bytecode as vector of uint32_t
     */
    static std::vector<uint32_t> loadShaderSpirv(const std::string& path,
                                                  ShaderStage stage,
                                                  const std::vector<std::pair<std::string, std::string>>& defines = {});
    
    /**
     * Load shader as raw bytes (for legacy code expecting char vectors)
     * 
     * @param path Path to shader source
     * @param stage Shader stage
     * @return Shader bytecode as char vector
     */
    static std::vector<char> loadShaderBytes(const std::string& path, ShaderStage stage);
    
    /**
     * Invalidate cache for a specific shader
     * Forces recompilation on next load
     */
    static void invalidateShader(const std::string& path);
    
    /**
     * Clear entire shader cache
     */
    static void clearCache();
    
    /**
     * Update shader manager (process hot-reloads)
     * Call once per frame from main thread
     */
    static void update();
    
    /**
     * Get compilation statistics
     */
    struct Stats {
        uint32_t totalLoads = 0;
        uint32_t cacheHits = 0;
        uint32_t compilations = 0;
        uint32_t failures = 0;
        double totalCompileTimeMs = 0.0;
    };
    static Stats getStats();
    
    /**
     * Enable/disable hot-reload
     */
    static void setHotReloadEnabled(bool enabled);
    static bool isHotReloadEnabled();
    
    /**
     * Get the Vulkan device being used
     */
    static VkDevice getDevice();

private:
    ShaderManager() = default;
    ~ShaderManager() = default;
    
    static ShaderManager& instance();
    
    VkShaderModule createShaderModule(const std::vector<uint32_t>& spirv);
    ShaderCompileResult compileShader(const std::string& path, ShaderStage stage,
                                      const std::vector<std::pair<std::string, std::string>>& defines);
    
    VkDevice device_ = VK_NULL_HANDLE;
    std::string shaderDir_;
    std::string cacheDir_;
    bool initialized_ = false;
    bool hotReloadEnabled_ = true;
    
    // Cache of compiled shader modules
    struct CachedShader {
        VkShaderModule module = VK_NULL_HANDLE;
        std::vector<uint32_t> spirv;
        uint64_t sourceHash = 0;
    };
    std::unordered_map<std::string, CachedShader> shaderCache_;
    std::mutex cacheMutex_;
    
    Stats stats_;
};

/**
 * Helper to infer shader stage from file extension
 */
ShaderStage inferShaderStage(const std::string& path);

} // namespace Sanic
