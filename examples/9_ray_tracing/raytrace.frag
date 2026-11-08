#version 430 core

// Runs once per pixel. Builds the ray from the camera through this pixel,
// finds the closest sphere it hits, and shades that point.

in vec2 TexCoords;  // (0,0) bottom-left of the screen, (1,1) top-right
out vec4 FragColor;

// Filled by uploadCamera() in main.cpp.
struct Camera {
    vec3 position;
    vec3 front;
    vec3 right;
    vec3 up;
    float fov;     // vertical field of view, degrees
    float aspect;  // width / height
};
uniform Camera camera;

// Mirrors `struct Sphere` in main.cpp. Uniform arrays need a fixed size, so
// the array has room for MAX_SPHERES and sphereCount says how many are used.
const int MAX_SPHERES = 16;
struct Sphere {
    vec3 center;
    float radius;
    vec3 color;
};
uniform Sphere spheres[MAX_SPHERES];
uniform int sphereCount;
