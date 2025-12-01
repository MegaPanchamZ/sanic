/**
 * SubstrateMaterial.cpp
 * 
 * Implementation of the Substrate layered material system.
 */

#include "SubstrateMaterial.h"
#include "VulkanContext.h"
#include <algorithm>
#include <cmath>
#include <fstream>

namespace Sanic {

// ============================================================================
// SUBSTRATE MATERIAL METHODS
// ============================================================================

SubstrateSlab& SubstrateMaterial::addSlab(SubstrateSlabType type) {
    if (slabCount >= MAX_SUBSTRATE_SLABS) {
        // Return last slab if at max
        return slabs[slabCount - 1];
    }
    
    slabs[slabCount] = SubstrateSlab{};
    slabs[slabCount].type = type;
    return slabs[slabCount++];
}

void SubstrateMaterial::removeSlab(uint32_t index) {
    if (index >= slabCount || slabCount <= 1) return;
    
    for (uint32_t i = index; i < slabCount - 1; ++i) {
        slabs[i] = slabs[i + 1];
    }
    slabCount--;
}

void SubstrateMaterial::reorderSlab(uint32_t from, uint32_t to) {
    if (from >= slabCount || to >= slabCount || from == to) return;
    
    SubstrateSlab temp = slabs[from];
    
    if (from < to) {
        for (uint32_t i = from; i < to; ++i) {
            slabs[i] = slabs[i + 1];
        }
    } else {
        for (uint32_t i = from; i > to; --i) {
            slabs[i] = slabs[i - 1];
        }
    }
    
    slabs[to] = temp;
}

// ============================================================================
// SUBSTRATE SYSTEM
// ============================================================================

SubstrateSystem::SubstrateSystem(VulkanContext& context) : context_(context) {
}

SubstrateSystem::~SubstrateSystem() {
    shutdown();
}

bool SubstrateSystem::initialize() {
    createResources();
    createPipelines();
    return true;
}

void SubstrateSystem::shutdown() {
    VkDevice device = context_.getDevice();
    
    if (materialBuffer_) {
        if (materialMapped_) {
            vkUnmapMemory(device, materialMemory_);
            materialMapped_ = nullptr;
        }
        vkDestroyBuffer(device, materialBuffer_, nullptr);
        vkFreeMemory(device, materialMemory_, nullptr);
        materialBuffer_ = VK_NULL_HANDLE;
    }
    
    if (descriptorPool_) {
        vkDestroyDescriptorPool(device, descriptorPool_, nullptr);
        descriptorPool_ = VK_NULL_HANDLE;
    }
    
    if (descriptorLayout_) {
        vkDestroyDescriptorSetLayout(device, descriptorLayout_, nullptr);
        descriptorLayout_ = VK_NULL_HANDLE;
    }
    
    if (evaluationShader_) {
        vkDestroyShaderModule(device, evaluationShader_, nullptr);
        evaluationShader_ = VK_NULL_HANDLE;
    }
    
    materials_.clear();
    idToGPUIndex_.clear();
}

void SubstrateSystem::createResources() {
    VkDevice device = context_.getDevice();
    
    // Create material buffer
    VkDeviceSize bufferSize = maxMaterials_ * SUBSTRATE_MATERIAL_STRIDE;
    
    VkBufferCreateInfo bufferInfo = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    vkCreateBuffer(device, &bufferInfo, nullptr, &materialBuffer_);
    
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, materialBuffer_, &memReqs);
    
    VkMemoryAllocateInfo allocInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = context_.findMemoryType(
        memReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    
    vkAllocateMemory(device, &allocInfo, nullptr, &materialMemory_);
    vkBindBufferMemory(device, materialBuffer_, materialMemory_, 0);
    
    vkMapMemory(device, materialMemory_, 0, bufferSize, 0, &materialMapped_);
    
    // Create descriptor layout
    VkDescriptorSetLayoutBinding binding = {};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_ALL;
    
    VkDescriptorSetLayoutCreateInfo layoutInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    
    vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorLayout_);
    
