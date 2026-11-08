// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "VulkanCommonHeaders.h"
#include "VulkanHelpers.h"

namespace primal::graphics::vulkan {
struct vulkan_frame_info;
}

// Tiled light culling (counterpart of D3D12LightCulling.h): a compute pass computes the frustum of each screen
// tile (when the view size or field of view changes), another one culls the cullable lights against those
// frustums and the depth buffer, writing a light grid + light index list that the gpass pixel shaders read.
namespace primal::graphics::vulkan::delight {

constexpr u32 light_culling_tile_size{ 32 };

bool initialize();
void shutdown();

[[nodiscard]] id::id_type add_culler();
void remove_culler(id::id_type id);

// Records the culling dispatches. Applies the barriers collected so far (the gpass transitions included, like
// D3D12) and adds the transitions of the light grid / index list for the gpass, which the caller applies.
void cull_lights(VkCommandBuffer cmd, const vulkan_frame_info& vulkan_info, vkx::vulkan_barrier& barriers);

// TODO: temporary for visualizing light culling. Remove later.
[[nodiscard]] vkx::buffer_range frustums(id::id_type light_culling_id, u32 frame_index);
[[nodiscard]] vkx::buffer_range light_grid_opaque(id::id_type light_culling_id, u32 frame_index);
[[nodiscard]] vkx::buffer_range light_index_list_opaque(id::id_type light_culling_id, u32 frame_index);
} // namespace primal::graphics::vulkan::delight
