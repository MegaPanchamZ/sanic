/**
 * ShaderIncluder.cpp
 * 
 * Implementation of the shader include system.
 */

#include "ShaderIncluder.h"
#include "ShaderCache.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

namespace Sanic {

ShaderIncluder::ShaderIncluder() = default;

ShaderIncluder::~ShaderIncluder() = default;

void ShaderIncluder::addIncludePath(const std::filesystem::path& path) {
    if (std::filesystem::exists(path) && std::filesystem::is_directory(path)) {
        // Avoid duplicates
        auto it = std::find(includePaths_.begin(), includePaths_.end(), path);
        if (it == includePaths_.end()) {
            includePaths_.push_back(path);
        }
    }
}

void ShaderIncluder::clearIncludePaths() {
    includePaths_.clear();
}

void ShaderIncluder::registerVirtualFile(const std::string& name, const std::string& content) {
    virtualFiles_[name] = content;
}

void ShaderIncluder::unregisterVirtualFile(const std::string& name) {
    virtualFiles_.erase(name);
}

uint64_t ShaderIncluder::computeIncludesHash() const {
    uint64_t hash = 14695981039346656037ULL;
    
    // Sort included files for consistent hashing
    std::vector<std::string> sortedFiles(includedFiles_.begin(), includedFiles_.end());
    std::sort(sortedFiles.begin(), sortedFiles.end());
    
    for (const auto& file : sortedFiles) {
        // Hash the filename
        for (char c : file) {
            hash ^= static_cast<uint64_t>(c);
            hash *= 1099511628211ULL;
        }
        
        // Hash the content if available
        auto cacheIt = fileCache_.find(file);
        if (cacheIt != fileCache_.end()) {
            hash ^= ShaderCache::hashSource(cacheIt->second);
            hash *= 1099511628211ULL;
        }
    }
    
    return hash;
}

void ShaderIncluder::resetTracking() {
    includedFiles_.clear();
    errors_.clear();
    // Don't clear file cache - it's still valid
}

shaderc_include_result* ShaderIncluder::GetInclude(
    const char* requested_source,
    shaderc_include_type type,
    const char* requesting_source,
    size_t include_depth) {
    
    // Check for include depth limit
    if (include_depth > MAX_INCLUDE_DEPTH) {
        auto data = std::make_unique<IncludeData>();
        data->content = "Error: Maximum include depth exceeded";
        data->sourceName = "";
        data->result.source_name = data->sourceName.c_str();
        data->result.source_name_length = 0;
        data->result.content = data->content.c_str();
        data->result.content_length = data->content.size();
        data->result.user_data = data.get();
        
        auto* result = &data->result;
        includeDataPool_.push_back(std::move(data));
        
        errors_.push_back("Maximum include depth exceeded for: " + std::string(requested_source));
        return result;
    }
    
    // Try to resolve the include
    auto resolved = resolveInclude(
        requested_source,
        requesting_source ? requesting_source : "",
        type
    );
    
    if (!resolved) {
        // Include not found
        auto data = std::make_unique<IncludeData>();
        data->content = "Error: Cannot find include file: " + std::string(requested_source);
        data->sourceName = "";
        data->result.source_name = data->sourceName.c_str();
        data->result.source_name_length = 0;
        data->result.content = data->content.c_str();
        data->result.content_length = data->content.size();
        data->result.user_data = data.get();
        
        auto* result = &data->result;
        includeDataPool_.push_back(std::move(data));
        
        errors_.push_back("Cannot find include file: " + std::string(requested_source));
        return result;
    }
    
    // Create include result
    auto data = std::make_unique<IncludeData>();
    data->content = std::move(resolved->first);
    data->sourceName = std::move(resolved->second);
    data->result.source_name = data->sourceName.c_str();
    data->result.source_name_length = data->sourceName.size();
    data->result.content = data->content.c_str();
    data->result.content_length = data->content.size();
    data->result.user_data = data.get();
    
    // Track this include for dependency management
    includedFiles_.insert(data->sourceName);
    
    auto* result = &data->result;
    includeDataPool_.push_back(std::move(data));
    return result;
}

void ShaderIncluder::ReleaseInclude(shaderc_include_result* data) {
    // Memory is managed by includeDataPool_, so we don't delete here
    // We could find and remove the entry, but keeping them around
    // for the duration of compilation is fine
}

std::optional<std::pair<std::string, std::string>> ShaderIncluder::resolveInclude(
    const std::string& requested,
    const std::string& requesting,
    shaderc_include_type type) {
    
    // First check virtual files
    auto virtualIt = virtualFiles_.find(requested);
    if (virtualIt != virtualFiles_.end()) {
        fileCache_[requested] = virtualIt->second;
        return std::make_pair(virtualIt->second, requested);
    }
    
    std::filesystem::path requestedPath(requested);
    
    // For relative includes (#include "file.h"), try relative to requesting file first
    if (type == shaderc_include_type_relative && !requesting.empty()) {
        std::filesystem::path requestingPath(requesting);
        auto parentDir = requestingPath.parent_path();
        auto resolvedPath = parentDir / requestedPath;
        
        if (std::filesystem::exists(resolvedPath)) {
            auto content = readFile(resolvedPath);
            if (content) {
                auto pathStr = resolvedPath.string();
                fileCache_[pathStr] = *content;
                return std::make_pair(*content, pathStr);
            }
        }
    }
    
    // Search include paths
    for (const auto& includePath : includePaths_) {
        auto resolvedPath = includePath / requestedPath;
        
        if (std::filesystem::exists(resolvedPath)) {
            auto content = readFile(resolvedPath);
            if (content) {
                auto pathStr = resolvedPath.string();
                fileCache_[pathStr] = *content;
                return std::make_pair(*content, pathStr);
            }
        }
    }
    
    // Try as absolute path
    if (requestedPath.is_absolute() && std::filesystem::exists(requestedPath)) {
        auto content = readFile(requestedPath);
        if (content) {
            auto pathStr = requestedPath.string();
            fileCache_[pathStr] = *content;
            return std::make_pair(*content, pathStr);
        }
    }
    
    return std::nullopt;
}

std::optional<std::string> ShaderIncluder::readFile(const std::filesystem::path& path) {
    // Check cache first
    auto pathStr = path.string();
    auto cacheIt = fileCache_.find(pathStr);
    if (cacheIt != fileCache_.end()) {
        return cacheIt->second;
    }
    
    std::ifstream file(path);
    if (!file.is_open()) {
        return std::nullopt;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

} // namespace Sanic
