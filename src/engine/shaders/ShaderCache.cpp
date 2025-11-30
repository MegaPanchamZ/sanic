/**
 * ShaderCache.cpp
 * 
 * Implementation of the shader caching system.
 */

#include "ShaderCache.h"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <chrono>

namespace Sanic {

// Cache file format version - increment when format changes
static constexpr uint32_t CACHE_FORMAT_VERSION = 1;

// Magic bytes for cache files
static constexpr uint32_t CACHE_MAGIC = 0x53484352; // "SHCR"
static constexpr uint32_t INDEX_MAGIC = 0x53484349; // "SHCI"

// Current compiler version - update when shaderc is updated
static constexpr uint32_t COMPILER_VERSION = 1;

ShaderCache::ShaderCache() = default;

ShaderCache::~ShaderCache() {
    if (initialized_) {
        shutdown();
    }
}

bool ShaderCache::initialize(const std::filesystem::path& cacheDir) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (initialized_) {
        return true;
    }
    
    cacheDir_ = cacheDir;
    
    // Create cache directory if it doesn't exist
    try {
        if (!std::filesystem::exists(cacheDir_)) {
            std::filesystem::create_directories(cacheDir_);
        }
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "ShaderCache: Failed to create cache directory: " << e.what() << std::endl;
        return false;
    }
    
    // Load existing cache index
    loadIndex();
    
    initialized_ = true;
    std::cout << "ShaderCache: Initialized at " << cacheDir_.string() 
              << " with " << diskIndex_.size() << " cached entries" << std::endl;
    
    return true;
}

void ShaderCache::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        return;
    }
    
    saveToDisk();
    saveIndex();
    
    memoryCache_.clear();
    diskIndex_.clear();
    initialized_ = false;
}

std::optional<ShaderCacheEntry> ShaderCache::lookup(const ShaderCacheKey& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check memory cache first
    auto memIt = memoryCache_.find(key);
    if (memIt != memoryCache_.end()) {
        stats_.hits++;
        return memIt->second;
    }
    
    // Check disk cache
    auto diskIt = diskIndex_.find(key);
    if (diskIt != diskIndex_.end()) {
        ShaderCacheEntry entry;
        if (readCacheFile(diskIt->second, entry)) {
            // Promote to memory cache
            memoryCache_[key] = entry;
            stats_.entriesInMemory = static_cast<uint32_t>(memoryCache_.size());
            stats_.totalSpirvBytes += entry.spirv.size() * sizeof(uint32_t);
            stats_.hits++;
            return entry;
        }
        // File read failed, remove from index
        diskIndex_.erase(diskIt);
    }
    
    stats_.misses++;
    return std::nullopt;
}

void ShaderCache::store(const ShaderCacheKey& key, const ShaderCacheEntry& entry) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Store in memory cache
    auto existingIt = memoryCache_.find(key);
    if (existingIt != memoryCache_.end()) {
        stats_.totalSpirvBytes -= existingIt->second.spirv.size() * sizeof(uint32_t);
    }
    
    memoryCache_[key] = entry;
    stats_.entriesInMemory = static_cast<uint32_t>(memoryCache_.size());
    stats_.totalSpirvBytes += entry.spirv.size() * sizeof(uint32_t);
    
    // Write to disk immediately for persistence
    auto filePath = getCacheFilePath(key);
    if (writeCacheFile(filePath, entry)) {
        diskIndex_[key] = filePath;
        stats_.diskEntries = static_cast<uint32_t>(diskIndex_.size());
    }
}

