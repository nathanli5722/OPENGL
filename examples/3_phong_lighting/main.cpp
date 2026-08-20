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

struct PosNormalVertex {
    float x, y, z;
    float nx, ny, nz;
};

// Unit cube (1x1x1) centered at the origin. Unlike the position-only cube in
// the perspective demo, each face needs its own 4 vertices (24 total) so
// every vertex can carry that face's flat normal for lighting.
static std::vector<PosNormalVertex> cubeVertices = {
    // back (-z)
    {-0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f},
    {-0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f},
    { 0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f},
    { 0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f},
    // front (+z)
    {-0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f},
    { 0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f},
    { 0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f},
    {-0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f},
    // left (-x)
    {-0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f},
    {-0.5f, -0.5f,  0.5f, -1.0f,  0.0f,  0.0f},
    {-0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f},
    {-0.5f,  0.5f, -0.5f, -1.0f,  0.0f,  0.0f},
    // right (+x)
    { 0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f},
    { 0.5f,  0.5f, -0.5f,  1.0f,  0.0f,  0.0f},
    { 0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f},
    { 0.5f, -0.5f,  0.5f,  1.0f,  0.0f,  0.0f},
    // top (+y)
    {-0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f},
    {-0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f},
    { 0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f},
    { 0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f},
    // bottom (-y)
    {-0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f},
    { 0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f},
    { 0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f},
    {-0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f},
};

// Two triangles per face, wound counter-clockwise as seen from outside.
static std::vector<unsigned int> cubeIndices = {
     0,  1,  2,   0,  2,  3, // back
     4,  5,  6,   4,  6,  7, // front
     8,  9, 10,   8, 10, 11, // left
    12, 13, 14,  12, 14, 15, // right
    16, 17, 18,  16, 18, 19, // top
    20, 21, 22,  20, 22, 23, // bottom
};

const float width = 1920.0f, height = 1080.0f;

int main() {
    try {
        Window window(width, height, "Phong Lighting");

        std::vector<VertexAttribute> layout = {
            {0, 3, offsetof(PosNormalVertex, x)},
            {1, 3, offsetof(PosNormalVertex, nx)},
        };
        VertexBuffer<PosNormalVertex> vertexBuffer(cubeVertices, cubeIndices, layout);

        Shader shader(PROJECT_SOURCE_DIR "/examples/3_phong_lighting/vertex.vert",
            PROJECT_SOURCE_DIR "/examples/3_phong_lighting/fragment.frag");

        Camera camera(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f, 0.0f, -1.0f),
                      glm::vec3(0.0f, 1.0f, 0.0f), 45.0f, width / height, 0.1f, 100.0f);
        glm::mat4 model = glm::mat4(1.0f);

        glm::vec3 lightPos(1.2f, 1.0f, 2.0f);
        glm::vec3 lightColor(1.0f, 1.0f, 1.0f);
        glm::vec3 objectColor(1.0f, 0.5f, 0.31f);

        window.setInputCallback([&](GLFWwindow* w, float deltaTime) {
            camera.processInput(w, deltaTime);
        });

        window.renderLoop([&](GLFWwindow*, float) {
            shader.use();
            shader.setMat4("projection", camera.getProjectionMatrix());
            shader.setMat4("view", camera.getViewMatrix());
            shader.setMat4("model", model);
            shader.setVec3("viewPos", camera.position);
            shader.setVec3("lightPos", lightPos);
            shader.setVec3("lightColor", lightColor);
            shader.setVec3("objectColor", objectColor);

            vertexBuffer.bind();
            vertexBuffer.draw();
            vertexBuffer.unbind();
        });
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
