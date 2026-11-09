// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "D3D12CommonHeaders.h"

namespace primal::graphics::d3d12::shaders
{

struct EngineShader
{
    enum Id : u32
    {
        fullscreen_triangle_vs = 0,
        post_process_ps,
        grid_frustums_cs,
        light_culling_cs,

        count
    };
};

auto initialize() -> bool;
auto shutdown() -> void;

auto get_engine_shader(EngineShader::Id id) -> D3D12_SHADER_BYTECODE;
}
