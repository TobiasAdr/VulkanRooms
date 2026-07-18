#include "MeshLoader.h"
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>
#include <iostream>

void MeshLoader::load(const std::string& filename, glm::vec3 offset, float scale) {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filename.c_str())) {
        throw std::runtime_error("Couldnt load obj " + err);
    }

    for (const auto& shape : shapes) {
        size_t indexOffset = 0;
        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {
            
            int fv = shape.mesh.num_face_vertices[f];
            if (fv != 3) { indexOffset += fv; continue; } // bara trianglar

            Triangle tri;
            glm::vec3 verts[3];
            for (int v = 0; v < 3; v++) {
                tinyobj::index_t idx = shape.mesh.indices[indexOffset + v];
                float x = attrib.vertices[3 * idx.vertex_index + 0];
                float y = attrib.vertices[3 * idx.vertex_index + 1];
                float z = attrib.vertices[3 * idx.vertex_index + 2];
                verts[v] = glm::vec3(x, y, z) * scale + offset;
            }

            // Beräkna normal
            glm::vec3 e1 = verts[1] - verts[0];
            glm::vec3 e2 = verts[2] - verts[0];
            glm::vec3 normal = glm::normalize(glm::cross(e1, e2));

            tri.v0 = glm::vec4(verts[0], 0.0f);
            tri.v1 = glm::vec4(verts[1], 0.0f);
            tri.v2 = glm::vec4(verts[2], 0.0f);
            tri.normal = glm::vec4(normal, 0.0f);

            triangles.push_back(tri);

            if (triangles.size() >= 1000) break;
            
            indexOffset += fv;
        }
    }

    std::cout << "Loaded " << triangles.size() << " triangles from " << filename << "\n";

}