    // Create descriptor pool
    VkDescriptorPoolSize poolSize = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
    
    VkDescriptorPoolCreateInfo poolInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    
    vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool_);
    
    // Allocate descriptor set
    VkDescriptorSetAllocateInfo setAllocInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    setAllocInfo.descriptorPool = descriptorPool_;
    setAllocInfo.descriptorSetCount = 1;
    setAllocInfo.pSetLayouts = &descriptorLayout_;
    
    vkAllocateDescriptorSets(device, &setAllocInfo, &descriptorSet_);
    
    // Update descriptor set
    VkDescriptorBufferInfo bufferDescInfo = {materialBuffer_, 0, VK_WHOLE_SIZE};
    
    VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = descriptorSet_;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &bufferDescInfo;
    
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

void SubstrateSystem::createPipelines() {
    // Load evaluation shader (if exists)
    std::ifstream file("shaders/substrate_eval.comp.spv", std::ios::binary | std::ios::ate);
    if (file.is_open()) {
        size_t fileSize = file.tellg();
        std::vector<char> buffer(fileSize);
        file.seekg(0);
        file.read(buffer.data(), fileSize);
        
        VkShaderModuleCreateInfo createInfo = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        createInfo.codeSize = buffer.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(buffer.data());
        
        vkCreateShaderModule(context_.getDevice(), &createInfo, nullptr, &evaluationShader_);
    }
}

uint32_t SubstrateSystem::createMaterial(const std::string& name) {
    auto material = std::make_unique<SubstrateMaterial>();
    material->name = name;
    material->id = nextMaterialId_++;
    material->slabCount = 1;  // Start with default slab
    
    // Find free GPU slot
    uint32_t gpuIndex = static_cast<uint32_t>(materials_.size());
    if (gpuIndex >= maxMaterials_) {
        return 0;  // Out of space
    }
    
    idToGPUIndex_[material->id] = gpuIndex;
    
    uploadMaterial(gpuIndex, *material);
    
    uint32_t id = material->id;
    materials_.push_back(std::move(material));
    return id;
}

void SubstrateSystem::updateMaterial(uint32_t id, const SubstrateMaterial& material) {
    auto it = idToGPUIndex_.find(id);
    if (it == idToGPUIndex_.end()) return;
    
    for (auto& mat : materials_) {
        if (mat->id == id) {
            *mat = material;
            mat->id = id;  // Preserve ID
            uploadMaterial(it->second, *mat);
            break;
        }
    }
}

void SubstrateSystem::deleteMaterial(uint32_t id) {
    auto it = idToGPUIndex_.find(id);
    if (it == idToGPUIndex_.end()) return;
    
    // Clear GPU data
    GPUSubstrateMaterial empty = {};
    memcpy(
        static_cast<uint8_t*>(materialMapped_) + it->second * SUBSTRATE_MATERIAL_STRIDE,
        &empty,
        sizeof(GPUSubstrateMaterial)
    );
    
    idToGPUIndex_.erase(it);
    
    materials_.erase(
        std::remove_if(materials_.begin(), materials_.end(),
            [id](const auto& mat) { return mat->id == id; }),
        materials_.end()
    );
}

SubstrateMaterial* SubstrateSystem::getMaterial(uint32_t id) {
    for (auto& mat : materials_) {
        if (mat->id == id) return mat.get();
    }
    return nullptr;
}

const SubstrateMaterial* SubstrateSystem::getMaterial(uint32_t id) const {
    for (const auto& mat : materials_) {
        if (mat->id == id) return mat.get();
    }
    return nullptr;
}

// ============================================================================
// PRESET MATERIALS
// ============================================================================

uint32_t SubstrateSystem::createDefaultLitMaterial() {
    uint32_t id = createMaterial("Default Lit");
    SubstrateMaterial* mat = getMaterial(id);
    if (mat) {
        mat->slabs[0].type = SubstrateSlabType::Standard;
        mat->slabs[0].baseColor = glm::vec3(0.8f);
        mat->slabs[0].metallic = 0.0f;
        mat->slabs[0].roughness = 0.5f;
        updateMaterial(id, *mat);
    }
    return id;
}

