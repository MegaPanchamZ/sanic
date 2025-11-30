/**
 * ShaderPermutation.h
 * 
 * Shader permutation system for managing shader variants.
 * Supports boolean, integer, and enum dimensions for creating
 * all valid combinations of shader features.
 * 
 * Features:
 * - Boolean permutations (USE_NORMAL_MAP, ENABLE_SSS, etc.)
 * - Integer permutations (QUALITY_LEVEL = 0, 1, 2, 3)
 * - Enum permutations (LIGHTING_MODEL = "LAMBERT", "PBR", "TOON")
 * - Permutation filtering (exclude invalid combinations)
 * - Pre-compilation of all variants
 */

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <variant>
#include <functional>
#include <memory>
#include <optional>

namespace Sanic {

/**
 * A single dimension of permutation (one axis of variation)
 */
struct PermutationDimension {
    std::string name;              // e.g., "USE_NORMAL_MAP"
    
    // Possible values for this dimension
    std::variant<
        std::vector<bool>,         // Boolean: true/false
        std::vector<int32_t>,      // Integer: 0, 1, 2, 3...
        std::vector<std::string>   // Enum: "LOW", "MEDIUM", "HIGH"
    > values;
    
    // Default value index
    uint32_t defaultIndex = 0;
    
    // Get total number of values
    uint32_t getValueCount() const {
        return std::visit([](const auto& v) -> uint32_t { 
            return static_cast<uint32_t>(v.size()); 
        }, values);
    }
};

/**
 * A specific permutation key (a selection of values from each dimension)
 */
struct PermutationKey {
    // Maps dimension name to selected value index
    std::unordered_map<std::string, uint32_t> dimensionValues;
    
    bool operator==(const PermutationKey& other) const {
        return dimensionValues == other.dimensionValues;
    }
    
    /**
     * Compute hash for this permutation
     */
    uint64_t hash() const {
        uint64_t h = 14695981039346656037ULL;
        for (const auto& [name, value] : dimensionValues) {
            for (char c : name) {
                h ^= static_cast<uint64_t>(c);
                h *= 1099511628211ULL;
            }
            h ^= value;
            h *= 1099511628211ULL;
        }
        return h;
    }
    
    /**
     * Create a key with default values for all dimensions
     */
    static PermutationKey createDefault(const std::vector<PermutationDimension>& dimensions) {
        PermutationKey key;
        for (const auto& dim : dimensions) {
            key.dimensionValues[dim.name] = dim.defaultIndex;
        }
        return key;
    }
};

/**
 * Hash functor for PermutationKey
 */
struct PermutationKeyHash {
    size_t operator()(const PermutationKey& key) const {
        return static_cast<size_t>(key.hash());
    }
};

// Forward declaration
class ShaderCompiler;

/**
 * A shader with multiple permutations (variants)
 */
class ShaderPermutationSet {
public:
    /**
     * Create a new permutation set
     * @param name Unique name for this shader
     */
    explicit ShaderPermutationSet(const std::string& name);
    ~ShaderPermutationSet();
    
    /**
     * Add a boolean dimension (results in 2 variants)
     * @param name Define name (e.g., "USE_NORMAL_MAP")
     * @param defaultValue Default value
     */
    void addBoolDimension(const std::string& name, bool defaultValue = false);
    
    /**
     * Add an integer dimension
     * @param name Define name (e.g., "QUALITY_LEVEL")
     * @param values Possible integer values
     * @param defaultValue Default value (must be in values)
     */
    void addIntDimension(const std::string& name, 
                        const std::vector<int32_t>& values, 
                        int32_t defaultValue = 0);
    
    /**
     * Add an enum/string dimension
     * @param name Define name (e.g., "LIGHTING_MODEL")
     * @param values Possible string values
     * @param defaultValue Default value (must be in values)
     */
    void addEnumDimension(const std::string& name,
                         const std::vector<std::string>& values,
                         const std::string& defaultValue);
    
    /**
     * Set the shader source code
     */
    void setSource(const std::string& source);
    
    /**
     * Set source from a file path (will be loaded when needed)
     */
    void setSourceFile(const std::string& path);
    
    /**
     * Generate preprocessor defines for a specific permutation
     * @param key The permutation key
     * @return Vector of (name, value) define pairs
     */
    std::vector<std::pair<std::string, std::string>> getDefines(const PermutationKey& key) const;
    
    /**
     * Generate all valid permutation keys
     */
    std::vector<PermutationKey> getAllPermutations() const;
    
    /**
     * Get the default permutation key
     */
    PermutationKey getDefaultPermutation() const;
    
    /**
     * Get total number of permutations
     */
    uint32_t getPermutationCount() const;
    
    /**
     * Set a filter function to exclude invalid permutation combinations
     * @param filter Function returning true for valid permutations
     */
    using PermutationFilter = std::function<bool(const PermutationKey&)>;
    void setFilter(PermutationFilter filter);
    
    /**
     * Check if a permutation is valid (passes filter)
     */
    bool isValidPermutation(const PermutationKey& key) const;
    
    /**
     * Pre-compile all permutations
     * @param compiler The shader compiler to use
     * @param stage Shader stage
     */
    void compileAll(class ShaderCompiler& compiler, uint32_t stage);
    
    // Accessors
    const std::string& getName() const { return name_; }
    const std::string& getSource() const;
    const std::vector<PermutationDimension>& getDimensions() const { return dimensions_; }
    
private:
    std::string name_;
    std::string source_;
    std::string sourcePath_;
    mutable std::string loadedSource_;  // Lazily loaded from file
    std::vector<PermutationDimension> dimensions_;
    PermutationFilter filter_;
    
    void generatePermutationsRecursive(
        std::vector<PermutationKey>& results,
        PermutationKey& current,
        size_t dimensionIndex) const;
};

/**
 * Global manager for shader permutation sets
 */
class ShaderPermutationManager {
public:
    static ShaderPermutationManager& getInstance();
    
    /**
     * Register a new permutation set
     */
    void registerShader(std::shared_ptr<ShaderPermutationSet> shader);
    
    /**
     * Get a registered shader by name
     */
    ShaderPermutationSet* getShader(const std::string& name);
    
    /**
     * Get all registered shaders
     */
    std::vector<std::string> getShaderNames() const;
    
    /**
     * Pre-compile all registered shaders
     */
    void precompileAll(class ShaderCompiler& compiler);
    
    /**
     * Clear all registered shaders
     */
    void clear();
    
private:
    ShaderPermutationManager() = default;
    ShaderPermutationManager(const ShaderPermutationManager&) = delete;
    ShaderPermutationManager& operator=(const ShaderPermutationManager&) = delete;
    
    std::unordered_map<std::string, std::shared_ptr<ShaderPermutationSet>> shaders_;
};

/**
 * Helper to create a permutation key with specific values
 */
class PermutationKeyBuilder {
public:
    PermutationKeyBuilder& set(const std::string& dimension, bool value) {
        key_.dimensionValues[dimension] = value ? 1 : 0;
        return *this;
    }
    
    PermutationKeyBuilder& set(const std::string& dimension, int32_t valueIndex) {
        key_.dimensionValues[dimension] = static_cast<uint32_t>(valueIndex);
        return *this;
    }
    
    PermutationKeyBuilder& setEnum(const std::string& dimension, uint32_t valueIndex) {
        key_.dimensionValues[dimension] = valueIndex;
        return *this;
    }
    
    PermutationKey build() const { return key_; }
    
private:
    PermutationKey key_;
};

} // namespace Sanic
