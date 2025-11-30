/**
 * ShaderCache.h
 * 
 * Disk and memory cache for compiled SPIR-V shaders.
 * Provides fast lookup and persistent storage to avoid recompilation.
 * 
 * Features:
 * - Memory cache for fast runtime lookup
 * - Disk persistence for cross-session caching
 * - Hash-based cache keys (source + defines + compiler version)
 * - Automatic invalidation on source changes
 * - Thread-safe operations
 */

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <filesystem>
#include <optional>
#include <cstdint>

namespace Sanic {

/**
 * Unique identifier for a compiled shader variant
 */
struct ShaderCacheKey {
    uint64_t sourceHash;           // Hash of source code + includes
    uint64_t definesHash;          // Hash of preprocessor defines
    uint32_t shaderStage;          // Vertex, Fragment, Compute, etc.
    uint32_t compilerVersion;      // For invalidation on compiler updates
    
    bool operator==(const ShaderCacheKey& other) const {
        return sourceHash == other.sourceHash &&
               definesHash == other.definesHash &&
               shaderStage == other.shaderStage &&
               compilerVersion == other.compilerVersion;
    }
};

/**
 * Hash functor for ShaderCacheKey
 */
struct ShaderCacheKeyHash {
    size_t operator()(const ShaderCacheKey& key) const {
        // FNV-1a inspired combining
        size_t h = 14695981039346656037ULL;
        h ^= key.sourceHash;
        h *= 1099511628211ULL;
        h ^= key.definesHash;
        h *= 1099511628211ULL;
        h ^= key.shaderStage;
        h *= 1099511628211ULL;
        h ^= key.compilerVersion;
        h *= 1099511628211ULL;
        return h;
    }
};

/**
 * Reflected binding information (cached with shader)
 */
struct ReflectedBinding {
    uint32_t set;
    uint32_t binding;
    uint32_t descriptorType;   // VkDescriptorType equivalent
    uint32_t count;
    std::string name;
    uint32_t size;             // For uniform/storage buffers
};

/**
 * Reflected push constant range
 */
struct ReflectedPushConstant {
    uint32_t offset;
    uint32_t size;
    std::string name;
};

/**
 * Reflected vertex input attribute
 */
struct ReflectedVertexInput {
    uint32_t location;
    uint32_t format;  // VkFormat
    std::string name;
};

/**
 * Cached shader entry containing SPIR-V and reflection data
 */
struct ShaderCacheEntry {
    std::vector<uint32_t> spirv;
    uint64_t timestamp;            // When compiled
    std::string entryPoint;
    
    // Reflection data (cached to avoid re-parsing SPIR-V)
    std::vector<ReflectedBinding> bindings;
    std::vector<ReflectedPushConstant> pushConstants;
    std::vector<ReflectedVertexInput> vertexInputs;
    
    // Compute shader workgroup size
    uint32_t workgroupSize[3] = {1, 1, 1};
};

/**
 * Cache statistics for monitoring
 */
struct ShaderCacheStats {
    uint32_t hits = 0;
    uint32_t misses = 0;
    uint32_t entriesInMemory = 0;
    uint64_t totalSpirvBytes = 0;
    uint32_t diskEntries = 0;
    uint64_t diskBytes = 0;
};

/**
 * Shader cache system providing both memory and disk caching
 */
class ShaderCache {
public:
    ShaderCache();
    ~ShaderCache();
    
    // Non-copyable
    ShaderCache(const ShaderCache&) = delete;
    ShaderCache& operator=(const ShaderCache&) = delete;
    
    /**
     * Initialize with cache directory
     * @param cacheDir Directory for persistent cache files
     * @return true if initialization successful
     */
    bool initialize(const std::filesystem::path& cacheDir);
    
    /**
     * Shutdown and save cache to disk
     */
    void shutdown();
    
    /**
     * Look up a compiled shader by key
     * @param key The cache key
     * @return Optional containing cached entry if found
     */
    std::optional<ShaderCacheEntry> lookup(const ShaderCacheKey& key);
    
    /**
     * Store a compiled shader
     * @param key The cache key
     * @param entry The compiled shader entry
     */
    void store(const ShaderCacheKey& key, const ShaderCacheEntry& entry);
    
    /**
     * Invalidate all entries with a specific source hash
     * Call this when a source file changes
     * @param sourceHash Hash of the changed source
     */
    void invalidate(uint64_t sourceHash);
    
    /**
     * Invalidate all cached entries
     */
    void invalidateAll();
    
    /**
     * Load cache from disk
     * @return true if load successful
     */
    bool loadFromDisk();
    
    /**
     * Save cache to disk
     * @return true if save successful
     */
    bool saveToDisk();
    
    /**
     * Get current cache statistics
     */
    ShaderCacheStats getStats() const;
    
    /**
     * Compute hash for source code
     */
    static uint64_t hashSource(const std::string& source);
    
    /**
     * Compute hash for preprocessor defines
     */
    static uint64_t hashDefines(const std::vector<std::pair<std::string, std::string>>& defines);
    
    /**
     * Get current compiler version for cache invalidation
     */
    static uint32_t getCompilerVersion();
    
private:
    std::filesystem::path cacheDir_;
    std::unordered_map<ShaderCacheKey, ShaderCacheEntry, ShaderCacheKeyHash> memoryCache_;
    mutable std::mutex mutex_;
    
    mutable ShaderCacheStats stats_;
    bool initialized_ = false;
    
    // File format helpers
    bool readCacheFile(const std::filesystem::path& path, ShaderCacheEntry& entry);
    bool writeCacheFile(const std::filesystem::path& path, const ShaderCacheEntry& entry);
    std::filesystem::path getCacheFilePath(const ShaderCacheKey& key);
    
    // Index file for quick lookup without reading all files
    bool loadIndex();
    bool saveIndex();
    std::unordered_map<ShaderCacheKey, std::filesystem::path, ShaderCacheKeyHash> diskIndex_;
};

/**
 * Get the global shader cache instance
 */
ShaderCache& GetShaderCache();

} // namespace Sanic
