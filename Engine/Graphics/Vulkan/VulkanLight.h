// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
// Port of Direct3D12/D3D12Light.h (Copyright (c) Arash Khatami). Only the light buffers differ (see vulkan_light_buffer).
#pragma once
#include "VulkanCommonHeaders.h"
#include "VulkanHelpers.h"

namespace primal::graphics::vulkan {
struct vulkan_frame_info;

namespace hlsl {
struct AmbientLightParameters;
}
}

namespace primal::graphics::vulkan::light {

bool initialize();
void shutdown();

void create_light_set(u64 light_set_key);
void remove_light_set(u64 light_set_key);
graphics::light create(light_init_info info);
void remove(light_id id, u64 light_set_key);
void set_parameter(light_id id, u64 light_set_key, light_parameter::parameter parameter, const void *const data, u32 data_size);
void get_parameter(light_id id, u64 light_set_key, light_parameter::parameter parameter, void *const data, u32 data_size);

void update_light_buffers(const vulkan_frame_info& vulkan_info);
[[nodiscard]] vkx::buffer_range non_cullable_light_buffer(u32 frame_index);
[[nodiscard]] vkx::buffer_range cullable_light_buffer(u32 frame_index);
[[nodiscard]] vkx::buffer_range culling_info_buffer(u32 frame_index);
[[nodiscard]] vkx::buffer_range bounding_spheres_buffer(u32 frame_index);
hlsl::AmbientLightParameters ambient_light(u64 light_set_key);
u32 non_cullable_light_count(u64 light_set_key);
u32 cullable_light_count(u64 light_set_key);
}