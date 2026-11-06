// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "CommonHeaders.h"
#include "VulkanInterface.h"
#include "VulkanCore.h"
#include "Graphics/GraphicsPlatformInterface.h"

namespace primal::graphics::vulkan {

void
get_platform_interface(platform_interface& pi)
{
    pi.initialize = core::initialize;
    pi.shutdown = core::shutdown;
    pi.set_option = core::set_option;
    pi.get_option = core::get_option;

    pi.surface.create = core::create_surface;
    pi.surface.remove = core::remove_surface;
    pi.surface.resize = core::resize_surface;
    pi.surface.width = core::surface_width;
    pi.surface.height = core::surface_height;
    pi.surface.render = core::render_surface;

    // TODO: lights, cameras and content.
    //       Until then those entries stay null: only surfaces can be used with the Vulkan backend.

    pi.platform = graphics_platform::vulkan;
}

} // namespace primal::graphics::vulkan
