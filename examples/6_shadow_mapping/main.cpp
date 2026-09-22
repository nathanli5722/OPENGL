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
import shadow_map;
import light;

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
        Window window(width, height, "Shadow Mapping");

        Model sceneModel = loadModel(DEMO_DIR "/assets/sponza.obj");

        Shader deferred_shader(DEMO_DIR "/vertex.vert", DEMO_DIR "/deferred-fragment.frag");
        ScreenQuad lighting_shader(DEMO_DIR "/lighting.frag");
        Shader shadow_shader(DEMO_DIR "/shadow_map.vert", DEMO_DIR "/shadow_map.frag");
        DirectionalLight dirLight(glm::vec3(0.2f, 1.0f, 0.1f));
        
        ShadowMap shadowMap(2048);

        Camera camera(glm::vec3(0.0f, 300.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f),
                      glm::vec3(0.0f, 1.0f, 0.0f), 45.0f, window.getAspect(),
                      1.0f, 10000.0f);
        camera.movementSpeed = 300.0f;

        GBuffer gBuffer(static_cast<int>(window.getWidth() * renderScale),
                        static_cast<int>(window.getHeight() * renderScale));

        glm::mat4 modelMat = glm::mat4(1.0f);

        // Sponza's raw .obj vertex bounds: X -1920.9..1799.9, Y -126.4..1429.4,
        // Z -1182.8..1105.4. sceneCenter is that box's midpoint and sceneRadius
        // is the distance from it to the farthest corner, so the shadow map's
        // orthographic frustum is sized to just cover the whole building.
        glm::vec3 sceneCenter(-60.5f, 651.5f, -38.7f);
        float sceneRadius = 2320.0f;

        window.setInputCallback([&](GLFWwindow* w, float deltaTime) {
            camera.processInput(w, deltaTime);
        });

        window.renderLoop([&](GLFWwindow*, float) {
            // Computed once and reused for both passes below: the shadow
            // pass renders depth from this matrix, and the lighting pass
            // must reproject fragments through this exact same matrix to
            // land in the same texel space, or the shadow test is
            // comparing against a light frustum it wasn't rendered from.
            glm::mat4 lightSpaceMatrix = dirLight.getLightSpaceMatrix(sceneCenter, sceneRadius);

            shadowMap.bindForWriting();
            shadowMap.clear();

            //use shader
            shadow_shader.use();
            shadow_shader.setMat4("lightSpaceMatrix", lightSpaceMatrix);

            sceneModel.drawDepthOnly(shadow_shader, modelMat);
            shadowMap.unbind();

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
            shadowMap.bindForReading(3);

            lighting_shader.shader.use();
            lighting_shader.shader.setInt("gPosition", 0);
            lighting_shader.shader.setInt("gNormal", 1);
            lighting_shader.shader.setInt("gAlbedoSpec", 2);
            lighting_shader.shader.setInt("shadowMap", 3);
            lighting_shader.shader.setMat4("lightSpaceMatrix", lightSpaceMatrix);
            lighting_shader.shader.setVec3("viewPos", camera.position);
            dirLight.setUniforms(lighting_shader.shader, "dirLight");
            lighting_shader.shader.setVec3("ambientColor", glm::vec3(1.0f));
            lighting_shader.shader.setFloat("ambientIntensity", 0.2f);
            lighting_shader.shader.setFloat("shininess", 32.0f);
            
            lighting_shader.draw();
        });
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
