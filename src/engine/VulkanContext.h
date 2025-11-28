#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <optional>
#include "Window.h"
#include <iostream>
#include <set>
#include <string>
#include <stdexcept>

struct QueueFamilyIndices {
    int32_t graphicsFamily = -1;
    int32_t presentFamily = -1;
    bool isComplete() { return graphicsFamily != -1 && presentFamily != -1; }
};

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

class VulkanContext {
public:
    VulkanContext(Window& window);
    ~VulkanContext();

    VkInstance getInstance() const { return instance; }
    VkPhysicalDevice getPhysicalDevice() const { return physicalDevice; }
    VkDevice getDevice() const { return device; }
    VkSurfaceKHR getSurface() const { return surface; }
    VkQueue getGraphicsQueue() const { return graphicsQueue; }
    VkQueue getPresentQueue() const { return presentQueue; }

    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);
    VkFormat findDepthFormat();
    
    bool isRayTracingSupported() const { return rayTracingSupported; }

private:
    void createInstance();
    void createSurface();
    void pickPhysicalDevice();
    void createLogicalDevice();
    
    bool isDeviceSuitable(VkPhysicalDevice device);
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);
    bool checkRayTracingSupport(VkPhysicalDevice device);

    Window& window;
    VkInstance instance;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device;
    VkSurfaceKHR surface;
    VkQueue graphicsQueue;
    VkQueue presentQueue;
    
    bool rayTracingSupported = false;
};
