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
        Window window(width, height, "Voxel GI");

        Model sceneModel = loadModel(std::string(PROJECT_SOURCE_DIR) +
                                      "/src/assets/Sponza-master/sponza.obj");

        Shader deferred_shader(DEMO_DIR "/vertex.vert", DEMO_DIR "/deferred-fragment.frag");
        ScreenQuad lighting_shader(DEMO_DIR "/lighting.frag");
        Shader shadow_shader(DEMO_DIR "/shadow_map.vert", DEMO_DIR "/shadow_map.frag");
        Shader voxelize_shader(DEMO_DIR "/voxelize.vert", DEMO_DIR "/voxelize.frag",
                                DEMO_DIR "/voxelize.geom");
        ComputeShader voxelResolveShader(DEMO_DIR "/voxel_resolve.comp");
        ScreenQuad voxelDebugShader(DEMO_DIR "/voxel_debug.frag");
        DirectionalLight dirLight(glm::vec3(0.2f, 1.0f, 0.1f));

        ShadowMap shadowMap(2048);

        Camera camera(glm::vec3(0.0f, 300.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f),
                      glm::vec3(0.0f, 1.0f, 0.0f), 45.0f, window.getAspect(),
                      1.0f, 10000.0f);
        camera.movementSpeed = 300.0f;

        // Scaled off the real framebuffer. The shadow map and voxel grid above
        // are deliberately *not* tied to it — their resolutions are quality
        // knobs for the light and the scene volume, nothing to do with the
        // window.
        GBuffer gBuffer(static_cast<int>(window.getWidth() * renderScale),
                        static_cast<int>(window.getHeight() * renderScale));

        glm::mat4 modelMat = glm::mat4(1.0f);

        // Sponza's raw .obj vertex bounds: X -1920.9..1799.9, Y -126.4..1429.4,
        // Z -1182.8..1105.4. sceneCenter is that box's midpoint and sceneRadius
        // is the distance from it to the farthest corner, so the shadow map's
        // orthographic frustum is sized to just cover the whole building. The
        // voxel grid reuses this same bounding sphere (as a cube) rather than
        // computing its own bounds.
        glm::vec3 sceneCenter(-60.5f, 651.5f, -38.7f);
        float sceneRadius = 2320.0f;

        // 128^3 costs ~32MB for the four R32UI accumulators plus ~19MB for
        // the resolved RGBA16F volume and its mips — a reasonable starting
        // resolution for fast iteration. Bump toward 256^3 (8x that) once the
        // pipeline is verified correct.
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
            //
            // Image unit 1, matching voxelize.frag's layout(binding = 1..4)
            // for rVoxel/gVoxel/bVoxel/aVoxel. Unit 0 is left alone because
            // that shader's albedoMap sits at *texture* unit 0 — separate
            // namespace, but keeping the numbers distinct avoids confusion.
            voxelGrid.bindForWriting(1);
            voxelGrid.clear();
            voxelize_shader.use();
            voxelize_shader.setVec3("voxelBoundsMin", voxelGrid.getBoundsMin());
            voxelize_shader.setVec3("voxelBoundsMax", voxelGrid.getBoundsMax());
            voxelize_shader.setInt("voxelResolution", voxelGrid.getResolution());

            // Model::draw already sets `model`, `albedo`/`hasAlbedoMap`, and
            // binds the albedo texture to texture unit 0 — which is exactly
            // the uniform set voxelize.vert/.frag declare. The accumulators
            // live on *image* units 1..4, a separate namespace, so unit 0
            // being taken by a sampler here is not a conflict.
            sceneModel.draw(voxelize_shader, modelMat);

            voxelGrid.unbind();

            // Resolve pass: average the integer accumulators into the
            // filterable RGBA16F volume, then build its mip chain. This has
            // to be a compute dispatch rather than more rasterization —
            // there is no triangle whose fragments cover every voxel, and
            // each voxel's result depends only on itself.
            voxelResolveShader.use();
            voxelResolveShader.setInt("voxelResolution", voxelGrid.getResolution());
            voxelGrid.bindForResolve(1);

            unsigned int groups =
                ComputeShader::groupCount(voxelGrid.getResolution(), 8);
            voxelResolveShader.dispatch(groups, groups, groups);

            voxelGrid.generateMips();

            if (showVoxelDebug) {
                // bindDefault() is what sets the viewport back to the window
                // — voxelGrid.unbind() leaves it at 128x128, and before that
                // the shadow map left it square. Drawing a 16:9 projection
                // into a square viewport is what stretches this view.
                window.bindDefault();
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
            window.bindDefault();
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