void ShaderCache::invalidate(uint64_t sourceHash) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Remove from memory cache
    for (auto it = memoryCache_.begin(); it != memoryCache_.end();) {
        if (it->first.sourceHash == sourceHash) {
            stats_.totalSpirvBytes -= it->second.spirv.size() * sizeof(uint32_t);
            it = memoryCache_.erase(it);
        } else {
            ++it;
        }
    }
    stats_.entriesInMemory = static_cast<uint32_t>(memoryCache_.size());
    
    // Remove from disk
    for (auto it = diskIndex_.begin(); it != diskIndex_.end();) {
        if (it->first.sourceHash == sourceHash) {
            try {
                std::filesystem::remove(it->second);
            } catch (...) {}
            it = diskIndex_.erase(it);
        } else {
            ++it;
        }
    }
    stats_.diskEntries = static_cast<uint32_t>(diskIndex_.size());
}

void ShaderCache::invalidateAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    memoryCache_.clear();
    
    // Delete all cache files
    for (const auto& [key, path] : diskIndex_) {
        try {
            std::filesystem::remove(path);
        } catch (...) {}
    }
    diskIndex_.clear();
    
    stats_ = ShaderCacheStats{};
}

bool ShaderCache::loadFromDisk() {
    // Already done in loadIndex - this loads individual entries on demand
    return true;
}

bool ShaderCache::saveToDisk() {
    // Entries are written immediately on store, but ensure index is saved
    return saveIndex();
}

ShaderCacheStats ShaderCache::getStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

uint64_t ShaderCache::hashSource(const std::string& source) {
    // FNV-1a 64-bit hash
    uint64_t hash = 14695981039346656037ULL;
    for (char c : source) {
        hash ^= static_cast<uint64_t>(c);
        hash *= 1099511628211ULL;
    }
    return hash;
}

uint64_t ShaderCache::hashDefines(const std::vector<std::pair<std::string, std::string>>& defines) {
    uint64_t hash = 14695981039346656037ULL;
    for (const auto& [name, value] : defines) {
        for (char c : name) {
            hash ^= static_cast<uint64_t>(c);
            hash *= 1099511628211ULL;
        }
        hash ^= '=';
        hash *= 1099511628211ULL;
        for (char c : value) {
            hash ^= static_cast<uint64_t>(c);
            hash *= 1099511628211ULL;
        }
        hash ^= '\n';
        hash *= 1099511628211ULL;
    }
    return hash;
}

uint32_t ShaderCache::getCompilerVersion() {
    return COMPILER_VERSION;
}

std::filesystem::path ShaderCache::getCacheFilePath(const ShaderCacheKey& key) {
    // Create a unique filename from the key
    char filename[64];
    snprintf(filename, sizeof(filename), "%016llx_%016llx_%u_%u.spvcache",
             static_cast<unsigned long long>(key.sourceHash),
             static_cast<unsigned long long>(key.definesHash),
             key.shaderStage,
             key.compilerVersion);
    return cacheDir_ / filename;
}

