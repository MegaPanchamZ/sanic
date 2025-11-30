/**
 * ShaderPermutation.cpp
 * 
 * Implementation of the shader permutation system.
 */

#include "ShaderPermutation.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

namespace Sanic {

ShaderPermutationSet::ShaderPermutationSet(const std::string& name)
    : name_(name) {
}

ShaderPermutationSet::~ShaderPermutationSet() = default;

void ShaderPermutationSet::addBoolDimension(const std::string& name, bool defaultValue) {
    PermutationDimension dim;
    dim.name = name;
    dim.values = std::vector<bool>{false, true};
    dim.defaultIndex = defaultValue ? 1 : 0;
    dimensions_.push_back(std::move(dim));
}

void ShaderPermutationSet::addIntDimension(const std::string& name,
                                            const std::vector<int32_t>& values,
                                            int32_t defaultValue) {
    if (values.empty()) return;
    
    PermutationDimension dim;
    dim.name = name;
    dim.values = values;
    
    // Find default value index
    auto it = std::find(values.begin(), values.end(), defaultValue);
    dim.defaultIndex = (it != values.end()) 
        ? static_cast<uint32_t>(std::distance(values.begin(), it)) 
        : 0;
    
    dimensions_.push_back(std::move(dim));
}

void ShaderPermutationSet::addEnumDimension(const std::string& name,
                                             const std::vector<std::string>& values,
                                             const std::string& defaultValue) {
    if (values.empty()) return;
    
    PermutationDimension dim;
    dim.name = name;
    dim.values = values;
    
    // Find default value index
    auto it = std::find(values.begin(), values.end(), defaultValue);
    dim.defaultIndex = (it != values.end())
        ? static_cast<uint32_t>(std::distance(values.begin(), it))
        : 0;
    
    dimensions_.push_back(std::move(dim));
}

void ShaderPermutationSet::setSource(const std::string& source) {
    source_ = source;
    sourcePath_.clear();
}

void ShaderPermutationSet::setSourceFile(const std::string& path) {
    sourcePath_ = path;
    source_.clear();
    loadedSource_.clear();
}

const std::string& ShaderPermutationSet::getSource() const {
    if (!source_.empty()) {
        return source_;
    }
    
    if (loadedSource_.empty() && !sourcePath_.empty()) {
        std::ifstream file(sourcePath_);
        if (file.is_open()) {
            std::stringstream buffer;
            buffer << file.rdbuf();
            loadedSource_ = buffer.str();
        }
    }
    
    return loadedSource_;
}

std::vector<std::pair<std::string, std::string>> ShaderPermutationSet::getDefines(
    const PermutationKey& key) const {
    
    std::vector<std::pair<std::string, std::string>> defines;
    
    for (const auto& dim : dimensions_) {
        auto it = key.dimensionValues.find(dim.name);
        uint32_t valueIndex = (it != key.dimensionValues.end()) 
            ? it->second 
            : dim.defaultIndex;
        
        std::visit([&](const auto& values) {
            using T = std::decay_t<decltype(values)>;
            
            if (valueIndex >= values.size()) {
                valueIndex = 0;
            }
            
            if constexpr (std::is_same_v<T, std::vector<bool>>) {
                // Boolean: define if true, don't define if false
                if (values[valueIndex]) {
                    defines.emplace_back(dim.name, "1");
                } else {
                    defines.emplace_back(dim.name, "0");
                }
            } else if constexpr (std::is_same_v<T, std::vector<int32_t>>) {
                // Integer: define with value
                defines.emplace_back(dim.name, std::to_string(values[valueIndex]));
            } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
                // Enum: define with the string value
                // Also define individual flags for each enum value
                for (size_t i = 0; i < values.size(); i++) {
                    std::string enumDefine = dim.name + "_" + values[i];
                    defines.emplace_back(enumDefine, (i == valueIndex) ? "1" : "0");
                }
                // And the main define with the index
                defines.emplace_back(dim.name, std::to_string(valueIndex));
            }
        }, dim.values);
    }
    
    return defines;
}

std::vector<PermutationKey> ShaderPermutationSet::getAllPermutations() const {
    std::vector<PermutationKey> results;
    
    if (dimensions_.empty()) {
        // No dimensions = single default permutation
        results.push_back(PermutationKey{});
        return results;
    }
    
    PermutationKey current;
    generatePermutationsRecursive(results, current, 0);
    
    return results;
}

PermutationKey ShaderPermutationSet::getDefaultPermutation() const {
    return PermutationKey::createDefault(dimensions_);
}

uint32_t ShaderPermutationSet::getPermutationCount() const {
    if (dimensions_.empty()) return 1;
    
    uint32_t count = 1;
    for (const auto& dim : dimensions_) {
        count *= dim.getValueCount();
    }
    return count;
}

void ShaderPermutationSet::setFilter(PermutationFilter filter) {
    filter_ = std::move(filter);
}

bool ShaderPermutationSet::isValidPermutation(const PermutationKey& key) const {
    if (filter_) {
        return filter_(key);
    }
    return true;
}

void ShaderPermutationSet::generatePermutationsRecursive(
    std::vector<PermutationKey>& results,
    PermutationKey& current,
    size_t dimensionIndex) const {
    
    if (dimensionIndex >= dimensions_.size()) {
        // Check filter before adding
        if (isValidPermutation(current)) {
            results.push_back(current);
        }
        return;
    }
    
    const auto& dim = dimensions_[dimensionIndex];
    uint32_t valueCount = dim.getValueCount();
    
    for (uint32_t i = 0; i < valueCount; i++) {
        current.dimensionValues[dim.name] = i;
        generatePermutationsRecursive(results, current, dimensionIndex + 1);
    }
}

void ShaderPermutationSet::compileAll(ShaderCompiler& /*compiler*/, uint32_t /*stage*/) {
    // Implementation would compile all permutations using the provided compiler
    // This will be implemented when ShaderCompiler is updated
    auto permutations = getAllPermutations();
    std::cout << "ShaderPermutationSet: " << name_ << " has " 
              << permutations.size() << " valid permutations" << std::endl;
}

// ShaderPermutationManager implementation

ShaderPermutationManager& ShaderPermutationManager::getInstance() {
    static ShaderPermutationManager instance;
    return instance;
}

void ShaderPermutationManager::registerShader(std::shared_ptr<ShaderPermutationSet> shader) {
    if (shader) {
        shaders_[shader->getName()] = std::move(shader);
    }
}

ShaderPermutationSet* ShaderPermutationManager::getShader(const std::string& name) {
    auto it = shaders_.find(name);
    return (it != shaders_.end()) ? it->second.get() : nullptr;
}

std::vector<std::string> ShaderPermutationManager::getShaderNames() const {
    std::vector<std::string> names;
    names.reserve(shaders_.size());
    for (const auto& [name, _] : shaders_) {
        names.push_back(name);
    }
    return names;
}

void ShaderPermutationManager::precompileAll(ShaderCompiler& compiler) {
    for (auto& [name, shader] : shaders_) {
        std::cout << "Pre-compiling shader: " << name << std::endl;
        // Stage would need to be specified per shader
        shader->compileAll(compiler, 0);
    }
}

void ShaderPermutationManager::clear() {
    shaders_.clear();
}

} // namespace Sanic
