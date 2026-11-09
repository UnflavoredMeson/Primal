// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "D3D12CommonHeaders.h"

namespace primal::graphics
{
namespace hlsl
{
struct AmbientLightParameters;
}

namespace d3d12
{
struct D3D12FrameInfo;
}
}

namespace primal::graphics::d3d12::light
{

auto initialize() -> bool;
auto shutdown() -> void;

auto create_light_set(u64 light_set_key) -> void;
auto remove_light_set(u64 light_set_key) -> void;
auto create(LightInitInfo info) -> graphics::Light;
auto remove(LightId id, u64 light_set_key) -> void;
auto set_parameter(
    LightId id,
    u64 light_set_key,
    LightParameter::Parameter parameter,
    const void *const data,
    u32 data_size) -> void;
auto get_parameter(LightId id, u64 light_set_key, LightParameter::Parameter parameter, void *const data, u32 data_size)
    -> void;

auto update_light_buffers(const D3D12FrameInfo &d3d12_info) -> void;
auto non_cullable_light_buffer(u32 frame_index) -> D3D12_GPU_VIRTUAL_ADDRESS;
auto cullable_light_buffer(u32 frame_index) -> D3D12_GPU_VIRTUAL_ADDRESS;
auto culling_info_buffer(u32 frame_index) -> D3D12_GPU_VIRTUAL_ADDRESS;
auto bounding_spheres_buffer(u32 frame_index) -> D3D12_GPU_VIRTUAL_ADDRESS;
auto ambient_light(u64 light_set_key) -> hlsl::AmbientLightParameters;
auto non_cullable_light_count(u64 light_set_key) -> u32;
auto cullable_light_count(u64 light_set_key) -> u32;
}