uint32_t SubstrateSystem::createClearCoatMaterial() {
    uint32_t id = createMaterial("Clear Coat");
    SubstrateMaterial* mat = getMaterial(id);
    if (mat) {
        // Base layer
        mat->slabs[0].type = SubstrateSlabType::Standard;
        mat->slabs[0].baseColor = glm::vec3(0.8f, 0.1f, 0.1f);  // Red car paint
        mat->slabs[0].metallic = 1.0f;
        mat->slabs[0].roughness = 0.3f;
        
        // Clear coat
        auto& clearCoat = mat->addSlab(SubstrateSlabType::ClearCoat);
        clearCoat.blendMode = SubstrateBlendMode::Alpha;
        clearCoat.blendWeight = 1.0f;
        clearCoat.clearCoatRoughness = 0.02f;
        clearCoat.clearCoatIOR = 1.5f;
        clearCoat.thickness = 0.1f;
        
        updateMaterial(id, *mat);
    }
    return id;
}

uint32_t SubstrateSystem::createSubsurfaceMaterial() {
    uint32_t id = createMaterial("Subsurface");
    SubstrateMaterial* mat = getMaterial(id);
    if (mat) {
        mat->slabs[0].type = SubstrateSlabType::Subsurface;
        mat->slabs[0].baseColor = glm::vec3(0.9f, 0.7f, 0.6f);  // Skin tone
        mat->slabs[0].subsurfaceColor = glm::vec3(1.0f, 0.2f, 0.1f);
        mat->slabs[0].subsurfaceRadius = 1.0f;
        mat->slabs[0].roughness = 0.4f;
        updateMaterial(id, *mat);
    }
    return id;
}

uint32_t SubstrateSystem::createClothMaterial() {
    uint32_t id = createMaterial("Cloth");
    SubstrateMaterial* mat = getMaterial(id);
    if (mat) {
        mat->slabs[0].type = SubstrateSlabType::Cloth;
        mat->slabs[0].baseColor = glm::vec3(0.2f, 0.3f, 0.6f);  // Blue fabric
        mat->slabs[0].sheenColor = glm::vec3(1.0f);
        mat->slabs[0].sheenRoughness = 0.5f;
        mat->slabs[0].roughness = 0.8f;
        mat->twoSided = true;
        updateMaterial(id, *mat);
    }
    return id;
}

uint32_t SubstrateSystem::createGlassMaterial() {
    uint32_t id = createMaterial("Glass");
    SubstrateMaterial* mat = getMaterial(id);
    if (mat) {
        mat->slabs[0].type = SubstrateSlabType::Transmission;
        mat->slabs[0].baseColor = glm::vec3(1.0f);
        mat->slabs[0].opacity = 0.1f;
        mat->slabs[0].roughness = 0.0f;
        mat->slabs[0].transmissionIOR = 1.5f;
        mat->slabs[0].absorption = glm::vec3(0.02f, 0.02f, 0.01f);
        updateMaterial(id, *mat);
    }
    return id;
}

// ============================================================================
// GPU BUFFER MANAGEMENT
// ============================================================================

void SubstrateSystem::updateGPUBuffers() {
    for (const auto& mat : materials_) {
        auto it = idToGPUIndex_.find(mat->id);
        if (it != idToGPUIndex_.end()) {
            uploadMaterial(it->second, *mat);
        }
    }
}

uint32_t SubstrateSystem::getMaterialGPUIndex(uint32_t id) const {
    auto it = idToGPUIndex_.find(id);
    return (it != idToGPUIndex_.end()) ? it->second : 0;
}

