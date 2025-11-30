#ifdef SANIC_ENABLE_VULKAN

#define VMA_IMPLEMENTATION
#include "VulkanRHI.h"
#include "../../core/Window.h"
#include "../../core/Log.h"

#include <algorithm>
#include <set>
#include <cstring>
#include <stdexcept>

#ifdef _WIN32
#include <Windows.h>
#include <vulkan/vulkan_win32.h>
#endif

namespace Sanic {

// ============================================================================
// Debug Callback
// ============================================================================
static VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {
    
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        LOG_ERROR("[Vulkan] {}", pCallbackData->pMessage);
    } else if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        LOG_WARN("[Vulkan] {}", pCallbackData->pMessage);
    } else if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
        LOG_INFO("[Vulkan] {}", pCallbackData->pMessage);
    } else {
        LOG_TRACE("[Vulkan] {}", pCallbackData->pMessage);
    }
    
    return VK_FALSE;
}

// ============================================================================
// VulkanRHI Constructor/Destructor
// ============================================================================
VulkanRHI::VulkanRHI() = default;

VulkanRHI::~VulkanRHI() {
    shutdown();
}

// ============================================================================
// Initialize
// ============================================================================
bool VulkanRHI::initialize(Window& window, const RHIConfig& config) {
    m_config = config;
    m_window = &window;
    m_validationEnabled = config.enableValidation;
    
    LOG_INFO("Initializing Vulkan RHI...");
    
    if (!createInstance(config)) {
        LOG_ERROR("Failed to create Vulkan instance");
        return false;
    }
    
    // Create surface
#ifdef _WIN32
    VkWin32SurfaceCreateInfoKHR surfaceInfo{};
    surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surfaceInfo.hwnd = static_cast<HWND>(window.getNativeHandle());
    surfaceInfo.hinstance = GetModuleHandle(nullptr);
    
    if (vkCreateWin32SurfaceKHR(m_instance, &surfaceInfo, nullptr, &m_surface) != VK_SUCCESS) {
        LOG_ERROR("Failed to create Vulkan surface");
        return false;
    }
#endif
    
    if (!selectPhysicalDevice()) {
        LOG_ERROR("Failed to find suitable GPU");
        return false;
    }
    
    if (!createLogicalDevice(config)) {
        LOG_ERROR("Failed to create logical device");
        return false;
    }
    
    if (!createAllocator()) {
        LOG_ERROR("Failed to create VMA allocator");
        return false;
    }
    
    if (!createSwapchain(window.getWidth(), window.getHeight())) {
        LOG_ERROR("Failed to create swapchain");
        return false;
    }
    
    if (!createCommandPools()) {
        LOG_ERROR("Failed to create command pools");
        return false;
    }
    
    if (!createSyncObjects()) {
        LOG_ERROR("Failed to create synchronization objects");
        return false;
    }
    
    if (!createDescriptorPools()) {
        LOG_ERROR("Failed to create descriptor pools");
        return false;
    }
    
    queryCapabilities();
    
    LOG_INFO("Vulkan RHI initialized successfully");
    LOG_INFO("  Device: {}", m_capabilities.deviceName);
    LOG_INFO("  Driver: {}", m_capabilities.driverVersion);
    LOG_INFO("  API Version: {}", m_capabilities.apiVersion);
    LOG_INFO("  Ray Tracing: {}", m_capabilities.supportsRayTracing ? "Yes" : "No");
    LOG_INFO("  Mesh Shaders: {}", m_capabilities.supportsMeshShaders ? "Yes" : "No");
    
    return true;
}

// ============================================================================
// Shutdown
// ============================================================================
void VulkanRHI::shutdown() {
    if (m_device == VK_NULL_HANDLE) return;
    
    LOG_INFO("Shutting down Vulkan RHI...");
    
    waitIdle();
    
    // Destroy per-frame resources
    for (auto& frame : m_frameResources) {
        if (frame.descriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(m_device, frame.descriptorPool, nullptr);
        }
        if (frame.inFlightFence != VK_NULL_HANDLE) {
            vkDestroyFence(m_device, frame.inFlightFence, nullptr);
        }
        if (frame.renderFinishedSemaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_device, frame.renderFinishedSemaphore, nullptr);
        }
        if (frame.imageAvailableSemaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_device, frame.imageAvailableSemaphore, nullptr);
        }
        if (frame.commandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(m_device, frame.commandPool, nullptr);
        }
    }
    
    destroySwapchain();
    
    if (m_allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(m_allocator);
        m_allocator = VK_NULL_HANDLE;
    }
    
    if (m_device != VK_NULL_HANDLE) {
        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }
    
    if (m_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }
    
    if (m_debugMessenger != VK_NULL_HANDLE) {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            m_instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func) {
            func(m_instance, m_debugMessenger, nullptr);
        }
        m_debugMessenger = VK_NULL_HANDLE;
    }
    
    if (m_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }
    
    LOG_INFO("Vulkan RHI shut down");
}

