import window;
import buffer;

#include <vector>
#include <GLFW/glfw3.h>

// Global buffer for demonstration purposes in the render loop
VertexBuffer* g_vertexBuffer = nullptr;

void render_callback(GLFWwindow* window) {
    if (g_vertexBuffer) {
        g_vertexBuffer->bind();
        g_vertexBuffer->draw();
        g_vertexBuffer->unbind();
    }
}

int main() {
    try {
        Window window(nullptr, 800, 600, "OpenGL Module Test");

        std::vector<VertexData> vertices = {
            {-0.5f, -0.5f, 0.0f},
            { 0.5f, -0.5f, 0.0f},
            { 0.0f,  0.5f, 0.0f}
        };

        std::vector<unsigned int> indices = {
            0, 1, 2
        };

        g_vertexBuffer = new VertexBuffer(vertices, indices);

        window.renderLoop(render_callback);

        delete g_vertexBuffer;
    } catch (const std::exception& e) {
        return 1;
    }

    return 0;
}