bool ShaderCache::readCacheFile(const std::filesystem::path& path, ShaderCacheEntry& entry) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    // Read header
    uint32_t magic, version;
    file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    
    if (magic != CACHE_MAGIC || version != CACHE_FORMAT_VERSION) {
        return false;
    }
    
    // Read timestamp
    file.read(reinterpret_cast<char*>(&entry.timestamp), sizeof(entry.timestamp));
    
    // Read entry point
    uint32_t entryPointLen;
    file.read(reinterpret_cast<char*>(&entryPointLen), sizeof(entryPointLen));
    entry.entryPoint.resize(entryPointLen);
    file.read(entry.entryPoint.data(), entryPointLen);
    
    // Read SPIR-V
    uint32_t spirvSize;
    file.read(reinterpret_cast<char*>(&spirvSize), sizeof(spirvSize));
    entry.spirv.resize(spirvSize);
    file.read(reinterpret_cast<char*>(entry.spirv.data()), spirvSize * sizeof(uint32_t));
    
    // Read workgroup size
    file.read(reinterpret_cast<char*>(entry.workgroupSize), sizeof(entry.workgroupSize));
    
    // Read bindings
    uint32_t bindingCount;
    file.read(reinterpret_cast<char*>(&bindingCount), sizeof(bindingCount));
    entry.bindings.resize(bindingCount);
    for (auto& binding : entry.bindings) {
        file.read(reinterpret_cast<char*>(&binding.set), sizeof(binding.set));
        file.read(reinterpret_cast<char*>(&binding.binding), sizeof(binding.binding));
        file.read(reinterpret_cast<char*>(&binding.descriptorType), sizeof(binding.descriptorType));
        file.read(reinterpret_cast<char*>(&binding.count), sizeof(binding.count));
        file.read(reinterpret_cast<char*>(&binding.size), sizeof(binding.size));
        
        uint32_t nameLen;
        file.read(reinterpret_cast<char*>(&nameLen), sizeof(nameLen));
        binding.name.resize(nameLen);
        file.read(binding.name.data(), nameLen);
    }
    
    // Read push constants
    uint32_t pushConstantCount;
    file.read(reinterpret_cast<char*>(&pushConstantCount), sizeof(pushConstantCount));
    entry.pushConstants.resize(pushConstantCount);
    for (auto& pc : entry.pushConstants) {
        file.read(reinterpret_cast<char*>(&pc.offset), sizeof(pc.offset));
        file.read(reinterpret_cast<char*>(&pc.size), sizeof(pc.size));
        
        uint32_t nameLen;
        file.read(reinterpret_cast<char*>(&nameLen), sizeof(nameLen));
        pc.name.resize(nameLen);
        file.read(pc.name.data(), nameLen);
    }
    
    // Read vertex inputs
    uint32_t vertexInputCount;
    file.read(reinterpret_cast<char*>(&vertexInputCount), sizeof(vertexInputCount));
    entry.vertexInputs.resize(vertexInputCount);
    for (auto& input : entry.vertexInputs) {
        file.read(reinterpret_cast<char*>(&input.location), sizeof(input.location));
        file.read(reinterpret_cast<char*>(&input.format), sizeof(input.format));
        
        uint32_t nameLen;
        file.read(reinterpret_cast<char*>(&nameLen), sizeof(nameLen));
        input.name.resize(nameLen);
        file.read(input.name.data(), nameLen);
    }
    
    return file.good();
}