// ============================================================================
// Instance Creation
// ============================================================================
bool VulkanRHI::createInstance(const RHIConfig& config) {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = config.applicationName;
    appInfo.applicationVersion = config.applicationVersion;
    appInfo.pEngineName = "Sanic Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;
    
    std::vector<const char*> extensions = {
        VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef _WIN32
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#endif
    };
    
    std::vector<const char*> layers;
    
    if (m_validationEnabled) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        layers.push_back("VK_LAYER_KHRONOS_validation");
    }
    
    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
    createInfo.ppEnabledLayerNames = layers.data();
    
    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (m_validationEnabled) {
        debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debugCreateInfo.messageSeverity = 
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugCreateInfo.messageType = 
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugCreateInfo.pfnUserCallback = VulkanDebugCallback;
        createInfo.pNext = &debugCreateInfo;
    }
    
    if (vkCreateInstance(&createInfo, nullptr, &m_instance) != VK_SUCCESS) {
        return false;
    }
    
    // Create debug messenger
    if (m_validationEnabled) {
        auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            m_instance, "vkCreateDebugUtilsMessengerEXT");
        if (func) {
            func(m_instance, &debugCreateInfo, nullptr, &m_debugMessenger);
        }
    }
    
    return true;
}

// ============================================================================
// Physical Device Selection
// ============================================================================
bool VulkanRHI::selectPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
    
    if (deviceCount == 0) {
        LOG_ERROR("No Vulkan-capable GPUs found");
        return false;
    }
    
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());
    
    // Score devices and pick the best one
    int bestScore = -1;
    
    for (const auto& device : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(device, &props);
        
        auto indices = findQueueFamilies(device);
        auto swapchainSupport = querySwapchainSupport(device);
        bool extensionsSupported = checkDeviceExtensionSupport(device);
        
        if (!indices.isComplete() || !extensionsSupported) continue;
        if (swapchainSupport.formats.empty() || swapchainSupport.presentModes.empty()) continue;
        
        int score = 0;
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) score += 10000;
        else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score += 1000;
        
        score += props.limits.maxImageDimension2D;
        
        if (score > bestScore) {
            bestScore = score;
            m_physicalDevice = device;
        }
    }
    
    if (m_physicalDevice == VK_NULL_HANDLE) {
        return false;
    }
    
    m_queueFamilies = findQueueFamilies(m_physicalDevice);
    vkGetPhysicalDeviceProperties(m_physicalDevice, &m_deviceProperties);
    vkGetPhysicalDeviceFeatures(m_physicalDevice, &m_deviceFeatures);
    
    return true;
}

