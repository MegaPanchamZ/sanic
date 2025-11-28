#pragma once
#include "Window.h"
#include "Camera.h"
#include "Vertex.h"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <stdexcept>
#include <vector>
#include <optional>
#include "GameObject.h"
#include "Skybox.h"
#include "AccelerationStructureBuilder.h"
#include "RTPipeline.h"
#include "VulkanContext.h"
#include "ShadowRenderer.h"
#include "DeferredRenderer.h"
#include "DDGISystem.h"

class Renderer {
public:
    Renderer(Window& window);
    ~Renderer();
    
    void drawFrame();
    void waitIdle();
    void processInput(float deltaTime);

private:
    VulkanContext vulkanContext;
    std::unique_ptr<ShadowRenderer> shadowRenderer;
    std::unique_ptr<DeferredRenderer> deferredRenderer;
    
    // Cached handles from VulkanContext for convenience
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkQueue graphicsQueue;
    VkQueue presentQueue;
    VkSurfaceKHR surface;

    VkSwapchainKHR swapchain;
    std::vector<VkImage> swapchainImages;
    VkFormat swapchainImageFormat;
    VkExtent2D swapchainExtent;
    
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);

    void createSwapchain();
    void createImageViews();
    void createRenderPass();
    void createGraphicsPipeline();
    void createFramebuffers();
    void createCommandPool();
    void createCommandBuffers();
    void createSyncObjects();
    
    VkShaderModule createShaderModule(const std::vector<char>& code);
    std::vector<char> readFile(const std::string& filename);

    std::vector<VkImageView> swapchainImageViews;
    VkRenderPass renderPass;
    VkPipelineLayout pipelineLayout;
    VkPipeline graphicsPipeline;
    std::vector<VkFramebuffer> swapchainFramebuffers;
    
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;
    
    VkSemaphore imageAvailableSemaphore;
    VkSemaphore renderFinishedSemaphore;
    VkFence inFlightFence;

    VkImage depthImage;
    VkDeviceMemory depthImageMemory;
    VkImageView depthImageView;
    
    // ========================================================================
    // AAA STANDARD: Uniform Buffer Object with CSM support
    // ========================================================================
    struct UniformBufferObject {
        alignas(16) glm::mat4 view;
        alignas(16) glm::mat4 proj;
        alignas(16) glm::vec4 lightPos;      // xyz = position/direction, w = type (0=directional)
        alignas(16) glm::vec4 viewPos;
        alignas(16) glm::vec4 lightColor;
        alignas(16) glm::mat4 lightSpaceMatrix;
        // Cascaded Shadow Maps (4 cascades)
        alignas(16) glm::mat4 cascadeViewProj[4];
        alignas(16) glm::vec4 cascadeSplits;   // View-space Z distances for cascade splits
        alignas(16) glm::vec4 shadowParams;    // x=mapSize, y=pcfRadius, z=bias, w=cascadeBlend
    };

    // PushConstantData now managed in DeferredRenderer/ShadowRenderer as needed
    
    VkBuffer uniformBuffer;
    VkDeviceMemory uniformBufferMemory;
    void* uniformBufferMapped;
    
    VkDescriptorSetLayout descriptorSetLayout;
    VkDescriptorPool descriptorPool;
    
    std::vector<GameObject> gameObjects;
    
    // Skybox
    std::unique_ptr<Skybox> skybox;
    VkDescriptorSetLayout skyboxDescriptorSetLayout;
    VkPipelineLayout skyboxPipelineLayout;
    VkPipeline skyboxPipeline;

    void createSkyboxGraphicsPipeline();
    void createSkyboxDescriptorSetLayout();
    
    // ========================================================================
    // AAA STANDARD: Ray Tracing Readiness (VK_KHR_ray_tracing_pipeline)
    // ========================================================================
    bool rayTracingSupported = false;
    void initRayTracingProperties();
    
    // Ray Tracing Properties (populated if supported)
    struct RayTracingProperties {
        uint32_t shaderGroupHandleSize = 0;
        uint32_t maxRayRecursionDepth = 0;
        uint32_t maxShaderGroupStride = 0;
    } rtProperties;
    
    std::unique_ptr<AccelerationStructureBuilder> asBuilder;
    std::vector<AccelerationStructure> blasList;
    AccelerationStructure tlas;
    void buildAccelerationStructures();
    
    // RT Pipeline
    std::unique_ptr<RTPipeline> rtPipeline;
    VkDescriptorSetLayout rtDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet rtDescriptorSet = VK_NULL_HANDLE;
    
    // RT Output Image
    VkImage rtOutputImage = VK_NULL_HANDLE;
    VkDeviceMemory rtOutputImageMemory = VK_NULL_HANDLE;
    VkImageView rtOutputImageView = VK_NULL_HANDLE;
    
    // RT Geometry Info Buffer (for textured ray tracing)
    struct RTGeometryInfo {
        uint64_t vertexBufferAddress;
        uint64_t indexBufferAddress;
        uint32_t textureIndex;
        uint32_t padding;
    };
    VkBuffer rtGeometryInfoBuffer = VK_NULL_HANDLE;
    VkDeviceMemory rtGeometryInfoBufferMemory = VK_NULL_HANDLE;
    static const uint32_t RT_MAX_TEXTURES = 16;
    
    // RT Rendering Toggle (press R to switch)
    bool useRayTracing = true;
    
    void createRTDescriptorSetLayout();
    void createRTOutputImage();
    void createRTDescriptorSet();
    void dispatchRayTracing(VkCommandBuffer commandBuffer);
    
    // ========================================================================
    // DDGI (Dynamic Diffuse Global Illumination)
    // ========================================================================
    std::unique_ptr<DDGISystem> ddgiSystem;
    bool ddgiEnabled = true;
    
    void createDepthResources();
    void createUniformBuffers();
    void createDescriptorSetLayout();
    void createDescriptorPool();
    void createDescriptorSet(GameObject& gameObject);
    void updateUniformBuffer(uint32_t currentImage);

    void loadGameObjects();
    std::shared_ptr<Mesh> createTerrainMesh();
    std::shared_ptr<Mesh> createSphereMesh(int segments, int rings);

    void createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory);
    void createImageWithLayers(uint32_t width, uint32_t height, uint32_t layers, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory);
    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);
    VkImageView createImageViewWithLayers(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t baseLayer, uint32_t layerCount, VkImageViewType viewType);
    
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    Camera camera;
    Window& window;
    
    VkFormat findDepthFormat(); 
};