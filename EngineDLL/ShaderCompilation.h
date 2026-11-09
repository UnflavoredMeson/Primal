// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "CommonHeaders.h"
#include "Graphics/Renderer.h"

struct ShaderFileInfo
{
    const char *file_name;
    const char *function;
    primal::graphics::ShaderType::Type type;
};

// Shader target of the platform's default graphics backend: DXIL for Direct3D 12, SPIR-V for Vulkan.
constexpr primal::graphics::GraphicsPlatform default_shader_platform{
#if PRIMAL_PLATFORM_WINDOWS
    primal::graphics::GraphicsPlatform::direct3d12
#else
    primal::graphics::GraphicsPlatform::vulkan
#endif
};

auto compile_shader(
    ShaderFileInfo info,
    u8 *code,
    u32 code_size,
    primal::utl::vector<std::wstring> &extra_args,
    bool include_errors_and_disassembly = false,
    primal::graphics::GraphicsPlatform platform = default_shader_platform) -> std::unique_ptr<u8[]>;
auto compile_shader(
    ShaderFileInfo info,
    const char *file_path,
    primal::utl::vector<std::wstring> &extra_args,
    bool include_errors_and_disassembly = false,
    primal::graphics::GraphicsPlatform platform = default_shader_platform) -> std::unique_ptr<u8[]>;
auto compile_shaders(primal::graphics::GraphicsPlatform platform = default_shader_platform) -> bool;

// Graphics backend to run on: the PRIMAL_GRAPHICS environment variable ("vulkan" or "d3d12"),
// else default_shader_platform. Unknown values are reported on stderr and ignored.
auto selected_graphics_platform() -> primal::graphics::GraphicsPlatform;
