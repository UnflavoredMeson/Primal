# Copyright (c) 2026 Charles Smith
# Distributed under the MIT license. See the LICENSE file in the project root for more information.

add_library(Engine STATIC)

# Platform-neutral sources.
target_sources(Engine PRIVATE
    Components/Entity.cpp
    Components/Geometry.cpp
    Components/Script.cpp
    Components/Transform.cpp
    Content/ContentLoader.cpp
    Content/ContentToEngine.cpp
    Core/Engine.cpp
    Core/Jobs.cpp
    Core/Log.cpp
    Graphics/Renderer.cpp
    Input/Input.cpp
)

# Named modules (import primal.jobs;). A new module's interface unit goes here; its .cpp goes above.
target_sources(Engine PUBLIC FILE_SET CXX_MODULES BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} FILES
    Core/Jobs.ixx
)

# Linux platform layer (SDL3). Window.cpp is compiled through PlatformLinux.cpp (IncludeWindowCpp.h).
target_sources(Engine PRIVATE
    Core/MainLinux.cpp
    Input/InputSDL.cpp
    Platform/PlatformLinux.cpp
)

# Vulkan backend (PRIMAL_GRAPHICS_VULKAN, on by default on Linux; see Common/PlatformDetection.h).
target_sources(Engine PRIVATE
    Graphics/Vulkan/VulkanCamera.cpp
    Graphics/Vulkan/VulkanContent.cpp
    Graphics/Vulkan/VulkanCore.cpp
    Graphics/Vulkan/VulkanGPass.cpp
    Graphics/Vulkan/VulkanHelpers.cpp
    Graphics/Vulkan/VulkanInterface.cpp
    Graphics/Vulkan/VulkanLight.cpp
    Graphics/Vulkan/VulkanLightCulling.cpp
    Graphics/Vulkan/VulkanMemoryAllocator.cpp
    Graphics/Vulkan/VulkanPostProcess.cpp
    Graphics/Vulkan/VulkanResources.cpp
    Graphics/Vulkan/VulkanShaders.cpp
    Graphics/Vulkan/VulkanSurface.cpp
    Graphics/Vulkan/VulkanUpload.cpp
)
# Vulkan Memory Allocator, vendored (same version on Linux and Windows). SYSTEM: no warnings from it.
target_include_directories(Engine SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/vma)

target_include_directories(Engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Common
)

target_link_libraries(Engine PUBLIC
    primal::build_settings
    primal::sal
    Microsoft::DirectXMath
    SDL3::SDL3
    Vulkan::Vulkan
)

# Shader compiler (HLSL -> DXIL/SPIR-V via DXC). Used by ShaderCompilerTool, EngineTest and, later, a Linux EngineDLL.
# NOTE: on Windows the .vcxproj files compile EngineDLL/ShaderCompilation.cpp directly.
find_library(DXCOMPILER_LIBRARY NAMES dxcompiler REQUIRED)
add_library(ShaderCompiler STATIC ${PROJECT_SOURCE_DIR}/EngineDLL/ShaderCompilation.cpp)
target_include_directories(ShaderCompiler PUBLIC ${PROJECT_SOURCE_DIR}/EngineDLL)
target_link_libraries(ShaderCompiler PUBLIC Engine ${DXCOMPILER_LIBRARY})
set_target_properties(ShaderCompiler PROPERTIES POSITION_INDEPENDENT_CODE ON)

# PIC so the engine can later be linked into a shared EngineDLL (libEngineDLL.so) for the editor.
set_target_properties(Engine PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME engine
)
