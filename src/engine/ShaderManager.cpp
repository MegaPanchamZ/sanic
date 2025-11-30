/**
 * ShaderManager.cpp
 * 
 * Implementation of the centralized shader management system.
 */

#include "ShaderManager.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>

namespace Sanic {

// Singleton instance
ShaderManager& ShaderManager::instance() {
    static ShaderManager s_instance;
    return s_instance;
}

bool ShaderManager::initialize(VkDevice device, const std::string& shaderDir, const std::string& cacheDir) {
    auto& mgr = instance();
    
    if (mgr.initialized_) {
        std::cout << "ShaderManager: Already initialized" << std::endl;
        return true;
    }
    
    if (device == VK_NULL_HANDLE) {
        std::cerr << "ShaderManager: Invalid VkDevice" << std::endl;
        return false;
    }
    
    mgr.device_ = device;
    mgr.shaderDir_ = shaderDir;
    mgr.cacheDir_ = cacheDir;
    
    // Ensure cache directory exists
    std::filesystem::create_directories(cacheDir);
    
    // Initialize the underlying compiler
    std::vector<std::string> includePaths = {shaderDir, ".", "../shaders"};
    if (!GetShaderCompiler().initialize(includePaths, cacheDir)) {
        std::cerr << "ShaderManager: Failed to initialize shader compiler" << std::endl;
        return false;
    }
    
    mgr.initialized_ = true;
    std::cout << "ShaderManager: Initialized (shaderDir=" << shaderDir 
              << ", cacheDir=" << cacheDir << ")" << std::endl;
    
    return true;
}

void ShaderManager::shutdown() {
    auto& mgr = instance();
    
    if (!mgr.initialized_) {
        return;
    }
    
    // Destroy all cached shader modules
    {
        std::lock_guard<std::mutex> lock(mgr.cacheMutex_);
        for (auto& [path, cached] : mgr.shaderCache_) {
            if (cached.module != VK_NULL_HANDLE) {
                vkDestroyShaderModule(mgr.device_, cached.module, nullptr);
            }
        }
        mgr.shaderCache_.clear();
    }
    
    GetShaderCompiler().shutdown();
    
    mgr.initialized_ = false;
    mgr.device_ = VK_NULL_HANDLE;
    
    std::cout << "ShaderManager: Shutdown complete" << std::endl;
}

bool ShaderManager::isInitialized() {
    return instance().initialized_;
}

VkDevice ShaderManager::getDevice() {
    return instance().device_;
}

ShaderStage inferShaderStage(const std::string& path) {
    // Get file extension
    size_t lastDot = path.rfind('.');
    if (lastDot == std::string::npos) {
        return ShaderStage::Fragment; // Default fallback
    }
    
    std::string ext = path.substr(lastDot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    if (ext == "vert") return ShaderStage::Vertex;
    if (ext == "frag") return ShaderStage::Fragment;
    if (ext == "comp") return ShaderStage::Compute;
    if (ext == "geom") return ShaderStage::Geometry;
    if (ext == "tesc") return ShaderStage::TessControl;
    if (ext == "tese") return ShaderStage::TessEvaluation;
    if (ext == "task") return ShaderStage::Task;
    if (ext == "mesh") return ShaderStage::Mesh;
    if (ext == "rgen") return ShaderStage::RayGen;
    if (ext == "rmiss") return ShaderStage::Miss;
    if (ext == "rchit") return ShaderStage::ClosestHit;
    if (ext == "rahit") return ShaderStage::AnyHit;
    if (ext == "rint") return ShaderStage::Intersection;
    if (ext == "rcall") return ShaderStage::Callable;
    
    return ShaderStage::Fragment; // Default fallback
}

ShaderCompileResult ShaderManager::compileShader(const std::string& path, ShaderStage stage,
                                                  const std::vector<std::pair<std::string, std::string>>& defines) {
    ShaderCompileOptions options;
    options.stage = stage;
    options.defines = defines;
    options.useCache = true;
    options.performReflection = false; // Skip reflection for now to speed up compilation
    
    // Extract filename for better error messages
    size_t lastSlash = path.rfind('/');
    if (lastSlash == std::string::npos) lastSlash = path.rfind('\\');
    options.sourceName = (lastSlash != std::string::npos) ? path.substr(lastSlash + 1) : path;
    
    // Try multiple search paths to find the shader source
    std::vector<std::string> searchPaths = {
        path,                          // As-is (if running from project root)
        "../" + path,                  // Parent directory (if running from build dir)
        shaderDir_ + "/" + options.sourceName,  // Just the filename in shader dir
        "../" + shaderDir_ + "/" + options.sourceName  // Parent shader dir
    };
    
    for (const auto& tryPath : searchPaths) {
        if (std::filesystem::exists(tryPath)) {
            return GetShaderCompiler().compileFile(tryPath, options);
        }
    }
    
    // None found - try the original path and let it fail with proper error
    return GetShaderCompiler().compileFile(path, options);
}

VkShaderModule ShaderManager::createShaderModule(const std::vector<uint32_t>& spirv) {
    if (spirv.empty() || device_ == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }
    
    VkShaderModuleCreateInfo createInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    createInfo.codeSize = spirv.size() * sizeof(uint32_t);
    createInfo.pCode = spirv.data();
    
    VkShaderModule module;
    if (vkCreateShaderModule(device_, &createInfo, nullptr, &module) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    
    return module;
}

VkShaderModule ShaderManager::loadShader(const std::string& path, ShaderStage stage,
                                          const std::vector<std::pair<std::string, std::string>>& defines) {
    auto& mgr = instance();
    
    if (!mgr.initialized_) {
        std::cerr << "ShaderManager: Not initialized, cannot load " << path << std::endl;
        return VK_NULL_HANDLE;
    }
    
    // Generate cache key
    std::string cacheKey = path + "_" + std::to_string(static_cast<int>(stage));
    for (const auto& [name, value] : defines) {
        cacheKey += "_" + name + "=" + value;
    }
    
    // Check cache
    {
        std::lock_guard<std::mutex> lock(mgr.cacheMutex_);
        auto it = mgr.shaderCache_.find(cacheKey);
        if (it != mgr.shaderCache_.end() && it->second.module != VK_NULL_HANDLE) {
            mgr.stats_.totalLoads++;
            mgr.stats_.cacheHits++;
            return it->second.module;
        }
    }
    
    // Compile shader
    auto result = mgr.compileShader(path, stage, defines);
    mgr.stats_.totalLoads++;
    
    if (!result.success) {
        std::cerr << "ShaderManager: Failed to compile " << path << std::endl;
        if (!result.errors.empty()) {
            std::cerr << "  Errors: " << result.errors << std::endl;
        }
        mgr.stats_.failures++;
        return VK_NULL_HANDLE;
    }
    
    mgr.stats_.compilations++;
    mgr.stats_.totalCompileTimeMs += result.compilationTimeMs;
    
    if (result.wasCached) {
        mgr.stats_.cacheHits++;
    }
    
    // Create shader module
    VkShaderModule module = mgr.createShaderModule(result.spirv);
    if (module == VK_NULL_HANDLE) {
        std::cerr << "ShaderManager: Failed to create VkShaderModule for " << path << std::endl;
        mgr.stats_.failures++;
        return VK_NULL_HANDLE;
    }
    
    // Cache it
    {
        std::lock_guard<std::mutex> lock(mgr.cacheMutex_);
        CachedShader cached;
        cached.module = module;
        cached.spirv = std::move(result.spirv);
        cached.sourceHash = result.sourceHash;
        mgr.shaderCache_[cacheKey] = std::move(cached);
    }
    
    return module;
}

std::vector<uint32_t> ShaderManager::loadShaderSpirv(const std::string& path, ShaderStage stage,
                                                       const std::vector<std::pair<std::string, std::string>>& defines) {
    auto& mgr = instance();
    
    if (!mgr.initialized_) {
        std::cerr << "ShaderManager: Not initialized, cannot load " << path << std::endl;
        return {};
    }
    
    // Generate cache key
    std::string cacheKey = path + "_" + std::to_string(static_cast<int>(stage));
    for (const auto& [name, value] : defines) {
        cacheKey += "_" + name + "=" + value;
    }
    
    // Check cache
    {
        std::lock_guard<std::mutex> lock(mgr.cacheMutex_);
        auto it = mgr.shaderCache_.find(cacheKey);
        if (it != mgr.shaderCache_.end() && !it->second.spirv.empty()) {
            mgr.stats_.totalLoads++;
            mgr.stats_.cacheHits++;
            return it->second.spirv;
        }
    }
    
    // Compile shader
    auto result = mgr.compileShader(path, stage, defines);
    mgr.stats_.totalLoads++;
    
    if (!result.success) {
        std::cerr << "ShaderManager: Failed to compile " << path << std::endl;
        if (!result.errors.empty()) {
            std::cerr << "  Errors: " << result.errors << std::endl;
        }
        mgr.stats_.failures++;
        return {};
    }
    
    mgr.stats_.compilations++;
    mgr.stats_.totalCompileTimeMs += result.compilationTimeMs;
    
    // Cache it (without creating VkShaderModule)
    std::vector<uint32_t> spirv = std::move(result.spirv);
    {
        std::lock_guard<std::mutex> lock(mgr.cacheMutex_);
        CachedShader& cached = mgr.shaderCache_[cacheKey];
        cached.spirv = spirv; // Copy
        cached.sourceHash = result.sourceHash;
    }
    
    return spirv;
}

std::vector<char> ShaderManager::loadShaderBytes(const std::string& path, ShaderStage stage) {
    auto spirv = loadShaderSpirv(path, stage);
    if (spirv.empty()) {
        return {};
    }
    
    // Convert uint32_t vector to char vector
    std::vector<char> bytes(spirv.size() * sizeof(uint32_t));
    std::memcpy(bytes.data(), spirv.data(), bytes.size());
    return bytes;
}

void ShaderManager::invalidateShader(const std::string& path) {
    auto& mgr = instance();
    
    std::lock_guard<std::mutex> lock(mgr.cacheMutex_);
    
    // Remove all cache entries that match this path (any stage/defines combo)
    for (auto it = mgr.shaderCache_.begin(); it != mgr.shaderCache_.end();) {
        if (it->first.find(path) == 0) {
            if (it->second.module != VK_NULL_HANDLE) {
                vkDestroyShaderModule(mgr.device_, it->second.module, nullptr);
            }
            it = mgr.shaderCache_.erase(it);
        } else {
            ++it;
        }
    }
}

void ShaderManager::clearCache() {
    auto& mgr = instance();
    
    std::lock_guard<std::mutex> lock(mgr.cacheMutex_);
    
    for (auto& [path, cached] : mgr.shaderCache_) {
        if (cached.module != VK_NULL_HANDLE) {
            vkDestroyShaderModule(mgr.device_, cached.module, nullptr);
        }
    }
    mgr.shaderCache_.clear();
    
    GetShaderCompiler().clearCache();
}

void ShaderManager::update() {
    // Process hot-reloads here if enabled
    // For now, just a placeholder for future hot-reload support
}

ShaderManager::Stats ShaderManager::getStats() {
    return instance().stats_;
}

void ShaderManager::setHotReloadEnabled(bool enabled) {
    instance().hotReloadEnabled_ = enabled;
}

bool ShaderManager::isHotReloadEnabled() {
    return instance().hotReloadEnabled_;
}

} // namespace Sanic
