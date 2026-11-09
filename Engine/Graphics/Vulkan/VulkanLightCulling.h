// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "VulkanCommonHeaders.h"
#include "VulkanHelpers.h"

namespace primal::graphics::vulkan
{
struct VulkanFrameInfo;
}

// Tiled light culling (counterpart of D3D12LightCulling.h): a compute pass computes the frustum of each screen
// tile (when the view size or field of view changes), another one culls the cullable lights against those
// frustums and the depth buffer, writing a light grid + light index list that the gpass pixel shaders read.
namespace primal::graphics::vulkan::delight
{

constexpr u32 light_culling_tile_size{32};

auto initialize() -> bool;
auto shutdown() -> void;

[[nodiscard]] auto add_culler() -> id::id_type;
auto remove_culler(id::id_type id) -> void;

// Records the culling dispatches. Applies the barriers collected so far (the gpass transitions included, like
// D3D12) and adds the transitions of the light grid / index list for the gpass, which the caller applies.
auto cull_lights(VkCommandBuffer cmd, const VulkanFrameInfo &vulkan_info, vkx::VulkanBarrier &barriers) -> void;

// TODO: temporary for visualizing light culling. Remove later.
[[nodiscard]] auto frustums(id::id_type light_culling_id, u32 frame_index) -> vkx::BufferRange;
[[nodiscard]] auto light_grid_opaque(id::id_type light_culling_id, u32 frame_index) -> vkx::BufferRange;
[[nodiscard]] auto light_index_list_opaque(id::id_type light_culling_id, u32 frame_index) -> vkx::BufferRange;
}
