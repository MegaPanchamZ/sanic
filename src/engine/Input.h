#pragma once
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

class Input {
public:
    static Input& getInstance();
    
    void init(GLFWwindow* window);
    void update();
    
    bool isKeyPressed(int key) const;
    bool isKeyDown(int key) const;
    glm::vec2 getMouseDelta() const;
    
    // GLFW callback bridges
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos);
    
private:
    Input() = default;
    ~Input() = default;
    Input(const Input&) = delete;
    Input& operator=(const Input&) = delete;
    
    void handleKey(int key, int action);
    void handleMouseMove(double xpos, double ypos);
    
    GLFWwindow* window = nullptr;
    bool keys[1024] = {false};
    bool firstMouse = true;
    double lastX = 400.0;
    double lastY = 300.0;
    glm::vec2 mouseDelta = glm::vec2(0.0f);
};