// ============================================================================
// Logical Device Creation
// ============================================================================
bool VulkanRHI::createLogicalDevice(const RHIConfig& config) {
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {
        m_queueFamilies.graphicsFamily.value(),
        m_queueFamilies.presentFamily.value()
    };
    
    if (m_queueFamilies.computeFamily.has_value()) {
        uniqueQueueFamilies.insert(m_queueFamilies.computeFamily.value());
    }
    if (m_queueFamilies.transferFamily.has_value()) {
        uniqueQueueFamilies.insert(m_queueFamilies.transferFamily.value());
    }
    
    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }
    
    // Required extensions
    std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
        VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
    };
    
    // Optional extensions
    if (config.enableMeshShaders) {
        deviceExtensions.push_back(VK_EXT_MESH_SHADER_EXTENSION_NAME);
    }
    if (config.enableRayTracing) {
        deviceExtensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
        deviceExtensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
        deviceExtensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
    }
    
    // Features chain
    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.features.samplerAnisotropy = VK_TRUE;
    features2.features.fillModeNonSolid = VK_TRUE;
    features2.features.wideLines = VK_TRUE;
    features2.features.multiDrawIndirect = VK_TRUE;
    features2.features.shaderInt64 = VK_TRUE;
    
    m_vulkan12Features = {};
    m_vulkan12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    m_vulkan12Features.bufferDeviceAddress = VK_TRUE;
    m_vulkan12Features.descriptorIndexing = VK_TRUE;
    m_vulkan12Features.descriptorBindingPartiallyBound = VK_TRUE;
    m_vulkan12Features.descriptorBindingVariableDescriptorCount = VK_TRUE;
    m_vulkan12Features.runtimeDescriptorArray = VK_TRUE;
    m_vulkan12Features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    m_vulkan12Features.drawIndirectCount = VK_TRUE;
    m_vulkan12Features.timelineSemaphore = VK_TRUE;
    features2.pNext = &m_vulkan12Features;
    
    m_vulkan13Features = {};
    m_vulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    m_vulkan13Features.dynamicRendering = VK_TRUE;
    m_vulkan13Features.synchronization2 = VK_TRUE;
    m_vulkan13Features.maintenance4 = VK_TRUE;
    m_vulkan12Features.pNext = &m_vulkan13Features;
    
    void** pNextChain = &m_vulkan13Features.pNext;
    
    if (config.enableMeshShaders) {
        m_meshShaderFeatures = {};
        m_meshShaderFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
        m_meshShaderFeatures.meshShader = VK_TRUE;
        m_meshShaderFeatures.taskShader = VK_TRUE;
        *pNextChain = &m_meshShaderFeatures;
        pNextChain = &m_meshShaderFeatures.pNext;
    }
    
    if (config.enableRayTracing) {
        m_rayTracingFeatures = {};
        m_rayTracingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
        m_rayTracingFeatures.rayTracingPipeline = VK_TRUE;
        *pNextChain = &m_rayTracingFeatures;
        pNextChain = &m_rayTracingFeatures.pNext;
        
        m_accelStructFeatures = {};
        m_accelStructFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
        m_accelStructFeatures.accelerationStructure = VK_TRUE;
        *pNextChain = &m_accelStructFeatures;
        pNextChain = &m_accelStructFeatures.pNext;
    }
    
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = &features2;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();
    
    if (vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device) != VK_SUCCESS) {
        return false;
    }
    
    // Get queues
    vkGetDeviceQueue(m_device, m_queueFamilies.graphicsFamily.value(), 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, m_queueFamilies.presentFamily.value(), 0, &m_presentQueue);
    
    if (m_queueFamilies.computeFamily.has_value()) {
        vkGetDeviceQueue(m_device, m_queueFamilies.computeFamily.value(), 0, &m_computeQueue);
    } else {
        m_computeQueue = m_graphicsQueue;
    }
    
    if (m_queueFamilies.transferFamily.has_value()) {
        vkGetDeviceQueue(m_device, m_queueFamilies.transferFamily.value(), 0, &m_transferQueue);
    } else {
        m_transferQueue = m_graphicsQueue;
    }
    
    // Query ray tracing properties
    if (config.enableRayTracing) {
        m_rayTracingProperties = {};
        m_rayTracingProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
        
        VkPhysicalDeviceProperties2 props2{};
        props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        props2.pNext = &m_rayTracingProperties;
        vkGetPhysicalDeviceProperties2(m_physicalDevice, &props2);
    }
    
    return true;
}

// ============================================================================
// VMA Allocator Creation
// ============================================================================
bool VulkanRHI::createAllocator() {
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocatorInfo.physicalDevice = m_physicalDevice;
    allocatorInfo.device = m_device;
    allocatorInfo.instance = m_instance;
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    
    return vmaCreateAllocator(&allocatorInfo, &m_allocator) == VK_SUCCESS;
}

