cmake_minimum_required(VERSION 3.20)
project(SanicEngine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Include FetchContent early ---
include(FetchContent)

# --- Build Options ---
option(SANIC_BUILD_EDITOR "Build the editor" OFF)  # Disabled - needs material/shader system fixes
option(SANIC_ENABLE_D3D12 "Enable DirectX 12 backend" OFF)  # Can enable with MSVC
option(SANIC_ENABLE_VULKAN "Enable Vulkan backend" ON)

# --- Compiler Detection ---
if(MSVC)
    message(STATUS "Compiler: MSVC - Full compatibility with Jolt Physics and D3D12")
    set(SANIC_USING_MSVC TRUE)
    # MSVC-specific flags for better performance
    add_compile_options(/MP)  # Multi-processor compilation
    add_compile_options(/W3)  # Warning level 3
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(STATUS "Compiler: GCC/MinGW - Some features may have compatibility issues")
    set(SANIC_USING_MINGW TRUE)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(STATUS "Compiler: Clang")
    set(SANIC_USING_CLANG TRUE)
endif()

# --- Platform Detection ---
if(WIN32)
    message(STATUS "Platform: Windows - D3D12 and Vulkan available")
    set(SANIC_PLATFORM_WINDOWS TRUE)
else()
    message(STATUS "Platform: Non-Windows - Vulkan only")
    set(SANIC_ENABLE_D3D12 OFF CACHE BOOL "" FORCE)
endif()

# --- Vulkan Setup ---
if(SANIC_ENABLE_VULKAN)
    # Try to find Vulkan via standard package
    find_package(Vulkan REQUIRED)

    if (Vulkan_FOUND)
        message(STATUS "Vulkan SDK found: ${Vulkan_INCLUDE_DIRS}")
    else()
        message(FATAL_ERROR "Vulkan SDK not found! Please set VK_SDK_PATH.")
    endif()
endif()

# --- DirectX 12 Setup (Windows only) ---
if(SANIC_ENABLE_D3D12 AND WIN32)
    message(STATUS "DirectX 12 backend enabled")
    
    # D3D12 Memory Allocator (similar to VMA for Vulkan)
    FetchContent_Declare(
        D3D12MemoryAllocator
        GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator.git
        GIT_TAG        v2.0.1
    )
    FetchContent_GetProperties(D3D12MemoryAllocator)
    if(NOT d3d12memoryallocator_POPULATED)
        FetchContent_Populate(D3D12MemoryAllocator)
    endif()
    
    # DirectX Headers (for latest D3D12 features)
    FetchContent_Declare(
        DirectX-Headers
        GIT_REPOSITORY https://github.com/microsoft/DirectX-Headers.git
        GIT_TAG        v1.613.1
    )
    FetchContent_MakeAvailable(DirectX-Headers)
endif()

# --- Dependencies (GLFW & GLM) ---

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
# Note: MinGW has ICE (Internal Compiler Error) issues with precompiled headers
# This is only needed for MinGW builds
if(SANIC_USING_MINGW)
    set(CMAKE_DISABLE_PRECOMPILE_HEADERS ON)
    message(STATUS "Jolt: Disabling precompiled headers for MinGW compatibility")
endif()

# Configure Jolt to use dynamic runtime on MSVC (to match our project)
if(MSVC)
    set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
endif()

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

# ImGui with Docking
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        docking
)
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
endif()

# ImGuizmo for transform gizmos
FetchContent_Declare(
    imguizmo
    GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
    GIT_TAG        master
)
FetchContent_GetProperties(imguizmo)
if(NOT imguizmo_POPULATED)
    FetchContent_Populate(imguizmo)
endif()

# Native File Dialog Extended
FetchContent_Declare(
    nfd
    GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended.git
    GIT_TAG        v1.1.1
)
set(NFD_PORTAL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nfd)

# .NET CoreCLR Scripting (loaded at runtime via hostfxr)
# No compile-time dependency needed - runtime loads hostfxr.dll dynamically
# Requires .NET 8 SDK to be installed for script compilation
message(STATUS "C# Scripting: .NET CoreCLR will be loaded at runtime")

# Shaderc library (for runtime shader compilation)
# Try finding shaderc from Vulkan SDK first, then as a system library
set(VK_SDK_PATH $ENV{VULKAN_SDK})
find_program(GLSLC_EXECUTABLE 
    NAMES glslc 
    PATHS ${VK_SDK_PATH}/Bin ${VK_SDK_PATH}/bin
)

