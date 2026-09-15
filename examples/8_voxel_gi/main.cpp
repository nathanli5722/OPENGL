#include "glad/glad.h"
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
import voxel_grid;

const float width = 1920.0f, height = 1080.0f;


int main() {
    try {
        Window window(width, height, "Voxel GI");

        Model sceneModel = loadModel(std::string(PROJECT_SOURCE_DIR) +
                                      "/src/assets/Sponza-master/sponza.obj");

        Shader deferred_shader(DEMO_DIR "/vertex.vert", DEMO_DIR "/deferred-fragment.frag");
        ScreenQuad lighting_shader(DEMO_DIR "/lighting.frag");
        Shader shadow_shader(DEMO_DIR "/shadow_map.vert", DEMO_DIR "/shadow_map.frag");
        Shader voxelize_shader(DEMO_DIR "/voxelize.vert", DEMO_DIR "/voxelize.frag",
                                DEMO_DIR "/voxelize.geom");
        ScreenQuad voxelDebugShader(DEMO_DIR "/voxel_debug.frag");
        DirectionalLight dirLight(glm::vec3(0.2f, 1.0f, 0.1f));

        ShadowMap shadowMap(2048);

        Camera camera(glm::vec3(0.0f, 300.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f),
                      glm::vec3(0.0f, 1.0f, 0.0f), 45.0f, width / height, 1.0f, 10000.0f);
        camera.movementSpeed = 300.0f;

        GBuffer gBuffer(static_cast<int>(width), static_cast<int>(height));

        glm::mat4 modelMat = glm::mat4(1.0f);

        // Sponza's raw .obj vertex bounds: X -1920.9..1799.9, Y -126.4..1429.4,
        // Z -1182.8..1105.4. sceneCenter is that box's midpoint and sceneRadius
        // is the distance from it to the farthest corner, so the shadow map's
        // orthographic frustum is sized to just cover the whole building. The
        // voxel grid reuses this same bounding sphere (as a cube) rather than
        // computing its own bounds.
        glm::vec3 sceneCenter(-60.5f, 651.5f, -38.7f);
        float sceneRadius = 2320.0f;

        // 128^3 RGBA16F (~16.8MB) is a reasonable starting resolution for
        // fast iteration; bump toward 256^3 once the pipeline is verified
        // correct.
        VoxelGrid voxelGrid(128, sceneCenter - glm::vec3(sceneRadius),
                                  sceneCenter + glm::vec3(sceneRadius));

        window.setInputCallback([&](GLFWwindow* w, float deltaTime) {
            camera.processInput(w, deltaTime);
        });

        // Press V to flip between the normal deferred+lighting output and a
        // raw raymarch of the voxel grid (voxel_debug.frag) — a quick way to
        // confirm voxelize.geom/.frag are actually placing geometry where
        // expected before trusting anything built on top of the grid.
        bool showVoxelDebug = false;
        bool vKeyWasDown = false;

        window.renderLoop([&](GLFWwindow* win, float) {
            bool vKeyIsDown = glfwGetKey(win, GLFW_KEY_V) == GLFW_PRESS;
            if (vKeyIsDown && !vKeyWasDown) showVoxelDebug = !showVoxelDebug;
            vKeyWasDown = vKeyIsDown;
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

            // Voxelization pass: rasterize the scene into the voxel grid.
            // TODO(you): Sponza is static, so this only needs to run once
            // rather than every frame — gate it behind a
            // `static bool voxelized = false` flag (or similar) once
            // you've confirmed it's correct; brute-force "every frame" is
            // fine while you're iterating on voxelize.geom/.frag.
            voxelGrid.bindForWriting();
            voxelGrid.clear();
            voxelize_shader.use();
            // TODO(you): set voxelize_shader's voxelBoundsMin/voxelBoundsMax/
            // voxelResolution uniforms (from voxelGrid.getBoundsMin()/
            // getBoundsMax()/getResolution()) and call
            // sceneModel.draw(voxelize_shader, modelMat).
            voxelGrid.unbind();
            voxelGrid.generateMips();

            if (showVoxelDebug) {
                // voxelGrid.unbind() already rebound the default framebuffer
                // and restored depth/cull/blend state; just clear it so a
                // stale depth buffer from a previous frame can't occlude
                // this pass's full-screen quad.
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

                voxelGrid.bindForReading(0);
                Shader& debugShader = voxelDebugShader.shader;
                debugShader.use();
                debugShader.setInt("voxelGrid", 0);
                debugShader.setVec3("voxelGridBoundsMin", voxelGrid.getBoundsMin());
                debugShader.setVec3("voxelGridBoundsMax", voxelGrid.getBoundsMax());
                debugShader.setFloat("voxelGridResolution",
                                      static_cast<float>(voxelGrid.getResolution()));
                debugShader.setVec3("viewPos", camera.position);
                debugShader.setMat4("invView", glm::inverse(camera.getViewMatrix()));
                debugShader.setMat4("invProjection", glm::inverse(camera.getProjectionMatrix()));

                voxelDebugShader.draw();
                return;
            }

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
            shadowMap.bindForReading(4);
            voxelGrid.bindForReading(5);

            lighting_shader.shader.use();
            lighting_shader.shader.setInt("gPosition", 0);
            lighting_shader.shader.setInt("gNormal", 1);
            lighting_shader.shader.setInt("gAlbedo", 2);
            lighting_shader.shader.setInt("gMetallicRoughness", 3);
            lighting_shader.shader.setInt("shadowMap", 4);
            lighting_shader.shader.setInt("voxelGrid", 5);
            lighting_shader.shader.setVec3("voxelGridBoundsMin", voxelGrid.getBoundsMin());
            lighting_shader.shader.setVec3("voxelGridBoundsMax", voxelGrid.getBoundsMax());
            lighting_shader.shader.setFloat("voxelGridResolution",
                                             static_cast<float>(voxelGrid.getResolution()));
            lighting_shader.shader.setMat4("lightSpaceMatrix", lightSpaceMatrix);
            lighting_shader.shader.setVec3("viewPos", camera.position);
            dirLight.setUniforms(lighting_shader.shader, "dirLight");
            lighting_shader.shader.setVec3("ambientColor", glm::vec3(1.0f));
            lighting_shader.shader.setFloat("ambientIntensity", 0.2f);

            lighting_shader.draw();
        });
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
