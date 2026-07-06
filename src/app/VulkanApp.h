#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vector>

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

    void createInstance();
    void initDebugMessenger();

private:
    GLFWwindow* window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;

    // Debug messenger (krävs för validation layers)
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;

    // Window size
    static constexpr int WIDTH = 800;
    static constexpr int HEIGHT = 600;
};