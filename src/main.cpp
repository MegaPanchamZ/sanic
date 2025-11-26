#include <iostream>
#include "engine/Window.h"
#include "engine/Renderer.h"
#include "engine/Input.h"
#include <chrono>

int main() {
    try {
        Window window(800, 600, "Sanic Engine");
        Renderer renderer(window);
        
        Input& input = Input::getInstance();
        input.init(window.getHandle());
        
        auto lastTime = std::chrono::high_resolution_clock::now();
        
        while (!window.shouldClose()) {
            auto currentTime = std::chrono::high_resolution_clock::now();
            float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
            lastTime = currentTime;
            
            input.update();
            window.pollEvents();
            
            renderer.processInput(deltaTime);
            renderer.drawFrame();
        }
        
        renderer.waitIdle();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
