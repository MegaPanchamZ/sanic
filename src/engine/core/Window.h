#pragma once
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <string>

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

namespace Sanic {

class Window {
public:
    Window(int width, int height, const std::string& title);
    ~Window();

    bool shouldClose() const;
    void pollEvents();
    GLFWwindow* getHandle() const { return window; }
    int getWidth() const { return width; }
    int getHeight() const { return height; }
    
    // Platform-specific native handle
    void* getNativeHandle() const {
#ifdef _WIN32
        return glfwGetWin32Window(window);
#else
        return nullptr;
#endif
    }

private:
    GLFWwindow* window;
    int width, height;
    std::string title;
};

} // namespace Sanic
