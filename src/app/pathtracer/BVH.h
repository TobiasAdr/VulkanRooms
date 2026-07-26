#pragma once
#include <glm/glm.hpp>
#include <vector>
#include "../scene/MeshLoader.h"

struct BVHNode {

    glm::vec3 aabbMin;
    int leftChild;
    glm::vec3 aabbMax;
    int rightChild;
    int triangleStart;
    int triangleCount;  
    float padding[2];

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

};