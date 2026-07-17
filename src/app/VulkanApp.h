#pragma once
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <optional>
#include <string>
#include <set>
#include <limits>
#include <algorithm>
#include <stdexcept>
#include <iostream>
#include <fstream>

const int WIDTH  = 800;
const int HEIGHT = 600;
const int MAX_FRAMES_IN_FLIGHT = 2;

const std::vector<const char*> deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;
    std::optional<uint32_t> computeFamily;
    bool isComplete() {
        return graphicsFamily.has_value() && presentFamily.has_value() && computeFamily.has_value();
    }
};

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR        capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR>   presentModes;
};

class VulkanApp {
public:
    VulkanApp();
    virtual ~VulkanApp();

    void run();

protected:
    GLFWwindow*              window           = nullptr;
    VkInstance               instance         = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger   = VK_NULL_HANDLE;
    VkSurfaceKHR             surface          = VK_NULL_HANDLE;
    VkPhysicalDevice         physicalDevice   = VK_NULL_HANDLE;
    VkDevice                 device           = VK_NULL_HANDLE;
    VkQueue                  graphicsQueue    = VK_NULL_HANDLE;
    VkQueue                  presentQueue     = VK_NULL_HANDLE;
    VkSwapchainKHR           swapChain        = VK_NULL_HANDLE;
    VkFormat                 swapChainImageFormat;
    VkExtent2D               swapChainExtent;
    std::vector<VkImage>     swapChainImages;
    std::vector<VkImageView> swapChainImageViews;
    VkCommandPool            commandPool      = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;
    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence>     inFlightFences;
    uint32_t                 currentFrame     = 0;
    bool                     framebufferResized = false;

    void initWindow();
    virtual void initVulkan();
    virtual void mainLoop();
    virtual void cleanup();

    void createInstance();
    void setupDebugMessenger();
    void createSurface();
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createSwapChain();
    void createImageViews();
    void createCommandPool();
    void createCommandBuffers();
    void createSyncObjects();
    void recreateSwapChain();
    void cleanupSwapChain();

    virtual void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) = 0;
    virtual void drawFrame();

    bool                     isDeviceSuitable(VkPhysicalDevice device);
    bool                     checkDeviceExtensionSupport(VkPhysicalDevice device);
    QueueFamilyIndices       findQueueFamilies(VkPhysicalDevice device);
    SwapChainSupportDetails  querySwapChainSupport(VkPhysicalDevice device);
    VkSurfaceFormatKHR       chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR         chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D               chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
    VkShaderModule           createShaderModule(const std::vector<char>& code);
    uint32_t                 findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    void transitionImageLayout(
        VkCommandBuffer cmd,
        VkImage image,
        VkImageLayout oldLayout,
        VkImageLayout newLayout,
        VkPipelineStageFlags srcStage,
        VkPipelineStageFlags dstStage,
        VkAccessFlags srcAccess,
        VkAccessFlags dstAccess);

    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
};