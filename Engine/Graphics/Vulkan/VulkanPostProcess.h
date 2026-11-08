// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "VulkanCommonHeaders.h"

namespace primal::graphics::vulkan {
struct vulkan_frame_info;
}

// Post-processing (counterpart of D3D12PostProcess.h): a fullscreen triangle that reads the gpass
// buffers and writes the surface's back buffer.
namespace primal::graphics::vulkan::fx {

bool initialize();
void shutdown();

// The target (a swapchain image view) must be in vkx::resource_states.render_target. Every pixel is
// written, so its previous contents don't matter. target_format selects the pipeline.
void post_process(VkCommandBuffer cmd, const vulkan_frame_info& vulkan_info, VkImageView target_rtv,
                  VkFormat target_format);

} // namespace primal::graphics::vulkan::fx
