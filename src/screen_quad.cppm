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

// A Shader whose vertex stage is always the same pass-through, plus the
// screen-space quad (two triangles spanning NDC) it draws — every full-screen
// pass (deferred lighting, post-processing, ...) differs only in its
// fragment shader, so this bundles the shared vertex shader + quad geometry
// once and takes just the fragment shader path per pass. Like ComputeShader
// adding dispatch(), it's used as a regular shader (use(), set*()) and then
// draw() runs the pass.
export class ScreenQuad : public Shader {
public:
    explicit ScreenQuad(const std::string& fragmentPath)
        : Shader(vertexShaderPath, fragmentPath),
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
