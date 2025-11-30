/**
 * ShaderIncluder.h
 * 
 * Virtual file system for shader includes using shaderc's IncluderInterface.
 * Supports:
 * - Multiple include search paths
 * - Virtual files for generated code
 * - Include dependency tracking for cache invalidation
 * - Both quoted and angled includes
 */

#pragma once

#include <shaderc/shaderc.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <vector>
#include <memory>
#include <optional>

namespace Sanic {

/**
 * Shader includer implementing shaderc's IncluderInterface
 */
class ShaderIncluder : public shaderc::CompileOptions::IncluderInterface {
public:
    ShaderIncluder();
    ~ShaderIncluder() override;
    
    /**
     * Add a search path for includes
     * @param path Directory to search for included files
     */
    void addIncludePath(const std::filesystem::path& path);
    
    /**
     * Clear all include paths
     */
    void clearIncludePaths();
    
    /**
     * Register a virtual file (for generated code or common includes)
     * @param name Virtual filename (used in #include directives)
     * @param content File contents
     */
    void registerVirtualFile(const std::string& name, const std::string& content);
    
    /**
     * Unregister a virtual file
     */
    void unregisterVirtualFile(const std::string& name);
    
    /**
     * Get the set of all files that were included during compilation
     * Useful for dependency tracking and cache invalidation
     */
    const std::unordered_set<std::string>& getIncludedFiles() const { return includedFiles_; }
    
    /**
     * Compute a hash of all included files (source content)
     * @return Combined hash of all included files
     */
    uint64_t computeIncludesHash() const;
    
    /**
     * Clear included files tracking
     * Call before starting a new compilation
     */
    void resetTracking();
    
    /**
     * Check if include resolution was successful
     */
    bool hadErrors() const { return !errors_.empty(); }
    
    /**
     * Get include resolution errors
     */
    const std::vector<std::string>& getErrors() const { return errors_; }
    
    // shaderc::CompileOptions::IncluderInterface implementation
    shaderc_include_result* GetInclude(
        const char* requested_source,
        shaderc_include_type type,
        const char* requesting_source,
        size_t include_depth) override;
    
    void ReleaseInclude(shaderc_include_result* data) override;
    
private:
    /**
     * Try to resolve an include path
     * @param requested The requested include path
     * @param requesting The file that's doing the including
     * @param type Whether it's a quoted or angled include
     * @return Resolved content and path, or nullopt if not found
     */
    std::optional<std::pair<std::string, std::string>> resolveInclude(
        const std::string& requested,
        const std::string& requesting,
        shaderc_include_type type);
    
    /**
     * Read file content from disk
     */
    std::optional<std::string> readFile(const std::filesystem::path& path);
    
    // Include search paths (in order of priority)
    std::vector<std::filesystem::path> includePaths_;
    
    // Virtual files (name -> content)
    std::unordered_map<std::string, std::string> virtualFiles_;
    
    // Track which files were included (for dependency tracking)
    std::unordered_set<std::string> includedFiles_;
    
    // Cache of file contents to avoid re-reading
    std::unordered_map<std::string, std::string> fileCache_;
    
    // Include resolution errors
    std::vector<std::string> errors_;
    
    // Memory management for include results
    struct IncludeData {
        std::string content;
        std::string sourceName;
        shaderc_include_result result;
    };
    std::vector<std::unique_ptr<IncludeData>> includeDataPool_;
    
    // Maximum include depth to prevent infinite recursion
    static constexpr size_t MAX_INCLUDE_DEPTH = 64;
};

} // namespace Sanic
