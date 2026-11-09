// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "VulkanCommonHeaders.h"

namespace primal::graphics::vulkan
{
struct VulkanFrameInfo;
}

// Post-processing (counterpart of D3D12PostProcess.h): a fullscreen triangle that reads the gpass
// buffers and writes the surface's back buffer.
namespace primal::graphics::vulkan::fx
{

auto initialize() -> bool;
auto shutdown() -> void;

// The target (a swapchain image view) must be in vkx::resource_states.render_target. Every pixel is
// written, so its previous contents don't matter. target_format selects the pipeline.
auto post_process(
    VkCommandBuffer cmd,
    const VulkanFrameInfo &vulkan_info,
    VkImageView target_rtv,
    VkFormat target_format) -> void;

}