if (NOT GLSLC_EXECUTABLE)
    message(WARNING "glslc executable not found - subprocess compilation unavailable")
endif()

# Find shaderc library for direct integration
find_library(SHADERC_LIB 
    NAMES shaderc_combined shaderc
    PATHS ${VK_SDK_PATH}/Lib ${VK_SDK_PATH}/lib
)

if(SHADERC_LIB)
    message(STATUS "Found shaderc library: ${SHADERC_LIB}")
else()
    message(WARNING "shaderc library not found - will use executable fallback")
endif()

# SPIRV-Reflect for shader reflection
FetchContent_Declare(
    spirv-reflect
    GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Reflect.git
    GIT_TAG        vulkan-sdk-1.3.268.0
)
FetchContent_GetProperties(spirv-reflect)
if(NOT spirv-reflect_POPULATED)
    FetchContent_Populate(spirv-reflect)
    # Build SPIRV-Reflect as a library
    add_library(spirv-reflect STATIC
        ${spirv-reflect_SOURCE_DIR}/spirv_reflect.c
    )
    target_include_directories(spirv-reflect PUBLIC ${spirv-reflect_SOURCE_DIR})
endif()

# imnodes for node graph editing
FetchContent_Declare(
    imnodes
    GIT_REPOSITORY https://github.com/Nelarius/imnodes.git
    GIT_TAG        master
)
FetchContent_GetProperties(imnodes)
if(NOT imnodes_POPULATED)
    FetchContent_Populate(imnodes)
endif()

# nlohmann/json for serialization
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

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
    src/engine/VulkanContext.cpp
    src/engine/ShadowRenderer.cpp
    src/engine/DeferredRenderer.cpp
    src/engine/DDGISystem.cpp
    src/engine/SSRSystem.cpp
    src/engine/HiZBuffer.cpp
    src/engine/SSRDenoiser.cpp
    src/engine/VisBufferRenderer.cpp
    src/engine/MeshletStreamer.cpp
    src/engine/SurfaceCacheManager.cpp
    src/engine/PhysicsSystem.cpp
    src/engine/VirtualShadowMap.cpp
    src/engine/ClusterHierarchy.cpp
    src/engine/ClusterCullingPipeline.cpp
    src/engine/HZBPipeline.cpp
    src/engine/IndirectDrawPipeline.cpp
    src/engine/SoftwareRasterizerPipeline.cpp
    src/engine/MaterialSystem.cpp
    src/engine/TemporalSystem.cpp
    src/engine/SurfaceCache.cpp
    src/engine/ScreenSpaceTracing.cpp
    src/engine/SDFGenerator.cpp
    src/engine/ScreenProbes.cpp
    src/engine/RadianceCache.cpp
    src/engine/GlobalIllumination.cpp
    src/engine/VirtualShadowMaps.cpp
    src/engine/RayTracedShadows.cpp
    src/engine/PostProcess.cpp
    src/engine/FinalRenderer.cpp
    src/engine/AssetCooker.cpp
    src/engine/AssetLoader.cpp
    src/engine/Animation.cpp
    src/engine/ECS.cpp
    src/engine/AudioSystem.cpp
    src/engine/UISystem.cpp
    src/engine/ParticleSystem.cpp
    src/engine/SceneSerializer.cpp
    src/engine/VolumetricLighting.cpp
    src/engine/TextureStreamer.cpp
    src/engine/ScriptingSystem.cpp
    # Shader System
    src/engine/ShaderManager.cpp
    src/engine/shaders/ShaderCache.cpp
    src/engine/shaders/ShaderIncluder.cpp
    src/engine/shaders/ShaderPermutation.cpp
    src/engine/shaders/ShaderReflection.cpp
    src/engine/shaders/ShaderHotReload.cpp
    src/engine/shaders/ShaderCompilerNew.cpp
    src/engine/shaders/ShaderLibrary.cpp
    # Material System - DISABLED: Requires significant API harmonization
    # Issues remaining after partial fixes:
    # 1. MaterialEditor.cpp: Missing imgui include paths
    # 2. MaterialGraph.cpp: Uses instance() vs getInstance(), areTypesCompatible wrong scope
    # 3. MaterialPin: Missing 'optional' field
    # 4. MaterialSerializer: Missing serializeNode/deserializeNode static methods
    # 5. MaterialCompiler.cpp: ShaderCompiler incomplete type (needs full include, not forward decl)
    # Already fixed: addInputPin/addOutputPin wrappers, REGISTER_MATERIAL_NODE_AUTO macro
    # src/engine/materials/MaterialNode.cpp
    # src/engine/materials/MaterialNodes/CommonNodes.cpp
    # src/engine/materials/MaterialGraph.cpp
    # src/engine/materials/MaterialCompiler.cpp
    # src/engine/materials/MaterialEditor.cpp
    # RHI Abstraction Layer
    src/engine/rhi/RHI.cpp
)

