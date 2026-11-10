// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "VulkanInterface.h"

#include "CommonHeaders.h"
#include "Graphics/GraphicsPlatformInterface.h"
#include "VulkanCamera.h"
#include "VulkanContent.h"
#include "VulkanCore.h"
#include "VulkanLight.h"

namespace primal::graphics::vulkan
{

auto get_platform_interface(PlatformInterface &pi) -> void
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

    pi.resources.add_mesh = content::submesh::add;
    pi.resources.remove_mesh = content::submesh::remove;
    pi.resources.add_texture = content::texture::add;
    pi.resources.remove_texture = content::texture::remove;
    pi.resources.add_material = content::material::add;
    pi.resources.remove_material = content::material::remove;
    pi.resources.add_render_item = content::render_item::add;
    pi.resources.remove_render_item = content::render_item::remove;

    pi.camera.create = camera::create;
    pi.camera.remove = camera::remove;
    pi.camera.set_parameter = camera::set_parameter;
    pi.camera.get_parameter = camera::get_parameter;

    pi.light.create_light_set = light::create_light_set;
    pi.light.remove_light_set = light::remove_light_set;
    pi.light.create = light::create;
    pi.light.remove = light::remove;
    pi.light.set_parameter = light::set_parameter;
    pi.light.get_parameter = light::get_parameter;

    pi.platform = GraphicsPlatform::vulkan;
}

}
