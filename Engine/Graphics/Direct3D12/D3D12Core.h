// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "D3D12CommonHeaders.h"
#include "D3D12Resources.h"

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
}
