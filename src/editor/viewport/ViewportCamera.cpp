/**
 * ViewportCamera.cpp
 * 
 * Implementation of editor viewport camera.
 */

#include "ViewportCamera.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>

namespace Sanic::Editor {

ViewportCamera::ViewportCamera() {
    updateOrbit();
}

void ViewportCamera::update(float deltaTime) {
    if (mode_ == CameraMode::FPS) {
        updateFPS(deltaTime);
    }
}

void ViewportCamera::onMouseMove(float deltaX, float deltaY, bool leftButton, bool middleButton, bool rightButton, bool alt, bool shift) {
    // Maya-style controls with Alt modifier
    // Alt + LMB: Orbit
    // Alt + MMB: Pan
    // Alt + RMB: Zoom
    
    // Also support:
    // RMB: FPS look (when in FPS mode or holding)
    // MMB: Pan
    
    bool orbit = (alt && leftButton) || (!alt && !leftButton && !middleButton && rightButton && mode_ == CameraMode::Orbit);
    bool pan = (alt && middleButton) || (!alt && middleButton);
    bool zoom = (alt && rightButton);
    
    if (orbit) {
        yaw_ -= deltaX * rotateSpeed_;
        pitch_ -= deltaY * rotateSpeed_;
        clampAngles();
        updateOrbit();
    } else if (pan) {
        glm::vec3 right = getRight();
        glm::vec3 up = getUp();
        
        float panScale = distance_ * panSpeed_;
        target_ -= right * deltaX * panScale;
        target_ += up * deltaY * panScale;
        
        updateOrbit();
    } else if (zoom) {
        float zoomAmount = deltaY * zoomSpeed_ * 0.01f * distance_;
        distance_ = std::max(0.1f, distance_ + zoomAmount);
        updateOrbit();
    } else if (rightButton && mode_ == CameraMode::FPS) {
        // FPS look
        yaw_ -= deltaX * rotateSpeed_;
        pitch_ -= deltaY * rotateSpeed_;
        clampAngles();
        
        // Update target based on new direction
        target_ = position_ + getForward() * distance_;
    }
}

void ViewportCamera::onMouseScroll(float delta) {
    // Zoom with scroll wheel
    float zoomFactor = 1.0f - delta * zoomSpeed_ * 0.1f;
    distance_ = std::max(0.1f, distance_ * zoomFactor);
    
    if (!isPerspective_) {
        orthoSize_ = std::max(0.1f, orthoSize_ * zoomFactor);
    }
    
    updateOrbit();
}

void ViewportCamera::onKeyDown(int key) {
    switch (key) {
        case GLFW_KEY_W: keyForward_ = true; break;
        case GLFW_KEY_S: keyBackward_ = true; break;
        case GLFW_KEY_A: keyLeft_ = true; break;
        case GLFW_KEY_D: keyRight_ = true; break;
        case GLFW_KEY_E: keyUp_ = true; break;
        case GLFW_KEY_Q: keyDown_ = true; break;
        case GLFW_KEY_LEFT_SHIFT: keyTurbo_ = true; break;
    }
}

void ViewportCamera::onKeyUp(int key) {
    switch (key) {
        case GLFW_KEY_W: keyForward_ = false; break;
        case GLFW_KEY_S: keyBackward_ = false; break;
        case GLFW_KEY_A: keyLeft_ = false; break;
        case GLFW_KEY_D: keyRight_ = false; break;
        case GLFW_KEY_E: keyUp_ = false; break;
        case GLFW_KEY_Q: keyDown_ = false; break;
        case GLFW_KEY_LEFT_SHIFT: keyTurbo_ = false; break;
    }
}

void ViewportCamera::setPosition(const glm::vec3& position) {
    position_ = position;
    
    // Update spherical coordinates
    glm::vec3 dir = target_ - position_;
    distance_ = glm::length(dir);
    if (distance_ > 0.001f) {
        dir = glm::normalize(dir);
        pitch_ = glm::degrees(asinf(dir.y));
        yaw_ = glm::degrees(atan2f(-dir.x, -dir.z));
    }
}

void ViewportCamera::setTarget(const glm::vec3& target) {
    target_ = target;
    updateOrbit();
}

void ViewportCamera::lookAt(const glm::vec3& position, const glm::vec3& target, const glm::vec3& up) {
    position_ = position;
    target_ = target;
    
    glm::vec3 dir = glm::normalize(target - position);
    distance_ = glm::length(target - position);
    
    pitch_ = glm::degrees(asinf(dir.y));
    yaw_ = glm::degrees(atan2f(-dir.x, -dir.z));
    
    clampAngles();
}

void ViewportCamera::focusOn(const glm::vec3& center, float radius) {
    target_ = center;
    
    // Calculate distance to fit object in view
    float fovRad = glm::radians(fov_);
    distance_ = radius / sinf(fovRad * 0.5f) * 1.5f;
    distance_ = std::max(distance_, radius + 1.0f);
    
    updateOrbit();
}

void ViewportCamera::focusOnBounds(const glm::vec3& min, const glm::vec3& max) {
    glm::vec3 center = (min + max) * 0.5f;
    float radius = glm::length(max - min) * 0.5f;
    focusOn(center, radius);
}

void ViewportCamera::snapToFront() {
    yaw_ = 180.0f;
    pitch_ = 0.0f;
    updateOrbit();
}

void ViewportCamera::snapToBack() {
    yaw_ = 0.0f;
    pitch_ = 0.0f;
    updateOrbit();
}

void ViewportCamera::snapToLeft() {
    yaw_ = 90.0f;
    pitch_ = 0.0f;
    updateOrbit();
}

void ViewportCamera::snapToRight() {
    yaw_ = -90.0f;
    pitch_ = 0.0f;
    updateOrbit();
}

void ViewportCamera::snapToTop() {
    yaw_ = 180.0f;
    pitch_ = 89.0f;
    updateOrbit();
}

void ViewportCamera::snapToBottom() {
    yaw_ = 180.0f;
    pitch_ = -89.0f;
    updateOrbit();
}

void ViewportCamera::togglePerspective() {
    isPerspective_ = !isPerspective_;
}

void ViewportCamera::setDistance(float distance) {
    distance_ = std::max(0.1f, distance);
    updateOrbit();
}

glm::vec3 ViewportCamera::getForward() const {
    float pitchRad = glm::radians(pitch_);
    float yawRad = glm::radians(yaw_);
    
    return glm::normalize(glm::vec3(
        -sinf(yawRad) * cosf(pitchRad),
        sinf(pitchRad),
        -cosf(yawRad) * cosf(pitchRad)
    ));
}

glm::vec3 ViewportCamera::getRight() const {
    return glm::normalize(glm::cross(getForward(), glm::vec3(0, 1, 0)));
}

glm::vec3 ViewportCamera::getUp() const {
    return glm::normalize(glm::cross(getRight(), getForward()));
}

glm::mat4 ViewportCamera::getViewMatrix() const {
    return glm::lookAt(position_, target_, glm::vec3(0, 1, 0));
}

glm::mat4 ViewportCamera::getProjectionMatrix() const {
    if (isPerspective_) {
        return glm::perspective(glm::radians(fov_), aspectRatio_, nearPlane_, farPlane_);
    } else {
        float halfWidth = orthoSize_ * aspectRatio_;
        float halfHeight = orthoSize_;
        return glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, nearPlane_, farPlane_);
    }
}

