#pragma once
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

struct Triangle {
    glm::vec4 v0;
    glm::vec4 v1;
    glm::vec4 v2;
    glm::vec4 normal; 
};

class MeshLoader {
public:
    std::vector<Triangle> triangles;

    void load(const std::string& filename, glm::vec3 offset, float scale);
};