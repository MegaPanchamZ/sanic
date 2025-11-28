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

class Renderer {
public:
    Renderer(Window& window);
    ~Renderer();
    
    void drawFrame();
    void waitIdle();
    void processInput(float deltaTime);

private:
    void createInstance();

    struct QueueFamilyIndices {
        uint32_t graphicsFamily = -1;
        uint32_t presentFamily = -1;
        bool isComplete() { return graphicsFamily != -1 && presentFamily != -1; }
    };

    struct SwapChainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    void pickPhysicalDevice();
    bool isDeviceSuitable(VkPhysicalDevice device);
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);

    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);

    void createLogicalDevice();
    void createSurface();
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

    struct PushConstantData {
        glm::mat4 model;
        glm::mat4 normalMatrix;
    };
    
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
    
    // Shadow Mapping
    VkRenderPass shadowRenderPass;
    VkPipelineLayout shadowPipelineLayout;
    VkPipeline shadowPipeline;
    VkImage shadowImage;
    VkDeviceMemory shadowImageMemory;
    VkImageView shadowImageView;
    VkSampler shadowSampler;
    VkFramebuffer shadowFramebuffer;
    
    void createShadowRenderPass();
    void createShadowGraphicsPipeline();
    void createShadowResources();
    
    // ========================================================================
    // AAA STANDARD: Ray Tracing Readiness (VK_KHR_ray_tracing_pipeline)
    // ========================================================================
    bool rayTracingSupported = false;
    bool checkRayTracingSupport(VkPhysicalDevice device);
    void initRayTracingProperties();
    
    // Ray Tracing Properties (populated if supported)
    struct RayTracingProperties {
        uint32_t shaderGroupHandleSize = 0;
        uint32_t maxRayRecursionDepth = 0;
        uint32_t maxShaderGroupStride = 0;
    } rtProperties;
    
    // Cascaded Shadow Map helpers
    void calculateCascadeSplits(float nearClip, float farClip, float lambda = 0.5f);
    std::array<glm::mat4, 4> cascadeViewProjMatrices;
    std::array<float, 4> cascadeSplitDistances;
    
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
    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);
    VkFormat findDepthFormat();
    VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    VkInstance instance;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device;
    VkQueue graphicsQueue;
    VkQueue presentQueue;
    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    std::vector<VkImage> swapchainImages;
    VkFormat swapchainImageFormat;
    VkExtent2D swapchainExtent;
    
    Camera camera;
    Window& window;
};