void SubstrateSystem::uploadMaterial(uint32_t gpuIndex, const SubstrateMaterial& material) {
    GPUSubstrateMaterial gpuMat = {};
    
    // Convert slabs
    for (uint32_t i = 0; i < material.slabCount && i < MAX_SUBSTRATE_SLABS; ++i) {
        gpuMat.slabs[i] = convertSlabToGPU(material.slabs[i]);
    }
    
    // Flags and counts
    uint32_t flags = 0;
    if (material.twoSided) flags |= 1;
    if (material.useTriplanarMapping) flags |= 2;
    
    uint32_t textureMask = 0;
    if (material.baseColorTexture >= 0) textureMask |= 1;
    if (material.normalTexture >= 0) textureMask |= 2;
    if (material.metallicRoughnessTexture >= 0) textureMask |= 4;
    if (material.emissiveTexture >= 0) textureMask |= 8;
    
    gpuMat.flagsAndCounts = glm::uvec4(material.slabCount, flags, textureMask, 0);
    
    gpuMat.textureIndices0 = glm::ivec4(
        material.baseColorTexture,
        material.normalTexture,
        material.metallicRoughnessTexture,
        material.emissiveTexture
    );
    
    gpuMat.textureIndices1 = glm::ivec4(
        material.clearCoatTexture,
        material.subsurfaceTexture,
        -1, -1
    );
    
    // Upload
    memcpy(
        static_cast<uint8_t*>(materialMapped_) + gpuIndex * SUBSTRATE_MATERIAL_STRIDE,
        &gpuMat,
        sizeof(GPUSubstrateMaterial)
    );
}

GPUSubstrateSlab SubstrateSystem::convertSlabToGPU(const SubstrateSlab& slab) const {
    GPUSubstrateSlab gpu = {};
    
    gpu.typeBlendWeightThickness = glm::vec4(
        static_cast<float>(slab.type),
        static_cast<float>(slab.blendMode),
        slab.blendWeight,
        slab.thickness
    );
    
    gpu.baseColorOpacity = glm::vec4(slab.baseColor, slab.opacity);
    
    gpu.metallicRoughnessSpecularAniso = glm::vec4(
        slab.metallic,
        slab.roughness,
        slab.specular,
        slab.anisotropy
    );
    
    gpu.normalClearCoatIOR = glm::vec4(
        slab.normalStrength,
        slab.clearCoatRoughness,
        slab.clearCoatIOR,
        slab.transmissionIOR
    );
    
    gpu.absorptionSubsurface = glm::vec4(slab.absorption, slab.subsurfaceRadius);
    
    gpu.subsurfaceColorPhase = glm::vec4(slab.subsurfaceColor, slab.subsurfacePhase);
    
    gpu.sheenColorRoughness = glm::vec4(slab.sheenColor, slab.sheenRoughness);
    
    gpu.thinFilmHair = glm::vec4(
        slab.thinFilmThickness,
        slab.thinFilmIOR,
        slab.hairScatter,
        slab.hairShift
    );
    
    return gpu;
}

// ============================================================================
// BSDF EVALUATION (CPU REFERENCE)
// ============================================================================

