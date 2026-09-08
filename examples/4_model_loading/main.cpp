#include <GLFW/glfw3.h>
#include <exception>
#include <iostream>
#include <glm/glm.hpp>

import window;
import shader;
import camera;
import model;

const float width = 1920.0f, height = 1080.0f;

int main() {
    try {
        Window window(width, height, "Model Loading");

        Model sceneModel = loadModel(DEMO_DIR "/assets/cube.obj");

        Shader shader(DEMO_DIR "/vertex.vert", DEMO_DIR "/fragment.frag");

        Camera camera(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f, 0.0f, -1.0f),
                      glm::vec3(0.0f, 1.0f, 0.0f), 45.0f, width / height, 0.1f, 100.0f);
        glm::mat4 modelMat = glm::mat4(1.0f);

        glm::vec3 lightPos(2.0f, 2.0f, 4.0f);
        glm::vec3 lightColor(1.0f, 1.0f, 1.0f);

        window.setInputCallback([&](GLFWwindow* w, float deltaTime) {
            camera.processInput(w, deltaTime);
        });

        window.renderLoop([&](GLFWwindow*, float) {
            shader.use();
            shader.setMat4("projection", camera.getProjectionMatrix());
            shader.setMat4("view", camera.getViewMatrix());

            shader.setVec3("lightPositions[0]", lightPos);
            shader.setVec3("lightColors[0]", lightColor);

            sceneModel.draw(shader, modelMat);
        });
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
