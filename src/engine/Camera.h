#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Camera {
public:
    Camera(float aspectRatio);
    
    void setPosition(const glm::vec3& position);
    void setRotation(float pitch, float yaw);
    void update(float deltaTime);
    void processKeyboard(int direction, float deltaTime, bool turbo = false);
    void processMouseMovement(float xoffset, float yoffset);
    
    glm::mat4 getViewMatrix() const;
    glm::mat4 getProjectionMatrix() const;
    glm::vec3 getPosition() const { return position; }
    
    enum CameraMovement {
        FORWARD,
        BACKWARD,
        LEFT,
        RIGHT,
        UP,
        DOWN
    };
    
private:
    glm::vec3 position;
    glm::vec3 front;
    glm::vec3 up;
    glm::vec3 right;
    glm::vec3 worldUp;
    float pitch;
    float yaw;
    float aspectRatio;
    float fov;
    float nearPlane;
    float farPlane;
    float movementSpeed;
    float mouseSensitivity;
    
    void updateCameraVectors();
};