// ============================================================================
// Swapchain Creation
// ============================================================================
bool VulkanRHI::createSwapchain(uint32_t width, uint32_t height) {
    auto swapchainSupport = querySwapchainSupport(m_physicalDevice);
    
    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapchainSupport.formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(swapchainSupport.presentModes);
    VkExtent2D extent = chooseSwapExtent(swapchainSupport.capabilities, width, height);
    
    uint32_t imageCount = swapchainSupport.capabilities.minImageCount + 1;
    if (swapchainSupport.capabilities.maxImageCount > 0) {
        imageCount = std::min(imageCount, swapchainSupport.capabilities.maxImageCount);
    }
    imageCount = std::max(imageCount, m_config.frameBufferCount);
    
    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    
    uint32_t queueFamilyIndices[] = {
        m_queueFamilies.graphicsFamily.value(),
        m_queueFamilies.presentFamily.value()
    };
    
    if (m_queueFamilies.graphicsFamily != m_queueFamilies.presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    
    createInfo.preTransform = swapchainSupport.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;
    
    if (vkCreateSwapchainKHR(m_device, &createInfo, nullptr, &m_swapchain) != VK_SUCCESS) {
        return false;
    }
    
    m_swapchainImageFormat = surfaceFormat.format;
    m_swapchainExtent = extent;
    m_swapchainFormat = FromVkFormat(surfaceFormat.format);
    
    // Get swapchain images
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, nullptr);
    m_swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, m_swapchainImages.data());
    
    // Create image views and texture wrappers
    m_swapchainImageViews.resize(imageCount);
    m_swapchainTextures.resize(imageCount);
    
    for (uint32_t i = 0; i < imageCount; i++) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_swapchainImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_swapchainImageFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        
        if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_swapchainImageViews[i]) != VK_SUCCESS) {
            return false;
        }
        
        RHITextureDesc desc{};
        desc.width = extent.width;
        desc.height = extent.height;
        desc.format = m_swapchainFormat;
        desc.usage = RHITextureUsage::RenderTarget;
        
        m_swapchainTextures[i] = std::make_unique<VulkanTexture>(
            this, m_swapchainImages[i], m_swapchainImageViews[i], desc);
    }
    
    return true;
}

void VulkanRHI::destroySwapchain() {
    m_swapchainTextures.clear();
    
    for (auto& view : m_swapchainImageViews) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(m_device, view, nullptr);
        }
    }
    m_swapchainImageViews.clear();
    m_swapchainImages.clear();
    
    if (m_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
}

bool VulkanRHI::recreateSwapchain(uint32_t width, uint32_t height) {
    waitIdle();
    destroySwapchain();
    return createSwapchain(width, height);
}

// ============================================================================
// Command Pools
// ============================================================================
bool VulkanRHI::createCommandPools() {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = m_queueFamilies.graphicsFamily.value();
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        
        if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_frameResources[i].commandPool) != VK_SUCCESS) {
            return false;
        }
        
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = m_frameResources[i].commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        
        if (vkAllocateCommandBuffers(m_device, &allocInfo, &m_frameResources[i].commandBuffer) != VK_SUCCESS) {
            return false;
        }
    }
    
    return true;
}

// ============================================================================
// Synchronization Objects
// ============================================================================
bool VulkanRHI::createSyncObjects() {
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_frameResources[i].imageAvailableSemaphore) != VK_SUCCESS ||
            vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_frameResources[i].renderFinishedSemaphore) != VK_SUCCESS ||
            vkCreateFence(m_device, &fenceInfo, nullptr, &m_frameResources[i].inFlightFence) != VK_SUCCESS) {
            return false;
        }
    }
    
    return true;
}

// ============================================================================
// Descriptor Pools
// ============================================================================
bool VulkanRHI::createDescriptorPools() {
    std::vector<VkDescriptorPoolSize> poolSizes = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 100 },
    };
    
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 10000;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_frameResources[i].descriptorPool) != VK_SUCCESS) {
            return false;
        }
    }
    
    return true;
}

