#include "PathTracer.h"
#include <fstream>

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
    createStorageImage();
    createPreviousImage();
    transitionImageLayoutImmediate(previousImage, 
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    loadMesh("../assets/bunny.obj");
    createCameraBuffer();
    createComputeDescriptors();
    createComputePipeline();

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
    imageInfo.usage         = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
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

void PathTracer::createCameraBuffer(){

    VkDeviceSize bufferSize = sizeof(CameraUBO);

    VkBufferCreateInfo cBuffer{};
    cBuffer.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    cBuffer.size = bufferSize;
    cBuffer.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    cBuffer.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if(vkCreateBuffer(device, &cBuffer, nullptr, &cameraBuffer) != VK_SUCCESS){

        throw std::runtime_error("Failed to create Camera Buffer");

    }

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(device, cameraBuffer, &memReq);

    VkMemoryAllocateInfo allocInfo{};

    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;

    VkPhysicalDeviceMemoryProperties memProp;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProp);

    uint32_t memTypeIndex = 0;
        VkMemoryPropertyFlags props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        for (uint32_t j = 0; j < memProp.memoryTypeCount; j++) {
            if ((memReq.memoryTypeBits & (1 << j)) &&
                (memProp.memoryTypes[j].propertyFlags & props) == props) {
                memTypeIndex = j;
                break;
            }
        }
        
    allocInfo.memoryTypeIndex = memTypeIndex;

    if(vkAllocateMemory(device, &allocInfo, nullptr, &cameraBufferMemory) != VK_SUCCESS)
        throw std::runtime_error("Kunde inte allokera kamera buffer minne");


    vkBindBufferMemory(device, cameraBuffer, cameraBufferMemory, 0);
    vkMapMemory(device, cameraBufferMemory, 0, bufferSize, 0, &cameraMapped);

}


void PathTracer::createDescriptorPool(){

    VkDescriptorPoolSize poolSizes[5]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[0].descriptorCount = 1;

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
void PathTracer::createDescriptorSetLayout(){

    VkDescriptorSetLayoutBinding bindings[5]{};
    bindings[0].binding         = 0;
    bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags= VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[4].binding         = 4;
    bindings[4].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 5;
    layoutInfo.pBindings    = bindings;
    vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &computeDescriptorSetLayout);

}

void PathTracer::createWrites(){

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageView   = storageImageView;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorBufferInfo triBufferInfo{};
    triBufferInfo.buffer = triangleBuffer;
    triBufferInfo.offset = 0;
    triBufferInfo.range = sizeof(Triangle) * triangleCount;

    VkDescriptorBufferInfo BVHNodeBufferInfo{};
    BVHNodeBufferInfo.buffer = BVHBuffer;
    BVHNodeBufferInfo.offset = 0;
    BVHNodeBufferInfo.range = sizeof(BVHNode) * BVHNodeCount;

    VkDescriptorBufferInfo cameraUBOInfo{};
    cameraUBOInfo.buffer = cameraBuffer;
    cameraUBOInfo.offset = 0;
    cameraUBOInfo.range = sizeof(CameraUBO);

    VkDescriptorImageInfo prevImageInfo{};
    prevImageInfo.imageView   = previousImageView;
    prevImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;


    const int num_writes = 5;

    VkWriteDescriptorSet writes[num_writes]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = computeDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].pImageInfo = &imageInfo;

    // Triangles
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = computeDescriptorSet;
    writes[1].dstBinding = 1;
    writes[1].dstArrayElement = 0;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    writes[1].pBufferInfo = &triBufferInfo;

    // BVH nodes
    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = computeDescriptorSet;
    writes[2].dstBinding = 2;
    writes[2].dstArrayElement = 0;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].descriptorCount = 1;
    writes[2].pBufferInfo = &BVHNodeBufferInfo;

    // Camera UBO
    writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet = computeDescriptorSet;
    writes[3].dstBinding = 3;
    writes[3].dstArrayElement = 0;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[3].descriptorCount = 1;
    writes[3].pBufferInfo = &cameraUBOInfo;

    writes[4].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[4].dstSet          = computeDescriptorSet;
    writes[4].dstBinding      = 4;
    writes[4].descriptorCount = 1;
    writes[4].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[4].pImageInfo      = &prevImageInfo;

    vkUpdateDescriptorSets(device, num_writes, writes, 0, nullptr);

}

void PathTracer::updateCameraBuffer() {
    CameraUBO data = cam.toUBO();
    memcpy(cameraMapped, &data, sizeof(CameraUBO));
}


void PathTracer::createComputeDescriptors() {

    createDescriptorSetLayout();
    createDescriptorPool();
    createWrites();

}

void PathTracer::pushConstants() {

    pc.frameCount   = 0;
    pc.useNEE       = 1;
    pc.useRealLens  = 0;
    pc.useTAA = 0;
    pc.samples = 2;
    pc.focalLength  = 5.0f;
    pc.apertureSize = 0.05f;
    

    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset     = 0;
    pushConstantRange.size       = sizeof(PushConstants);

}