bool ShaderCache::writeCacheFile(const std::filesystem::path& path, const ShaderCacheEntry& entry) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    // Write header
    uint32_t magic = CACHE_MAGIC;
    uint32_t version = CACHE_FORMAT_VERSION;
    file.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));
    
    // Write timestamp
    file.write(reinterpret_cast<const char*>(&entry.timestamp), sizeof(entry.timestamp));
    
    // Write entry point
    uint32_t entryPointLen = static_cast<uint32_t>(entry.entryPoint.size());
    file.write(reinterpret_cast<const char*>(&entryPointLen), sizeof(entryPointLen));
    file.write(entry.entryPoint.data(), entryPointLen);
    
    // Write SPIR-V
    uint32_t spirvSize = static_cast<uint32_t>(entry.spirv.size());
    file.write(reinterpret_cast<const char*>(&spirvSize), sizeof(spirvSize));
    file.write(reinterpret_cast<const char*>(entry.spirv.data()), spirvSize * sizeof(uint32_t));
    
    // Write workgroup size
    file.write(reinterpret_cast<const char*>(entry.workgroupSize), sizeof(entry.workgroupSize));
    
    // Write bindings
    uint32_t bindingCount = static_cast<uint32_t>(entry.bindings.size());
    file.write(reinterpret_cast<const char*>(&bindingCount), sizeof(bindingCount));
    for (const auto& binding : entry.bindings) {
        file.write(reinterpret_cast<const char*>(&binding.set), sizeof(binding.set));
        file.write(reinterpret_cast<const char*>(&binding.binding), sizeof(binding.binding));
        file.write(reinterpret_cast<const char*>(&binding.descriptorType), sizeof(binding.descriptorType));
        file.write(reinterpret_cast<const char*>(&binding.count), sizeof(binding.count));
        file.write(reinterpret_cast<const char*>(&binding.size), sizeof(binding.size));
        
        uint32_t nameLen = static_cast<uint32_t>(binding.name.size());
        file.write(reinterpret_cast<const char*>(&nameLen), sizeof(nameLen));
        file.write(binding.name.data(), nameLen);
    }
    
    // Write push constants
    uint32_t pushConstantCount = static_cast<uint32_t>(entry.pushConstants.size());
    file.write(reinterpret_cast<const char*>(&pushConstantCount), sizeof(pushConstantCount));
    for (const auto& pc : entry.pushConstants) {
        file.write(reinterpret_cast<const char*>(&pc.offset), sizeof(pc.offset));
        file.write(reinterpret_cast<const char*>(&pc.size), sizeof(pc.size));
        
        uint32_t nameLen = static_cast<uint32_t>(pc.name.size());
        file.write(reinterpret_cast<const char*>(&nameLen), sizeof(nameLen));
        file.write(pc.name.data(), nameLen);
    }
    
    // Write vertex inputs
    uint32_t vertexInputCount = static_cast<uint32_t>(entry.vertexInputs.size());
    file.write(reinterpret_cast<const char*>(&vertexInputCount), sizeof(vertexInputCount));
    for (const auto& input : entry.vertexInputs) {
        file.write(reinterpret_cast<const char*>(&input.location), sizeof(input.location));
        file.write(reinterpret_cast<const char*>(&input.format), sizeof(input.format));
        
        uint32_t nameLen = static_cast<uint32_t>(input.name.size());
        file.write(reinterpret_cast<const char*>(&nameLen), sizeof(nameLen));
        file.write(input.name.data(), nameLen);
    }
    
    return file.good();
}

bool ShaderCache::loadIndex() {
    auto indexPath = cacheDir_ / "cache_index.bin";
    std::ifstream file(indexPath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    uint32_t magic, version, count;
    file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    
    if (magic != INDEX_MAGIC || version != CACHE_FORMAT_VERSION) {
        return false;
    }
    
    file.read(reinterpret_cast<char*>(&count), sizeof(count));
    
    for (uint32_t i = 0; i < count; i++) {
        ShaderCacheKey key;
        file.read(reinterpret_cast<char*>(&key), sizeof(key));
        
        uint32_t pathLen;
        file.read(reinterpret_cast<char*>(&pathLen), sizeof(pathLen));
        std::string pathStr(pathLen, '\0');
        file.read(pathStr.data(), pathLen);
        
        auto filePath = cacheDir_ / pathStr;
        if (std::filesystem::exists(filePath)) {
            diskIndex_[key] = filePath;
        }
    }
    
    stats_.diskEntries = static_cast<uint32_t>(diskIndex_.size());
    return true;
}

bool ShaderCache::saveIndex() {
    auto indexPath = cacheDir_ / "cache_index.bin";
    std::ofstream file(indexPath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    uint32_t magic = INDEX_MAGIC;
    uint32_t version = CACHE_FORMAT_VERSION;
    uint32_t count = static_cast<uint32_t>(diskIndex_.size());
    
    file.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));
    file.write(reinterpret_cast<const char*>(&count), sizeof(count));
    
    for (const auto& [key, path] : diskIndex_) {
        file.write(reinterpret_cast<const char*>(&key), sizeof(key));
        
        std::string pathStr = path.filename().string();
        uint32_t pathLen = static_cast<uint32_t>(pathStr.size());
        file.write(reinterpret_cast<const char*>(&pathLen), sizeof(pathLen));
        file.write(pathStr.data(), pathLen);
    }
    
    return file.good();
}

// Global instance
static ShaderCache g_shaderCache;

ShaderCache& GetShaderCache() {
    return g_shaderCache;
}

} // namespace Sanic
