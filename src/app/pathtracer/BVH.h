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

    void build(const std::vector<Triangle>& triangles);

private:

    int buildRecursive(std::vector<Triangle>& tris, int start, int count);

    glm::vec3 computeAABBMin(const std::vector<Triangle>& tris, int start, int count);
    glm::vec3 computeAABBMax(const std::vector<Triangle>& tris, int start, int count);
    glm::vec3 computeCentroid(const Triangle& tri);

}