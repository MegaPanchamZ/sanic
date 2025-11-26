cmake_minimum_required(VERSION 3.20)
project(SanicEngine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Vulkan Setup ---
# Try to find Vulkan via standard package
find_package(Vulkan REQUIRED)

if (Vulkan_FOUND)
    message(STATUS "Vulkan SDK found: ${Vulkan_INCLUDE_DIRS}")
else()
    message(FATAL_ERROR "Vulkan SDK not found! Please set VK_SDK_PATH.")
endif()

# --- Dependencies (GLFW & GLM) ---
include(FetchContent)

# GLFW
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.3.8
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# GLM
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        0.9.9.8
)
FetchContent_GetProperties(glm)
if(NOT glm_POPULATED)
    FetchContent_Populate(glm)
endif()

# --- Source Files ---
set(SOURCES
    src/main.cpp
    src/engine/Window.cpp
    src/engine/Renderer.cpp
    src/engine/Camera.cpp
    src/engine/Input.cpp
    src/engine/stb_image_impl.cpp
    src/engine/tiny_obj_loader_impl.cpp
    src/engine/Mesh.cpp
    src/engine/Texture.cpp
)

# --- Executable ---
add_executable(SanicEngine ${SOURCES})

# --- Include Directories ---
target_include_directories(SanicEngine PRIVATE 
    src 
    ${Vulkan_INCLUDE_DIRS}
    ${glfw_SOURCE_DIR}/include
    ${glm_SOURCE_DIR}
)

# --- Linking ---
add_compile_definitions(GLM_FORCE_RADIANS GLM_FORCE_DEPTH_ZERO_TO_ONE)

target_link_libraries(SanicEngine PRIVATE 
    Vulkan::Vulkan 
    glfw 
)

# Copy shaders if we had any (placeholder)
# file(COPY shaders DESTINATION ${CMAKE_BINARY_DIR})
