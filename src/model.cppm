module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "tiny_obj_loader.h"
#include "tiny_gltf.h"

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

    // Which source channel metallicTexPath/roughnessTexPath's image holds
    // its data in: 0 (red) by default, matching an OBJ's own single-channel
    // grayscale map. glTF instead packs both into one RGB texture (see
    // loadGltfModel), so that path points both fields at the same file and
    // sets these to 2 (blue) and 1 (green) respectively.
    int metallicChannel = 0;
    int roughnessChannel = 0;
};

// The portion of a mesh that shares a single material: owns that slice's
// GPU-side vertex buffer, any textures its material references, and the
// material's scalar/color fallbacks. draw() configures all of that onto
// whatever shader you hand it (which must already be use()'d, with
// scene-level uniforms like view/projection/lights already set) and issues
// the draw call, so callers don't need to know this material's uniform
// names. Not exported — Model (below) is the public entry point and owns
// one Submesh per material a loaded OBJ references.
class Submesh {
public:
    ModelMaterial material;

    Submesh(std::vector<ModelVertex> vertices, std::vector<unsigned int> indices, ModelMaterial material)
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
            metallicMap.emplace(this->material.metallicTexPath, ColorSpace::Linear, this->material.metallicChannel);
        if (!this->material.roughnessTexPath.empty())
            roughnessMap.emplace(this->material.roughnessTexPath, ColorSpace::Linear, this->material.roughnessChannel);
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

    // Issues the draw call only — no material uniforms, no texture binds.
    // For passes that only care about vertex positions (e.g. a shadow map's
    // depth pass) and would otherwise waste driver calls setting up state a
    // depth-only shader never reads. The vertex buffer still carries its
    // normal/uv attributes, but a depth-only vertex shader simply doesn't
    // declare or read them, so nothing needs to be stripped out for this.
    void drawDepthOnly() const {
        vertexBuffer.bind();
        vertexBuffer.draw();
        vertexBuffer.unbind();
    }

private:
    VertexBuffer<ModelVertex> vertexBuffer;
    std::optional<Texture> albedoMap, normalMap, metallicMap, roughnessMap, emissiveMap;
};

// A loaded OBJ mesh, split into one Submesh per material it references (see
// loadModel). draw() sets the shared model matrix once, then draws every
// submesh with its own material bound — a single-material OBJ just ends up
// with one submesh, so this covers both cases with one code path.
export class Model {
public:
    explicit Model(std::vector<Submesh> submeshes) : submeshes(std::move(submeshes)) {}

    void draw(const Shader& shader, const glm::mat4& modelMatrix) const {
        shader.setMat4("model", modelMatrix);
        for (const auto& submesh : submeshes)
            submesh.draw(shader);
    }

    // Same as draw(), but for a shadow map's depth pass: sets the model
    // matrix, then draws each submesh's geometry only, skipping the
    // material/texture setup a depth-only shader has no use for.
    void drawDepthOnly(const Shader& shader, const glm::mat4& modelMatrix) const {
        shader.setMat4("model", modelMatrix);
        for (const auto& submesh : submeshes)
            submesh.drawDepthOnly();
    }

private:
    std::vector<Submesh> submeshes;
};

