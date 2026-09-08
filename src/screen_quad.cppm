module;

#include <cstddef>
#include <string>
#include <vector>

export module screen_quad;

import buffer;
import shader;

export struct ScreenQuadVertex {
    float x, y;
    float u, v;
};

// A screen-space quad (two triangles spanning NDC) paired with a shader
// whose vertex stage is always this same pass-through — every full-screen
// pass (deferred lighting, post-processing, ...) differs only in its
// fragment shader, so this bundles the shared vertex shader + quad geometry
// once and takes just the fragment shader path per pass. Set any
// pass-specific uniforms on `shader` (after shader.use()) before draw().
export class ScreenQuad {
public:
    Shader shader;

    explicit ScreenQuad(const std::string& fragmentPath)
        : shader(vertexShaderPath, fragmentPath),
          vertexBuffer(vertices, indices, std::vector<VertexAttribute>{
              {0, 2, offsetof(ScreenQuadVertex, x)},
              {1, 2, offsetof(ScreenQuadVertex, u)},
          })
    {}

    void draw() const {
        vertexBuffer.bind();
        vertexBuffer.draw();
        vertexBuffer.unbind();
    }

private:
    static inline const std::string vertexShaderPath =
        std::string(PROJECT_SOURCE_DIR) + "/src/shaders/screen_quad.vert";

    static inline std::vector<ScreenQuadVertex> vertices = {
        {-1.0f,  1.0f, 0.0f, 1.0f},
        {-1.0f, -1.0f, 0.0f, 0.0f},
        { 1.0f, -1.0f, 1.0f, 0.0f},
        { 1.0f,  1.0f, 1.0f, 1.0f},
    };

    static inline std::vector<unsigned int> indices = {0, 1, 2, 0, 2, 3};

    VertexBuffer<ScreenQuadVertex> vertexBuffer;
};
