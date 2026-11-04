# Copyright (c) 2026 Charles Smith
# Distributed under the MIT license. See the LICENSE file in the project root for more information.

add_library(Engine STATIC)

# Platform-neutral sources.
target_sources(Engine PRIVATE
    Components/Entity.cpp
    Components/Geometry.cpp
    Components/Script.cpp
    Components/Transform.cpp
    Content/ContentToEngine.cpp
    Graphics/Renderer.cpp
    Input/Input.cpp
)

# NOTE: Windows-only sources are not part of the Linux build yet. Attempting to build as step 1

target_include_directories(Engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Common
)

target_link_libraries(Engine PUBLIC
    primal::build_settings
    primal::sal
    Microsoft::DirectXMath
)

# PIC so the engine can later be linked into a shared EngineDLL (libEngineDLL.so) for the editor.
set_target_properties(Engine PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME engine
)
