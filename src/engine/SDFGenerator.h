/**
 * SDFGenerator.h
 * 
 * Signed Distance Field generation for meshes and global scene.
 * Implements per-mesh SDFs and cascaded global distance field.
 * 
 * Turn 22-24: SDF system for ray tracing fallback
 */

#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>
#include <string>

class VulkanContext;

// Per-mesh SDF data
struct MeshSDF {
    uint32_t meshId;
    
    // Volume dimensions
    glm::ivec3 resolution;
    float voxelSize;
    
    // Bounds
    glm::vec3 boundsMin;
    glm::vec3 boundsMax;
    
    // GPU resources
    VkImage volumeImage;
    VkImageView volumeView;
    VkDeviceMemory volumeMemory;
    
    // Atlas placement (if using shared atlas)
    glm::ivec3 atlasOffset;
    bool inAtlas;
};

// Global SDF cascade level
struct SDFCascade {
    float voxelSize;
    glm::vec3 center;           // World-space center
    glm::vec3 extent;           // Half-extents
    
    VkImage volumeImage;
    VkImageView volumeView;
    VkDeviceMemory volumeMemory;
    
    glm::ivec3 resolution;      // Typically 128^3 or 256^3
    bool needsUpdate;
};

// GPU-side mesh SDF descriptor
struct GPUMeshSDF {
    glm::vec4 boundsMin;        // xyz = min, w = voxelSize
    glm::vec4 boundsMax;        // xyz = max, w = unused
    glm::ivec4 atlasOffset;     // xyz = offset, w = resolution
};

// Configuration
struct SDFConfig {
    // Per-mesh SDF
    uint32_t defaultMeshResolution = 64;
    float meshPadding = 0.1f;           // Padding around mesh bounds
    uint32_t maxMeshSDFs = 256;
    
    // Global SDF
    uint32_t cascadeCount = 4;
    uint32_t cascadeResolution = 128;
    float cascadeScale = 2.0f;          // Scale factor between cascades
    float baseCascadeExtent = 50.0f;    // First cascade half-extent
    
    // Atlas for small mesh SDFs
    bool useMeshAtlas = true;
    glm::ivec3 atlasResolution = glm::ivec3(512, 512, 512);
};

class SDFGenerator {
public:
    SDFGenerator() = default;
    ~SDFGenerator();
    
    bool initialize(VulkanContext* context, const SDFConfig& config = SDFConfig{});
    void cleanup();
    
    /**
     * Generate SDF for a mesh
     * @param meshId Unique mesh identifier
     * @param vertices Vertex positions (vec3 array)
     * @param vertexCount Number of vertices
     * @param indices Triangle indices
     * @param indexCount Number of indices
     * @return True if successful
     */
    bool generateMeshSDF(uint32_t meshId,
                         const float* vertices,
                         uint32_t vertexCount,
                         const uint32_t* indices,
                         uint32_t indexCount);
    
    /**
     * Generate SDF from vertex/index buffers on GPU
     */
    void generateMeshSDFGPU(VkCommandBuffer cmd,
                            uint32_t meshId,
                            VkBuffer vertexBuffer,
                            uint32_t vertexCount,
                            VkBuffer indexBuffer,
                            uint32_t indexCount,
                            const glm::vec3& boundsMin,
                            const glm::vec3& boundsMax);
    
    /**
     * Update global SDF cascades
     * Should be called when camera moves or scene changes
     */
    void updateGlobalSDF(VkCommandBuffer cmd,
                         const glm::vec3& cameraPos,
                         VkBuffer instanceBuffer,
                         uint32_t instanceCount);
    
    /**
     * Get mesh SDF for shader binding
     */
    const MeshSDF* getMeshSDF(uint32_t meshId) const;
    
    /**
     * Get global SDF cascade for shader binding
     */
    VkImageView getGlobalSDFView(uint32_t cascadeLevel) const;
    
    /**
     * Get all cascade info for shader
     */
    struct CascadeInfo {
        glm::vec4 centerExtent;     // xyz = center, w = halfExtent
        float voxelSize;
        float pad[3];
    };
    void getCascadeInfo(std::vector<CascadeInfo>& outInfo) const;
    
    /**
     * Get mesh SDF atlas
     */
    VkImageView getMeshSDFAtlasView() const { return meshAtlasView_; }
    VkBuffer getMeshSDFDescriptorBuffer() const { return meshDescBuffer_; }
    
private:
    bool createGlobalCascades();
    bool createMeshAtlas();
    bool createPipelines();
    bool loadShader(const std::string& path, VkShaderModule& outModule);
    
    // CPU-side SDF generation (fallback)
    void generateSDFCPU(const float* vertices, uint32_t vertexCount,
                        const uint32_t* indices, uint32_t indexCount,
                        std::vector<float>& outSDF,
                        glm::ivec3 resolution,
                        const glm::vec3& boundsMin,
                        float voxelSize);
    
    // Distance to triangle
    float pointTriangleDistance(const glm::vec3& p,
                                const glm::vec3& a,
                                const glm::vec3& b,
                                const glm::vec3& c);
    
    VulkanContext* context_ = nullptr;
    SDFConfig config_;
    
    // Mesh SDFs
    std::unordered_map<uint32_t, MeshSDF> meshSDFs_;
    
    // Mesh SDF atlas
    VkImage meshAtlas_ = VK_NULL_HANDLE;
    VkImageView meshAtlasView_ = VK_NULL_HANDLE;
    VkDeviceMemory meshAtlasMemory_ = VK_NULL_HANDLE;
    
    // Mesh SDF descriptors
    VkBuffer meshDescBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory meshDescMemory_ = VK_NULL_HANDLE;
    std::vector<GPUMeshSDF> meshDescriptors_;
    
    // Atlas allocation tracking
    struct AtlasBlock {
        glm::ivec3 offset;
        glm::ivec3 size;
        bool used;
        uint32_t meshId;
    };
    std::vector<AtlasBlock> atlasBlocks_;
    
    // Global SDF cascades
    std::vector<SDFCascade> cascades_;
    glm::vec3 lastCameraPos_;
    
    // Compute pipelines
    VkPipeline meshSDFPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout meshSDFLayout_ = VK_NULL_HANDLE;
    VkPipeline globalSDFPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout globalSDFLayout_ = VK_NULL_HANDLE;
    VkPipeline sdfCombinePipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout sdfCombineLayout_ = VK_NULL_HANDLE;
    
    // Descriptors
    VkDescriptorSetLayout meshSDFDescLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout globalSDFDescLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descPool_ = VK_NULL_HANDLE;
    VkDescriptorSet meshSDFDescSet_ = VK_NULL_HANDLE;
    VkDescriptorSet globalSDFDescSet_ = VK_NULL_HANDLE;
    
    bool initialized_ = false;
};