# RHI Backend Sources (conditionally added)
if(SANIC_ENABLE_VULKAN)
    list(APPEND SOURCES
        src/engine/rhi/vulkan/VulkanRHI.cpp
        src/engine/rhi/vulkan/VulkanResources.cpp
        src/engine/rhi/vulkan/VulkanCommandList.cpp
        src/engine/rhi/vulkan/VulkanConversions.cpp
    )
endif()

# D3D12 Backend (disabled due to MinGW compatibility issues)
# if(SANIC_ENABLE_D3D12 AND WIN32)
#     list(APPEND SOURCES
#         src/engine/rhi/d3d12/D3D12RHI.cpp
#         src/engine/rhi/d3d12/D3D12Resources.cpp
#         src/engine/rhi/d3d12/D3D12CommandList.cpp
#         src/engine/rhi/d3d12/D3D12Conversions.cpp
#     )
# endif()

# --- Engine Library ---
# Build engine as a library so it can be shared between targets
add_library(SanicEngineLib STATIC ${SOURCES})

target_include_directories(SanicEngineLib PRIVATE 
    src 
    ${Vulkan_INCLUDE_DIRS}
    ${glfw_SOURCE_DIR}/include
    ${glm_SOURCE_DIR}
    ${VulkanMemoryAllocator_SOURCE_DIR}/include
    ${JoltPhysics_SOURCE_DIR}/..
    ${meshoptimizer_SOURCE_DIR}/src
    ${spirv-reflect_SOURCE_DIR}
    ${imgui_SOURCE_DIR}
    ${imnodes_SOURCE_DIR}
    $<$<BOOL:${SANIC_ENABLE_D3D12}>:${d3d12memoryallocator_SOURCE_DIR}/include>
)

target_compile_definitions(SanicEngineLib PRIVATE 
    GLM_FORCE_RADIANS 
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    JPH_PROFILE_ENABLED 
    JPH_DEBUG_RENDERER
    $<$<BOOL:${SANIC_ENABLE_VULKAN}>:SANIC_ENABLE_VULKAN>
    $<$<BOOL:${SANIC_ENABLE_D3D12}>:SANIC_ENABLE_D3D12>
    $<$<BOOL:${SHADERC_LIB}>:SANIC_HAS_SHADERC_LIB>
)

target_link_libraries(SanicEngineLib PRIVATE 
    glfw 
    Jolt
    meshoptimizer
    spirv-reflect
    nlohmann_json::nlohmann_json
    $<$<BOOL:${SHADERC_LIB}>:${SHADERC_LIB}>
)

# Vulkan linkage
if(SANIC_ENABLE_VULKAN)
    target_link_libraries(SanicEngineLib PRIVATE Vulkan::Vulkan)
endif()

# D3D12 linkage (Windows only)
if(SANIC_ENABLE_D3D12 AND WIN32)
    target_include_directories(SanicEngineLib PRIVATE
        ${d3d12memoryallocator_SOURCE_DIR}/include
    )
    target_link_libraries(SanicEngineLib PRIVATE
        d3d12.lib
        dxgi.lib
        dxguid.lib
        d3dcompiler.lib
        Microsoft::DirectX-Headers
    )
endif()

# --- Main Executable ---
add_executable(SanicEngine src/main.cpp)

target_include_directories(SanicEngine PRIVATE 
    src 
    ${Vulkan_INCLUDE_DIRS}
    ${glfw_SOURCE_DIR}/include
    ${glm_SOURCE_DIR}
    ${VulkanMemoryAllocator_SOURCE_DIR}/include
    ${JoltPhysics_SOURCE_DIR}/..
    ${meshoptimizer_SOURCE_DIR}/src
)

