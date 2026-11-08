#include "glad/glad.h"
#include <GLFW/glfw3.h>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <glm/glm.hpp>

import window;
import shader;
import camera;
import screen_quad;

// The simplest ray tracer: draw one quad covering the whole window, and in
// its fragment shader (raytrace.frag) shoot a ray from the camera through
// each pixel and color it by whichever sphere the ray hits first.
//
// There's no vertex data for the scene. The spheres are sent as an array of
// uniforms, and the camera as one uniform struct.

const float width = 1920.0f, height = 1080.0f;

// Must match MAX_SPHERES in raytrace.frag, which sizes the uniform array.
const int maxSpheres = 16;

// Mirrors `struct Sphere` in raytrace.frag.
struct Sphere {
    glm::vec3 center;
    float radius;
    glm::vec3 color;
};

// The scene. A huge sphere far below works as a flat-looking ground.
const std::vector<Sphere> spheres = {
    {{ 0.0f, -1000.0f, 0.0f}, 1000.0f, {0.5f, 0.5f, 0.5f}},
    {{-2.1f,     1.0f, 0.0f},    1.0f, {0.8f, 0.2f, 0.2f}},
    {{ 0.0f,     1.0f, 0.0f},    1.0f, {0.2f, 0.8f, 0.2f}},
    {{ 2.1f,     1.0f, 0.0f},    1.0f, {0.2f, 0.3f, 0.9f}},
};

// Each field of each array element is its own uniform, set by name, e.g.
// "spheres[2].radius". Uniforms keep their values until changed, so this
// only needs to run once while the spheres stay put.
void uploadSpheres(const Shader& shader, const std::vector<Sphere>& spheres) {
    if (spheres.size() > maxSpheres)
        throw std::runtime_error("Too many spheres: raise maxSpheres and MAX_SPHERES");

    shader.use();
    shader.setInt("sphereCount", static_cast<int>(spheres.size()));
    for (std::size_t i = 0; i < spheres.size(); ++i) {
        std::string name = "spheres[" + std::to_string(i) + "]";
        shader.setVec3(name + ".center", spheres[i].center);
        shader.setFloat(name + ".radius", spheres[i].radius);
        shader.setVec3(name + ".color", spheres[i].color);
    }
}

// Fills `uniform Camera camera` in raytrace.frag. Called every frame, since
// the camera moves.
void uploadCamera(const Shader& shader, const Camera& camera) {
    // The camera's three axes. A ray for a pixel is `front` nudged along
    // `right` and `up` by how far that pixel is from the center of the
    // screen.
    glm::vec3 right = glm::normalize(glm::cross(camera.front, camera.worldUp));
    glm::vec3 up = glm::cross(right, camera.front);

    shader.setVec3("camera.position", camera.position);
    shader.setVec3("camera.front", camera.front);
    shader.setVec3("camera.right", right);
    shader.setVec3("camera.up", up);
    shader.setFloat("camera.fov", camera.fov);
    shader.setFloat("camera.aspect", camera.aspect);
}

int main() {
    try {
        Window window(width, height, "Ray Tracing");

        ScreenQuad raytraceShader(DEMO_DIR "/raytrace.frag");
        uploadSpheres(raytraceShader, spheres);

        Camera camera(glm::vec3(0.0f, 2.0f, 7.0f), glm::vec3(0.0f, -0.2f, -1.0f),
                      glm::vec3(0.0f, 1.0f, 0.0f), 45.0f, window.getAspect());

        window.setInputCallback([&](GLFWwindow* w, float deltaTime) {
            camera.processInput(w, deltaTime);
        });

        window.renderLoop([&](GLFWwindow*, float) {
            window.bindDefault();
            raytraceShader.use();
            uploadCamera(raytraceShader, camera);
            raytraceShader.draw();
        });
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
