// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "VulkanCommonHeaders.h"
#include "VulkanHelpers.h"

namespace primal::graphics::vulkan {
struct vulkan_frame_info;
class vulkan_render_texture;
class vulkan_depth_buffer;
} // namespace primal::graphics::vulkan

// Geometry pass (counterpart of D3D12GPass.h): depth prepass + main pass into an HDR main buffer, which
// post-processing reads. Draws the frame's render items directly (no ExecuteIndirect, see VulkanGPass.cpp).
namespace primal::graphics::vulkan::gpass {

constexpr VkFormat main_buffer_format{ VK_FORMAT_R16G16B16A16_SFLOAT }; // DXGI_FORMAT_R16G16B16A16_FLOAT
constexpr VkFormat depth_buffer_format{ VK_FORMAT_D32_SFLOAT };         // DXGI_FORMAT_D32_FLOAT

bool initialize();
void shutdown();

[[nodiscard]] const vulkan_render_texture& main_buffer();
[[nodiscard]] const vulkan_depth_buffer& depth_buffer();

// NOTE: call this every frame before rendering anything in gpass.
void set_size(math::u32v2 size);

void depth_prepass(VkCommandBuffer cmd, const vulkan_frame_info& vulkan_info);
void render(VkCommandBuffer cmd, const vulkan_frame_info& vulkan_info);

void add_transitions_for_depth_prepass(vkx::vulkan_barrier& barriers);
void add_transitions_for_gpass(vkx::vulkan_barrier& barriers);
void add_transitions_for_post_process(vkx::vulkan_barrier& barriers);

} // namespace primal::graphics::vulkan::gpass