// ============================================================================
// Query Capabilities
// ============================================================================
void VulkanRHI::queryCapabilities() {
    m_capabilities.deviceName = m_deviceProperties.deviceName;
    
    uint32_t major = VK_VERSION_MAJOR(m_deviceProperties.driverVersion);
    uint32_t minor = VK_VERSION_MINOR(m_deviceProperties.driverVersion);
    uint32_t patch = VK_VERSION_PATCH(m_deviceProperties.driverVersion);
    m_capabilities.driverVersion = std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
    m_capabilities.apiVersion = "Vulkan 1.3";
    
    m_capabilities.vendorID = m_deviceProperties.vendorID;
    m_capabilities.deviceID = m_deviceProperties.deviceID;
    
    // Features
    m_capabilities.supportsRayTracing = m_rayTracingFeatures.rayTracingPipeline == VK_TRUE;
    m_capabilities.supportsMeshShaders = m_meshShaderFeatures.meshShader == VK_TRUE;
    m_capabilities.supportsBufferDeviceAddress = m_vulkan12Features.bufferDeviceAddress == VK_TRUE;
    m_capabilities.supportsBindless = m_vulkan12Features.descriptorIndexing == VK_TRUE;
    m_capabilities.supportsMultiDrawIndirectCount = m_vulkan12Features.drawIndirectCount == VK_TRUE;
    m_capabilities.supportsTimestampQueries = true;
    
    // Limits
    m_capabilities.maxBoundDescriptorSets = m_deviceProperties.limits.maxBoundDescriptorSets;
    m_capabilities.maxPushConstantSize = m_deviceProperties.limits.maxPushConstantsSize;
    m_capabilities.maxUniformBufferSize = m_deviceProperties.limits.maxUniformBufferRange;
    m_capabilities.maxStorageBufferSize = m_deviceProperties.limits.maxStorageBufferRange;
    m_capabilities.maxTexture2DSize = m_deviceProperties.limits.maxImageDimension2D;
    m_capabilities.maxTexture3DSize = m_deviceProperties.limits.maxImageDimension3D;
    m_capabilities.maxTextureCubeSize = m_deviceProperties.limits.maxImageDimensionCube;
    m_capabilities.maxTextureArrayLayers = m_deviceProperties.limits.maxImageArrayLayers;
    m_capabilities.maxColorAttachments = m_deviceProperties.limits.maxColorAttachments;
    m_capabilities.timestampPeriod = m_deviceProperties.limits.timestampPeriod;
    
    m_capabilities.maxComputeWorkGroupSize[0] = m_deviceProperties.limits.maxComputeWorkGroupSize[0];
    m_capabilities.maxComputeWorkGroupSize[1] = m_deviceProperties.limits.maxComputeWorkGroupSize[1];
    m_capabilities.maxComputeWorkGroupSize[2] = m_deviceProperties.limits.maxComputeWorkGroupSize[2];
    m_capabilities.maxComputeWorkGroupCount[0] = m_deviceProperties.limits.maxComputeWorkGroupCount[0];
    m_capabilities.maxComputeWorkGroupCount[1] = m_deviceProperties.limits.maxComputeWorkGroupCount[1];
    m_capabilities.maxComputeWorkGroupCount[2] = m_deviceProperties.limits.maxComputeWorkGroupCount[2];
    
    // Ray tracing
    if (m_capabilities.supportsRayTracing) {
        m_capabilities.maxRayRecursionDepth = m_rayTracingProperties.maxRayRecursionDepth;
        m_capabilities.shaderGroupHandleSize = m_rayTracingProperties.shaderGroupHandleSize;
        m_capabilities.shaderGroupBaseAlignment = m_rayTracingProperties.shaderGroupBaseAlignment;
    }
    
    // Memory
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProps);
    
    for (uint32_t i = 0; i < memProps.memoryHeapCount; i++) {
        if (memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            m_capabilities.dedicatedVideoMemory += memProps.memoryHeaps[i].size;
        } else {
            m_capabilities.sharedSystemMemory += memProps.memoryHeaps[i].size;
        }
    }
}

// ============================================================================
// Helper Functions
// ============================================================================
VulkanQueueFamilyIndices VulkanRHI::findQueueFamilies(VkPhysicalDevice device) {
    VulkanQueueFamilyIndices indices;
    
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());
    
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        // Graphics queue
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = i;
        }
        
        // Dedicated compute queue
        if ((queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) &&
            !(queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            indices.computeFamily = i;
        }
        
        // Dedicated transfer queue
        if ((queueFamilies[i].queueFlags & VK_QUEUE_TRANSFER_BIT) &&
            !(queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
            !(queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
            indices.transferFamily = i;
        }
        
        // Present queue
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport);
        if (presentSupport) {
            indices.presentFamily = i;
        }
    }
    
    // Fallbacks
    if (!indices.computeFamily.has_value()) {
        indices.computeFamily = indices.graphicsFamily;
    }
    if (!indices.transferFamily.has_value()) {
        indices.transferFamily = indices.graphicsFamily;
    }
    
    return indices;
}

VulkanSwapchainSupportDetails VulkanRHI::querySwapchainSupport(VkPhysicalDevice device) {
    VulkanSwapchainSupportDetails details;
    
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_surface, &details.capabilities);
    
    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, nullptr);
    if (formatCount > 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, details.formats.data());
    }
    
    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, nullptr);
    if (presentModeCount > 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, details.presentModes.data());
    }
    
    return details;
}

