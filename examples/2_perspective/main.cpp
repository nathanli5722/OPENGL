#include <GLFW/glfw3.h>
#include <cstddef>
#include <exception>
#include <iostream>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

import window;
import buffer;
import shader;
import camera;

// Unit cube (1x1x1) centered at the origin, 8 unique corners.
// Only positions are stored, matching VertexData{x, y, z}.
//
//        7-------6
//       /|      /|
//      3-------2 |
//      | 4-----|-5
//      |/      |/
//      0-------1
//
static std::vector<VertexData> cubeVertices = {
    {-0.5f, -0.5f, -0.5f}, // 0 back-bottom-left
    { 0.5f, -0.5f, -0.5f}, // 1 back-bottom-right
    { 0.5f,  0.5f, -0.5f}, // 2 back-top-right
    {-0.5f,  0.5f, -0.5f}, // 3 back-top-left
    {-0.5f, -0.5f,  0.5f}, // 4 front-bottom-left
    { 0.5f, -0.5f,  0.5f}, // 5 front-bottom-right
    { 0.5f,  0.5f,  0.5f}, // 6 front-top-right
    {-0.5f,  0.5f,  0.5f}, // 7 front-top-left>
};

// 12 triangles (2 per face), wound counter-clockwise as seen from outside
// the cube so the winding is correct if you enable back-face culling later.
static std::vector<unsigned int> cubeIndices = {
    0, 3, 2,  0, 2, 1, // back  (-z)
    4, 5, 6,  4, 6, 7, // front (+z)
    0, 4, 7,  0, 7, 3, // left  (-x)
    1, 2, 6,  1, 6, 5, // right (+x)
    3, 7, 6,  3, 6, 2, // top   (+y)
    0, 1, 5,  0, 5, 4, // bottom (-y)
};

float width = 1920.0f, height = 1080.0f;

int main() {
    try {
        Window window(width, height, "Perspective");
        std::vector<VertexAttribute> layout = {
            {0, 3, offsetof(VertexData, x)},
        };
        VertexBuffer<VertexData> vertexBuffer(cubeVertices, cubeIndices, layout);
        Shader shader(PROJECT_SOURCE_DIR "/examples/2_perspective/vertex.vert",
            PROJECT_SOURCE_DIR "/examples/2_perspective/green.frag");

        Camera camera(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f, 0.0f, -1.0f),
                      glm::vec3(0.0f, 1.0f, 0.0f), 45.0f, width / height, 0.1f, 100.0f);
        glm::mat4 model = glm::mat4(1.0f);

        window.setInputCallback([&](GLFWwindow* w, float deltaTime) {
            camera.processInput(w, deltaTime);
        });

        window.renderLoop([&](GLFWwindow*, float) {
            vertexBuffer.bind();
            shader.use();
            shader.setMat4("projection", camera.getProjectionMatrix());
            shader.setMat4("view", camera.getViewMatrix());
            shader.setMat4("model", model);
            vertexBuffer.draw();
            vertexBuffer.unbind();
        });
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
