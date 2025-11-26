#include "Camera.h"

Camera::Camera(float aspectRatio)
    : position(0.0f, 0.0f, 3.0f)
    , front(0.0f, 0.0f, -1.0f)
    , up(0.0f, 1.0f, 0.0f)
    , worldUp(0.0f, 1.0f, 0.0f)
    , pitch(0.0f)
    , yaw(-90.0f)
    , aspectRatio(aspectRatio)
    , fov(45.0f)
    , nearPlane(0.1f)
    , farPlane(100.0f)
    , movementSpeed(2.5f)
    , mouseSensitivity(0.1f)
{
    updateCameraVectors();
}

void Camera::setPosition(const glm::vec3& newPosition) {
    position = newPosition;
}

void Camera::setRotation(float newPitch, float newYaw) {
    pitch = newPitch;
    yaw = newYaw;
    updateCameraVectors();
}

void Camera::update(float deltaTime) {
    // Update logic if needed
}

void Camera::processKeyboard(int direction, float deltaTime, bool turbo) {
    float velocity = movementSpeed * deltaTime;
    if (turbo) {
        velocity *= 5.0f; // Turbo modifier for Sonic-style high-speed movement
    }
    
    switch (direction) {
        case FORWARD:
            position += front * velocity;
            break;
        case BACKWARD:
            position -= front * velocity;
            break;
        case LEFT:
            position -= right * velocity;
            break;
        case RIGHT:
            position += right * velocity;
            break;
        case UP:
            position += worldUp * velocity;
            break;
        case DOWN:
            position -= worldUp * velocity;
            break;
    }
}

void Camera::processMouseMovement(float xoffset, float yoffset) {
    xoffset *= mouseSensitivity;
    yoffset *= mouseSensitivity;

    yaw += xoffset;
    pitch += yoffset;

    // Constrain pitch to avoid screen flip
    if (pitch > 89.0f)
        pitch = 89.0f;
    if (pitch < -89.0f)
        pitch = -89.0f;

    updateCameraVectors();
}

glm::mat4 Camera::getViewMatrix() const {
    return glm::lookAt(position, position + front, up);
}

glm::mat4 Camera::getProjectionMatrix() const {
    glm::mat4 proj = glm::perspective(glm::radians(fov), aspectRatio, nearPlane, farPlane);
    proj[1][1] *= -1; // Flip Y for Vulkan
    return proj;
}

void Camera::updateCameraVectors() {
    glm::vec3 newFront;
    newFront.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    newFront.y = sin(glm::radians(pitch));
    newFront.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    front = glm::normalize(newFront);
    right = glm::normalize(glm::cross(front, worldUp));
    up = glm::normalize(glm::cross(right, front));
}