bool VulkanRHI::checkDeviceExtensionSupport(VkPhysicalDevice device) {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());
    
    std::set<std::string> requiredExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };
    
    for (const auto& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }
    
    return requiredExtensions.empty();
}

VkSurfaceFormatKHR VulkanRHI::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
    for (const auto& format : availableFormats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    return availableFormats[0];
}

VkPresentModeKHR VulkanRHI::chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
    if (!m_config.vsync) {
        for (const auto& mode : availablePresentModes) {
            if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
                return mode;
            }
        }
    }
    
    for (const auto& mode : availablePresentModes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return mode;
        }
    }
    
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanRHI::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, uint32_t width, uint32_t height) {
    if (capabilities.currentExtent.width != UINT32_MAX) {
        return capabilities.currentExtent;
    }
    
    VkExtent2D extent = { width, height };
    extent.width = std::clamp(extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    extent.height = std::clamp(extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    return extent;
}

VkQueue VulkanRHI::getQueue(RHIQueueType type) const {
    switch (type) {
        case RHIQueueType::Graphics: return m_graphicsQueue;
        case RHIQueueType::Compute: return m_computeQueue;
        case RHIQueueType::Transfer: return m_transferQueue;
        default: return m_graphicsQueue;
    }
}

uint32_t VulkanRHI::getQueueFamilyIndex(RHIQueueType type) const {
    switch (type) {
        case RHIQueueType::Graphics: return getGraphicsQueueFamily();
        case RHIQueueType::Compute: return getComputeQueueFamily();
        case RHIQueueType::Transfer: return getTransferQueueFamily();
        default: return getGraphicsQueueFamily();
    }
}

// ============================================================================
// Resource Creation
// ============================================================================
std::unique_ptr<IRHIBuffer> VulkanRHI::createBuffer(const RHIBufferDesc& desc) {
    return std::make_unique<VulkanBuffer>(this, desc);
}

std::unique_ptr<IRHITexture> VulkanRHI::createTexture(const RHITextureDesc& desc) {
    return std::make_unique<VulkanTexture>(this, desc);
}

std::unique_ptr<IRHITextureView> VulkanRHI::createTextureView(
    IRHITexture* texture, RHIFormat format,
    uint32_t baseMip, uint32_t mipCount,
    uint32_t baseLayer, uint32_t layerCount) {
    auto vkTexture = static_cast<VulkanTexture*>(texture);
    return std::make_unique<VulkanTextureView>(this, vkTexture, format, baseMip, mipCount, baseLayer, layerCount);
}

std::unique_ptr<IRHISampler> VulkanRHI::createSampler(const RHISamplerDesc& desc) {
    return std::make_unique<VulkanSampler>(this, desc);
}

std::unique_ptr<IRHIPipeline> VulkanRHI::createGraphicsPipeline(const RHIGraphicsPipelineDesc& desc) {
    auto pipeline = std::make_unique<VulkanPipeline>(this, RHIPipelineType::Graphics);
    // TODO: Implement full pipeline creation
    return pipeline;
}

std::unique_ptr<IRHIPipeline> VulkanRHI::createComputePipeline(const RHIComputePipelineDesc& desc) {
    auto pipeline = std::make_unique<VulkanPipeline>(this, RHIPipelineType::Compute);
    // TODO: Implement full pipeline creation
    return pipeline;
}

std::unique_ptr<IRHIPipeline> VulkanRHI::createRayTracingPipeline(const RHIRayTracingPipelineDesc& desc) {
    auto pipeline = std::make_unique<VulkanPipeline>(this, RHIPipelineType::RayTracing);
    // TODO: Implement full pipeline creation
    return pipeline;
}

std::unique_ptr<IRHIFence> VulkanRHI::createFence(bool signaled) {
    return std::make_unique<VulkanFence>(this, signaled);
}

std::unique_ptr<IRHISemaphore> VulkanRHI::createSemaphore() {
    return std::make_unique<VulkanSemaphore>(this);
}

std::unique_ptr<IRHIQueryPool> VulkanRHI::createQueryPool(QueryType type, uint32_t count) {
    VkQueryType vkType = VK_QUERY_TYPE_TIMESTAMP;
    switch (type) {
        case QueryType::Occlusion: vkType = VK_QUERY_TYPE_OCCLUSION; break;
        case QueryType::Timestamp: vkType = VK_QUERY_TYPE_TIMESTAMP; break;
        case QueryType::PipelineStatistics: vkType = VK_QUERY_TYPE_PIPELINE_STATISTICS; break;
    }
    return std::make_unique<VulkanQueryPool>(this, vkType, count);
}

std::unique_ptr<IRHIAccelerationStructure> VulkanRHI::createAccelerationStructure(bool isTopLevel, uint64_t size) {
    return std::make_unique<VulkanAccelerationStructure>(this, isTopLevel, size);
}

IRHI::AccelerationStructureSizes VulkanRHI::getAccelerationStructureSizes(const RHIAccelerationStructureBuildInfo& info) {
    // TODO: Implement using vkGetAccelerationStructureBuildSizesKHR
    AccelerationStructureSizes sizes{};
    return sizes;
}

std::unique_ptr<IRHICommandList> VulkanRHI::createCommandList(RHIQueueType queue) {
    return std::make_unique<VulkanCommandList>(this, queue);
}

// ============================================================================
// Command Submission
// ============================================================================
void VulkanRHI::submit(IRHICommandList* cmdList, IRHIFence* signalFence) {
    auto vkCmdList = static_cast<VulkanCommandList*>(cmdList);
    VkCommandBuffer cmdBuffer = vkCmdList->getCommandBuffer();
    
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;
    
    VkFence fence = VK_NULL_HANDLE;
    if (signalFence) {
        fence = static_cast<VulkanFence*>(signalFence)->getFence();
    }
    
    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, fence);
}

void VulkanRHI::submitAsync(IRHICommandList* cmdList, RHIQueueType queue, IRHIFence* signalFence) {
    auto vkCmdList = static_cast<VulkanCommandList*>(cmdList);
    VkCommandBuffer cmdBuffer = vkCmdList->getCommandBuffer();
    
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;
    
    VkFence fence = VK_NULL_HANDLE;
    if (signalFence) {
        fence = static_cast<VulkanFence*>(signalFence)->getFence();
    }
    
    vkQueueSubmit(getQueue(queue), 1, &submitInfo, fence);
}

void VulkanRHI::submit(const SubmitInfo& info, RHIQueueType queue) {
    std::vector<VkCommandBuffer> cmdBuffers(info.commandListCount);
    for (uint32_t i = 0; i < info.commandListCount; i++) {
        cmdBuffers[i] = static_cast<VulkanCommandList*>(info.commandLists[i])->getCommandBuffer();
    }
    
    std::vector<VkSemaphore> waitSemaphores(info.waitSemaphoreCount);
    std::vector<VkPipelineStageFlags> waitStages(info.waitSemaphoreCount, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    for (uint32_t i = 0; i < info.waitSemaphoreCount; i++) {
        waitSemaphores[i] = static_cast<VulkanSemaphore*>(info.waitSemaphores[i])->getSemaphore();
    }
    
    std::vector<VkSemaphore> signalSemaphores(info.signalSemaphoreCount);
    for (uint32_t i = 0; i < info.signalSemaphoreCount; i++) {
        signalSemaphores[i] = static_cast<VulkanSemaphore*>(info.signalSemaphores[i])->getSemaphore();
    }
    
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = info.waitSemaphoreCount;
    submitInfo.pWaitSemaphores = waitSemaphores.data();
    submitInfo.pWaitDstStageMask = waitStages.data();
    submitInfo.commandBufferCount = info.commandListCount;
    submitInfo.pCommandBuffers = cmdBuffers.data();
    submitInfo.signalSemaphoreCount = info.signalSemaphoreCount;
    submitInfo.pSignalSemaphores = signalSemaphores.data();
    
    VkFence fence = VK_NULL_HANDLE;
    if (info.signalFence) {
        fence = static_cast<VulkanFence*>(info.signalFence)->getFence();
    }
    
    vkQueueSubmit(getQueue(queue), 1, &submitInfo, fence);
}

// ============================================================================
// Swapchain
// ============================================================================
IRHITexture* VulkanRHI::getBackBuffer() {
    return m_swapchainTextures[m_currentImageIndex].get();
}

void VulkanRHI::present() {
    VkSemaphore signalSemaphores[] = { m_frameResources[m_currentFrame].renderFinishedSemaphore };
    
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_swapchain;
    presentInfo.pImageIndices = &m_currentImageIndex;
    
    VkResult result = vkQueuePresentKHR(m_presentQueue, &presentInfo);
    
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        // Swapchain needs recreation
        if (m_window) {
            recreateSwapchain(m_window->getWidth(), m_window->getHeight());
        }
    }
}

void VulkanRHI::resize(uint32_t width, uint32_t height) {
    recreateSwapchain(width, height);
}

// ============================================================================
// Frame Management
// ============================================================================
void VulkanRHI::beginFrame() {
    auto& frame = m_frameResources[m_currentFrame];
    
    // Wait for this frame's fence
    vkWaitForFences(m_device, 1, &frame.inFlightFence, VK_TRUE, UINT64_MAX);
    
    // Acquire next swapchain image
    VkResult result = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX,
        frame.imageAvailableSemaphore, VK_NULL_HANDLE, &m_currentImageIndex);
    
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        if (m_window) {
            recreateSwapchain(m_window->getWidth(), m_window->getHeight());
        }
        return;
    }
    
    vkResetFences(m_device, 1, &frame.inFlightFence);
    
    // Reset per-frame descriptor pool
    vkResetDescriptorPool(m_device, frame.descriptorPool, 0);
    
    m_frameStarted = true;
}

