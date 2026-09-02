#pragma once
#include "../VulkanApp.h"
#include "BVH.h"
#include "../scene/MeshLoader.h"
#include "imgui_impl_vulkan.h"
#include "imgui_impl_glfw.h"
#include <OpenImageDenoise/oidn.hpp>
#include <vector>

struct PushConstants {
    uint32_t jittering = 0;
    uint32_t frameCount = 0;
    uint32_t useNEE;
    uint32_t useRealLens;
    uint32_t useTAA;
    uint32_t samples;
    float focalLength;
    float apertureSize;
};

struct CameraUBO {
    glm::vec4 position;
    glm::vec4 forward;
    glm::vec4 right;
    glm::vec4 up;
    
    glm::vec4 prevPosition;
    glm::vec4 prevForward;
    glm::vec4 prevRight;
    glm::vec4 prevUp;
};

struct Camera {
    glm::vec3 pos   = {2.0f, 1.2f, 2.0f};
    float yaw       = 0.f;
    float pitch     = 0.f;

    glm::vec3 forward() const {
        return glm::normalize(glm::vec3(
            cos(glm::radians(pitch)) * sin(glm::radians(yaw)),
            sin(glm::radians(pitch)),
            cos(glm::radians(pitch)) * cos(glm::radians(yaw))
        ));
    }
    glm::vec3 right() const { return glm::normalize(glm::cross(forward(), {0,1,0})); }
    glm::vec3 up()    const { return glm::cross(right(), forward()); }

    CameraUBO toUBO() const {
        return { glm::vec4(pos,0), glm::vec4(forward(),0), glm::vec4(right(),0), glm::vec4(up(),0) };
    }
};

class PathTracer : public VulkanApp {
public:
    PathTracer();
    ~PathTracer();

protected:
    bool firstFrame = true;

    double lastMouseX = 0.0;
    double lastMouseY = 0.0;
    bool firstMouseMove = true;

    Camera cam;
    VkBuffer cameraBuffer = VK_NULL_HANDLE;
    VkDeviceMemory cameraBufferMemory = VK_NULL_HANDLE;
    void* cameraMapped = nullptr;
    void updateCameraBuffer();
    void moveCamera();

    VkBuffer BVHBuffer = VK_NULL_HANDLE;
    VkDeviceMemory BVHBufferMemory = VK_NULL_HANDLE;
    uint32_t BVHNodeCount = 0;

    VkBuffer triangleBuffer = VK_NULL_HANDLE;
    VkDeviceMemory triangleBufferMemory = VK_NULL_HANDLE;
    uint32_t triangleCount = 0;
    
    VkImage        storageImage       = VK_NULL_HANDLE;
    VkDeviceMemory storageImageMemory = VK_NULL_HANDLE;
    VkImageView    storageImageView   = VK_NULL_HANDLE;

    VkDescriptorSetLayout computeDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool       descriptorPool             = VK_NULL_HANDLE;
    VkDescriptorSet        computeDescriptorSet       = VK_NULL_HANDLE;

    VkPipeline       computePipeline       = VK_NULL_HANDLE;
    VkPipelineLayout computePipelineLayout = VK_NULL_HANDLE;

    VkPushConstantRange             pushConstantRange{};
    VkPipelineShaderStageCreateInfo stageInfo{};
    VkPipelineLayoutCreateInfo      layoutInfo{};
    VkComputePipelineCreateInfo     pipelineInfo{};
    VkShaderModule compModule;

    VkImage previousImage;
    VkDeviceMemory previousImageMemory;
    VkImageView previousImageView;

    uint32_t frameCount = 0;
    PushConstants pc;

    oidn::DeviceRef oidnDevice;
    oidn::FilterRef oidnFilter;
    bool useOIDN = false;

    std::vector<float> oidnColor;
    std::vector<float> oidnOutput;

    VkBuffer readbackBuffer = VK_NULL_HANDLE;
    VkDeviceMemory readbackBufferMemory = VK_NULL_HANDLE;

    VkBuffer uploadBuffer = VK_NULL_HANDLE;
    VkDeviceMemory uploadBufferMemory = VK_NULL_HANDLE;

    void initOIDN();
    void createOIDNBuffers();
    void runOIDNDenoise();

    void initVulkan() override;
    void cleanup()    override;

    void createStorageImage();
    void createComputeDescriptors();
    void createDescriptorSetLayout();
    void createDescriptorPool();
    void createWrites();    
    
    void pushConstants();
    void createShaderInfo();
    void createLayoutInfo();
    void createPipelineInfo();
    void createComputePipeline();
    
    void createPreviousImage();
    void transitionImageLayoutImmediate(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);

    void loadMesh(const std::string& filename);
    void createTriangleBuffer(const std::vector<Triangle>& triangles);
    void createCameraBuffer();
    void createBVHBuffer(const std::vector<BVHNode>& nodes);    

    void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;
    void drawFrame() override;
};