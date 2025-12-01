#include "Input.h"

Input& Input::getInstance() {
    static Input instance;
    return instance;
}

void Input::init(GLFWwindow* win) {
    window = win;
    // Don't install GLFW callbacks here - ImGui needs them!
    // We'll poll input state directly instead.
    // ImGui_ImplGlfw already installed callbacks via glfwSetWindowUserPointer.
    
    // Don't capture the cursor by default - editor needs it free
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    
    // Initialize mouse position
    double x, y;
    glfwGetCursorPos(window, &x, &y);
    lastX = x;
    lastY = y;
    firstMouse = false;
}

void Input::update() {
    // Poll mouse position each frame instead of using callbacks
    double x, y;
    glfwGetCursorPos(window, &x, &y);
    
    mouseDelta = glm::vec2(
        static_cast<float>(x - lastX),
        static_cast<float>(lastY - y)  // Reversed Y
    );
    
    lastX = x;
    lastY = y;
}

bool Input::isKeyPressed(int key) const {
    // Use direct polling - callbacks are handled by ImGui
    return glfwGetKey(window, key) == GLFW_PRESS;
}

bool Input::isKeyDown(int key) const {
    return glfwGetKey(window, key) == GLFW_PRESS;
}

glm::vec2 Input::getMouseDelta() const {
    return mouseDelta;
}

// These callback functions are kept for compatibility but not installed
// ImGui handles the callbacks now
void Input::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    // Not used - ImGui handles this
}

void Input::cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    // Not used - we poll mouse position in update()
}

void Input::handleKey(int key, int action) {
    // Not used
}

void Input::handleMouseMove(double xpos, double ypos) {
    // Not used
}