void VulkanRHI::endFrame() {
    m_frameStarted = false;
    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    m_frameCount++;
}

// ============================================================================
// Synchronization
// ============================================================================
void VulkanRHI::waitIdle() {
    if (m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);
    }
}

void VulkanRHI::waitQueueIdle(RHIQueueType queue) {
    vkQueueWaitIdle(getQueue(queue));
}

// ============================================================================
// Memory Stats
// ============================================================================
RHIMemoryStats VulkanRHI::getMemoryStats() const {
    RHIMemoryStats stats{};
    
    if (m_allocator) {
        VmaTotalStatistics vmaStats;
        vmaCalculateStatistics(m_allocator, &vmaStats);
        
        stats.usedDeviceMemory = vmaStats.total.statistics.allocationBytes;
        stats.totalDeviceMemory = m_capabilities.dedicatedVideoMemory;
        stats.allocationCount = vmaStats.total.statistics.allocationCount;
    }
    
    return stats;
}

// ============================================================================
// Debug
// ============================================================================
void VulkanRHI::setDebugName(IRHIResource* resource, const char* name) {
    // TODO: Implement using VK_EXT_debug_utils
}

void VulkanRHI::beginCapture() {
    // TODO: Integration with RenderDoc
}

void VulkanRHI::endCapture() {
    // TODO: Integration with RenderDoc
}