target_compile_definitions(SanicEngine PRIVATE 
    GLM_FORCE_RADIANS 
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    JPH_PROFILE_ENABLED 
    JPH_DEBUG_RENDERER
)

target_link_libraries(SanicEngine PRIVATE SanicEngineLib)

# --- Asset Cooker Tool ---
add_executable(sanic_cooker src/AssetCookerTool.cpp)

target_include_directories(sanic_cooker PRIVATE 
    src 
    ${Vulkan_INCLUDE_DIRS}
    ${glfw_SOURCE_DIR}/include
    ${glm_SOURCE_DIR}
    ${VulkanMemoryAllocator_SOURCE_DIR}/include
    ${JoltPhysics_SOURCE_DIR}/..
    ${meshoptimizer_SOURCE_DIR}/src
)

target_compile_definitions(sanic_cooker PRIVATE 
    GLM_FORCE_RADIANS 
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    JPH_PROFILE_ENABLED 
    JPH_DEBUG_RENDERER
)

target_link_libraries(sanic_cooker PRIVATE SanicEngineLib)

# --- Editor (ImGui-based) ---
if(SANIC_BUILD_EDITOR)
    message(STATUS "Building Sanic Editor with ImGui docking branch")
    
    # ImGui library
    set(IMGUI_SOURCES
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
    )
    
    # ImGuizmo sources
    set(IMGUIZMO_SOURCES
        ${imguizmo_SOURCE_DIR}/ImGuizmo.cpp
        ${imguizmo_SOURCE_DIR}/ImCurveEdit.cpp
        ${imguizmo_SOURCE_DIR}/ImGradient.cpp
        ${imguizmo_SOURCE_DIR}/ImSequencer.cpp
        ${imguizmo_SOURCE_DIR}/GraphEditor.cpp
    )
    
    # imnodes sources (for material node editor)
    set(IMNODES_SOURCES
        ${imnodes_SOURCE_DIR}/imnodes.cpp
    )
    
    # Editor source files
    set(EDITOR_SOURCES
        src/editor/Editor.cpp
        src/editor/EditorWindow.cpp
        src/editor/EditorLayout.cpp
        src/editor/core/Selection.cpp
        src/editor/core/UndoSystem.cpp
        src/editor/core/Commands.cpp
        src/editor/core/Shortcuts.cpp
        src/editor/viewport/Viewport.cpp
        src/editor/viewport/ViewportCamera.cpp
        src/editor/viewport/Gizmo.cpp
        src/editor/viewport/Grid.cpp
        src/editor/panels/HierarchyPanel.cpp
        src/editor/panels/InspectorPanel.cpp
        src/editor/panels/AssetBrowser.cpp
        src/editor/panels/ConsolePanel.cpp
        src/editor/panels/Menubar.cpp
        src/editor/panels/Toolbar.cpp
        src/editor/widgets/PropertyWidgets.cpp
        src/editor/preview/PreviewRenderer.cpp
        src/editor/main_editor.cpp
    )
    
    add_executable(SanicEditor ${EDITOR_SOURCES} ${IMGUI_SOURCES} ${IMGUIZMO_SOURCES} ${IMNODES_SOURCES})
    
    target_include_directories(SanicEditor PRIVATE
        src
        ${Vulkan_INCLUDE_DIRS}
        ${glfw_SOURCE_DIR}/include
        ${glm_SOURCE_DIR}
        ${VulkanMemoryAllocator_SOURCE_DIR}/include
        ${JoltPhysics_SOURCE_DIR}/..
        ${meshoptimizer_SOURCE_DIR}/src
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
        ${imguizmo_SOURCE_DIR}
        ${imnodes_SOURCE_DIR}
        ${spirv-reflect_SOURCE_DIR}
    )
    
    target_compile_definitions(SanicEditor PRIVATE
        GLM_FORCE_RADIANS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
        JPH_PROFILE_ENABLED
        JPH_DEBUG_RENDERER
        SANIC_EDITOR
        $<$<BOOL:${SHADERC_LIB}>:SANIC_HAS_SHADERC_LIB>
    )
    
    target_link_libraries(SanicEditor PRIVATE
        SanicEngineLib
        nfd
        spirv-reflect
        nlohmann_json::nlohmann_json
        $<$<BOOL:${SHADERC_LIB}>:${SHADERC_LIB}>
    )
endif()

# Copy shaders if we had any (placeholder)
# file(COPY shaders DESTINATION ${CMAKE_BINARY_DIR})
