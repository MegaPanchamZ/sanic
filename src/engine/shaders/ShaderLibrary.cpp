/**
 * ShaderLibrary.cpp
 * 
 * Implementation of the shader library.
 */

#include "ShaderLibrary.h"
#include <iostream>
#include <algorithm>

namespace Sanic {

ShaderLibrary::ShaderLibrary() = default;

ShaderLibrary::~ShaderLibrary() {
    shutdown();
}

bool ShaderLibrary::initialize(const ShaderLibraryConfig& config) {
    if (initialized_) {
        return true;
    }
    
    config_ = config;
    
    if (config_.device == VK_NULL_HANDLE) {
        std::cerr << "ShaderLibrary: No Vulkan device provided" << std::endl;
        return false;
    }
    
    // Initialize the compiler
    if (!GetShaderCompiler().initialize(config_.shaderDirs, config_.cacheDir)) {
        std::cerr << "ShaderLibrary: Failed to initialize shader compiler" << std::endl;
        return false;
    }
    
    // Setup hot-reload if enabled
    if (config_.enableHotReload) {
        std::vector<std::filesystem::path> watchPaths;
        for (const auto& dir : config_.shaderDirs) {
            watchPaths.emplace_back(dir);
        }
        
        if (GetShaderHotReload().start(watchPaths)) {
            hotReloadCallbackId_ = GetShaderHotReload().onReload(
                [this](const std::string& path) { onFileChanged(path); });
        }
    }
    
    initialized_ = true;
    std::cout << "ShaderLibrary: Initialized" << std::endl;
    
    return true;
}

void ShaderLibrary::shutdown() {
    if (!initialized_) {
        return;
    }
    
    // Stop hot-reload
    if (config_.enableHotReload) {
        GetShaderHotReload().removeCallback(hotReloadCallbackId_);
        GetShaderHotReload().stop();
    }
    
    // Destroy all modules
    for (auto& [key, module] : modules_) {
        if (module && module->module != VK_NULL_HANDLE) {
            vkDestroyShaderModule(config_.device, module->module, nullptr);
        }
    }
    modules_.clear();
    
    programs_.clear();
    permutationSets_.clear();
    fileToPrograms_.clear();
    
    GetShaderCompiler().shutdown();
    
    initialized_ = false;
}

void ShaderLibrary::update() {
    // Process pending reloads on main thread
    std::vector<std::string> toReload;
    {
        std::lock_guard<std::mutex> lock(pendingReloadsMutex_);
        toReload = std::move(pendingReloads_);
        pendingReloads_.clear();
    }
    
    for (const auto& programName : toReload) {
        if (reloadProgram(programName)) {
            stats_.hotReloads++;
            if (config_.onProgramReloaded) {
                config_.onProgramReloaded(programName);
            }
        } else {
            stats_.failedReloads++;
        }
    }
}

std::shared_ptr<ShaderModule> ShaderLibrary::loadModule(const std::string& path,
                                                         ShaderStage stage,
                                                         const ShaderCompileOptions& options) {
    // Generate cache key
    std::string key = path + "_" + std::to_string(static_cast<int>(stage)) + "_0";
    
    // Check if already loaded
    auto it = modules_.find(key);
    if (it != modules_.end()) {
        return it->second;
    }
    
    // Compile shader
    ShaderCompileOptions opts = options;
    opts.stage = stage;
    
    auto result = GetShaderCompiler().compileFile(path, opts);
    if (!result.success) {
        std::cerr << "ShaderLibrary: Failed to compile " << path << std::endl;
        std::cerr << "  " << result.errors << std::endl;
        return nullptr;
    }
    
    // Create Vulkan shader module
    VkShaderModule vkModule = createVkShaderModule(result.spirv);
    if (vkModule == VK_NULL_HANDLE) {
        std::cerr << "ShaderLibrary: Failed to create VkShaderModule for " << path << std::endl;
        return nullptr;
    }
    
    // Create module info
    auto module = std::make_shared<ShaderModule>();
    module->module = vkModule;
    module->stage = stage;
    module->entryPoint = opts.entryPoint;
    module->sourcePath = path;
    module->reflection = result.reflection;
    
    modules_[key] = module;
    stats_.modulesLoaded++;
    
    return module;
}

std::shared_ptr<ShaderModule> ShaderLibrary::loadModule(ShaderPermutationSet& permSet,
                                                         const PermutationKey& permKey,
                                                         ShaderStage stage,
                                                         const ShaderCompileOptions& options) {
    // Generate cache key including permutation
    uint64_t permHash = permKey.hash();
    std::string key = permSet.getName() + "_" + std::to_string(static_cast<int>(stage)) + "_" +
                      std::to_string(permHash);
    
    // Check if already loaded
    auto it = modules_.find(key);
    if (it != modules_.end()) {
        return it->second;
    }
    
    // Compile with permutation
    ShaderCompileOptions opts = options;
    opts.stage = stage;
    
    auto result = GetShaderCompiler().compilePermutation(permSet, permKey, opts);
    if (!result.success) {
        std::cerr << "ShaderLibrary: Failed to compile permutation of " << permSet.getName() << std::endl;
        std::cerr << "  " << result.errors << std::endl;
        return nullptr;
    }
    
    // Create Vulkan shader module
    VkShaderModule vkModule = createVkShaderModule(result.spirv);
    if (vkModule == VK_NULL_HANDLE) {
        return nullptr;
    }
    
    auto module = std::make_shared<ShaderModule>();
    module->module = vkModule;
    module->stage = stage;
    module->entryPoint = opts.entryPoint;
    module->sourcePath = permSet.getName();
    module->reflection = result.reflection;
    module->permutationHash = permHash;
    
    modules_[key] = module;
    stats_.modulesLoaded++;
    
    return module;
}

std::shared_ptr<ShaderProgram> ShaderLibrary::createProgram(
    const std::string& name,
    const std::vector<std::shared_ptr<ShaderModule>>& stages) {
    
    if (stages.empty()) {
        return nullptr;
    }
    
    auto program = std::make_shared<ShaderProgram>();
    program->name = name;
    program->stages = stages;
    
    // Merge reflection data from all stages
    mergeReflection(*program);
    
    // Track file dependencies
    for (const auto& stage : stages) {
        if (!stage->sourcePath.empty()) {
            fileToPrograms_[stage->sourcePath].push_back(name);
        }
    }
    
    programs_[name] = program;
    stats_.programsCreated++;
    
    return program;
}

std::shared_ptr<ShaderProgram> ShaderLibrary::loadGraphicsProgram(
    const std::string& name,
    const std::string& vertPath,
    const std::string& fragPath,
    const ShaderCompileOptions& options) {
    
    auto vertModule = loadModule(vertPath, ShaderStage::Vertex, options);
    auto fragModule = loadModule(fragPath, ShaderStage::Fragment, options);
    
    if (!vertModule || !fragModule) {
        return nullptr;
    }
    
    return createProgram(name, {vertModule, fragModule});
}

std::shared_ptr<ShaderProgram> ShaderLibrary::loadComputeProgram(
    const std::string& name,
    const std::string& compPath,
    const ShaderCompileOptions& options) {
    
    auto compModule = loadModule(compPath, ShaderStage::Compute, options);
    
    if (!compModule) {
        return nullptr;
    }
    
    return createProgram(name, {compModule});
}

std::shared_ptr<ShaderProgram> ShaderLibrary::getProgram(const std::string& name) {
    auto it = programs_.find(name);
    return (it != programs_.end()) ? it->second : nullptr;
}

bool ShaderLibrary::hasProgram(const std::string& name) const {
    return programs_.find(name) != programs_.end();
}

bool ShaderLibrary::reloadProgram(const std::string& name) {
    auto it = programs_.find(name);
    if (it == programs_.end()) {
        return false;
    }
    
    auto& program = it->second;
    bool success = true;
    
    // Reload each stage
    for (auto& stage : program->stages) {
        // Clear cache for this file
        GetShaderCache().invalidate(ShaderCache::hashSource(stage->sourcePath));
        
        // Recompile
        ShaderCompileOptions opts;
        opts.stage = stage->stage;
        opts.entryPoint = stage->entryPoint;
        
        auto result = GetShaderCompiler().compileFile(stage->sourcePath, opts);
        if (!result.success) {
            std::cerr << "ShaderLibrary: Failed to reload " << stage->sourcePath << std::endl;
            std::cerr << "  " << result.errors << std::endl;
            success = false;
            continue;
        }
        
        // Destroy old module
        if (stage->module != VK_NULL_HANDLE) {
            vkDestroyShaderModule(config_.device, stage->module, nullptr);
        }
        
        // Create new module
        stage->module = createVkShaderModule(result.spirv);
        stage->reflection = result.reflection;
        
        if (stage->module == VK_NULL_HANDLE) {
            success = false;
        }
    }
    
    // Update merged reflection
    if (success) {
        mergeReflection(*program);
    }
    
    program->needsReload = !success;
    
    std::cout << "ShaderLibrary: " << (success ? "Reloaded" : "Failed to reload") 
              << " program " << name << std::endl;
    
    return success;
}

void ShaderLibrary::reloadProgramsUsing(const std::string& sourcePath) {
    auto it = fileToPrograms_.find(sourcePath);
    if (it != fileToPrograms_.end()) {
        for (const auto& programName : it->second) {
            reloadProgram(programName);
        }
    }
}

std::vector<std::string> ShaderLibrary::getProgramNames() const {
    std::vector<std::string> names;
    names.reserve(programs_.size());
    for (const auto& [name, _] : programs_) {
        names.push_back(name);
    }
    return names;
}

void ShaderLibrary::registerPermutationSet(std::shared_ptr<ShaderPermutationSet> permSet) {
    if (permSet) {
        permutationSets_[permSet->getName()] = std::move(permSet);
    }
}

ShaderPermutationSet* ShaderLibrary::getPermutationSet(const std::string& name) {
    auto it = permutationSets_.find(name);
    return (it != permutationSets_.end()) ? it->second.get() : nullptr;
}

void ShaderLibrary::precompileAll() {
    for (auto& [name, permSet] : permutationSets_) {
        ShaderCompileOptions options;
        GetShaderCompiler().precompileAllPermutations(*permSet, options);
    }
}

VkShaderModule ShaderLibrary::createVkShaderModule(const std::vector<uint32_t>& spirv) {
    VkShaderModuleCreateInfo createInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    createInfo.codeSize = spirv.size() * sizeof(uint32_t);
    createInfo.pCode = spirv.data();
    
    VkShaderModule module;
    if (vkCreateShaderModule(config_.device, &createInfo, nullptr, &module) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    
    return module;
}

void ShaderLibrary::destroyModule(ShaderModule* module) {
    if (module && module->module != VK_NULL_HANDLE) {
        vkDestroyShaderModule(config_.device, module->module, nullptr);
        module->module = VK_NULL_HANDLE;
    }
}

void ShaderLibrary::onFileChanged(const std::string& path) {
    // Queue reload for main thread
    auto it = fileToPrograms_.find(path);
    if (it != fileToPrograms_.end()) {
        std::lock_guard<std::mutex> lock(pendingReloadsMutex_);
        for (const auto& programName : it->second) {
            if (std::find(pendingReloads_.begin(), pendingReloads_.end(), programName) ==
                pendingReloads_.end()) {
                pendingReloads_.push_back(programName);
            }
        }
    }
}

void ShaderLibrary::mergeReflection(ShaderProgram& program) {
    program.descriptors.clear();
    program.pushConstants.clear();
    program.vertexInputs.clear();
    
    DescriptorLayoutBuilder layoutBuilder;
    
    for (const auto& stage : program.stages) {
        if (!stage->reflection) continue;
        
        layoutBuilder.addShader(*stage->reflection);
        
        // Collect push constants
        for (const auto& pc : stage->reflection->pushConstants) {
            // Check if we already have this push constant
            bool found = false;
            for (auto& existing : program.pushConstants) {
                if (existing.offset == pc.offset && existing.size == pc.size) {
                    // Merge stage flags
                    existing.stageFlags = static_cast<ShaderStageFlags>(
                        static_cast<uint32_t>(existing.stageFlags) |
                        static_cast<uint32_t>(stage->reflection->stage));
                    found = true;
                    break;
                }
            }
            if (!found) {
                program.pushConstants.push_back(pc);
            }
        }
        
        // Collect vertex inputs from vertex shader
        if (stage->stage == ShaderStage::Vertex) {
            program.vertexInputs = stage->reflection->inputAttributes;
        }
    }
    
    // Get merged descriptors
    for (uint32_t set : layoutBuilder.getSets()) {
        auto bindings = layoutBuilder.getSetBindings(set);
        for (auto& binding : bindings) {
            program.descriptors.push_back(std::move(binding));
        }
    }
}

// Global instance
static ShaderLibrary g_shaderLibrary;

ShaderLibrary& GetShaderLibrary() {
    return g_shaderLibrary;
}

} // namespace Sanic
