#include <GLFW/glfw3.h>
#include <exception>
#include <iostream>
#include <glm/glm.hpp>

import window;
import shader;
import camera;
import model;
import framebuffer;
import screen_quad;

const float width = 1920.0f, height = 1080.0f;


int main() {
    try {
        Window window(width, height, "Deferred Rendering");

        Model sceneModel = loadModel(DEMO_DIR "/assets/sponza.obj");

        Shader deferred_shader(DEMO_DIR "/vertex.vert", DEMO_DIR "/deferred-fragment.frag");
        ScreenQuad lighting_shader(DEMO_DIR "/lighting.frag");

        Camera camera(glm::vec3(0.0f, 300.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f),
                      glm::vec3(0.0f, 1.0f, 0.0f), 45.0f, width / height, 1.0f, 10000.0f);
        camera.movementSpeed = 300.0f;

        GBuffer gBuffer(static_cast<int>(width), static_cast<int>(height));

        glm::mat4 modelMat = glm::mat4(1.0f);

        window.setInputCallback([&](GLFWwindow* w, float deltaTime) {
            camera.processInput(w, deltaTime);
        });

        window.renderLoop([&](GLFWwindow*, float) {
            // Geometry pass: render the scene into the G-buffer.
            gBuffer.bindForWriting();
            gBuffer.clear();

            deferred_shader.use();
            deferred_shader.setMat4("view", camera.getViewMatrix());
            deferred_shader.setMat4("projection", camera.getProjectionMatrix());

            sceneModel.draw(deferred_shader, modelMat);

            gBuffer.unbind();

            // Lighting pass: sample the G-buffer from a full-screen quad.
            gBuffer.bindForReading();

            lighting_shader.shader.use();
            lighting_shader.shader.setInt("gPosition", 0);
            lighting_shader.shader.setInt("gNormal", 1);
            lighting_shader.shader.setInt("gAlbedoSpec", 2);
            lighting_shader.shader.setVec3("viewPos", camera.position);
            lighting_shader.shader.setVec3("dirLight.direction", glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f)));
            lighting_shader.shader.setVec3("dirLight.color", glm::vec3(1.0f, 1.0f, 1.0f));
            lighting_shader.shader.setFloat("dirLight.intensity", 1.0f);
            lighting_shader.shader.setVec3("ambientColor", glm::vec3(0.1f, 0.1f, 0.1f));
            lighting_shader.shader.setFloat("ambientIntensity", 1.0f);
            lighting_shader.shader.setFloat("shininess", 32.0f);
            
            lighting_shader.draw();
        });
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
