#include <cstddef>
#include <iostream>
#include <vector>
#include <GLFW/glfw3.h>

import window;
import buffer;
import shader;

int main() {
    try {
        Window window(800, 600, "OpenGL Module Test");

        std::vector<VertexData> vertices = {
            {-0.5f, -0.5f, 0.0f},
            { 0.5f, -0.5f, 0.0f},
            { 0.0f,  0.5f, 0.0f}
        };

        std::vector<unsigned int> indices = {
            0, 1, 2
        };

        std::vector<VertexAttribute> layout = {
            {0, 3, offsetof(VertexData, x)},
        };
        VertexBuffer<VertexData> vertexBuffer(vertices, indices, layout);
        Shader shader(PROJECT_SOURCE_DIR "/examples/1_hello_triangle/vertex.vert",
            PROJECT_SOURCE_DIR "/examples/1_hello_triangle/fragment.frag");

        window.renderLoop([&](GLFWwindow*, float) {
            shader.use();
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