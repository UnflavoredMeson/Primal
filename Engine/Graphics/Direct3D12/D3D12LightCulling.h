// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "D3D12CommonHeaders.h"
#include "D3D12Helpers.h"

namespace primal::graphics::d3d12
{
struct D3D12FrameInfo;
}

namespace primal::graphics::d3d12::delight
{

constexpr u32 light_culling_tile_size{32};

auto initialize() -> bool;
auto shutdown() -> void;

[[nodiscard]] auto add_culler() -> id::id_type;
auto remove_culler(id::id_type id) -> void;

auto cull_lights(
    id3d12_graphics_command_list *const cmd_list,
    const D3D12FrameInfo &d3d12_info,
    d3dx::D3D12ResourceBarrier &barriers) -> void;

// TODO: temporary for visualizing light culling. Remove later.
auto frustums(id::id_type light_culling_id, u32 frame_index) -> D3D12_GPU_VIRTUAL_ADDRESS;
auto light_grid_opaque(id::id_type light_culling_id, u32 frame_index) -> D3D12_GPU_VIRTUAL_ADDRESS;
auto light_index_list_opaque(id::id_type light_culling_id, u32 frame_index) -> D3D12_GPU_VIRTUAL_ADDRESS;
}
