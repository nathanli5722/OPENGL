module;

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "tiny_obj_loader.h"

export module model;

import buffer;
import shader;
import texture;

export struct ModelVertex {
    float x, y, z;
    float nx, ny, nz;
    float u, v;
};

// A minimal metallic-roughness material description, read from an OBJ's
// .mtl PBR extension (Pr/Pm/Ke and map_Pr/map_Pm/map_Ke/norm). A texture
// path is empty when the material has no corresponding map, in which case
// the scalar/color field next to it should be used directly instead.
export struct ModelMaterial {
    glm::vec3 albedo{1.0f, 1.0f, 1.0f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    glm::vec3 emissive{0.0f, 0.0f, 0.0f};

    std::string albedoTexPath;
    std::string normalTexPath;
    std::string metallicTexPath;
    std::string roughnessTexPath;
    std::string emissiveTexPath;
};

// Owns a mesh's GPU-side vertex buffer, any textures its material
// references, and the material's scalar/color fallbacks. draw() configures
// all of that onto whatever shader you hand it (which must already be
// use()'d, with scene-level uniforms like view/projection/lights already
// set) and issues the draw call, so callers don't need to know this
// material's uniform names.
export class Model {
public:
    ModelMaterial material;

    Model(std::vector<ModelVertex> vertices, std::vector<unsigned int> indices, ModelMaterial material)
        : material(std::move(material)),
          vertexBuffer(vertices, indices, std::vector<VertexAttribute>{
              {0, 3, offsetof(ModelVertex, x)},
              {1, 3, offsetof(ModelVertex, nx)},
              {2, 2, offsetof(ModelVertex, u)},
          })
    {
        if (!this->material.albedoTexPath.empty())
            albedoMap.emplace(this->material.albedoTexPath, ColorSpace::SRGB);
        if (!this->material.normalTexPath.empty())
            normalMap.emplace(this->material.normalTexPath, ColorSpace::Linear);
        if (!this->material.metallicTexPath.empty())
            metallicMap.emplace(this->material.metallicTexPath, ColorSpace::Linear);
        if (!this->material.roughnessTexPath.empty())
            roughnessMap.emplace(this->material.roughnessTexPath, ColorSpace::Linear);
        if (!this->material.emissiveTexPath.empty())
            emissiveMap.emplace(this->material.emissiveTexPath, ColorSpace::SRGB);
    }

    void draw(const Shader& shader) const {
        shader.setVec3("albedo", material.albedo);
        shader.setFloat("metallic", material.metallic);
        shader.setFloat("roughness", material.roughness);
        shader.setVec3("emissive", material.emissive);

        shader.setBool("hasAlbedoMap", albedoMap.has_value());
        shader.setBool("hasNormalMap", normalMap.has_value());
        shader.setBool("hasMetallicMap", metallicMap.has_value());
        shader.setBool("hasRoughnessMap", roughnessMap.has_value());
        shader.setBool("hasEmissiveMap", emissiveMap.has_value());

        if (albedoMap)    { albedoMap->bind(0);    shader.setInt("albedoMap", 0); }
        if (normalMap)    { normalMap->bind(1);    shader.setInt("normalMap", 1); }
        if (metallicMap)  { metallicMap->bind(2);  shader.setInt("metallicMap", 2); }
        if (roughnessMap) { roughnessMap->bind(3); shader.setInt("roughnessMap", 3); }
        if (emissiveMap)  { emissiveMap->bind(4);  shader.setInt("emissiveMap", 4); }

        vertexBuffer.bind();
        vertexBuffer.draw();
        vertexBuffer.unbind();
    }

private:
    VertexBuffer<ModelVertex> vertexBuffer;
    std::optional<Texture> albedoMap, normalMap, metallicMap, roughnessMap, emissiveMap;
};

// Loads a single-material OBJ mesh. Every (position, normal, uv) triple
// referenced by a face becomes its own output vertex rather than being
// deduplicated against matching vertices from other faces, so the index
// buffer is just 0..N-1 in order; simple, at the cost of vertex reuse.
export Model loadModel(const std::string& objPath) {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string err;

    auto slash = objPath.find_last_of("/\\");
    std::string baseDir = (slash == std::string::npos) ? "./" : objPath.substr(0, slash + 1);

    bool ok = tinyobj::LoadObj(&attrib, &shapes, &materials, &err,
                               objPath.c_str(), baseDir.c_str());
    if (!ok)
        throw std::runtime_error("Failed to load model: " + objPath + " (" + err + ")");

    std::vector<ModelVertex> vertices;
    std::vector<unsigned int> indices;
    for (const auto& shape : shapes) {
        for (const auto& index : shape.mesh.indices) {
            ModelVertex vertex{};

            vertex.x = attrib.vertices[3 * index.vertex_index + 0];
            vertex.y = attrib.vertices[3 * index.vertex_index + 1];
            vertex.z = attrib.vertices[3 * index.vertex_index + 2];

            if (index.normal_index >= 0) {
                vertex.nx = attrib.normals[3 * index.normal_index + 0];
                vertex.ny = attrib.normals[3 * index.normal_index + 1];
                vertex.nz = attrib.normals[3 * index.normal_index + 2];
            }

            if (index.texcoord_index >= 0) {
                vertex.u = attrib.texcoords[2 * index.texcoord_index + 0];
                vertex.v = attrib.texcoords[2 * index.texcoord_index + 1];
            }

            indices.push_back(static_cast<unsigned int>(vertices.size()));
            vertices.push_back(vertex);
        }
    }

    ModelMaterial material;
    if (!materials.empty()) {
        const auto& mat = materials[0];
        material.albedo = glm::vec3(mat.diffuse[0], mat.diffuse[1], mat.diffuse[2]);
        material.metallic = mat.metallic;
        material.roughness = mat.roughness;
        material.emissive = glm::vec3(mat.emission[0], mat.emission[1], mat.emission[2]);

        auto texPath = [&](const std::string& name) {
            return name.empty() ? std::string() : baseDir + name;
        };
        material.albedoTexPath = texPath(mat.diffuse_texname);
        material.normalTexPath = texPath(mat.normal_texname);
        material.metallicTexPath = texPath(mat.metallic_texname);
        material.roughnessTexPath = texPath(mat.roughness_texname);
        material.emissiveTexPath = texPath(mat.emissive_texname);
    }

    return Model(std::move(vertices), std::move(indices), std::move(material));
}
