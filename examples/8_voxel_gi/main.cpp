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
// change.
const float renderScale = 1.0f;

const glm::mat4 modelMat = glm::mat4(1.0f);

// Sponza's raw .obj vertex bounds: X -1920.9..1799.9, Y -126.4..1429.4,
// Z -1182.8..1105.4. sceneCenter is that box's midpoint and sceneRadius
// is the distance from it to the farthest corner, so the shadow map's
// orthographic frustum is sized to just cover the whole building. The
// voxel grid reuses this same bounding sphere (as a cube) rather than
// computing its own bounds.
const glm::vec3 sceneCenter(-60.5f, 651.5f, -38.7f);
const float sceneRadius = 2320.0f;

// Shadow pass: render the scene's depth from the light into the shadow map.
void renderShadowPass(const ShadowMap& shadowMap, const Shader& shadowShader,
                      const Model& sceneModel, const glm::mat4& lightSpaceMatrix) {
    shadowMap.bindForWriting();
    shadowMap.clear();

    shadowShader.use();
    shadowShader.setMat4("lightSpaceMatrix", lightSpaceMatrix);

    sceneModel.drawDepthOnly(shadowShader, modelMat);
    shadowMap.unbind();
}

// Voxelization pass: rasterize the scene into the voxel grid.
void voxelizeScene(const VoxelGrid& voxelGrid, const Shader& voxelizeShader,
                   const Model& sceneModel, const DirectionalLight& dirLight,
                   const ShadowMap& shadowMap, const glm::mat4& lightSpaceMatrix) {
    // Image unit 1, matching voxelize.frag's layout(binding = 1..4)
    // for rVoxel/gVoxel/bVoxel/aVoxel. Unit 0 is left alone because
    // that shader's albedoMap sits at *texture* unit 0 — separate
    // namespace, but keeping the numbers distinct avoids confusion.
    voxelGrid.bindForWriting(1);
    voxelGrid.clear();
    voxelizeShader.use();
    voxelizeShader.setVec3("voxelBoundsMin", voxelGrid.getBoundsMin());
    voxelizeShader.setVec3("voxelBoundsMax", voxelGrid.getBoundsMax());
    voxelizeShader.setInt("voxelResolution", voxelGrid.getResolution());
    voxelizeShader.setMat4("lightSpaceMatrix", lightSpaceMatrix);
    dirLight.setUniforms(voxelizeShader, "dirLight");
    // voxelize.frag reads the shadow map on texture unit 5 — past the
    // 0..4 that Model::draw binds material maps to. Without this, unit
    // 5 holds only last frame's 3D voxel texture, the sampler2D reads
    // depth 0, and every voxel tests as shadowed.
    shadowMap.bindForReading(5);

    // Model::draw already sets `model`, `albedo`/`hasAlbedoMap`, and
    // binds the albedo texture to texture unit 0 — which is exactly
    // the uniform set voxelize.vert/.frag declare. The accumulators
    // live on *image* units 1..4, a separate namespace, so unit 0
    // being taken by a sampler here is not a conflict.
    sceneModel.draw(voxelizeShader, modelMat);

    voxelGrid.unbind();
}

// Resolve pass: average the integer accumulators into the filterable RGBA16F
// volume, then build its mip chain. This has to be a compute dispatch rather
// than more rasterization — there is no triangle whose fragments cover every
// voxel, and each voxel's result depends only on itself.
void resolveVoxels(const VoxelGrid& voxelGrid, const ComputeShader& resolveShader) {
    resolveShader.use();
    resolveShader.setInt("voxelResolution", voxelGrid.getResolution());
    voxelGrid.bindForResolve(1);

    unsigned int groups = ComputeShader::groupCount(voxelGrid.getResolution(), 8);
    resolveShader.dispatch(groups, groups, groups);

    voxelGrid.generateMips();
}