// Loads an OBJ mesh, grouping its faces by material into one Submesh per
// material referenced (faces with no material assigned share one bucket
// with the default ModelMaterial). Every (position, normal, uv) triple
// referenced by a face becomes its own output vertex rather than being
// deduplicated against matching vertices from other faces, so each
// submesh's index buffer is just 0..N-1 in order; simple, at the cost of
// vertex reuse.
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

    // Some .mtl files (e.g. Sponza's) are authored on Windows and use
    // backslashes as path separators, which aren't separators on Linux —
    // normalize to forward slashes so the resulting path actually
    // resolves to the texture file's real location.
    auto texPath = [&](const std::string& name) {
        if (name.empty()) return std::string();
        std::string normalized = name;
        std::replace(normalized.begin(), normalized.end(), '\\', '/');
        return baseDir + normalized;
    };

    auto materialFor = [&](int materialId) {
        ModelMaterial material;
        if (materialId < 0 || static_cast<std::size_t>(materialId) >= materials.size())
            return material;

        const auto& mat = materials[materialId];
        material.albedo = glm::vec3(mat.diffuse[0], mat.diffuse[1], mat.diffuse[2]);
        material.metallic = mat.metallic;
        // tinyobj defaults roughness to 0.0 when a .mtl has no Pr line at
        // all (plain Phong .mtls predating the PBR extension, e.g. this
        // classic Sponza), rather than leaving it unset — indistinguishable
        // here from a material that genuinely wants a mirror finish. 0.0
        // isn't just "very shiny": it collapses the GGX distribution's
        // denominator toward zero, producing blown-out/NaN highlights
        // instead of a normal specular response. A real Pr value close to
        // (but not exactly) 0 is unaffected by this — only the exact
        // default sentinel falls back to ModelMaterial's own default.
        material.roughness = (mat.roughness > 0.0f) ? mat.roughness : material.roughness;
        material.emissive = glm::vec3(mat.emission[0], mat.emission[1], mat.emission[2]);
        material.albedoTexPath = texPath(mat.diffuse_texname);
        material.normalTexPath = texPath(mat.normal_texname);
        material.metallicTexPath = texPath(mat.metallic_texname);
        material.roughnessTexPath = texPath(mat.roughness_texname);
        material.emissiveTexPath = texPath(mat.emissive_texname);
        return material;
    };

    // LoadObj triangulates by default, so each shape's faces are exactly 3
    // indices wide and material_ids has one entry per face.
    std::unordered_map<int, std::vector<ModelVertex>> verticesByMaterial;
    for (const auto& shape : shapes) {
        for (std::size_t face = 0; face < shape.mesh.material_ids.size(); ++face) {
            auto& vertices = verticesByMaterial[shape.mesh.material_ids[face]];
            for (int v = 0; v < 3; ++v) {
                const auto& index = shape.mesh.indices[3 * face + v];
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

                vertices.push_back(vertex);
            }
        }
    }

    std::vector<Submesh> submeshes;
    submeshes.reserve(verticesByMaterial.size());
    for (auto& [materialId, vertices] : verticesByMaterial) {
        std::vector<unsigned int> indices(vertices.size());
        for (std::size_t i = 0; i < indices.size(); ++i)
            indices[i] = static_cast<unsigned int>(i);
        submeshes.emplace_back(std::move(vertices), std::move(indices), materialFor(materialId));
    }

    return Model(std::move(submeshes));
}

// Reads a tightly-typed float accessor (glTF's POSITION/NORMAL/TEXCOORD_0
// attributes are always VEC3/VEC3/VEC2 of float components from every
// exporter this project has needed) into a flat buffer of
// accessor.count * numComponents floats. Other component types (e.g. a
// quantized VEC3) aren't handled — good enough for this project's own
// converted assets rather than arbitrary glTF in the wild.
static std::vector<float> readFloatAccessor(const tinygltf::Model& gltf, int accessorIdx, int numComponents) {
    const auto& accessor = gltf.accessors[accessorIdx];
    if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT)
        throw std::runtime_error("Only float glTF accessors are supported");

    const auto& bufferView = gltf.bufferViews[accessor.bufferView];
    const auto& buffer = gltf.buffers[bufferView.buffer];
    const unsigned char* base = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
    std::size_t stride = static_cast<std::size_t>(accessor.ByteStride(bufferView));

    std::vector<float> out(accessor.count * static_cast<std::size_t>(numComponents));
    for (std::size_t i = 0; i < accessor.count; ++i) {
        const float* elem = reinterpret_cast<const float*>(base + i * stride);
        for (int c = 0; c < numComponents; ++c)
            out[i * static_cast<std::size_t>(numComponents) + c] = elem[c];
    }
    return out;
}

// Reads a triangle-index accessor into unsigned ints regardless of its
// on-disk component type (glTF indices are commonly unsigned short to save
// space, but may be byte or int depending on vertex count).
static std::vector<unsigned int> readIndexAccessor(const tinygltf::Model& gltf, int accessorIdx) {
    const auto& accessor = gltf.accessors[accessorIdx];
    const auto& bufferView = gltf.bufferViews[accessor.bufferView];
    const auto& buffer = gltf.buffers[bufferView.buffer];
    const unsigned char* base = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
    std::size_t stride = static_cast<std::size_t>(accessor.ByteStride(bufferView));

    std::vector<unsigned int> out(accessor.count);
    for (std::size_t i = 0; i < accessor.count; ++i) {
        const unsigned char* elem = base + i * stride;
        switch (accessor.componentType) {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                out[i] = *elem;
                break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                out[i] = *reinterpret_cast<const std::uint16_t*>(elem);
                break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                out[i] = *reinterpret_cast<const std::uint32_t*>(elem);
                break;
            default:
                throw std::runtime_error("Unsupported glTF index component type");
        }
    }
    return out;
}

