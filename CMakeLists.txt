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

# Vulkan Memory Allocator (VMA)
FetchContent_Declare(
    VulkanMemoryAllocator
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG        v3.0.1
)
FetchContent_MakeAvailable(VulkanMemoryAllocator)

# Jolt Physics
set(CMAKE_DISABLE_PRECOMPILE_HEADERS ON) # Fix MinGW ICE
FetchContent_Declare(
    JoltPhysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG        v5.0.0
    SOURCE_SUBDIR  Build
)
set(USE_SSE4_2 ON CACHE BOOL "" FORCE) # Enable SSE4.2 for Jolt
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(JoltPhysics)

# meshoptimizer
FetchContent_Declare(
    meshoptimizer
    GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git
    GIT_TAG        v0.20
)
set(MESHOPT_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(meshoptimizer)

# Shaderc (Executable approach)
set(VK_SDK_PATH $ENV{VULKAN_SDK})
find_program(GLSLC_EXECUTABLE 
    NAMES glslc 
    PATHS ${VK_SDK_PATH}/Bin ${VK_SDK_PATH}/bin
)

if (NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found! Please ensure VULKAN_SDK is set correctly.")
endif()

# Escape backslashes for C++ string literal
string(REPLACE "\\" "\\\\" GLSLC_PATH_ESCAPED "${GLSLC_EXECUTABLE}")
add_compile_definitions(GLSLC_PATH="${GLSLC_PATH_ESCAPED}")

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
    src/engine/Skybox.cpp
    src/engine/ShaderCompiler.cpp
    src/engine/DescriptorManager.cpp
    src/engine/AccelerationStructureBuilder.cpp
    src/engine/RTPipeline.cpp
)

# --- Executable ---
add_executable(SanicEngine ${SOURCES})

# --- Include Directories ---
target_include_directories(SanicEngine PRIVATE 
    src 
    ${Vulkan_INCLUDE_DIRS}
    ${glfw_SOURCE_DIR}/include
    ${glm_SOURCE_DIR}
    ${VulkanMemoryAllocator_SOURCE_DIR}/include
    ${JoltPhysics_SOURCE_DIR}
    ${meshoptimizer_SOURCE_DIR}/src
)

# --- Linking ---
add_compile_definitions(GLM_FORCE_RADIANS GLM_FORCE_DEPTH_ZERO_TO_ONE)
# Jolt defines
target_compile_definitions(SanicEngine PRIVATE JPH_PROFILE_ENABLED JPH_DEBUG_RENDERER)

target_link_libraries(SanicEngine PRIVATE 
    Vulkan::Vulkan 
    glfw 
    Jolt
    meshoptimizer
)

# Copy shaders if we had any (placeholder)
# file(COPY shaders DESTINATION ${CMAKE_BINARY_DIR})
