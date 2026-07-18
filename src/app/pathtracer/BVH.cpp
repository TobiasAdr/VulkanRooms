#include "BVH.h"
#include <algorithm>
#include <iostream>


// ---------------------------- AABB MIN/MAX ---------------------------------

// These functions define the bounding box area. 
// The parameters take in the triangles and start dividing them into AABB's
glm::vec3 BVH::computeAABBMin(const std::vector<Triangle>& tris, int start, int count) {

    glm::vec3 minP(1e30f); // Really big number

    for(int i = start; i < start + count; i++){


        // Check all vertices, find the smallest find the smallest coordinates.  
        minP = glm::min(minP, glm::vec3(tris[i].v0));
        minP = glm::min(minP, glm::vec3(tris[i].v1));
        minP = glm::min(minP, glm::vec3(tris[i].v2));


    }

    return minP;
}

glm::vec3 BVH::computeAABBMax(const std::vector<Triangle>& tris, int start, int count) {


    glm::vec3 maxP(-1e30f); // Really small

    for(int i = start; i < start + count; i++){

        maxP = glm::max(maxP, glm::vec3(tris[i].v0));
        maxP = glm::max(maxP, glm::vec3(tris[i].v1));
        maxP = glm::max(maxP, glm::vec3(tris[i].v2));

    }

    return maxP;

}


// ----------------------- CENTROID ---------------------

// We compute the centroid to determine which group the triangle fit in when dividing the space. 

glm::vec3 BVH::computeCentroid(const Triangle& tri){


    return (glm::vec3(tri.v0) + glm::vec3(tri.v1) + glm::vec3(tri.v2) ) / 3.0f;


} 

// ------------------------ BUILD --------------------------

// Recursively compute each node in the tree
// If a node has less than or equal to 4 triangles, its a leaf node. 

int BVH::buildRecursive(std::vector<Triangle>& tris, int start, int count){

    BVHNode node{};

    node.aabbMin = computeAABBMin(tris, start, count);
    node.aabbMax = computeAABBMax(tris, start, count);


    // 4 or less triangles per leaf. 
    if(count <= 4){

        node.leftChild = -1;
        node.rightChild = -1;
        node.triangleStart = start;
        node.triangleCount = count;
        nodes.push_back(node);
        return static_cast<int>(nodes.size() - 1);

    }

    // -- SPLIT --

    // Find the longest axis

    glm::vec3 extent = node.aabbMax - node.aabbMin; // gives the size of the box in every dimension (x,y,z)
    int axis = 0; // Assume that x is the longest. 

    if(extent.y > extent.x) axis = 1;
    if(extent.z > extent[axis]) axis = 2; // extent[axis] is the current longest axis. 

    // -- TRIANGLE SORT --

    // Where to split the node
    float mid = (node.aabbMin[axis] + node.aabbMax[axis]) * 0.5f;

    // std::partition reorders elements in a range. Elements that satisfy the condition comes first. 
    // returns an iterator (it) to the second group (the group that did not satisfy the condition)
    // the condition in our case is the computeCentroid < mid;
    // so the first elements consist of elements that are put to the left side of the split, followed by the ones ending up on the right. 
    auto it = std::partition(tris.begin() + start, tris.begin() + start + count,
    
        [&](const Triangle& tri) {

            return computeCentroid(tri)[axis] < mid;

        });

    // Subtracting iterators give the amount of elements between them.
    // In this case, since it points to the first element in the right group, the leftCount becomes the amount of elements in the left group. 
    
    int leftCount = static_cast<int>(it - (tris.begin() + start));

    // If all the elements end up int he left group, split them in half. Fallback. 
    if(leftCount == 0 || leftCount == count)
        leftCount = count / 2;

    node.triangleStart = -1;
    node.triangleCount = 0;

    int nodeIndex = static_cast<int>(nodes.size());
    nodes.push_back(node);

    int left  = buildRecursive(tris, start, leftCount);
    int right = buildRecursive(tris, start + leftCount, count - leftCount);

    nodes[nodeIndex].leftChild  = left;
    nodes[nodeIndex].rightChild = right;

    return nodeIndex;

}

void BVH::build(const std::vector<Triangle>& triangles){

    sortedTriangles = triangles;
    buildRecursive(sortedTriangles, 0, static_cast<int>(sortedTriangles.size()));
    std::cout << "BVH built with" << nodes.size() << "nodes\n";

}