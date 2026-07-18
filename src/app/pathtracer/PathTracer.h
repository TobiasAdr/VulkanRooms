#pragma once
#include "../VulkanApp.h"
#include "BVH.h"
#include "../scene/MeshLoader.h"


struct PushConstants {


    uint32_t frameCount = 0;

};

class PathTracer : public VulkanApp {

public:
    PathTracer();
    ~PathTracer();

protected:

    // BVH
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
    VkDescriptorPool      descriptorPool             = VK_NULL_HANDLE;
    VkDescriptorSet       computeDescriptorSet       = VK_NULL_HANDLE;

    VkPipeline       computePipeline       = VK_NULL_HANDLE;
    VkPipelineLayout computePipelineLayout = VK_NULL_HANDLE;

    VkPushConstantRange       pushConstantRange{};
    VkPipelineShaderStageCreateInfo stageInfo{};
    VkPipelineLayoutCreateInfo      layoutInfo{};
    VkComputePipelineCreateInfo pipelineInfo{};
    VkShaderModule compModule;

    uint32_t frameCount = 0;

    PushConstants pc;

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

    void loadMesh(const std::string& filename);
    void createTriangleBuffer(const std::vector<Triangle>& triangles);
    void createBVHBuffer(const std::vector<BVHNode>& nodes);    

    void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;

};