#pragma once
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

struct CameraUBO {

    glm::vec4 position;
    glm::vec4 forward;
    glm::vec4 right;
    glm::vec4 up;
    float fov;
    float aspectRatio;
    float focalDistance;
    float aperture;

}

struct PushConstants {


    uint32_t frameCount;


}


class PathTracer{


    
}