// A node's local transform, composed from either an explicit 4x4 matrix or
// separate TRS (translation/rotation/scale) properties — glTF allows either
// form and a node has at most one of them set.
static glm::mat4 gltfNodeLocalTransform(const tinygltf::Node& node) {
    if (node.matrix.size() == 16) {
        glm::mat4 m;
        for (int i = 0; i < 16; ++i)
            glm::value_ptr(m)[i] = static_cast<float>(node.matrix[i]);
        return m;
    }

    glm::vec3 translation(0.0f);
    glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 scale(1.0f);
    if (node.translation.size() == 3)
        translation = glm::vec3(node.translation[0], node.translation[1], node.translation[2]);
    if (node.rotation.size() == 4)
        rotation = glm::quat(static_cast<float>(node.rotation[3]), static_cast<float>(node.rotation[0]),
                              static_cast<float>(node.rotation[1]), static_cast<float>(node.rotation[2]));
    if (node.scale.size() == 3)
        scale = glm::vec3(node.scale[0], node.scale[1], node.scale[2]);

    return glm::translate(glm::mat4(1.0f), translation) * glm::mat4_cast(rotation)
         * glm::scale(glm::mat4(1.0f), scale);
}

// Loads a glTF (.gltf, JSON + external .bin/textures) or glTF-Binary (.glb)
// mesh, grouping its faces by material into one Submesh per material
// referenced, same as loadModel() does for OBJ — so both loaders produce
// the same Model/Submesh/ModelMaterial types and everything downstream
// (drawing, shadow passes, the deferred G-buffer) is format-agnostic.
//
// glTF's own node hierarchy has no equivalent in Model (which just draws
// flat per-material submeshes under one shared model matrix), so each
// node's transform is baked directly into its mesh's vertices while
// walking the scene graph, rather than carried through to draw time.
//
// Image loading is disabled in tiny_gltf_impl.cpp (see the comment there),
// so only external image URIs are supported here — embedded (data-URI or
// .glb-packed) images aren't read. glTF also packs metallic and roughness
// into one texture (G=roughness, B=metallic) rather than OBJ's separate
// map_Pm/map_Pr files; ModelMaterial has no combined-texture slot, so both
// fields are pointed at the same path and loaded as two independent
// Textures — doubles that one texture's GPU memory in exchange for not
// needing a second material/texture-binding path through Submesh.
export Model loadGltfModel(const std::string& gltfPath) {
    tinygltf::Model gltfModel;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    auto slash = gltfPath.find_last_of("/\\");
    std::string baseDir = (slash == std::string::npos) ? "./" : gltfPath.substr(0, slash + 1);

    bool isBinary = gltfPath.size() >= 4 && gltfPath.compare(gltfPath.size() - 4, 4, ".glb") == 0;
    bool ok = isBinary ? loader.LoadBinaryFromFile(&gltfModel, &err, &warn, gltfPath)
                        : loader.LoadASCIIFromFile(&gltfModel, &err, &warn, gltfPath);
    if (!ok)
        throw std::runtime_error("Failed to load model: " + gltfPath + " (" + err + ")");

    auto texPath = [&](int textureIdx) {
        if (textureIdx < 0) return std::string();
        int imageIdx = gltfModel.textures[textureIdx].source;
        if (imageIdx < 0) return std::string();
        return baseDir + gltfModel.images[imageIdx].uri;
    };

    auto materialFor = [&](int materialIdx) {
        ModelMaterial material;
        if (materialIdx < 0 || static_cast<std::size_t>(materialIdx) >= gltfModel.materials.size())
            return material;

        const auto& mat = gltfModel.materials[materialIdx];
        const auto& pbr = mat.pbrMetallicRoughness;
        material.albedo = glm::vec3(pbr.baseColorFactor[0], pbr.baseColorFactor[1], pbr.baseColorFactor[2]);
        // Clamped defensively: some FBX->glTF conversions (assimp among
        // them) derive these from a non-PBR source material via a formula
        // that can land outside [0, 1] — e.g. a negative roughnessFactor —
        // which is meaningless to the BRDF. Harmless when a texture is also
        // present (the scalar is then just an unused fallback), but wrong
        // if a material has no texture and only this factor.
        material.metallic = glm::clamp(static_cast<float>(pbr.metallicFactor), 0.0f, 1.0f);
        material.roughness = glm::clamp(static_cast<float>(pbr.roughnessFactor), 0.0f, 1.0f);
        material.emissive = glm::vec3(mat.emissiveFactor[0], mat.emissiveFactor[1], mat.emissiveFactor[2]);

        material.albedoTexPath = texPath(pbr.baseColorTexture.index);
        material.normalTexPath = texPath(mat.normalTexture.index);
        material.emissiveTexPath = texPath(mat.emissiveTexture.index);

        // glTF packs metallic (B) and roughness (G) into one texture, unlike
        // OBJ's separate single-channel maps — point both fields at the same
        // file and tell Texture which channel each one actually needs (see
        // ModelMaterial::metallicChannel/roughnessChannel).
        std::string metallicRoughnessPath = texPath(pbr.metallicRoughnessTexture.index);
        material.metallicTexPath = metallicRoughnessPath;
        material.roughnessTexPath = metallicRoughnessPath;
        material.metallicChannel = 2;
        material.roughnessChannel = 1;

        return material;
    };

    std::unordered_map<int, std::vector<ModelVertex>> verticesByMaterial;

    std::function<void(int, const glm::mat4&)> visitNode = [&](int nodeIdx, const glm::mat4& parentTransform) {
        const auto& node = gltfModel.nodes[nodeIdx];
        glm::mat4 worldTransform = parentTransform * gltfNodeLocalTransform(node);

        if (node.mesh >= 0) {
            glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(worldTransform)));

            for (const auto& primitive : gltfModel.meshes[node.mesh].primitives) {
                if (primitive.mode != TINYGLTF_MODE_TRIANGLES || primitive.indices < 0)
                    continue;

                auto positionIt = primitive.attributes.find("POSITION");
                if (positionIt == primitive.attributes.end())
                    continue;
                std::vector<float> positions = readFloatAccessor(gltfModel, positionIt->second, 3);

                std::vector<float> normals;
                auto normalIt = primitive.attributes.find("NORMAL");
                if (normalIt != primitive.attributes.end())
                    normals = readFloatAccessor(gltfModel, normalIt->second, 3);

                std::vector<float> uvs;
                auto uvIt = primitive.attributes.find("TEXCOORD_0");
                if (uvIt != primitive.attributes.end())
                    uvs = readFloatAccessor(gltfModel, uvIt->second, 2);

                std::vector<unsigned int> indices = readIndexAccessor(gltfModel, primitive.indices);

                auto& outVertices = verticesByMaterial[primitive.material];
                for (unsigned int index : indices) {
                    ModelVertex vertex{};

                    glm::vec4 worldPos = worldTransform * glm::vec4(
                        positions[3 * index], positions[3 * index + 1], positions[3 * index + 2], 1.0f);
                    vertex.x = worldPos.x;
                    vertex.y = worldPos.y;
                    vertex.z = worldPos.z;

                    if (!normals.empty()) {
                        glm::vec3 n = glm::normalize(normalMatrix * glm::vec3(
                            normals[3 * index], normals[3 * index + 1], normals[3 * index + 2]));
                        vertex.nx = n.x;
                        vertex.ny = n.y;
                        vertex.nz = n.z;
                    }

                    if (!uvs.empty()) {
                        vertex.u = uvs[2 * index];
                        vertex.v = uvs[2 * index + 1];
                    }

                    outVertices.push_back(vertex);
                }
            }
        }

        for (int child : node.children)
            visitNode(child, worldTransform);
    };

    const auto& scene = gltfModel.scenes[gltfModel.defaultScene >= 0 ? gltfModel.defaultScene : 0];
    for (int rootNode : scene.nodes)
        visitNode(rootNode, glm::mat4(1.0f));

    std::vector<Submesh> submeshes;
    submeshes.reserve(verticesByMaterial.size());
    for (auto& [materialIdx, vertices] : verticesByMaterial) {
        std::vector<unsigned int> indices(vertices.size());
        for (std::size_t i = 0; i < indices.size(); ++i)
            indices[i] = static_cast<unsigned int>(i);
        submeshes.emplace_back(std::move(vertices), std::move(indices), materialFor(materialIdx));
    }

    return Model(std::move(submeshes));
}