double VulkanRHI::getTimestampFrequency() const {
    return 1.0e9 / m_capabilities.timestampPeriod;
}

// ============================================================================
// Ray Tracing
// ============================================================================
IRHI::ShaderBindingTableInfo VulkanRHI::getShaderBindingTableInfo() const {
    ShaderBindingTableInfo info{};
    if (m_capabilities.supportsRayTracing) {
        info.handleSize = m_rayTracingProperties.shaderGroupHandleSize;
        info.handleAlignment = m_rayTracingProperties.shaderGroupHandleAlignment;
        info.baseAlignment = m_rayTracingProperties.shaderGroupBaseAlignment;
    }
    return info;
}

bool VulkanRHI::getShaderGroupHandles(IRHIPipeline* pipeline,
                                      uint32_t firstGroup, uint32_t groupCount,
                                      void* data, size_t dataSize) {
    if (!m_capabilities.supportsRayTracing) return false;
    
    auto vkPipeline = static_cast<VulkanPipeline*>(pipeline);
    
    auto func = (PFN_vkGetRayTracingShaderGroupHandlesKHR)vkGetDeviceProcAddr(
        m_device, "vkGetRayTracingShaderGroupHandlesKHR");
    if (!func) return false;
    
    return func(m_device, vkPipeline->getPipeline(), firstGroup, groupCount,
                dataSize, data) == VK_SUCCESS;
}

// ============================================================================
// Single-Time Commands
// ============================================================================
VkCommandBuffer VulkanRHI::beginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_frameResources[m_currentFrame].commandPool;
    allocInfo.commandBufferCount = 1;
    
    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(m_device, &allocInfo, &commandBuffer);
    
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    
    return commandBuffer;
}

void VulkanRHI::endSingleTimeCommands(VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);
    
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    
    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_graphicsQueue);
    
    vkFreeCommandBuffers(m_device, m_frameResources[m_currentFrame].commandPool, 1, &commandBuffer);
}

} // namespace Sanic

#endif // SANIC_ENABLE_VULKAN
