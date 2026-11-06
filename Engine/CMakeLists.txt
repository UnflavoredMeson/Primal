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
    Graphics/Renderer.cpp
    Input/Input.cpp
)

# Linux platform layer (SDL3). Window.cpp is compiled through PlatformLinux.cpp (IncludeWindowCpp.h).
target_sources(Engine PRIVATE
    Core/MainLinux.cpp
    Input/InputSDL.cpp
    Platform/PlatformLinux.cpp
)

target_include_directories(Engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Common
)

target_link_libraries(Engine PUBLIC
    primal::build_settings
    primal::sal
    Microsoft::DirectXMath
    SDL3::SDL3
)

# PIC so the engine can later be linked into a shared EngineDLL (libEngineDLL.so) for the editor.
set_target_properties(Engine PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME engine
)
