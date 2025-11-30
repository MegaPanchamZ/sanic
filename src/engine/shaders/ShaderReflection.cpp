/**
 * ShaderReflection.cpp
 * 
 * Implementation of SPIR-V reflection using SPIRV-Reflect library.
 */

#include "ShaderReflection.h"
#include <spirv_reflect.h>
#include <algorithm>
#include <iostream>
#include <set>
#include <map>

namespace Sanic {

std::optional<ShaderReflectionData> ShaderReflection::reflect(
    const std::vector<uint32_t>& spirv,
    const std::string& entryPoint) {
    
    if (spirv.empty()) {
        return std::nullopt;
    }
    
    SpvReflectShaderModule module;
    SpvReflectResult result = spvReflectCreateShaderModule(
        spirv.size() * sizeof(uint32_t),
        spirv.data(),
        &module
    );
    
    if (result != SPV_REFLECT_RESULT_SUCCESS) {
        std::cerr << "ShaderReflection: Failed to create reflection module" << std::endl;
        return std::nullopt;
    }
    
    ShaderReflectionData data;
    data.entryPoint = entryPoint;
    
    // Map SPIRV-Reflect stage to our stage flags
    switch (module.shader_stage) {
        case SPV_REFLECT_SHADER_STAGE_VERTEX_BIT:
            data.stage = ShaderStageFlags::Vertex;
            break;
        case SPV_REFLECT_SHADER_STAGE_TESSELLATION_CONTROL_BIT:
            data.stage = ShaderStageFlags::TessellationControl;
            break;
        case SPV_REFLECT_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
            data.stage = ShaderStageFlags::TessellationEvaluation;
            break;
        case SPV_REFLECT_SHADER_STAGE_GEOMETRY_BIT:
            data.stage = ShaderStageFlags::Geometry;
            break;
        case SPV_REFLECT_SHADER_STAGE_FRAGMENT_BIT:
            data.stage = ShaderStageFlags::Fragment;
            break;
        case SPV_REFLECT_SHADER_STAGE_COMPUTE_BIT:
            data.stage = ShaderStageFlags::Compute;
            data.localSizeX = module.entry_points[0].local_size.x;
            data.localSizeY = module.entry_points[0].local_size.y;
            data.localSizeZ = module.entry_points[0].local_size.z;
            break;
        case SPV_REFLECT_SHADER_STAGE_TASK_BIT_EXT:
            data.stage = ShaderStageFlags::TaskEXT;
            break;
        case SPV_REFLECT_SHADER_STAGE_MESH_BIT_EXT:
            data.stage = ShaderStageFlags::MeshEXT;
            break;
        case SPV_REFLECT_SHADER_STAGE_RAYGEN_BIT_KHR:
            data.stage = ShaderStageFlags::RaygenKHR;
            break;
        case SPV_REFLECT_SHADER_STAGE_ANY_HIT_BIT_KHR:
            data.stage = ShaderStageFlags::AnyHitKHR;
            break;
        case SPV_REFLECT_SHADER_STAGE_CLOSEST_HIT_BIT_KHR:
            data.stage = ShaderStageFlags::ClosestHitKHR;
            break;
        case SPV_REFLECT_SHADER_STAGE_MISS_BIT_KHR:
            data.stage = ShaderStageFlags::MissKHR;
            break;
        case SPV_REFLECT_SHADER_STAGE_INTERSECTION_BIT_KHR:
            data.stage = ShaderStageFlags::IntersectionKHR;
            break;
        case SPV_REFLECT_SHADER_STAGE_CALLABLE_BIT_KHR:
            data.stage = ShaderStageFlags::CallableKHR;
            break;
        default:
            data.stage = static_cast<ShaderStageFlags>(0);
    }
    
    // Reflect descriptors
    reflectDescriptors(&module, data);
    
    // Reflect push constants
    reflectPushConstants(&module, data);
    
    // Reflect inputs (for vertex shaders)
    if (data.stage == ShaderStageFlags::Vertex) {
        reflectInputs(&module, data);
    }
    
    // Reflect specialization constants
    reflectSpecConstants(&module, data);
    
    spvReflectDestroyShaderModule(&module);
    
    return data;
}

void ShaderReflection::reflectDescriptors(void* modulePtr, ShaderReflectionData& data) {
    SpvReflectShaderModule* module = static_cast<SpvReflectShaderModule*>(modulePtr);
    
    uint32_t count = 0;
    SpvReflectResult result = spvReflectEnumerateDescriptorBindings(module, &count, nullptr);
    if (result != SPV_REFLECT_RESULT_SUCCESS || count == 0) {
        return;
    }
    
    std::vector<SpvReflectDescriptorBinding*> bindings(count);
    result = spvReflectEnumerateDescriptorBindings(module, &count, bindings.data());
    if (result != SPV_REFLECT_RESULT_SUCCESS) {
        return;
    }
    
    for (const auto* binding : bindings) {
        ReflectedDescriptor desc;
        desc.set = binding->set;
        desc.binding = binding->binding;
        desc.count = binding->count;
        desc.name = binding->name ? binding->name : "";
        
        // Map descriptor type
        switch (binding->descriptor_type) {
            case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER:
                desc.type = DescriptorType::Sampler;
                break;
            case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
                desc.type = DescriptorType::CombinedImageSampler;
                break;
            case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
                desc.type = DescriptorType::SampledImage;
                break;
            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                desc.type = DescriptorType::StorageImage;
                break;
            case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
                desc.type = DescriptorType::UniformTexelBuffer;
                break;
            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
                desc.type = DescriptorType::StorageTexelBuffer;
                break;
            case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                desc.type = DescriptorType::UniformBuffer;
                break;
            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                desc.type = DescriptorType::StorageBuffer;
                break;
            case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
                desc.type = DescriptorType::UniformBufferDynamic;
                break;
            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
                desc.type = DescriptorType::StorageBufferDynamic;
                break;
            case SPV_REFLECT_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
                desc.type = DescriptorType::InputAttachment;
                break;
            case SPV_REFLECT_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
                desc.type = DescriptorType::AccelerationStructure;
                break;
            default:
                continue;
        }
        
        // For buffers, get block info
        if (binding->type_description) {
            desc.blockSize = binding->block.size;
            reflectMembers(binding->type_description, desc.members);
        }
        
        // For images, get dimension info
        if (binding->image.dim != SpvDimMax) {
            desc.imageDimension = static_cast<uint32_t>(binding->image.dim);
            desc.imageArrayed = binding->image.arrayed != 0;
            desc.imageMultisampled = binding->image.ms != 0;
        }
        
        data.descriptors.push_back(std::move(desc));
    }
}

void ShaderReflection::reflectPushConstants(void* modulePtr, ShaderReflectionData& data) {
    SpvReflectShaderModule* module = static_cast<SpvReflectShaderModule*>(modulePtr);
    
    uint32_t count = 0;
    SpvReflectResult result = spvReflectEnumeratePushConstantBlocks(module, &count, nullptr);
    if (result != SPV_REFLECT_RESULT_SUCCESS || count == 0) {
        return;
    }
    
    std::vector<SpvReflectBlockVariable*> blocks(count);
    result = spvReflectEnumeratePushConstantBlocks(module, &count, blocks.data());
    if (result != SPV_REFLECT_RESULT_SUCCESS) {
        return;
    }
    
    for (const auto* block : blocks) {
        ReflectedPushConstantBlock pc;
        pc.offset = block->offset;
        pc.size = block->size;
        pc.stageFlags = data.stage;
        pc.name = block->name ? block->name : "";
        
        // Reflect members
        for (uint32_t i = 0; i < block->member_count; i++) {
            const auto& member = block->members[i];
            ReflectedMember m;
            m.name = member.name ? member.name : "";
            m.offset = member.offset;
            m.size = member.size;
            m.arrayStride = member.array.stride;
            m.matrixStride = member.numeric.matrix.stride;
            m.columns = member.numeric.matrix.column_count;
            m.rows = member.numeric.matrix.row_count;
            m.rowMajor = (member.decoration_flags & SPV_REFLECT_DECORATION_ROW_MAJOR) != 0;
            pc.members.push_back(std::move(m));
        }
        
        data.pushConstants.push_back(std::move(pc));
    }
}

void ShaderReflection::reflectInputs(void* modulePtr, ShaderReflectionData& data) {
    SpvReflectShaderModule* module = static_cast<SpvReflectShaderModule*>(modulePtr);
    
    uint32_t count = 0;
    SpvReflectResult result = spvReflectEnumerateInputVariables(module, &count, nullptr);
    if (result != SPV_REFLECT_RESULT_SUCCESS || count == 0) {
        return;
    }
    
    std::vector<SpvReflectInterfaceVariable*> inputs(count);
    result = spvReflectEnumerateInputVariables(module, &count, inputs.data());
    if (result != SPV_REFLECT_RESULT_SUCCESS) {
        return;
    }
    
    for (const auto* input : inputs) {
        // Skip built-in inputs
        if (input->built_in != -1) {
            continue;
        }
        
        ReflectedInputAttribute attr;
        attr.location = input->location;
        attr.binding = 0;  // Usually 0 for vertex inputs
        attr.name = input->name ? input->name : "";
        attr.offset = 0;  // Would be calculated based on layout
        
        // Determine format and vec size
        if (input->type_description) {
            attr.vecSize = input->numeric.vector.component_count;
            if (attr.vecSize == 0) attr.vecSize = 1;
            
            // Get base type
            uint32_t baseType = input->type_description->type_flags & 
                SPV_REFLECT_TYPE_FLAG_FLOAT ? 0 :
                (input->type_description->type_flags & SPV_REFLECT_TYPE_FLAG_INT ? 1 : 2);
            
            attr.format = getVkFormat(baseType, attr.vecSize, 1);
        }
        
        data.inputAttributes.push_back(std::move(attr));
    }
    
    // Sort by location
    std::sort(data.inputAttributes.begin(), data.inputAttributes.end(),
        [](const auto& a, const auto& b) { return a.location < b.location; });
}

void ShaderReflection::reflectSpecConstants(void* modulePtr, ShaderReflectionData& data) {
    SpvReflectShaderModule* module = static_cast<SpvReflectShaderModule*>(modulePtr);
    
    uint32_t count = 0;
    SpvReflectResult result = spvReflectEnumerateSpecializationConstants(module, &count, nullptr);
    if (result != SPV_REFLECT_RESULT_SUCCESS || count == 0) {
        return;
    }
    
    std::vector<SpvReflectSpecializationConstant*> constants(count);
    result = spvReflectEnumerateSpecializationConstants(module, &count, constants.data());
    if (result != SPV_REFLECT_RESULT_SUCCESS) {
        return;
    }
    
    uint32_t offset = 0;
    for (const auto* constant : constants) {
        ReflectedSpecConstant spec;
        spec.constantId = constant->constant_id;
        spec.name = constant->name ? constant->name : "";
        spec.size = 4;  // Most spec constants are 4 bytes
        spec.offset = offset;
        offset += spec.size;
        
        data.specConstants.push_back(std::move(spec));
    }
}

void ShaderReflection::reflectMembers(void* typeDescPtr, std::vector<ReflectedMember>& members) {
    SpvReflectTypeDescription* typeDesc = static_cast<SpvReflectTypeDescription*>(typeDescPtr);
    
    for (uint32_t i = 0; i < typeDesc->member_count; i++) {
        const auto& srcMember = typeDesc->members[i];
        
        ReflectedMember member;
        member.name = srcMember.struct_member_name ? srcMember.struct_member_name : "";
        member.offset = 0;  // Would need additional info
        member.size = 0;
        member.arrayStride = 0;
        member.matrixStride = 0;
        member.columns = srcMember.traits.numeric.matrix.column_count;
        member.rows = srcMember.traits.numeric.matrix.row_count;
        member.rowMajor = false;
        
        // Recursively reflect nested structs
        if (srcMember.type_flags & SPV_REFLECT_TYPE_FLAG_STRUCT) {
            reflectMembers(const_cast<SpvReflectTypeDescription*>(&srcMember), member.members);
        }
        
        members.push_back(std::move(member));
    }
}

std::vector<ReflectedDescriptor> ShaderReflection::mergeDescriptors(
    const std::vector<ShaderReflectionData>& stages) {
    
    // Use map to merge by (set, binding)
    std::map<std::pair<uint32_t, uint32_t>, ReflectedDescriptor> merged;
    
    for (const auto& stage : stages) {
        for (const auto& desc : stage.descriptors) {
            auto key = std::make_pair(desc.set, desc.binding);
            auto it = merged.find(key);
            
            if (it == merged.end()) {
                merged[key] = desc;
            } else {
                // Verify compatibility and merge
                if (it->second.type != desc.type) {
                    std::cerr << "ShaderReflection: Descriptor type mismatch at set=" 
                              << desc.set << " binding=" << desc.binding << std::endl;
                }
            }
        }
    }
    
    std::vector<ReflectedDescriptor> result;
    for (auto& [key, desc] : merged) {
        result.push_back(std::move(desc));
    }
    
    return result;
}

void ShaderReflection::toCacheEntry(const ShaderReflectionData& reflection,
                                    ShaderCacheEntry& entry) {
    entry.entryPoint = reflection.entryPoint;
    entry.workgroupSize[0] = reflection.localSizeX;
    entry.workgroupSize[1] = reflection.localSizeY;
    entry.workgroupSize[2] = reflection.localSizeZ;
    
    // Convert bindings
    entry.bindings.clear();
    for (const auto& desc : reflection.descriptors) {
        ReflectedBinding binding;
        binding.set = desc.set;
        binding.binding = desc.binding;
        binding.descriptorType = static_cast<uint32_t>(desc.type);
        binding.count = desc.count;
        binding.name = desc.name;
        binding.size = desc.blockSize;
        entry.bindings.push_back(std::move(binding));
    }
    
    // Convert push constants
    entry.pushConstants.clear();
    for (const auto& pc : reflection.pushConstants) {
        ReflectedPushConstant pushConstant;
        pushConstant.offset = pc.offset;
        pushConstant.size = pc.size;
        pushConstant.name = pc.name;
        entry.pushConstants.push_back(std::move(pushConstant));
    }
    
    // Convert vertex inputs
    entry.vertexInputs.clear();
    for (const auto& input : reflection.inputAttributes) {
        ReflectedVertexInput vertexInput;
        vertexInput.location = input.location;
        vertexInput.format = input.format;
        vertexInput.name = input.name;
        entry.vertexInputs.push_back(std::move(vertexInput));
    }
}

uint32_t ShaderReflection::getVkFormat(uint32_t baseType, uint32_t vecSize, uint32_t columns) {
    // VkFormat values for common types
    // baseType: 0 = float, 1 = int, 2 = uint
    
    if (columns > 1) {
        // Matrix types would need special handling
        return 0;
    }
    
    if (baseType == 0) {  // float
        switch (vecSize) {
            case 1: return 100;  // VK_FORMAT_R32_SFLOAT
            case 2: return 103;  // VK_FORMAT_R32G32_SFLOAT
            case 3: return 106;  // VK_FORMAT_R32G32B32_SFLOAT
            case 4: return 109;  // VK_FORMAT_R32G32B32A32_SFLOAT
        }
    } else if (baseType == 1) {  // int
        switch (vecSize) {
            case 1: return 99;   // VK_FORMAT_R32_SINT
            case 2: return 102;  // VK_FORMAT_R32G32_SINT
            case 3: return 105;  // VK_FORMAT_R32G32B32_SINT
            case 4: return 108;  // VK_FORMAT_R32G32B32A32_SINT
        }
    } else {  // uint
        switch (vecSize) {
            case 1: return 98;   // VK_FORMAT_R32_UINT
            case 2: return 101;  // VK_FORMAT_R32G32_UINT
            case 3: return 104;  // VK_FORMAT_R32G32B32_UINT
            case 4: return 107;  // VK_FORMAT_R32G32B32A32_UINT
        }
    }
    
    return 0;
}

std::string ShaderReflection::descriptorTypeName(DescriptorType type) {
    switch (type) {
        case DescriptorType::Sampler: return "Sampler";
        case DescriptorType::CombinedImageSampler: return "CombinedImageSampler";
        case DescriptorType::SampledImage: return "SampledImage";
        case DescriptorType::StorageImage: return "StorageImage";
        case DescriptorType::UniformTexelBuffer: return "UniformTexelBuffer";
        case DescriptorType::StorageTexelBuffer: return "StorageTexelBuffer";
        case DescriptorType::UniformBuffer: return "UniformBuffer";
        case DescriptorType::StorageBuffer: return "StorageBuffer";
        case DescriptorType::UniformBufferDynamic: return "UniformBufferDynamic";
        case DescriptorType::StorageBufferDynamic: return "StorageBufferDynamic";
        case DescriptorType::InputAttachment: return "InputAttachment";
        case DescriptorType::AccelerationStructure: return "AccelerationStructure";
        default: return "Unknown";
    }
}

std::string ShaderReflection::stageName(ShaderStageFlags stage) {
    switch (stage) {
        case ShaderStageFlags::Vertex: return "Vertex";
        case ShaderStageFlags::TessellationControl: return "TessellationControl";
        case ShaderStageFlags::TessellationEvaluation: return "TessellationEvaluation";
        case ShaderStageFlags::Geometry: return "Geometry";
        case ShaderStageFlags::Fragment: return "Fragment";
        case ShaderStageFlags::Compute: return "Compute";
        case ShaderStageFlags::TaskEXT: return "Task";
        case ShaderStageFlags::MeshEXT: return "Mesh";
        case ShaderStageFlags::RaygenKHR: return "RayGen";
        case ShaderStageFlags::AnyHitKHR: return "AnyHit";
        case ShaderStageFlags::ClosestHitKHR: return "ClosestHit";
        case ShaderStageFlags::MissKHR: return "Miss";
        case ShaderStageFlags::IntersectionKHR: return "Intersection";
        case ShaderStageFlags::CallableKHR: return "Callable";
        default: return "Unknown";
    }
}

// ShaderReflectionData helper implementations

std::optional<ReflectedDescriptor> ShaderReflectionData::findDescriptor(
    uint32_t set, uint32_t binding) const {
    for (const auto& desc : descriptors) {
        if (desc.set == set && desc.binding == binding) {
            return desc;
        }
    }
    return std::nullopt;
}

std::optional<ReflectedDescriptor> ShaderReflectionData::findDescriptor(
    const std::string& name) const {
    for (const auto& desc : descriptors) {
        if (desc.name == name) {
            return desc;
        }
    }
    return std::nullopt;
}

std::optional<ReflectedInputAttribute> ShaderReflectionData::findInput(
    uint32_t location) const {
    for (const auto& input : inputAttributes) {
        if (input.location == location) {
            return input;
        }
    }
    return std::nullopt;
}

std::optional<ReflectedInputAttribute> ShaderReflectionData::findInput(
    const std::string& name) const {
    for (const auto& input : inputAttributes) {
        if (input.name == name) {
            return input;
        }
    }
    return std::nullopt;
}

uint32_t ShaderReflectionData::getTotalPushConstantSize() const {
    uint32_t total = 0;
    for (const auto& pc : pushConstants) {
        total += pc.size;
    }
    return total;
}

std::vector<uint32_t> ShaderReflectionData::getDescriptorSets() const {
    std::set<uint32_t> sets;
    for (const auto& desc : descriptors) {
        sets.insert(desc.set);
    }
    return std::vector<uint32_t>(sets.begin(), sets.end());
}

// DescriptorLayoutBuilder implementation

void DescriptorLayoutBuilder::addShader(const ShaderReflectionData& reflection) {
    for (const auto& desc : reflection.descriptors) {
        auto& binding = descriptors_[desc.set][desc.binding];
        if (binding.name.empty()) {
            binding = desc;
        }
    }
    combinedStages_ = static_cast<ShaderStageFlags>(
        static_cast<uint32_t>(combinedStages_) | static_cast<uint32_t>(reflection.stage));
}

std::vector<ReflectedDescriptor> DescriptorLayoutBuilder::getSetBindings(uint32_t set) const {
    std::vector<ReflectedDescriptor> result;
    auto setIt = descriptors_.find(set);
    if (setIt != descriptors_.end()) {
        for (const auto& [binding, desc] : setIt->second) {
            result.push_back(desc);
        }
    }
    return result;
}

std::vector<uint32_t> DescriptorLayoutBuilder::getSets() const {
    std::vector<uint32_t> result;
    for (const auto& [set, _] : descriptors_) {
        result.push_back(set);
    }
    return result;
}

bool DescriptorLayoutBuilder::isCompatible(const DescriptorLayoutBuilder& other) const {
    for (const auto& [set, bindings] : descriptors_) {
        auto otherSetIt = other.descriptors_.find(set);
        if (otherSetIt == other.descriptors_.end()) {
            continue;
        }
        
        for (const auto& [binding, desc] : bindings) {
            auto otherBindingIt = otherSetIt->second.find(binding);
            if (otherBindingIt == otherSetIt->second.end()) {
                continue;
            }
            
            if (desc.type != otherBindingIt->second.type) {
                return false;
            }
        }
    }
    return true;
}

} // namespace Sanic
