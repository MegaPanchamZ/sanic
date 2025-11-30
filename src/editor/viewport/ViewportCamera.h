/**
 * ViewportCamera.h
 * 
 * Camera controller for the editor viewport.
 * 
 * Supports:
 * - Orbit mode (Maya-style)
 * - FPS mode (fly-through)
 * - Pan mode
 * - Zoom
 * - Focus on selection
 */

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Sanic::Editor {

enum class CameraMode {
    Orbit,      // Rotate around focus point
    FPS,        // First-person fly-through
    Pan         // Pan on view plane
};

class ViewportCamera {
public:
    ViewportCamera();
    
    // Update camera state
    void update(float deltaTime);
    
    // Input handling
    void onMouseMove(float deltaX, float deltaY, bool leftButton, bool middleButton, bool rightButton, bool alt, bool shift);
    void onMouseScroll(float delta);
    void onKeyDown(int key);
    void onKeyUp(int key);
    
    // Camera positioning
    void setPosition(const glm::vec3& position);
    void setTarget(const glm::vec3& target);
    void lookAt(const glm::vec3& position, const glm::vec3& target, const glm::vec3& up = glm::vec3(0, 1, 0));
    
    // Focus on point or bounds
    void focusOn(const glm::vec3& center, float radius = 5.0f);
    void focusOnBounds(const glm::vec3& min, const glm::vec3& max);
    
    // Snap to axis views
    void snapToFront();
    void snapToBack();
    void snapToLeft();
    void snapToRight();
    void snapToTop();
    void snapToBottom();
    void togglePerspective();
    
    // Camera properties
    const glm::vec3& getPosition() const { return position_; }
    const glm::vec3& getTarget() const { return target_; }
    glm::vec3 getForward() const;
    glm::vec3 getRight() const;
    glm::vec3 getUp() const;
    
    float getDistance() const { return glm::distance(position_, target_); }
    void setDistance(float distance);
    
    // Projection
    void setAspectRatio(float aspect) { aspectRatio_ = aspect; }
    void setFov(float fov) { fov_ = fov; }
    void setNearFar(float near, float far) { nearPlane_ = near; farPlane_ = far; }
    bool isPerspective() const { return isPerspective_; }
    void setOrthographicSize(float size) { orthoSize_ = size; }
    
    float getFov() const { return fov_; }
    float getNearPlane() const { return nearPlane_; }
    float getFarPlane() const { return farPlane_; }
    float getAspectRatio() const { return aspectRatio_; }
    
    // Matrices
    glm::mat4 getViewMatrix() const;
    glm::mat4 getProjectionMatrix() const;
    glm::mat4 getViewProjectionMatrix() const;
    
    // Movement settings
    void setMoveSpeed(float speed) { moveSpeed_ = speed; }
    void setRotateSpeed(float speed) { rotateSpeed_ = speed; }
    void setZoomSpeed(float speed) { zoomSpeed_ = speed; }
    void setPanSpeed(float speed) { panSpeed_ = speed; }
    
    float getMoveSpeed() const { return moveSpeed_; }
    
    // Mode
    void setMode(CameraMode mode) { mode_ = mode; }
    CameraMode getMode() const { return mode_; }
    
    // Ray from screen position (for picking)
    struct Ray {
        glm::vec3 origin;
        glm::vec3 direction;
    };
    Ray screenToRay(const glm::vec2& screenPos, const glm::vec2& viewportSize) const;
    
private:
    void updateOrbit();
    void updateFPS(float deltaTime);
    void clampAngles();
    
    // Camera state
    glm::vec3 position_ = glm::vec3(5, 5, 5);
    glm::vec3 target_ = glm::vec3(0, 0, 0);
    
    // Spherical coordinates (for orbit)
    float yaw_ = -45.0f;     // Horizontal angle (degrees)
    float pitch_ = 30.0f;    // Vertical angle (degrees)
    float distance_ = 10.0f;  // Distance from target
    
    // Projection
    float fov_ = 60.0f;
    float aspectRatio_ = 16.0f / 9.0f;
    float nearPlane_ = 0.1f;
    float farPlane_ = 10000.0f;
    float orthoSize_ = 10.0f;
    bool isPerspective_ = true;
    
    // Movement settings
    float moveSpeed_ = 10.0f;
    float rotateSpeed_ = 0.3f;
    float zoomSpeed_ = 1.0f;
    float panSpeed_ = 0.01f;
    
    // Mode
    CameraMode mode_ = CameraMode::Orbit;
    
    // Input state (for FPS mode)
    bool keyForward_ = false;
    bool keyBackward_ = false;
    bool keyLeft_ = false;
    bool keyRight_ = false;
    bool keyUp_ = false;
    bool keyDown_ = false;
    bool keyTurbo_ = false;
};

} // namespace Sanic::Editor