glm::mat4 ViewportCamera::getViewProjectionMatrix() const {
    return getProjectionMatrix() * getViewMatrix();
}

ViewportCamera::Ray ViewportCamera::screenToRay(const glm::vec2& screenPos, const glm::vec2& viewportSize) const {
    // Convert screen position to NDC
    glm::vec2 ndc;
    ndc.x = (screenPos.x / viewportSize.x) * 2.0f - 1.0f;
    ndc.y = 1.0f - (screenPos.y / viewportSize.y) * 2.0f;  // Flip Y
    
    // Get inverse VP matrix
    glm::mat4 invVP = glm::inverse(getViewProjectionMatrix());
    
    // Unproject near and far points
    glm::vec4 nearPoint = invVP * glm::vec4(ndc, -1.0f, 1.0f);
    glm::vec4 farPoint = invVP * glm::vec4(ndc, 1.0f, 1.0f);
    
    nearPoint /= nearPoint.w;
    farPoint /= farPoint.w;
    
    Ray ray;
    ray.origin = glm::vec3(nearPoint);
    ray.direction = glm::normalize(glm::vec3(farPoint - nearPoint));
    
    return ray;
}

void ViewportCamera::updateOrbit() {
    float pitchRad = glm::radians(pitch_);
    float yawRad = glm::radians(yaw_);
    
    // Calculate position from spherical coordinates
    position_.x = target_.x + distance_ * sinf(yawRad) * cosf(pitchRad);
    position_.y = target_.y + distance_ * sinf(pitchRad);
    position_.z = target_.z + distance_ * cosf(yawRad) * cosf(pitchRad);
}

void ViewportCamera::updateFPS(float deltaTime) {
    float speed = moveSpeed_ * (keyTurbo_ ? 3.0f : 1.0f) * deltaTime;
    
    glm::vec3 forward = getForward();
    glm::vec3 right = getRight();
    glm::vec3 up(0, 1, 0);
    
    glm::vec3 movement(0);
    
    if (keyForward_) movement += forward;
    if (keyBackward_) movement -= forward;
    if (keyRight_) movement += right;
    if (keyLeft_) movement -= right;
    if (keyUp_) movement += up;
    if (keyDown_) movement -= up;
    
    if (glm::length(movement) > 0.001f) {
        movement = glm::normalize(movement) * speed;
        position_ += movement;
        target_ += movement;
    }
}

void ViewportCamera::clampAngles() {
    pitch_ = std::clamp(pitch_, -89.0f, 89.0f);
    
    while (yaw_ > 180.0f) yaw_ -= 360.0f;
    while (yaw_ < -180.0f) yaw_ += 360.0f;
}

} // namespace Sanic::Editor
