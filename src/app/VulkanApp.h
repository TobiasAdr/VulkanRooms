#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <cstdint> 
#include <limits> 
#include <algorithm> 
#include <optional>
#include <set>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <cstring>
#include <fstream>



struct QueueFamilyIndices{

    // Value set to 0: hasValue = True
    // Value not set, hasValue = False
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;
    std::optional<uint32_t> computeFamily;

    // does a queue family exist that supports graphics on the physical device? 
    bool isComplete() {

        return graphicsFamily.has_value() && presentFamily.has_value();
    
    };

};

struct SwapChainSupportDetails {

    VkSurfaceCapabilitiesKHR capabilities; // Min, max width/height/number of swapchain images etc
    std::vector<VkSurfaceFormatKHR> formats; // Pixel format, surface format
    std::vector<VkPresentModeKHR> presentModes; // Available presentation modes

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
    void cleanup();

    void pickPhysicalDevice();
    void createLogicalDevice();
    void createInstance();
    void createSurface();
    void createSwapChain();
    void createImageViews();
    void createRenderPass();
    void createGraphicsPipeline();
    void createFramebuffers();
    void createCommandPool();
    void createCommandBuffers();
    void createSyncObjects();



    // Ray - tracing
    void createStorageImage();
    void createComputeDescriptors();
    void createComputePipeline();



    void recreateSwapChain();
    void cleanupSwapChain();

    void drawFrame();

    void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);

    void setupDebugMessenger();

    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);


    void transitionImageLayout(
    VkCommandBuffer cmd,
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkPipelineStageFlags srcStage,
    VkPipelineStageFlags dstStage,
    VkAccessFlags srcAccess,
    VkAccessFlags dstAccess);

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
private:

    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
    bool isDeviceSuitable(VkPhysicalDevice device);
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);


private:

    // Ray Tracing
    VkImage storageImage;
    VkDeviceMemory storageImageMemory;
    VkImageView storageImageView;

    VkDescriptorSetLayout computeDescriptorSetLayout;
    VkDescriptorPool      descriptorPool;
    VkDescriptorSet       computeDescriptorSet;


    VkPipeline computePipeline;
    VkPipelineLayout computePipelineLayout;

    GLFWwindow* window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;

    // Debug messenger (krävs för validation layers)
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;

    // Surface that will be used to present images to the window
    VkSurfaceKHR surface;

    // Physical device (GPU) that will be used to run the Vulkan application
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;

    // Logical device (GPU) that will be used to run the Vulkan application
    VkDevice device = VK_NULL_HANDLE;

    // Queue that will be used to submit commands to the GPU
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;

    // Swap-chain
    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);

    VkSwapchainKHR swapChain;
    std::vector<VkImage> swapChainImages;
    VkFormat swapChainImageFormat;
    VkExtent2D swapChainExtent;

    // Image View, Images
    std::vector<VkImageView> swapChainImageViews;

    // Shaders
    VkShaderModule createShaderModule(const std::vector<char>& code);

    // Render pass 
    VkRenderPass renderPass;

    // Pipeline
    VkPipelineLayout pipelineLayout;

    // Graphics pipeline
    VkPipeline graphicsPipeline;

    // Frame buffers
    std::vector<VkFramebuffer> swapChainFramebuffers;

    // Command Pool
    VkCommandPool commandPool;

    // Command buffers
    std::vector<VkCommandBuffer> commandBuffers;

    const int MAX_FRAMES_IN_FLIGHT = 2;
    uint32_t currentFrame = 0;  

    // Synchronization
    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;

    bool framebufferResized = false;

    // device extensions.
    const std::vector<const char*> deviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };

    // Window size
    static constexpr int WIDTH = 800;
    static constexpr int HEIGHT = 600;

};