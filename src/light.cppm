module;

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>

export module light;

import shader;

// A directional light (e.g. the sun): a single direction with no position,
// so its rays are treated as parallel across the whole scene rather than
// spreading from a point. Owns the color/intensity a lighting pass shades
// with, and derives the view+projection matrix a shadow pass renders the
// scene from — an orthographic projection, since parallel rays have no
// vanishing point (unlike a point/spot light's perspective shadow
// projection, which would need a different light type to add later).
export class DirectionalLight {
public:
    glm::vec3 direction;
    glm::vec3 color;
    float intensity;

    DirectionalLight(glm::vec3 direction, glm::vec3 color = glm::vec3(1.0f), float intensity = 1.0f)
        : direction(glm::normalize(direction)), color(color), intensity(intensity)
    {}

    // Builds the view+projection matrix a shadow pass renders the scene
    // from: an orthographic box sized to just cover a scene bounding
    // sphere (sceneCenter, sceneRadius), viewed from outside that sphere
    // looking back along `direction` toward its center. Callers use this
    // with Model::drawDepthOnly() for the shadow pass, then sample the
    // resulting depth map again in the lighting pass (reprojecting each
    // fragment through this same matrix) to test whether it's in shadow.
    glm::mat4 getLightSpaceMatrix(const glm::vec3& sceneCenter, float sceneRadius) const {
        glm::vec3 lightPos = sceneCenter + direction * sceneRadius * 2.0f;
        glm::mat4 lightView = glm::lookAt(lightPos, sceneCenter, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 lightProjection = glm::ortho(-sceneRadius, sceneRadius,
                                                -sceneRadius, sceneRadius,
                                                0.1f, sceneRadius * 4.0f);
        return lightProjection * lightView;
    }

    void setUniforms(const Shader& shader, const std::string& name) const {
        shader.setVec3((name + ".direction").c_str(), direction);
        shader.setVec3((name + ".color").c_str(), color);
        shader.setFloat((name + ".intensity").c_str(), intensity);
    }
};
