#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <iostream>
#include <stdexcept>
#include <vector>

const uint32_t WIDTH = 800;
const uint32_t HEIGHT = 600;

class VulkanApp
{
public:
    VulkanApp();
    ~VulkanApp();
    void run();

private:
    VkInstance instance;

    void createInstance();
    void initWindow();
    void initVulkan();
    void mainLoop();
    void cleanUp();

private:
    GLFWwindow* window = nullptr;
};