// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "D3D12CommonHeaders.h"
#include "D3D12Resources.h"

#include <span>

namespace primal::graphics::d3d12
{

namespace camera
{
class D3D12Camera;
}

struct D3D12FrameInfo
{
    const FrameInfo *info{nullptr};
    camera::D3D12Camera *camera{nullptr};
    D3D12_GPU_VIRTUAL_ADDRESS global_shader_data{0};
    u32 surface_width{0};
    u32 surface_height{0};
    id::id_type light_culling_id{id::invalid_id};
    u32 frame_index{0};
};
}

namespace primal::graphics::d3d12::core
{

auto initialize() -> bool;
auto shutdown() -> void;

template <typename T>
constexpr auto release(T *&resource) -> void
{
    if (resource)
    {
        resource->Release();
        resource = nullptr;
    }
}

namespace detail
{
auto deferred_release(IUnknown *resource) -> void;
}

template <typename T>
constexpr auto deferred_release(T *&resource) -> void
{
    if (resource)
    {
        detail::deferred_release(resource);
        resource = nullptr;
    }
}
auto set_option(RendererOption::Option option, const void *const parameter, u32 parameter_size) -> void;
auto get_option(RendererOption::Option option, void *const parameter, u32 parameter_size) -> void;

[[nodiscard]] auto device() -> id3d12_device *;
[[nodiscard]] auto rtv_heap() -> DescriptorHeap &;
[[nodiscard]] auto dsv_heap() -> DescriptorHeap &;
[[nodiscard]] auto srv_heap() -> DescriptorHeap &;
[[nodiscard]] auto uav_heap() -> DescriptorHeap &;
[[nodiscard]] auto cbuffer() -> ConstantBuffer &;
[[nodiscard]] auto current_frame_index() -> u32;
auto set_deferred_releases_flag(u32 frame_idx) -> void;

[[nodiscard]] auto allow_tearing() -> bool;
[[nodiscard]] auto vsync_enabled() -> bool;
[[nodiscard]] auto create_surface(platform::Window window) -> Surface;
auto remove_surface(SurfaceId id) -> void;
auto resize_surface(SurfaceId id, u32, u32) -> void;
[[nodiscard]] auto surface_width(SurfaceId id) -> u32;
[[nodiscard]] auto surface_height(SurfaceId id) -> u32;
auto render_surface(SurfaceId id, FrameInfo info) -> void;

// Parallel recording: jobs record draws into command lists of their own (one allocator
// per thread and frame), which run between two parts of the frame's command list.
// - prepare_thread_command_lists(): on the recording thread before it starts the jobs;
// - begin_thread_command_list(): from a job; a list of the calling thread, reset and recording, with the descriptor
//   heaps, viewport and scissor of the surface being rendered set. The job closes it;
// - execute_thread_command_lists(): on the recording thread after the jobs; closes the frame's command list, queues
//   it and then these lists, and opens a new one (with descriptor heaps, viewport and scissor set again).
auto prepare_thread_command_lists() -> void;
[[nodiscard]] auto begin_thread_command_list() -> id3d12_graphics_command_list *;
auto execute_thread_command_lists(std::span<ID3D12CommandList *const> lists) -> void;
// The frame's command list being recorded (changes in execute_thread_command_lists()).
[[nodiscard]] auto command_list() -> id3d12_graphics_command_list *;
// PRIMAL_D3D12_PARALLEL_RECORDING=0 records every draw on the rendering thread.
[[nodiscard]] auto parallel_recording_enabled() -> bool;
}
