// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
// Port of Direct3D12/D3D12Light.h (Copyright (c) Arash Khatami). Only the light buffers differ (see
// vulkan_light_buffer).
#pragma once
#include "VulkanCommonHeaders.h"
#include "VulkanHelpers.h"

namespace primal::graphics
{
namespace hlsl
{
struct AmbientLightParameters;
}

namespace vulkan
{
struct VulkanFrameInfo;
}
}

namespace primal::graphics::vulkan::light
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

auto update_light_buffers(const VulkanFrameInfo &vulkan_info) -> void;
[[nodiscard]] auto non_cullable_light_buffer(u32 frame_index) -> vkx::BufferRange;
[[nodiscard]] auto cullable_light_buffer(u32 frame_index) -> vkx::BufferRange;
[[nodiscard]] auto culling_info_buffer(u32 frame_index) -> vkx::BufferRange;
[[nodiscard]] auto bounding_spheres_buffer(u32 frame_index) -> vkx::BufferRange;
auto ambient_light(u64 light_set_key) -> hlsl::AmbientLightParameters;
auto non_cullable_light_count(u64 light_set_key) -> u32;
auto cullable_light_count(u64 light_set_key) -> u32;
}
