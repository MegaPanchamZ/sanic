#pragma once

#include "VulkanContext.h"
#include "GameObject.h"
#include <glm/glm.hpp>
#include <vector>

class VirtualShadowMap {
public:
    VirtualShadowMap(VulkanContext& context);
    ~VirtualShadowMap();

    void update(VkCommandBuffer cmd, const glm::mat4& viewProj, const glm::vec3& lightDir, VkImageView depthImageView, VkBuffer uniformBuffer);
    
    VkImage getPhysicalAtlas() const { return physicalAtlas; }
    VkImageView getPhysicalAtlasView() const { return physicalAtlasView; }
    
    VkImage getPageTable() const { return pageTable; }
    VkImageView getPageTableView() const { return pageTableView; }

    struct VSMUniformData {
        glm::mat4 lightViewProj;
        glm::vec4 pageTableParams; // x: virtualSize, y: pageSize, z: physicalSize, w: unused
    };

    VSMUniformData getUniformData() const;

private:
    void createResources();
    void createPageTable();
    void createPhysicalAtlas();
    void createPipelines();
    void createPageRequestsBuffer();

    VulkanContext& context;

    // Constants
    const uint32_t VIRTUAL_SIZE = 16384;
    const uint32_t PAGE_SIZE = 128;
    const uint32_t PHYSICAL_SIZE = 4096;
    
    // Derived constants
    const uint32_t PAGE_TABLE_SIZE = VIRTUAL_SIZE / PAGE_SIZE; // 128x128
    const uint32_t PHYSICAL_PAGES_PER_ROW = PHYSICAL_SIZE / PAGE_SIZE; // 32
    const uint32_t TOTAL_PHYSICAL_PAGES = PHYSICAL_PAGES_PER_ROW * PHYSICAL_PAGES_PER_ROW; // 1024
    const uint32_t TOTAL_VIRTUAL_PAGES = PAGE_TABLE_SIZE * PAGE_TABLE_SIZE;

    // Resources
    VkImage pageTable;
    VkDeviceMemory pageTableMemory;
    VkImageView pageTableView;

    VkImage physicalAtlas;
    VkDeviceMemory physicalAtlasMemory;
    VkImageView physicalAtlasView;
    
    VkBuffer pageRequestsBuffer;
    VkDeviceMemory pageRequestsBufferMemory;
    
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    
    VkPipelineLayout markingPipelineLayout;
    VkPipeline markingPipeline;
    VkDescriptorSetLayout markingDescriptorSetLayout;
    VkDescriptorPool descriptorPool;
    VkDescriptorSet markingDescriptorSet;
    VkSampler sampler;
    
    VkPipelineLayout clearPipelineLayout;
    VkPipeline clearPipeline;
    
    // Shadow Rendering
    VkRenderPass shadowRenderPass;
    VkPipelineLayout shadowPipelineLayout;
    VkPipeline shadowPipeline;
    VkDescriptorSetLayout shadowDescriptorSetLayout;
    VkDescriptorSet shadowDescriptorSet;
    VkFramebuffer shadowFramebuffer; // Pre-created framebuffer (not per-frame)
    
    // Screen dimensions for proper dispatch sizing
    uint32_t screenWidth = 1920;
    uint32_t screenHeight = 1080;
    
    void createShadowRenderPass();
    void createShadowPipeline();
    void createShadowDescriptorSet();
    void createShadowFramebuffer();

public:
    void renderNaniteShadows(VkCommandBuffer cmd, const std::vector<GameObject>& gameObjects);
    void setScreenSize(uint32_t width, uint32_t height) { screenWidth = width; screenHeight = height; }

    // CPU side tracking
    std::vector<uint32_t> pageTableData; // Maps virtual page index to physical page index
    std::vector<bool> physicalPageAllocated;
    
    glm::mat4 currentLightViewProj;
    
    // Helpers
    void createImage(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkImage& image, VkDeviceMemory& memory);
    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspect);
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
    VkShaderModule createShaderModule(const std::vector<char>& code);
    std::vector<char> readFile(const std::string& filename);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
};
