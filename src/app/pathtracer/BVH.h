#pragma once
#include <glm/glm.hpp>
#include <vector>

struct BVHNode {

    glm::vec3 aabbMin;
    int leftChild;

    glm::vec3 aabbMax;
    int rightChild;

    int triangleStart;
    int triangleCount;

    //float padding[2];

}

struct Triangle {

    glm::vec4 v0;
    glm::vec4 v1;
    glm::vec4 v2;
    glm::vec4 normal; 

};

class BVH {

public:

    std::vector<BVHNode> nodes;
    std::vector<Triangle> sortedTriangles;

}