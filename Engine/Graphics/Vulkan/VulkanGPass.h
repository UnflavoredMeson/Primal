// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "VulkanCommonHeaders.h"
#include "VulkanHelpers.h"

namespace primal::graphics::vulkan
{
struct VulkanFrameInfo;
class VulkanRenderTexture;
class VulkanDepthBuffer;
}

// Geometry pass (counterpart of D3D12GPass.h): depth prepass + main pass into an HDR main buffer, which
// post-processing reads. Draws the frame's render items directly (no ExecuteIndirect, see VulkanGPass.cpp).
namespace primal::graphics::vulkan::gpass
{

constexpr VkFormat main_buffer_format{VK_FORMAT_R16G16B16A16_SFLOAT}; // DXGI_FORMAT_R16G16B16A16_FLOAT
constexpr VkFormat depth_buffer_format{VK_FORMAT_D32_SFLOAT};         // DXGI_FORMAT_D32_FLOAT

auto initialize() -> bool;
auto shutdown() -> void;

[[nodiscard]] auto main_buffer() -> const VulkanRenderTexture &;
[[nodiscard]] auto depth_buffer() -> const VulkanDepthBuffer &;

// First step of a frame, before anything is allocated from the frame's constant buffer: gathers the surface's
// render items and returns the constant buffer bytes the gpass will allocate for them (core reserves them).
[[nodiscard]] auto collect_render_items(const FrameInfo &info, SurfaceId surface_id) -> u64;

// NOTE: call this every frame before rendering anything in gpass.
auto set_size(math::u32v2 size) -> void;

auto depth_prepass(VkCommandBuffer cmd, const VulkanFrameInfo &vulkan_info) -> void;
auto render(VkCommandBuffer cmd, const VulkanFrameInfo &vulkan_info) -> void;

auto add_transitions_for_depth_prepass(vkx::VulkanBarrier &barriers) -> void;
auto add_transitions_for_gpass(vkx::VulkanBarrier &barriers) -> void;
auto add_transitions_for_post_process(vkx::VulkanBarrier &barriers) -> void;

}