// Raw raymarch of the voxel grid straight to the window, in place of the
// normal geometry + lighting passes.
void renderVoxelDebug(const Window& window, const VoxelGrid& voxelGrid,
                      const ScreenQuad& debugShader, const Camera& camera) {
    // bindDefault() is what sets the viewport back to the window
    // — voxelGrid.unbind() leaves it at 128x128, and before that
    // the shadow map left it square. Drawing a 16:9 projection
    // into a square viewport is what stretches this view.
    window.bindDefault();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    voxelGrid.bindForReading(0);
    debugShader.use();
    debugShader.setInt("voxelGrid", 0);
    debugShader.setVec3("voxelGridBoundsMin", voxelGrid.getBoundsMin());
    debugShader.setVec3("voxelGridBoundsMax", voxelGrid.getBoundsMax());
    debugShader.setFloat("voxelGridResolution",
                         static_cast<float>(voxelGrid.getResolution()));
    debugShader.setVec3("viewPos", camera.position);
    debugShader.setMat4("invView", glm::inverse(camera.getViewMatrix()));
    debugShader.setMat4("invProjection", glm::inverse(camera.getProjectionMatrix()));

    debugShader.draw();
}

// Geometry pass: render the scene into the G-buffer.
void renderGeometryPass(const GBuffer& gBuffer, const Shader& deferredShader,
                        const Model& sceneModel, const Camera& camera) {
    gBuffer.bindForWriting();
    gBuffer.clear();

    deferredShader.use();
    deferredShader.setMat4("view", camera.getViewMatrix());
    deferredShader.setMat4("projection", camera.getProjectionMatrix());

    sceneModel.draw(deferredShader, modelMat);

    gBuffer.unbind();
}

// Lighting pass: sample the G-buffer from a full-screen quad.
void renderLightingPass(const Window& window, const GBuffer& gBuffer,
                        const ShadowMap& shadowMap, const VoxelGrid& voxelGrid,
                        const ScreenQuad& lightingShader, const DirectionalLight& dirLight,
                        const Camera& camera, const glm::mat4& lightSpaceMatrix) {
    window.bindDefault();
    gBuffer.bindForReading();
    shadowMap.bindForReading(5);
    voxelGrid.bindForReading(6);

    lightingShader.use();
    lightingShader.setInt("gPosition", 0);
    lightingShader.setInt("gNormal", 1);
    lightingShader.setInt("gAlbedo", 2);
    lightingShader.setInt("gMetallicRoughness", 3);
    lightingShader.setVec3("voxelGridBoundsMin", voxelGrid.getBoundsMin());
    lightingShader.setVec3("voxelGridBoundsMax", voxelGrid.getBoundsMax());
    lightingShader.setFloat("voxelGridResolution",
                            static_cast<float>(voxelGrid.getResolution()));
    lightingShader.setFloat("sceneRadius", sceneRadius);
    lightingShader.setMat4("lightSpaceMatrix", lightSpaceMatrix);
    lightingShader.setVec3("viewPos", camera.position);
    dirLight.setUniforms(lightingShader, "dirLight");
    lightingShader.setVec3("ambientColor", glm::vec3(1.0f));
    lightingShader.setFloat("ambientIntensity", 0.2f);

    lightingShader.draw();
}

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
        DirectionalLight dirLight(glm::vec3(0.4f, 1.0f, 0.1f), 4.0f);

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

            // Computed once and reused for every pass below: the shadow
            // pass renders depth from this matrix, and the voxelization and
            // lighting passes must reproject fragments through this exact
            // same matrix to land in the same texel space, or the shadow
            // test is comparing against a light frustum it wasn't rendered
            // from.
            glm::mat4 lightSpaceMatrix = dirLight.getLightSpaceMatrix(sceneCenter, sceneRadius);

            renderShadowPass(shadowMap, shadow_shader, sceneModel, lightSpaceMatrix);

            // TODO(you): Sponza is static, so voxelization + resolve only
            // need to run once rather than every frame — gate them behind a
            // `static bool voxelized = false` flag (or similar) once you've
            // confirmed they're correct; brute-force "every frame" is fine
            // while you're iterating on voxelize.geom/.frag.
            voxelizeScene(voxelGrid, voxelize_shader, sceneModel, dirLight,
                          shadowMap, lightSpaceMatrix);
            resolveVoxels(voxelGrid, voxelResolveShader);

            if (showVoxelDebug) {
                renderVoxelDebug(window, voxelGrid, voxelDebugShader, camera);
                return;
            }

            renderGeometryPass(gBuffer, deferred_shader, sceneModel, camera);
            renderLightingPass(window, gBuffer, shadowMap, voxelGrid, lighting_shader,
                               dirLight, camera, lightSpaceMatrix);
        });
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
