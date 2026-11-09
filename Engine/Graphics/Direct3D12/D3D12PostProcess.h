// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "D3D12CommonHeaders.h"

namespace primal::graphics::d3d12
{
struct D3D12FrameInfo;
}

namespace primal::graphics::d3d12::fx
{

auto initialize() -> bool;
auto shutdown() -> void;

auto post_process(
    id3d12_graphics_command_list *cmd_list,
    const D3D12FrameInfo &d3d12_info,
    D3D12_CPU_DESCRIPTOR_HANDLE target_rtv) -> void;

}
