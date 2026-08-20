module;

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

export module camera;

export enum class CameraMovement {
    Forward,
    Backward,
    Left,
    Right,
    Up,
    Down,
};

export enum class CameraRotation {
    Left,
    Right,
    Up,
    Down,
};

// Owns the view and projection matrices for a scene. Movement is driven by
// feeding it a direction + deltaTime each frame (see processKeyboard); it
// does no input polling itself.
export class Camera {
public:
    glm::vec3 position;
    glm::vec3 front;
    // Fixed reference vector (usually (0,1,0)) used to derive `right` and,
    // via lookAt, the camera's actual on-screen up axis. It does not tilt
    // with `front` as the camera pitches, so it is not the camera's local
    // up direction.
    glm::vec3 worldUp;
    float movementSpeed;
    float turnSpeed;

    float fov;
    float aspect;
    float nearPlane;
    float farPlane;

    Camera(glm::vec3 position = glm::vec3(0.0f, 0.0f, 3.0f),
           glm::vec3 front = glm::vec3(0.0f, 0.0f, -1.0f),
           glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f),
           float fov = 45.0f, float aspect = 800.0f / 600.0f,
           float nearPlane = 0.1f, float farPlane = 100.0f)
        : position(position), front(glm::normalize(front)), worldUp(worldUp), movementSpeed(2.5f),
          turnSpeed(90.0f), fov(fov), aspect(aspect), nearPlane(nearPlane), farPlane(farPlane)
    {
        // Derive yaw/pitch from the given front so rotation continues smoothly
        // from whatever direction the camera started out facing.
        pitch = glm::degrees(glm::asin(this->front.y));
        yaw = glm::degrees(glm::atan(this->front.z, this->front.x));
    }

    glm::mat4 getViewMatrix() const {
        return glm::lookAt(position, position + front, worldUp);
    }

    glm::mat4 getProjectionMatrix() const {
        return glm::perspective(glm::radians(fov), aspect, nearPlane, farPlane);
    }

    void processKeyboard(CameraMovement direction, float deltaTime) {
        float velocity = movementSpeed * deltaTime;
        glm::vec3 right = glm::normalize(glm::cross(front, worldUp));
        switch (direction) {
            case CameraMovement::Forward:  position += front * velocity; break;
            case CameraMovement::Backward: position -= front * velocity; break;
            case CameraMovement::Left:     position -= right * velocity; break;
            case CameraMovement::Right:    position += right * velocity; break;
            case CameraMovement::Up:       position += worldUp * velocity; break;
            case CameraMovement::Down:     position -= worldUp * velocity; break;
        }
    }

    // Turns the camera in place by adjusting yaw/pitch, then rebuilding
    // `front` from them. `worldUp` is left untouched rather than tilting
    // with `front`, so there's no roll.
    void processRotation(CameraRotation direction, float deltaTime) {
        float angle = turnSpeed * deltaTime;
        switch (direction) {
            case CameraRotation::Left:  yaw -= angle; break;
            case CameraRotation::Right: yaw += angle; break;
            case CameraRotation::Up:    pitch += angle; break;
            case CameraRotation::Down:  pitch -= angle; break;
        }
        // Clamp away from +/-90 degrees so front never flips past straight up/down.
        pitch = glm::clamp(pitch, -89.0f, 89.0f);

        glm::vec3 newFront;
        newFront.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
        newFront.y = sin(glm::radians(pitch));
        newFront.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
        front = glm::normalize(newFront);
    }

    // Polls WASD + Space/Ctrl (move) and the arrow keys (look) and feeds
    // them into processKeyboard/processRotation. A ready-made default for
    // demos that just want a flying camera; call processKeyboard/
    // processRotation directly instead if you need different bindings.
    void processInput(GLFWwindow* window, float deltaTime) {
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
            processKeyboard(CameraMovement::Forward, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            processKeyboard(CameraMovement::Backward, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            processKeyboard(CameraMovement::Left, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
            processKeyboard(CameraMovement::Right, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
            processKeyboard(CameraMovement::Up, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
            processKeyboard(CameraMovement::Down, deltaTime);

        if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)
            processRotation(CameraRotation::Left, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS)
            processRotation(CameraRotation::Right, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)
            processRotation(CameraRotation::Up, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)
            processRotation(CameraRotation::Down, deltaTime);
    }

private:
    float yaw;
    float pitch;
};
