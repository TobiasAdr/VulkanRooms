#include "PathTracer.h"
#include <fstream>
#include <iostream>
#include <glm/gtc/matrix_transform.hpp>

static std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open())
        throw std::runtime_error("failed to open file!" + filename + "\n");
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
}

PathTracer::PathTracer() {}
PathTracer::~PathTracer() {}

void PathTracer::initVulkan() {
    VulkanApp::initVulkan();

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    createStorageImage();
    createPingPongImage();
    createGBuffers();
    createPreviousImage();
    transitionImageLayoutImmediate(previousImage, 
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    createCameraBuffer();

  //  loadMesh();

    createComputeDescriptors();
    createComputePipeline();

    createAtrousDescriptors();
    createAtrousPipeline();

    initOIDN();
    createOIDNBuffers();
}

void PathTracer::initOIDN() {
    oidnDevice = oidn::newDevice();
    oidnDevice.commit();

    size_t numPixels = swapChainExtent.width * swapChainExtent.height;
    oidnColor.resize(numPixels * 3);
    oidnOutput.resize(numPixels * 3);

    oidnFilter = oidnDevice.newFilter("RT");
    oidnFilter.setImage("color",  oidnColor.data(),  oidn::Format::Float3, swapChainExtent.width, swapChainExtent.height);
    oidnFilter.setImage("output", oidnOutput.data(), oidn::Format::Float3, swapChainExtent.width, swapChainExtent.height);
    oidnFilter.set("hdr", true);
    oidnFilter.commit();
}

void PathTracer::createOIDNBuffers() {
    VkDeviceSize bufferSize = swapChainExtent.width * swapChainExtent.height * 4 * sizeof(float);

    auto createStaging = [&](VkBuffer& buffer, VkDeviceMemory& memory, VkBufferUsageFlags usage) {
        VkBufferCreateInfo bufInfo{};
        bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufInfo.size = bufferSize;
        bufInfo.usage = usage;
        bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(device, &bufInfo, nullptr, &buffer);

        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(device, buffer, &memReq);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, 
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        vkAllocateMemory(device, &allocInfo, nullptr, &memory);
        vkBindBufferMemory(device, buffer, memory, 0);
    };

    createStaging(readbackBuffer, readbackBufferMemory, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    createStaging(uploadBuffer,   uploadBufferMemory,   VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
}

void PathTracer::runOIDNDenoise() {
    vkDeviceWaitIdle(device);

    VkDeviceSize bufferSize = swapChainExtent.width * swapChainExtent.height * 4 * sizeof(float);
    uint32_t width = swapChainExtent.width;
    uint32_t height = swapChainExtent.height;

    VkCommandBufferAllocateInfo cmdAlloc{};
    cmdAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAlloc.commandPool = commandPool;
    cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAlloc.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &cmdAlloc, &cmd);

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    transitionImageLayout(cmd, storageImage, 
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);

    VkBufferImageCopy copyRegion{};
    copyRegion.bufferOffset = 0;
    copyRegion.bufferRowLength = 0;
    copyRegion.bufferImageHeight = 0;
    copyRegion.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copyRegion.imageOffset = {0, 0, 0};
    copyRegion.imageExtent = {width, height, 1};

    vkCmdCopyImageToBuffer(cmd, storageImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readbackBuffer, 1, &copyRegion);

    transitionImageLayout(cmd, storageImage, 
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);
    vkFreeCommandBuffers(device, commandPool, 1, &cmd);

    float* srcData = nullptr;
    vkMapMemory(device, readbackBufferMemory, 0, bufferSize, 0, (void**)&srcData);
    for (size_t i = 0; i < width * height; ++i) {
        oidnColor[i * 3 + 0] = srcData[i * 4 + 0];
        oidnColor[i * 3 + 1] = srcData[i * 4 + 1];
        oidnColor[i * 3 + 2] = srcData[i * 4 + 2];
    }
    vkUnmapMemory(device, readbackBufferMemory);

    oidnFilter.execute();

    const char* errorMessage;
    if (oidnDevice.getError(errorMessage) != oidn::Error::None) {
        std::cerr << "OIDN Error: " << errorMessage << std::endl;
    }

    float* dstData = nullptr;
    vkMapMemory(device, uploadBufferMemory, 0, bufferSize, 0, (void**)&dstData);
    for (size_t i = 0; i < width * height; ++i) {
        dstData[i * 4 + 0] = oidnOutput[i * 3 + 0];
        dstData[i * 4 + 1] = oidnOutput[i * 3 + 1];
        dstData[i * 4 + 2] = oidnOutput[i * 3 + 2];
        dstData[i * 4 + 3] = 1.0f;
    }
    vkUnmapMemory(device, uploadBufferMemory);

    vkAllocateCommandBuffers(device, &cmdAlloc, &cmd);
    vkBeginCommandBuffer(cmd, &beginInfo);

    vkCmdCopyBufferToImage(cmd, uploadBuffer, storageImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

    transitionImageLayout(cmd, storageImage, 
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT);

    vkEndCommandBuffer(cmd);
    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);
    vkFreeCommandBuffers(device, commandPool, 1, &cmd);
}

void PathTracer::transitionImageLayoutImmediate(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool        = commandPool;
    cmdAllocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &cmdAllocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    transitionImageLayout(cmd, image,
        oldLayout, newLayout,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, VK_ACCESS_SHADER_READ_BIT);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &cmd;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);
    vkFreeCommandBuffers(device, commandPool, 1, &cmd);
}

void PathTracer::createStorageImage() {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType     = VK_IMAGE_TYPE_2D;
    imageInfo.format        = VK_FORMAT_R32G32B32A32_SFLOAT;
    imageInfo.extent        = {swapChainExtent.width, swapChainExtent.height, 1};
    imageInfo.mipLevels     = 1;
    imageInfo.arrayLayers   = 1;
    imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage         = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    vkCreateImage(device, &imageInfo, nullptr, &storageImage);

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(device, storageImage, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vkAllocateMemory(device, &allocInfo, nullptr, &storageImageMemory);
    vkBindImageMemory(device, storageImage, storageImageMemory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image            = storageImage;
    viewInfo.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format           = VK_FORMAT_R32G32B32A32_SFLOAT;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    vkCreateImageView(device, &viewInfo, nullptr, &storageImageView);
}

void PathTracer::createPingPongImage() {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType     = VK_IMAGE_TYPE_2D;
    imageInfo.format        = VK_FORMAT_R32G32B32A32_SFLOAT;
    imageInfo.extent        = {swapChainExtent.width, swapChainExtent.height, 1};
    imageInfo.mipLevels     = 1;
    imageInfo.arrayLayers   = 1;
    imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage         = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    vkCreateImage(device, &imageInfo, nullptr, &pingPongImage);

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(device, pingPongImage, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vkAllocateMemory(device, &allocInfo, nullptr, &pingPongImageMemory);
    vkBindImageMemory(device, pingPongImage, pingPongImageMemory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image            = pingPongImage;
    viewInfo.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format           = VK_FORMAT_R32G32B32A32_SFLOAT;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    vkCreateImageView(device, &viewInfo, nullptr, &pingPongImageView);

    transitionImageLayoutImmediate(pingPongImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
}

void PathTracer::createGBuffers() {
    auto createImageHelper = [&](VkImage& img, VkDeviceMemory& mem, VkImageView& view) {
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType     = VK_IMAGE_TYPE_2D;
        info.format        = VK_FORMAT_R32G32B32A32_SFLOAT;
        info.extent        = {swapChainExtent.width, swapChainExtent.height, 1};
        info.mipLevels     = 1;
        info.arrayLayers   = 1;
        info.samples       = VK_SAMPLE_COUNT_1_BIT;
        info.tiling        = VK_IMAGE_TILING_OPTIMAL;
        info.usage         = VK_IMAGE_USAGE_STORAGE_BIT;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        vkCreateImage(device, &info, nullptr, &img);

        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(device, img, &req);

        VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocInfo.allocationSize  = req.size;
        allocInfo.memoryTypeIndex = findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkAllocateMemory(device, &allocInfo, nullptr, &mem);
        vkBindImageMemory(device, img, mem, 0);

        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image            = img;
        viewInfo.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format           = VK_FORMAT_R32G32B32A32_SFLOAT;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCreateImageView(device, &viewInfo, nullptr, &view);

        transitionImageLayoutImmediate(img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    };

    createImageHelper(normalDepthImage, normalDepthImageMemory, normalDepthImageView);
    createImageHelper(albedoImage, albedoImageMemory, albedoImageView);
}

void PathTracer::createCameraBuffer() {
    VkDeviceSize bufferSize = sizeof(CameraUBO);

    VkBufferCreateInfo cBuffer{};
    cBuffer.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    cBuffer.size = bufferSize;
    cBuffer.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    cBuffer.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    vkCreateBuffer(device, &cBuffer, nullptr, &cameraBuffer);

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(device, cameraBuffer, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, 
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    vkAllocateMemory(device, &allocInfo, nullptr, &cameraBufferMemory);
    vkBindBufferMemory(device, cameraBuffer, cameraBufferMemory, 0);
    vkMapMemory(device, cameraBufferMemory, 0, bufferSize, 0, &cameraMapped);
}

void PathTracer::createDescriptorPool() {
    VkDescriptorPoolSize poolSizes[5]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[0].descriptorCount = 3;

    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[1].descriptorCount = 1;

    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[2].descriptorCount = 1;

    poolSizes[3].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[3].descriptorCount = 1;

    poolSizes[4].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[4].descriptorCount = 1;
  
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 5;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = 1;

    vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool     = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts        = &computeDescriptorSetLayout;
    vkAllocateDescriptorSets(device, &allocInfo, &computeDescriptorSet);
}

void PathTracer::createDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding bindings[7]{};
    bindings[0].binding         = 0;
    bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[1].binding         = 1;
    bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[2].binding         = 2;
    bindings[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[3].binding         = 3;
    bindings[3].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[4].binding         = 4;
    bindings[4].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[5].binding         = 5;
    bindings[5].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[6].binding         = 6;
    bindings[6].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[6].descriptorCount = 1;
    bindings[6].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 7;
    layoutInfo.pBindings    = bindings;
    vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &computeDescriptorSetLayout);
}

void PathTracer::createWrites() {
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageView   = storageImageView;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorBufferInfo triBufferInfo{};
    triBufferInfo.buffer = triangleBuffer;
    triBufferInfo.offset = 0;
    triBufferInfo.range  = sizeof(Triangle) * triangleCount;

    VkDescriptorBufferInfo BVHNodeBufferInfo{};
    BVHNodeBufferInfo.buffer = BVHBuffer;
    BVHNodeBufferInfo.offset = 0;
    BVHNodeBufferInfo.range  = sizeof(BVHNode) * BVHNodeCount;

    VkDescriptorBufferInfo cameraUBOInfo{};
    cameraUBOInfo.buffer = cameraBuffer;
    cameraUBOInfo.offset = 0;
    cameraUBOInfo.range  = sizeof(CameraUBO);

    VkDescriptorImageInfo prevImageInfo{};
    prevImageInfo.imageView   = previousImageView;
    prevImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorImageInfo normalDepthImgInfo{};
    normalDepthImgInfo.imageView   = normalDepthImageView;
    normalDepthImgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorImageInfo albedoImgInfo{};
    albedoImgInfo.imageView   = albedoImageView;
    albedoImgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    const int num_writes = 7;
    VkWriteDescriptorSet writes[num_writes]{};
    writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet          = computeDescriptorSet;
    writes[0].dstBinding      = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].pImageInfo      = &imageInfo;

    writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet          = computeDescriptorSet;
    writes[1].dstBinding      = 1;
    writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    writes[1].pBufferInfo     = &triBufferInfo;

    writes[2].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet          = computeDescriptorSet;
    writes[2].dstBinding      = 2;
    writes[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].descriptorCount = 1;
    writes[2].pBufferInfo     = &BVHNodeBufferInfo;

    writes[3].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet          = computeDescriptorSet;
    writes[3].dstBinding      = 3;
    writes[3].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[3].descriptorCount = 1;
    writes[3].pBufferInfo     = &cameraUBOInfo;

    writes[4].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[4].dstSet          = computeDescriptorSet;
    writes[4].dstBinding      = 4;
    writes[4].descriptorCount = 1;
    writes[4].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[4].pImageInfo      = &prevImageInfo;

    writes[5].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[5].dstSet          = computeDescriptorSet;
    writes[5].dstBinding      = 5;
    writes[5].descriptorCount = 1;
    writes[5].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[5].pImageInfo      = &normalDepthImgInfo;

    writes[6].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[6].dstSet          = computeDescriptorSet;
    writes[6].dstBinding      = 6;
    writes[6].descriptorCount = 1;
    writes[6].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[6].pImageInfo      = &albedoImgInfo;

    vkUpdateDescriptorSets(device, num_writes, writes, 0, nullptr);
}

void PathTracer::createAtrousDescriptors() {
    VkDescriptorSetLayoutBinding bindings[4]{};
    bindings[0].binding         = 0;
    bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[1].binding         = 1;
    bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[2].binding         = 2;
    bindings[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[3].binding         = 3;
    bindings[3].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 4;
    layoutInfo.pBindings    = bindings;
    vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &atrousDescriptorSetLayout);

    VkDescriptorPoolSize poolSizes[1]{};
    poolSizes[0].type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[0].descriptorCount = 8;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes    = poolSizes;
    poolInfo.maxSets       = 2;
    vkCreateDescriptorPool(device, &poolInfo, nullptr, &atrousDescriptorPool);

    std::vector<VkDescriptorSetLayout> layouts = {atrousDescriptorSetLayout, atrousDescriptorSetLayout};
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool     = atrousDescriptorPool;
    allocInfo.descriptorSetCount = 2;
    allocInfo.pSetLayouts        = layouts.data();

    VkDescriptorSet sets[2];
    vkAllocateDescriptorSets(device, &allocInfo, sets);
    atrousDescriptorSetPing = sets[0];
    atrousDescriptorSetPong = sets[1];

    VkDescriptorImageInfo storageImgInfo{};
    storageImgInfo.imageView   = storageImageView;
    storageImgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorImageInfo pingPongImgInfo{};
    pingPongImgInfo.imageView   = pingPongImageView;
    pingPongImgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorImageInfo normalDepthImgInfo{};
    normalDepthImgInfo.imageView   = normalDepthImageView;
    normalDepthImgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorImageInfo albedoImgInfo{};
    albedoImgInfo.imageView   = albedoImageView;
    albedoImgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet writesPing[4]{};
    writesPing[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writesPing[0].dstSet          = atrousDescriptorSetPing;
    writesPing[0].dstBinding      = 0;
    writesPing[0].descriptorCount = 1;
    writesPing[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writesPing[0].pImageInfo      = &storageImgInfo;

    writesPing[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writesPing[1].dstSet          = atrousDescriptorSetPing;
    writesPing[1].dstBinding      = 1;
    writesPing[1].descriptorCount = 1;
    writesPing[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writesPing[1].pImageInfo      = &pingPongImgInfo;

    writesPing[2].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writesPing[2].dstSet          = atrousDescriptorSetPing;
    writesPing[2].dstBinding      = 2;
    writesPing[2].descriptorCount = 1;
    writesPing[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writesPing[2].pImageInfo      = &normalDepthImgInfo;

    writesPing[3].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writesPing[3].dstSet          = atrousDescriptorSetPing;
    writesPing[3].dstBinding      = 3;
    writesPing[3].descriptorCount = 1;
    writesPing[3].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writesPing[3].pImageInfo      = &albedoImgInfo;

    vkUpdateDescriptorSets(device, 4, writesPing, 0, nullptr);

    VkWriteDescriptorSet writesPong[4]{};
    writesPong[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writesPong[0].dstSet          = atrousDescriptorSetPong;
    writesPong[0].dstBinding      = 0;
    writesPong[0].descriptorCount = 1;
    writesPong[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writesPong[0].pImageInfo      = &pingPongImgInfo;

    writesPong[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writesPong[1].dstSet          = atrousDescriptorSetPong;
    writesPong[1].dstBinding      = 1;
    writesPong[1].descriptorCount = 1;
    writesPong[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writesPong[1].pImageInfo      = &storageImgInfo;

    writesPong[2].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writesPong[2].dstSet          = atrousDescriptorSetPong;
    writesPong[2].dstBinding      = 2;
    writesPong[2].descriptorCount = 1;
    writesPong[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writesPong[2].pImageInfo      = &normalDepthImgInfo;

    writesPong[3].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writesPong[3].dstSet          = atrousDescriptorSetPong;
    writesPong[3].dstBinding      = 3;
    writesPong[3].descriptorCount = 1;
    writesPong[3].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writesPong[3].pImageInfo      = &albedoImgInfo;

    vkUpdateDescriptorSets(device, 4, writesPong, 0, nullptr);
}

void PathTracer::createAtrousPipeline() {
    atrousPushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    atrousPushConstantRange.offset     = 0;
    atrousPushConstantRange.size       = sizeof(AtrousPushConstants);

    VkPipelineLayoutCreateInfo plInfo{};
    plInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plInfo.setLayoutCount         = 1;
    plInfo.pSetLayouts            = &atrousDescriptorSetLayout;
    plInfo.pushConstantRangeCount = 1;
    plInfo.pPushConstantRanges    = &atrousPushConstantRange;
    vkCreatePipelineLayout(device, &plInfo, nullptr, &atrousPipelineLayout);

    auto compCode = readFile("shaders/atrous.spv");
    VkShaderModule module = createShaderModule(compCode);

    VkPipelineShaderStageCreateInfo sInfo{};
    sInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    sInfo.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    sInfo.module = module;
    sInfo.pName  = "main";

    VkComputePipelineCreateInfo cpInfo{};
    cpInfo.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpInfo.stage  = sInfo;
    cpInfo.layout = atrousPipelineLayout;
    vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpInfo, nullptr, &atrousPipeline);

    vkDestroyShaderModule(device, module, nullptr);
}

void PathTracer::updateCameraBuffer() {
    static Camera prevCam = cam; 

    CameraUBO data;
    data.position = glm::vec4(cam.pos, 0);
    data.forward  = glm::vec4(cam.forward(), 0);
    data.right    = glm::vec4(cam.right(), 0);
    data.up       = glm::vec4(cam.up(), 0);

    data.prevPosition = glm::vec4(prevCam.pos, 0);
    data.prevForward  = glm::vec4(prevCam.forward(), 0);
    data.prevRight    = glm::vec4(prevCam.right(), 0);
    data.prevUp       = glm::vec4(prevCam.up(), 0);

    memcpy(cameraMapped, &data, sizeof(CameraUBO));
    prevCam = cam; 
}

void PathTracer::createComputeDescriptors() {
    createDescriptorSetLayout();
    createDescriptorPool();
    createWrites();
}

void PathTracer::pushConstants() {
    pc.jittering        = 0;
    pc.frameCount       = 0;
    pc.useNEE           = 1;
    pc.useRealLens      = 0;
    pc.useTAA           = 0;
    pc.samples          = 2;
    pc.focalLength      = 5.0f;
    pc.apertureSize     = 0.05f;
    pc.useAtrous        = 0;
    pc.atrousIterations = 2;

    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset     = 0;
    pushConstantRange.size       = sizeof(PushConstants);
}

void PathTracer::createComputePipeline() {
    pushConstants();

    auto compCode = readFile("shaders/comp.spv");
    VkShaderModule compModule = createShaderModule(compCode);

    VkPipelineShaderStageCreateInfo sInfo{};
    sInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    sInfo.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    sInfo.module = compModule;
    sInfo.pName  = "main";

    VkPipelineLayoutCreateInfo lInfo{};
    lInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    lInfo.setLayoutCount         = 1;
    lInfo.pSetLayouts            = &computeDescriptorSetLayout;
    lInfo.pushConstantRangeCount = 1;
    lInfo.pPushConstantRanges    = &pushConstantRange;
    vkCreatePipelineLayout(device, &lInfo, nullptr, &computePipelineLayout);

    VkComputePipelineCreateInfo pInfo{};
    pInfo.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pInfo.stage  = sInfo;
    pInfo.layout = computePipelineLayout;
    vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pInfo, nullptr, &computePipeline);

    vkDestroyShaderModule(device, compModule, nullptr);
}

void PathTracer::loadMesh() {
    MeshLoader baseLoader;
    baseLoader.load("../assets/chair.obj", glm::vec3(0.0f), 0.5f);
    const std::vector<Triangle>& baseChair = baseLoader.triangles;

    std::vector<Triangle> allTriangles;

    auto addChairTransformed = [&](glm::vec3 pos, glm::vec3 rotDegrees) {
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, pos);
        model = glm::rotate(model, glm::radians(rotDegrees.y), glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model, glm::radians(rotDegrees.x), glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model, glm::radians(rotDegrees.z), glm::vec3(0.0f, 0.0f, 1.0f));

        glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(model)));

        for (const auto& tri : baseChair) {
            Triangle t;
            t.v0 = model * glm::vec4(glm::vec3(tri.v0), 1.0f);
            t.v1 = model * glm::vec4(glm::vec3(tri.v1), 1.0f);
            t.v2 = model * glm::vec4(glm::vec3(tri.v2), 1.0f);

            t.normal = glm::vec4(glm::normalize(normalMatrix * glm::vec3(tri.normal)), 0.0f);
            allTriangles.push_back(t);
        }
    };

    glm::vec3 center = glm::vec3(8.0f, 0.0f, 8.0f);

    addChairTransformed(center + glm::vec3(-0.35f, 0.00f, -0.20f), glm::vec3(  0.0f,   20.0f,   0.0f));
    addChairTransformed(center + glm::vec3( 0.40f, 0.00f,  0.15f), glm::vec3(  0.0f, -110.0f,   0.0f));
    addChairTransformed(center + glm::vec3(-0.15f, 0.00f,  0.55f), glm::vec3(  0.0f,  165.0f,   0.0f));

    addChairTransformed(center + glm::vec3( 0.70f, 0.35f, -0.40f), glm::vec3( 78.0f,   45.0f, -25.0f));
    addChairTransformed(center + glm::vec3(-0.65f, 0.20f,  0.60f), glm::vec3( 15.0f, -140.0f,  85.0f));

    addChairTransformed(center + glm::vec3(-0.05f, 0.72f, -0.10f), glm::vec3(-12.0f,   65.0f,   8.0f));
    addChairTransformed(center + glm::vec3( 0.25f, 0.80f,  0.30f), glm::vec3( 35.0f, -170.0f, -30.0f));

    addChairTransformed(center + glm::vec3( 0.05f, 1.28f,  0.10f), glm::vec3(170.0f,   35.0f,  15.0f));
    addChairTransformed(center + glm::vec3(-0.25f, 1.15f,  0.45f), glm::vec3(-45.0f,  110.0f, -65.0f));

    BVH bvh;
    bvh.build(allTriangles);

    createTriangleBuffer(bvh.sortedTriangles);
    createBVHBuffer(bvh.nodes);
}

void PathTracer::createTriangleBuffer(const std::vector<Triangle>& triangles) {
    VkDeviceSize bufferSize = sizeof(Triangle) * triangles.size();
    triangleCount = static_cast<uint32_t>(triangles.size());

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;

    VkBufferCreateInfo stagingInfo{};
    stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingInfo.size = bufferSize;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(device, &stagingInfo, nullptr, &stagingBuffer);

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(device, stagingBuffer, &memReq);

    VkMemoryAllocateInfo stagingAllocInfo{};
    stagingAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    stagingAllocInfo.allocationSize = memReq.size;
    stagingAllocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    vkAllocateMemory(device, &stagingAllocInfo, nullptr, &stagingMemory);
    vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0);

    void* data;
    vkMapMemory(device, stagingMemory, 0, bufferSize, 0, &data);
    memcpy(data, triangles.data(), bufferSize);
    vkUnmapMemory(device, stagingMemory);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    vkCreateBuffer(device, &bufferInfo, nullptr, &triangleBuffer);
    vkGetBufferMemoryRequirements(device, triangleBuffer, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vkAllocateMemory(device, &allocInfo, nullptr, &triangleBufferMemory);
    vkBindBufferMemory(device, triangleBuffer, triangleBufferMemory, 0);

    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandPool = commandPool;
    cmdAllocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &cmdAllocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkBufferCopy copyRegion{};
    copyRegion.size = bufferSize;
    vkCmdCopyBuffer(cmd, stagingBuffer, triangleBuffer, 1, &copyRegion);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);
    vkFreeCommandBuffers(device, commandPool, 1, &cmd);

    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);
}

void PathTracer::createBVHBuffer(const std::vector<BVHNode>& nodes) {
    VkDeviceSize bufferSize = sizeof(BVHNode) * nodes.size();
    BVHNodeCount = static_cast<uint32_t>(nodes.size());

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;

    VkBufferCreateInfo stagingInfo{};
    stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingInfo.size = bufferSize;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(device, &stagingInfo, nullptr, &stagingBuffer);

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(device, stagingBuffer, &memReq);

    VkMemoryAllocateInfo stagingAllocInfo{};
    stagingAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    stagingAllocInfo.allocationSize = memReq.size;
    stagingAllocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    vkAllocateMemory(device, &stagingAllocInfo, nullptr, &stagingMemory);
    vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0);

    void* data;
    vkMapMemory(device, stagingMemory, 0, bufferSize, 0, &data);
    memcpy(data, nodes.data(), bufferSize);
    vkUnmapMemory(device, stagingMemory);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    vkCreateBuffer(device, &bufferInfo, nullptr, &BVHBuffer);
    vkGetBufferMemoryRequirements(device, BVHBuffer, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vkAllocateMemory(device, &allocInfo, nullptr, &BVHBufferMemory);
    vkBindBufferMemory(device, BVHBuffer, BVHBufferMemory, 0);

    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandPool = commandPool;
    cmdAllocInfo.commandBufferCount = 1; 

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &cmdAllocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkBufferCopy copyRegion{};
    copyRegion.size = bufferSize;
    vkCmdCopyBuffer(cmd, stagingBuffer, BVHBuffer, 1, &copyRegion);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);
    vkFreeCommandBuffers(device, commandPool, 1, &cmd);

    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);
}

void PathTracer::recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
    pc.frameCount++;

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkImageLayout oldStorageLayout = firstFrame ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL;
    transitionImageLayout(commandBuffer, storageImage,
        oldStorageLayout, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT);
    firstFrame = false;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
        computePipelineLayout, 0, 1, &computeDescriptorSet, 0, nullptr);
    vkCmdPushConstants(commandBuffer, computePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

    vkCmdDispatch(commandBuffer,
        (swapChainExtent.width  + 7) / 8,
        (swapChainExtent.height + 7) / 8, 1);

    transitionImageLayout(commandBuffer, storageImage,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);

    transitionImageLayout(commandBuffer, previousImage,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, VK_ACCESS_TRANSFER_WRITE_BIT);

    VkImageCopy copyRegion{};
    copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copyRegion.extent         = {swapChainExtent.width, swapChainExtent.height, 1};

    vkCmdCopyImage(commandBuffer,
        storageImage,  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        previousImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &copyRegion);

    transitionImageLayout(commandBuffer, previousImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

    transitionImageLayout(commandBuffer, storageImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT);

    VkImage blitSourceImage = storageImage;

    if (pc.useAtrous == 1) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, atrousPipeline);

        bool ping = true;
        for (int i = 0; i < pc.atrousIterations; ++i) {
            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(commandBuffer,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0, 1, &barrier, 0, nullptr, 0, nullptr);

            VkDescriptorSet currentSet = ping ? atrousDescriptorSetPing : atrousDescriptorSetPong;
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                atrousPipelineLayout, 0, 1, &currentSet, 0, nullptr);

            AtrousPushConstants apc;
            apc.stepSize    = 1 << i;
            apc.phiColor    = 0.08f;
            apc.phiNormal   = 64.0f;
            apc.phiDepth    = 0.02f;
            apc.isFinalPass = (i == pc.atrousIterations - 1) ? 1 : 0;

            vkCmdPushConstants(commandBuffer, atrousPipelineLayout,
                VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(apc), &apc);

            vkCmdDispatch(commandBuffer,
                (swapChainExtent.width  + 7) / 8,
                (swapChainExtent.height + 7) / 8, 1);

            ping = !ping;
        }

        blitSourceImage = ping ? storageImage : pingPongImage;

        VkMemoryBarrier finalBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        finalBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        finalBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 1, &finalBarrier, 0, nullptr, 0, nullptr);
    }

    transitionImageLayout(commandBuffer, blitSourceImage,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);

    transitionImageLayout(commandBuffer, swapChainImages[imageIndex],
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, VK_ACCESS_TRANSFER_WRITE_BIT);

    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[1]  = {(int32_t)swapChainExtent.width, (int32_t)swapChainExtent.height, 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.dstOffsets[1]  = {(int32_t)swapChainExtent.width, (int32_t)swapChainExtent.height, 1};
    vkCmdBlitImage(commandBuffer,
        blitSourceImage,             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        swapChainImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &blit, VK_FILTER_NEAREST);

    transitionImageLayout(commandBuffer, blitSourceImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT);

    VkRenderPassBeginInfo rpBegin{};
    rpBegin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderPass        = imGuiRenderPass;
    rpBegin.framebuffer       = imGuiFramebuffers[imageIndex];
    rpBegin.renderArea.offset = {0, 0};
    rpBegin.renderArea.extent = swapChainExtent;
    rpBegin.clearValueCount   = 0;

    vkCmdBeginRenderPass(commandBuffer, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
    vkCmdEndRenderPass(commandBuffer);

    vkEndCommandBuffer(commandBuffer);
}

void PathTracer::cleanup() {
    vkDeviceWaitIdle(device);

    vkDestroyBuffer(device, readbackBuffer, nullptr);
    vkFreeMemory(device, readbackBufferMemory, nullptr);
    vkDestroyBuffer(device, uploadBuffer, nullptr);
    vkFreeMemory(device, uploadBufferMemory, nullptr);

    vkDestroyBuffer(device, triangleBuffer, nullptr);
    vkFreeMemory(device, triangleBufferMemory, nullptr);
    vkDestroyBuffer(device, BVHBuffer, nullptr);
    vkFreeMemory(device, BVHBufferMemory, nullptr);
    vkDestroyBuffer(device, cameraBuffer, nullptr);
    vkFreeMemory(device, cameraBufferMemory, nullptr);

    vkDestroyPipeline(device, atrousPipeline, nullptr);
    vkDestroyPipelineLayout(device, atrousPipelineLayout, nullptr);
    vkDestroyDescriptorPool(device, atrousDescriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(device, atrousDescriptorSetLayout, nullptr);

    vkDestroyPipeline(device, computePipeline, nullptr);
    vkDestroyPipelineLayout(device, computePipelineLayout, nullptr);
    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(device, computeDescriptorSetLayout, nullptr);

    vkDestroyImageView(device, normalDepthImageView, nullptr);
    vkDestroyImage(device, normalDepthImage, nullptr);
    vkFreeMemory(device, normalDepthImageMemory, nullptr);

    vkDestroyImageView(device, albedoImageView, nullptr);
    vkDestroyImage(device, albedoImage, nullptr);
    vkFreeMemory(device, albedoImageMemory, nullptr);

    vkDestroyImageView(device, pingPongImageView, nullptr);
    vkDestroyImage(device, pingPongImage, nullptr);
    vkFreeMemory(device, pingPongImageMemory, nullptr);

    vkDestroyImageView(device, storageImageView, nullptr);
    vkDestroyImage(device, storageImage, nullptr);
    vkFreeMemory(device, storageImageMemory, nullptr);

    vkDestroyImageView(device, previousImageView, nullptr);
    vkDestroyImage(device, previousImage, nullptr);
    vkFreeMemory(device, previousImageMemory, nullptr);

    VulkanApp::cleanup();
}

void PathTracer::moveCamera() {
    static bool cursorLocked = true;
    static bool escPressedLastFrame = false;

    bool escPressed = (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS);
    if (escPressed && !escPressedLastFrame) {
        cursorLocked = !cursorLocked;
        glfwSetInputMode(window, GLFW_CURSOR, cursorLocked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        firstMouseMove = true;
    }
    escPressedLastFrame = escPressed;

    bool moved = false;
    float moveSpeed = 0.05f;
    float mouseSensitivity = 0.1f;

    if (cursorLocked) {
        double mouseX, mouseY;
        glfwGetCursorPos(window, &mouseX, &mouseY);

        if (firstMouseMove) {
            lastMouseX = mouseX;
            lastMouseY = mouseY;
            firstMouseMove = false;
        }

        float xOffset = static_cast<float>(mouseX - lastMouseX);
        float yOffset = static_cast<float>(lastMouseY - mouseY);

        lastMouseX = mouseX;
        lastMouseY = mouseY;

        if (std::abs(xOffset) > 0.0001f || std::abs(yOffset) > 0.0001f) {
            cam.yaw   -= xOffset * mouseSensitivity;
            cam.pitch += yOffset * mouseSensitivity;

            if (cam.pitch > 89.0f)  cam.pitch = 89.0f;
            if (cam.pitch < -89.0f) cam.pitch = -89.0f;

            moved = true;
        }
    }

    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantCaptureKeyboard) {
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) { cam.pos += cam.forward() * moveSpeed; moved = true; }
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) { cam.pos -= cam.forward() * moveSpeed; moved = true; }
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) { cam.pos -= cam.right()   * moveSpeed; moved = true; }
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) { cam.pos += cam.right()   * moveSpeed; moved = true; }
    }

    if (!pc.useTAA && moved) {
        pc.frameCount = 0;
    }
}

void PathTracer::createPreviousImage() {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType     = VK_IMAGE_TYPE_2D;
    imageInfo.format        = VK_FORMAT_R32G32B32A32_SFLOAT;
    imageInfo.extent        = {swapChainExtent.width, swapChainExtent.height, 1};
    imageInfo.mipLevels     = 1;
    imageInfo.arrayLayers   = 1;
    imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage         = VK_IMAGE_USAGE_STORAGE_BIT 
                            | VK_IMAGE_USAGE_TRANSFER_SRC_BIT 
                            | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    vkCreateImage(device, &imageInfo, nullptr, &previousImage);

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(device, previousImage, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vkAllocateMemory(device, &allocInfo, nullptr, &previousImageMemory);
    vkBindImageMemory(device, previousImage, previousImageMemory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image            = previousImage;
    viewInfo.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format           = VK_FORMAT_R32G32B32A32_SFLOAT;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    vkCreateImageView(device, &viewInfo, nullptr, &previousImageView);
}

void PathTracer::drawFrame() {
    moveCamera();
    updateCameraBuffer();

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin("Settings");

    ImGuiIO& io = ImGui::GetIO();
    ImGui::Text("FPS: %.1f (%.2f ms/frame)", io.Framerate, 1000.0f / io.Framerate);
    ImGui::Separator();

    ImGui::Checkbox("Use OIDN (Heavy)", &useOIDN);
    if (ImGui::Button("Run OIDN Single Shot")) {
        runOIDNDenoise();
    }

    bool jittering = pc.jittering;
    if (ImGui::Checkbox("Jittering", &jittering)) {
        pc.jittering = jittering ? 1 : 0;
        pc.frameCount = 0;
    }

    bool nee = pc.useNEE;
    if (ImGui::Checkbox("NEE", &nee)) {
        pc.useNEE = nee ? 1 : 0;
        pc.frameCount = 0;
    }

    bool realLens = pc.useRealLens;
    if (ImGui::Checkbox("Real Lens", &realLens)) {
        pc.useRealLens = realLens ? 1 : 0;
        pc.frameCount = 0;
    }

    bool taa = pc.useTAA;
    if (ImGui::Checkbox("TAA", &taa)) {
        pc.useTAA = taa ? 1 : 0;
        pc.frameCount = 0;
    }

    bool atrous = pc.useAtrous;
    if (ImGui::Checkbox("Use A-trous Multi-Pass", &atrous)) {
        pc.useAtrous = atrous ? 1 : 0;
        pc.frameCount = 0;
    }

    if (pc.useAtrous) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::SliderInt("Iterations", &pc.atrousIterations, 1, 5)) {
            pc.frameCount = 0;
        }
    }

    int samples = static_cast<int>(pc.samples);
    if (ImGui::SliderInt("Samples", &samples, 1, 16)) {
        pc.samples = static_cast<uint32_t>(samples);
        pc.frameCount = 0;
    }

    if (ImGui::SliderFloat("Focal Length",  &pc.focalLength,  0.1f, 20.0f)) pc.frameCount = 0;
    if (ImGui::SliderFloat("Aperture Size", &pc.apertureSize, 0.0f, 0.5f))  pc.frameCount = 0;

    ImGui::End();
    ImGui::Render();

    VulkanApp::drawFrame();

    if (useOIDN) {
        runOIDNDenoise();
    }
}