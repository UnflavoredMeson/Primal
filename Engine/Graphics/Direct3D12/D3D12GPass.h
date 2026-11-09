// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once

#include "D3D12CommonHeaders.h"

namespace primal::graphics::d3d12
{
struct D3D12FrameInfo;
}
namespace primal::graphics::d3d12::gpass
{

constexpr DXGI_FORMAT main_buffer_format{DXGI_FORMAT_R16G16B16A16_FLOAT};
constexpr DXGI_FORMAT depth_buffer_format{DXGI_FORMAT_D32_FLOAT};

struct OpaqueRootParameter
{
    enum Parameter : u32
    {
        global_shader_data,
        per_object_data,
        position_buffer,
        element_buffer,
        material_data,
        directional_lights,
        cullable_lights,
        light_grid,
        light_index_list,

        count
    };
};

struct DrawIndexedIndirectCommand
{
    union
    {
        struct
        {
            D3D12_GPU_VIRTUAL_ADDRESS parameters[OpaqueRootParameter::count];
        } opaque;
    };

    D3D12_INDEX_BUFFER_VIEW index_buffer_view;
    D3D12_DRAW_INDEXED_ARGUMENTS draw_indexed_args;
};

auto initialize() -> bool;
auto shutdown() -> void;

[[nodiscard]] auto main_buffer() -> const D3D12RenderTexture &;
[[nodiscard]] auto depth_buffer() -> const D3D12DepthBuffer &;

// NOTE: call this every frame before rendering anything in gpass.
auto set_size(math::u32v2 size) -> void;
auto depth_prepass(id3d12_graphics_command_list *cmd_list, const D3D12FrameInfo &d3d12_info) -> void;
auto render(id3d12_graphics_command_list *cmd_list, const D3D12FrameInfo &d3d12_info) -> void;
auto depth_prepass_indirect(id3d12_graphics_command_list *cmd_list, const D3D12FrameInfo &d3d12_info) -> void;
auto render_indirect(id3d12_graphics_command_list *cmd_list, const D3D12FrameInfo &d3d12_info) -> void;

auto add_transitions_for_depth_prepass(d3dx::D3D12ResourceBarrier &barriers) -> void;
auto add_transitions_for_gpass(d3dx::D3D12ResourceBarrier &barriers) -> void;
auto add_transitions_for_post_process(d3dx::D3D12ResourceBarrier &barriers) -> void;

auto set_render_targets_for_depth_prepass(id3d12_graphics_command_list *cmd_list) -> void;
auto set_render_targets_for_gpass(id3d12_graphics_command_list *cmd_list) -> void;
}