void PathTracer::createShaderInfo(){

    auto compCode = readFile("shaders/comp.spv");
    compModule = createShaderModule(compCode);

    stageInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = compModule;
    stageInfo.pName  = "main";

}

void PathTracer::createLayoutInfo(){

    layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount         = 1;
    layoutInfo.pSetLayouts            = &computeDescriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges    = &pushConstantRange;
    vkCreatePipelineLayout(device, &layoutInfo, nullptr, &computePipelineLayout);

}
void PathTracer::createPipelineInfo(){

    pipelineInfo.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage  = stageInfo;
    pipelineInfo.layout = computePipelineLayout;
    vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &computePipeline);

}

void PathTracer::loadMesh(const std::string& filename) {

    MeshLoader loader;
    
    loader.load("../assets/chair.obj", glm::vec3(2.0f, 0.f, 0.5f), .5f);
    loader.load("../assets/chair.obj", glm::vec3(4.0f, 0.f, 4.f), .5f);
    loader.load("../assets/chair.obj", glm::vec3(7.0f, 0.f, 1.f), .5f);
    loader.load("../assets/chair.obj", glm::vec3(7.0f, 0.f, 8.f), .5f);
    loader.load("../assets/chair.obj", glm::vec3(9.0f, 0.f, 3.f), .5f);

    loader.load("../assets/smalltable.obj", glm::vec3(4.0f, 0.f, 3.f), .5f);
    loader.load("../assets/smalltable.obj", glm::vec3(7.0f, 0.f, 4.f), .5f);
    
    loader.load("../assets/bunny.obj", glm::vec3(4.f, 0.3f, 3.f), 5.0f);

    BVH bvh;
    bvh.build(loader.triangles);

    createTriangleBuffer(bvh.sortedTriangles);
    createBVHBuffer(bvh.nodes);

}

void PathTracer::createTriangleBuffer(const std::vector<Triangle>& triangles) {

    VkDeviceSize bufferSize = sizeof(Triangle) * triangles.size();
    triangleCount = static_cast<uint32_t>(triangles.size());

    // Staging buffer
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

    // GPU buffer
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
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vkAllocateMemory(device, &allocInfo, nullptr, &triangleBufferMemory);
    vkBindBufferMemory(device, triangleBuffer, triangleBufferMemory, 0);

    // Kopiera via command buffer
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

void PathTracer::createBVHBuffer(const std::vector<BVHNode>& nodes){

    VkDeviceSize bufferSize = sizeof(BVHNode) * nodes.size();
    BVHNodeCount = static_cast<uint32_t>(nodes.size());

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;

    // ------------------- STAGING BUFFER -------------------

    // Staging buffer lets the GPU memory read from host_visible to device local. 

    // This lets the entirety of the buffer be on the gpu for reading. 

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

    // ----------------- STAGING BUFFER END -------------------------

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
    
    // We record a one time command to send the BVH nodes to the GPU

    // This is done one time on init. 

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

    // Graphics queue is used here to transfer the triangles to GPU

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);
    vkFreeCommandBuffers(device, commandPool, 1, &cmd);

    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);

    std::cout << "BVH created with " << BVHNodeCount << " nodes!\n";

}

void PathTracer::createComputePipeline() {

    pushConstants();
    createShaderInfo();
    createLayoutInfo();
    createPipelineInfo();

    vkDestroyShaderModule(device, compModule, nullptr);

}

void PathTracer::recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) {

    pc.frameCount++;

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    transitionImageLayout(commandBuffer, storageImage,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, VK_ACCESS_SHADER_WRITE_BIT);

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
        storageImage,                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        swapChainImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &blit, VK_FILTER_NEAREST);

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

    vkDestroyBuffer(device, triangleBuffer, nullptr);
    vkFreeMemory(device, triangleBufferMemory, nullptr);
    vkDestroyBuffer(device, BVHBuffer, nullptr);
    vkFreeMemory(device, BVHBufferMemory, nullptr);
    vkDestroyBuffer(device, cameraBuffer, nullptr);
    vkFreeMemory(device, cameraBufferMemory, nullptr);

    vkDestroyPipeline(device, computePipeline, nullptr);
    vkDestroyPipelineLayout(device, computePipelineLayout, nullptr);
    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(device, computeDescriptorSetLayout, nullptr);
    vkDestroyImageView(device, storageImageView, nullptr);
    vkDestroyImage(device, storageImage, nullptr);
    vkFreeMemory(device, storageImageMemory, nullptr);

    // TAA, previous
    vkDestroyImageView(device, previousImageView, nullptr);
    vkDestroyImage(device, previousImage, nullptr);
    vkFreeMemory(device, previousImageMemory, nullptr);

    VulkanApp::cleanup();
}

void PathTracer::moveCamera(){

    bool moved = false;
    float speed = 0.05f;

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) { cam.pos += cam.forward() * speed; moved = true; }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) { cam.pos -= cam.forward() * speed; moved = true; }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) { cam.pos -= cam.right()   * speed; moved = true; }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) { cam.pos += cam.right()   * speed; moved = true; }

    if (moved) pc.frameCount = 0;

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
                            | VK_IMAGE_USAGE_TRANSFER_DST_BIT; // behövs för copy
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
}