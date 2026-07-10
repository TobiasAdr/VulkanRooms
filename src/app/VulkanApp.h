#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vector>
#include <optional>
#include <set>


struct QueueFamilyIndices{

    // Value set to 0: hasValue = True
    // Value not set, hasValue = False
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    // does a queue family exist that supports graphics on the physical device? 
    bool isComplete() {

        return graphicsFamily.has_value() && presentFamily.has_value();
    
    };

};

class VulkanApp {
public:
    void run();

    VulkanApp();
    ~VulkanApp();

private:
    void initWindow();
    void initVulkan();
    void mainLoop();
    void cleanUp();

    void pickPhysicalDevice();
    void createLogicalDevice();
    void createInstance();
    void createSurface();
    void setupDebugMessenger();

private:

    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
    bool isDeviceSuitable(VkPhysicalDevice device);
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);


private:
    GLFWwindow* window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;

    // Debug messenger (krävs för validation layers)
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;

    // Surface that will be used to present images to the window
    VkSurfaceKHR surface;

    // Physical device (GPU) that will be used to run the Vulkan application
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;

    // Queue that will be used to submit commands to the GPU
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;

    // Logical device (GPU) that will be used to run the Vulkan application
    VkDevice device = VK_NULL_HANDLE;

    // device extensions.
    const std::vector<const char*> deviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };

    // Window size
    static constexpr int WIDTH = 800;
    static constexpr int HEIGHT = 600;

};