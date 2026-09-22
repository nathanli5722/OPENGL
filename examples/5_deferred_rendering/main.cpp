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

// The framebuffer size to aim for, in pixels — Window converts this to the
// logical size the compositor wants, so it means the same thing on a HiDPI
// display as on a 1x one. The value actually obtained is window.getWidth()/
// getHeight(), which is what render targets are sized from below.
const float width = 1920.0f, height = 1080.0f;

// Fraction of the framebuffer the geometry pass is shaded at. The lighting
// pass always covers the whole window and samples the G-buffer with 0..1 UVs,
// so a value below 1.0 simply trades sharpness for fill rate without any other
// change. 1.0 is native resolution.
const float renderScale = 1.0f;


int main() {
    try {
        Window window(width, height, "Deferred Rendering");

        Model sceneModel = loadModel(DEMO_DIR "/assets/sponza.obj");

        Shader deferred_shader(DEMO_DIR "/vertex.vert", DEMO_DIR "/deferred-fragment.frag");
        ScreenQuad lighting_shader(DEMO_DIR "/lighting.frag");

        Camera camera(glm::vec3(0.0f, 300.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f),
                      glm::vec3(0.0f, 1.0f, 0.0f), 45.0f, window.getAspect(),
                      1.0f, 10000.0f);
        camera.movementSpeed = 300.0f;

        GBuffer gBuffer(static_cast<int>(window.getWidth() * renderScale),
                        static_cast<int>(window.getHeight() * renderScale));

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
            // The geometry pass left the viewport at the G-buffer's size, which
            // with renderScale < 1 is smaller than the window; bindDefault()
            // is what makes this quad cover the whole framebuffer.
            window.bindDefault();
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