namespace SubstrateBSDF {

glm::vec3 evaluateSlab(const SubstrateSlab& slab,
                       const glm::vec3& V,
                       const glm::vec3& L,
                       const glm::vec3& N,
                       float& outThroughput) {
    glm::vec3 H = glm::normalize(V + L);
    float NdotV = std::max(glm::dot(N, V), 0.0001f);
    float NdotL = std::max(glm::dot(N, L), 0.0001f);
    float NdotH = std::max(glm::dot(N, H), 0.0f);
    float VdotH = std::max(glm::dot(V, H), 0.0f);
    
    glm::vec3 result(0.0f);
    outThroughput = 1.0f;
    
    switch (slab.type) {
        case SubstrateSlabType::Standard: {
            // Standard metallic-roughness PBR
            glm::vec3 F0 = glm::mix(glm::vec3(0.04f * slab.specular), slab.baseColor, slab.metallic);
            
            float D = distributionGGX(NdotH, slab.roughness);
            float G = geometrySmith(NdotV, NdotL, slab.roughness);
            glm::vec3 F = fresnelSchlick(VdotH, F0);
            
            glm::vec3 kD = (glm::vec3(1.0f) - F) * (1.0f - slab.metallic);
            glm::vec3 diffuse = kD * slab.baseColor / 3.14159265f;
            glm::vec3 specular = (D * G * F) / std::max(4.0f * NdotV * NdotL, 0.0001f);
            
            result = (diffuse + specular) * NdotL;
            break;
        }
        
        case SubstrateSlabType::ClearCoat: {
            // Clear coat Fresnel
            float F = fresnelDielectric(VdotH, slab.clearCoatIOR);
            float D = distributionGGX(NdotH, slab.clearCoatRoughness);
            float G = geometrySmith(NdotV, NdotL, slab.clearCoatRoughness);
            
            float specular = (D * G * F) / std::max(4.0f * NdotV * NdotL, 0.0001f);
            result = glm::vec3(specular * NdotL);
            
            // Throughput for layers below
            outThroughput = 1.0f - F;
            break;
        }
        
        case SubstrateSlabType::Transmission: {
            // Simplified transmission
            float F = fresnelDielectric(VdotH, slab.transmissionIOR);
            
            // Absorption through material
            glm::vec3 absorption = glm::exp(-slab.absorption * slab.thickness);
            
            // Transmitted light
            glm::vec3 transmitted = (1.0f - F) * absorption * slab.baseColor;
            result = transmitted;
            
            outThroughput = 1.0f - F;
            break;
        }
        
        case SubstrateSlabType::Subsurface: {
            // Simplified subsurface approximation
            glm::vec3 F0(0.04f);
            glm::vec3 F = fresnelSchlick(VdotH, F0);
            
            // Curvature-based wrap lighting
            float wrap = 0.5f;
            float diffuse = std::max(0.0f, (NdotL + wrap) / (1.0f + wrap));
            
            // SSS contribution
            float sssNdotL = std::max(0.0f, glm::dot(N, -L));
            glm::vec3 sss = slab.subsurfaceColor * sssNdotL * slab.subsurfaceRadius * 0.5f;
            
            result = ((glm::vec3(1.0f) - F) * slab.baseColor * diffuse + sss);
            break;
        }
        
        case SubstrateSlabType::Cloth: {
            // Ashikhmin cloth model
            glm::vec3 F0(0.04f);
            
            // Diffuse
            float diffuse = NdotL / 3.14159265f;
            
            // Sheen (edge highlight)
            float sheenFactor = std::pow(1.0f - VdotH, 5.0f);
            glm::vec3 sheen = slab.sheenColor * sheenFactor;
            
            result = (slab.baseColor * diffuse + sheen) * NdotL;
            break;
        }
        
        default:
            result = slab.baseColor * NdotL / 3.14159265f;
            break;
    }
    
    return result * slab.blendWeight;
}

glm::vec3 evaluateMaterial(const SubstrateMaterial& material,
                           const glm::vec3& V,
                           const glm::vec3& L,
                           const glm::vec3& N) {
    glm::vec3 totalBSDF(0.0f);
    float throughput = 1.0f;
    
    // Evaluate from top layer down (reverse order)
    for (int32_t i = static_cast<int32_t>(material.slabCount) - 1; i >= 0; --i) {
        const SubstrateSlab& slab = material.slabs[i];
        
        float slabThroughput;
        glm::vec3 slabBSDF = evaluateSlab(slab, V, L, N, slabThroughput);
        
        // Accumulate with current throughput
        totalBSDF += throughput * slabBSDF;
        
        // Update throughput for lower layers
        throughput *= slabThroughput;
        
        // Apply absorption if thickness > 0
        if (slab.thickness > 0.0f && glm::length(slab.absorption) > 0.0f) {
            throughput *= glm::dot(glm::exp(-slab.absorption * slab.thickness), glm::vec3(1.0f / 3.0f));
        }
        
        // Early out if no light reaches lower layers
        if (throughput < 0.001f) break;
    }
    
    return totalBSDF;
}

} // namespace SubstrateBSDF

} // namespace Sanic
