#pragma once
#include "../VulkanApp.h"


struct PushConstants {


    uint32_t frameCount = 0;

};

class PathTracer : public VulkanApp {
public:
    PathTracer();
    ~PathTracer();

protected:
    VkImage        storageImage       = VK_NULL_HANDLE;
    VkDeviceMemory storageImageMemory = VK_NULL_HANDLE;
    VkImageView    storageImageView   = VK_NULL_HANDLE;

    VkDescriptorSetLayout computeDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool      descriptorPool             = VK_NULL_HANDLE;
    VkDescriptorSet       computeDescriptorSet       = VK_NULL_HANDLE;

    VkPipeline       computePipeline       = VK_NULL_HANDLE;
    VkPipelineLayout computePipelineLayout = VK_NULL_HANDLE;

    uint32_t frameCount = 0;

    PushConstants pc;

    void initVulkan() override;
    void cleanup()    override;

    void createStorageImage();
    void createComputeDescriptors();
    void createComputePipeline();

    void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;
};