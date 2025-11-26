#include "Input.h"

Input& Input::getInstance() {
    static Input instance;
    return instance;
}

void Input::init(GLFWwindow* win) {
    window = win;
    glfwSetWindowUserPointer(window, this);
    glfwSetKeyCallback(window, Input::keyCallback);
    glfwSetCursorPosCallback(window, Input::cursorPosCallback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}

void Input::update() {
    // Reset per-frame state
    mouseDelta = glm::vec2(0.0f);
}

bool Input::isKeyPressed(int key) const {
    if (key >= 0 && key < 1024) {
        return keys[key];
    }
    return false;
}

bool Input::isKeyDown(int key) const {
    return glfwGetKey(window, key) == GLFW_PRESS;
}

glm::vec2 Input::getMouseDelta() const {
    return mouseDelta;
}

void Input::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    Input* input = static_cast<Input*>(glfwGetWindowUserPointer(window));
    if (input) {
        input->handleKey(key, action);
    }
}

void Input::cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    Input* input = static_cast<Input*>(glfwGetWindowUserPointer(window));
    if (input) {
        input->handleMouseMove(xpos, ypos);
    }
}

void Input::handleKey(int key, int action) {
    if (key >= 0 && key < 1024) {
        if (action == GLFW_PRESS) {
            keys[key] = true;
        } else if (action == GLFW_RELEASE) {
            keys[key] = false;
        }
    }
}

void Input::handleMouseMove(double xpos, double ypos) {
    if (firstMouse) {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }

    double xoffset = xpos - lastX;
    double yoffset = lastY - ypos; // Reversed since y-coordinates range from bottom to top

    lastX = xpos;
    lastY = ypos;

    mouseDelta = glm::vec2(static_cast<float>(xoffset), static_cast<float>(yoffset));
}
