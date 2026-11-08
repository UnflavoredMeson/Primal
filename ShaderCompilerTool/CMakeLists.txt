# Copyright (c) 2026 Charles Smith
# Distributed under the MIT license. See the LICENSE file in the project root for more information.

add_executable(ShaderCompilerTool Main.cpp)
target_link_libraries(ShaderCompilerTool PRIVATE ShaderCompiler)
# Next to EngineTest (x64/<Config>/): the tool resolves the shader paths relative to its own folder.
set_target_properties(ShaderCompilerTool PROPERTIES
    OUTPUT_NAME PrimalShaderCompiler
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/x64/$<CONFIG>"
)

# Compile the engine shaders on every build. The tool's own up-to-date check (source timestamps + shaders.bin.key)
# makes this a no-op when nothing changed; DXC errors fail the build.
add_custom_target(EngineShaders ALL
    COMMAND ShaderCompilerTool vulkan
    COMMENT "Compiling engine shaders (when changed)"
    VERBATIM
)
