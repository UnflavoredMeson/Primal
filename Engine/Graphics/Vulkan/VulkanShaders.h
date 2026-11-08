// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "VulkanCommonHeaders.h"
#include "VulkanHelpers.h"

// Engine shaders (counterpart of D3D12Shaders.h). Loaded from ./shaders/vulkan/shaders.bin, which
// EngineDLL/ShaderCompilation.cpp writes with compile_shaders(graphics_platform::vulkan).
namespace primal::graphics::vulkan::shaders {

// Same order as engine_shader in ShaderCompilation.cpp and D3D12Shaders.h (the blob is in this order).
struct engine_shader
{
    enum id : u32
    {
        fullscreen_triangle_vs = 0,
        post_process_ps,
        grid_frustums_cs,
        light_culling_cs,

        count
    };
};

bool initialize();
void shutdown();

// SPIR-V code + entry point ("main" for every shader, see vkx::spirv_entry_point).
[[nodiscard]] vkx::shader_bytecode get_engine_shader(engine_shader::id id);

} // namespace primal::graphics::vulkan::shaders
