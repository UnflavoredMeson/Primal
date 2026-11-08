// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "CommonHeaders.h"
#include "Graphics/Renderer.h"

struct shader_file_info
{
    const char*         file_name;
    const char*         function;
    primal::graphics::shader_type::type   type;
};

// Shader target of the platform's default graphics backend: DXIL for Direct3D 12, SPIR-V for Vulkan.
constexpr primal::graphics::graphics_platform default_shader_platform{
#if PRIMAL_PLATFORM_WINDOWS
    primal::graphics::graphics_platform::direct3d12
#else
    primal::graphics::graphics_platform::vulkan
#endif
};

std::unique_ptr<u8[]> compile_shader(shader_file_info info, u8* code, u32 code_size, primal::utl::vector<std::wstring>& extra_args,
                                     bool include_errors_and_disassembly = false,
                                     primal::graphics::graphics_platform platform = default_shader_platform);
std::unique_ptr<u8[]> compile_shader(shader_file_info info, const char* file_path, primal::utl::vector<std::wstring>& extra_args,
                                     bool include_errors_and_disassembly = false,
                                     primal::graphics::graphics_platform platform = default_shader_platform);
bool compile_shaders(primal::graphics::graphics_platform platform = default_shader_platform);

// Graphics backend to run on: the PRIMAL_GRAPHICS environment variable ("vulkan" or "d3d12"),
// else default_shader_platform. Unknown values are reported on stderr and ignored.
primal::graphics::graphics_platform selected_graphics_platform();