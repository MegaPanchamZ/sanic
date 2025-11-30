/**
 * ShaderReflection.h
 * 
 * SPIR-V reflection for extracting shader metadata.
 * Uses SPIRV-Reflect library to parse descriptor bindings,
 * push constants, and vertex inputs from compiled shaders.
 */

#pragma once

#include "ShaderCache.h"
#include <vector>
#include <string>
#include <cstdint>
#include <optional>
#include <map>

namespace Sanic {

/**
 * Descriptor types matching Vulkan
 */
enum class DescriptorType : uint32_t {
    Sampler = 0,
    CombinedImageSampler = 1,
    SampledImage = 2,
    StorageImage = 3,
    UniformTexelBuffer = 4,
    StorageTexelBuffer = 5,
    UniformBuffer = 6,
    StorageBuffer = 7,
    UniformBufferDynamic = 8,
    StorageBufferDynamic = 9,
    InputAttachment = 10,
    AccelerationStructure = 1000150000,
};

/**
 * Shader stage flags
 */
enum class ShaderStageFlags : uint32_t {
    Vertex = 0x00000001,
    TessellationControl = 0x00000002,
    TessellationEvaluation = 0x00000004,
    Geometry = 0x00000008,
    Fragment = 0x00000010,
    Compute = 0x00000020,
    AllGraphics = 0x0000001F,
    All = 0x7FFFFFFF,
    RaygenKHR = 0x00000100,
    AnyHitKHR = 0x00000200,
    ClosestHitKHR = 0x00000400,
    MissKHR = 0x00000800,
    IntersectionKHR = 0x00001000,
    CallableKHR = 0x00002000,
    TaskEXT = 0x00000040,
    MeshEXT = 0x00000080,
};

/**
 * Member of a struct (for uniform/storage buffer reflection)
 */
struct ReflectedMember {
    std::string name;
    uint32_t offset;
    uint32_t size;
    uint32_t arrayStride;    // 0 if not an array
    uint32_t matrixStride;   // 0 if not a matrix
    uint32_t columns;        // For matrices
    uint32_t rows;           // For matrices/vectors
    bool rowMajor;
    std::vector<ReflectedMember> members;  // For nested structs
};

/**
 * Reflected descriptor set binding
 */
struct ReflectedDescriptor {
    uint32_t set;
    uint32_t binding;
    DescriptorType type;
    uint32_t count;          // Array size (1 for non-arrays)
    std::string name;
    
    // For buffers: size and members
    uint32_t blockSize;
    std::vector<ReflectedMember> members;
    
    // For images: dimensionality
    uint32_t imageDimension; // 1D, 2D, 3D, Cube, etc.
    bool imageArrayed;
    bool imageMultisampled;
};

/**
 * Reflected push constant block
 */
struct ReflectedPushConstantBlock {
    uint32_t offset;
    uint32_t size;
    ShaderStageFlags stageFlags;
    std::string name;
    std::vector<ReflectedMember> members;
};

/**
 * Reflected vertex input attribute
 */
struct ReflectedInputAttribute {
    uint32_t location;
    uint32_t binding;      // Usually 0
    uint32_t format;       // VkFormat
    uint32_t offset;
    std::string name;
    uint32_t vecSize;      // Number of components (1-4)
};

/**
 * Reflected specialization constant
 */
struct ReflectedSpecConstant {
    uint32_t constantId;
    std::string name;
    uint32_t size;         // In bytes
    uint32_t offset;       // Offset in specialization data
};

/**
 * Complete reflection data for a shader module
 */
struct ShaderReflectionData {
    ShaderStageFlags stage;
    std::string entryPoint;
    
    std::vector<ReflectedDescriptor> descriptors;
    std::vector<ReflectedPushConstantBlock> pushConstants;
    std::vector<ReflectedInputAttribute> inputAttributes;
    std::vector<ReflectedSpecConstant> specConstants;
    
    // Compute shader info
    uint32_t localSizeX = 1;
    uint32_t localSizeY = 1;
    uint32_t localSizeZ = 1;
    
    // Helper methods
    std::optional<ReflectedDescriptor> findDescriptor(uint32_t set, uint32_t binding) const;
    std::optional<ReflectedDescriptor> findDescriptor(const std::string& name) const;
    std::optional<ReflectedInputAttribute> findInput(uint32_t location) const;
    std::optional<ReflectedInputAttribute> findInput(const std::string& name) const;
    
    uint32_t getTotalPushConstantSize() const;
    std::vector<uint32_t> getDescriptorSets() const;
};

/**
 * Shader reflection utilities
 */
class ShaderReflection {
public:
    /**
     * Reflect a SPIR-V shader module
     * @param spirv The SPIR-V bytecode
     * @param entryPoint Entry point name (default "main")
     * @return Reflection data or nullopt on failure
     */
    static std::optional<ShaderReflectionData> reflect(
        const std::vector<uint32_t>& spirv,
        const std::string& entryPoint = "main");
    
    /**
     * Merge reflection data from multiple shader stages
     * @param stages Vector of stage reflection data
     * @return Merged descriptor layout info
     */
    static std::vector<ReflectedDescriptor> mergeDescriptors(
        const std::vector<ShaderReflectionData>& stages);
    
    /**
     * Convert reflection data to cache entry format
     */
    static void toCacheEntry(const ShaderReflectionData& reflection,
                            ShaderCacheEntry& entry);
    
    /**
     * Get VkFormat for a reflected input attribute
     */
    static uint32_t getVkFormat(uint32_t baseType, uint32_t vecSize, uint32_t columns);
    
    /**
     * Get string name for descriptor type
     */
    static std::string descriptorTypeName(DescriptorType type);
    
    /**
     * Get string name for shader stage
     */
    static std::string stageName(ShaderStageFlags stage);
    
private:
    // Internal reflection helpers
    static void reflectDescriptors(void* module, ShaderReflectionData& data);
    static void reflectPushConstants(void* module, ShaderReflectionData& data);
    static void reflectInputs(void* module, ShaderReflectionData& data);
    static void reflectSpecConstants(void* module, ShaderReflectionData& data);
    static void reflectMembers(void* typeDesc, std::vector<ReflectedMember>& members);
};

/**
 * Descriptor set layout builder from reflection
 */
class DescriptorLayoutBuilder {
public:
    /**
     * Add a reflected shader's descriptors
     */
    void addShader(const ShaderReflectionData& reflection);
    
    /**
     * Get bindings for a specific set
     */
    std::vector<ReflectedDescriptor> getSetBindings(uint32_t set) const;
    
    /**
     * Get all descriptor sets used
     */
    std::vector<uint32_t> getSets() const;
    
    /**
     * Check if layout is compatible with another
     */
    bool isCompatible(const DescriptorLayoutBuilder& other) const;
    
private:
    // set -> binding -> descriptor
    std::map<uint32_t, std::map<uint32_t, ReflectedDescriptor>> descriptors_;
    ShaderStageFlags combinedStages_ = static_cast<ShaderStageFlags>(0);
};

} // namespace Sanic
