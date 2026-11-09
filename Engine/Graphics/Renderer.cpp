// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "Renderer.h"
#include "GraphicsPlatformInterface.h"
#if PRIMAL_GRAPHICS_D3D12
#include "Direct3D12/D3D12Interface.h"
#endif
#if PRIMAL_GRAPHICS_VULKAN
#include "Vulkan/VulkanInterface.h"
#endif

namespace primal::graphics
{
namespace
{

// Defines where the compiled engine shaders file is located for each one of the supported APIs.
constexpr const char *engine_shader_paths[]{"./shaders/d3d12/shaders.bin", "./shaders/vulkan/shaders.bin"};

PlatformInterface gfx{};

#ifndef PRIMAL_PLUS
auto set_platform_interface(GraphicsPlatform platform, PlatformInterface &pi) -> bool
{
    switch (platform)
    {
#if PRIMAL_GRAPHICS_D3D12
        case GraphicsPlatform::direct3d12: d3d12::get_platform_interface(pi); break;
#endif
#if PRIMAL_GRAPHICS_VULKAN
        case GraphicsPlatform::vulkan: vulkan::get_platform_interface(pi); break;
#endif
        default: return false;
    }

    assert(pi.platform == platform);
    return true;
}
#endif
}

#ifdef PRIMAL_PLUS
extern bool set_platform_interface(GraphicsPlatform platform, PlatformInterface &pi);
#endif

auto initialize(GraphicsPlatform platform) -> bool
{
    return set_platform_interface(platform, gfx) && gfx.initialize();
}

auto shutdown() -> void
{
    if (gfx.platform != (GraphicsPlatform)-1)
        gfx.shutdown();
}

auto set_option(RendererOption::Option option, const void *const parameter, u32 parameter_size) -> void
{
    gfx.set_option(option, parameter, parameter_size);
}

auto get_option(RendererOption::Option option, void *const parameter, u32 parameter_size) -> void
{
    gfx.get_option(option, parameter, parameter_size);
}

auto get_engine_shaders_path() -> const char *
{
    return engine_shader_paths[(u32)gfx.platform];
}

auto get_engine_shaders_path(GraphicsPlatform platform) -> const char *
{
    return engine_shader_paths[(u32)platform];
}

auto create_surface(platform::Window window) -> Surface
{
    return gfx.surface.create(window);
}

auto remove_surface(SurfaceId id) -> void
{
    assert(id::is_valid(id));
    gfx.surface.remove(id);
}

auto Surface::resize(u32 width, u32 height) const -> void
{
    assert(is_valid());
    gfx.surface.resize(_id, width, height);
}

auto Surface::width() const -> u32
{
    assert(is_valid());
    return gfx.surface.width(_id);
}

auto Surface::height() const -> u32
{
    assert(is_valid());
    return gfx.surface.height(_id);
}

auto Surface::render(FrameInfo info) const -> void
{
    assert(is_valid());
    gfx.surface.render(_id, info);
}

auto create_light_set(u64 light_set_key) -> void
{
    gfx.light.create_light_set(light_set_key);
}
auto remove_light_set(u64 light_set_key) -> void
{
    gfx.light.remove_light_set(light_set_key);
}

auto create_light(LightInitInfo info) -> Light
{
    return gfx.light.create(info);
}

auto remove_light(LightId id, u64 light_set_key) -> void
{
    gfx.light.remove(id, light_set_key);
}

auto Light::is_enabled(bool is_enabled) const -> void
{
    assert(is_valid());
    gfx.light.set_parameter(_id, _light_set_key, LightParameter::is_enabled, &is_enabled, sizeof(is_enabled));
}

auto Light::intensity(f32 intensity) const -> void
{
    assert(is_valid());
    gfx.light.set_parameter(_id, _light_set_key, LightParameter::intensity, &intensity, sizeof(intensity));
}

auto Light::color(math::v3 color) const -> void
{
    assert(is_valid());
    gfx.light.set_parameter(_id, _light_set_key, LightParameter::color, &color, sizeof(color));
}

auto Light::attenuation(math::v3 attenuation) const -> void
{
    assert(is_valid());
    gfx.light.set_parameter(_id, _light_set_key, LightParameter::attenuation, &attenuation, sizeof(attenuation));
}

auto Light::range(f32 range) const -> void
{
    assert(is_valid());
    gfx.light.set_parameter(_id, _light_set_key, LightParameter::range, &range, sizeof(range));
}

auto Light::cone_angles(f32 umbra, f32 penumbra) const -> void
{
    assert(is_valid());
    gfx.light.set_parameter(_id, _light_set_key, LightParameter::umbra, &umbra, sizeof(umbra));
    gfx.light.set_parameter(_id, _light_set_key, LightParameter::penumbra, &penumbra, sizeof(penumbra));
}

auto Light::is_enabled() const -> bool
{
    assert(is_valid());
    bool is_enabled;
    gfx.light.get_parameter(_id, _light_set_key, LightParameter::is_enabled, &is_enabled, sizeof(is_enabled));
    return is_enabled;
}

auto Light::intensity() const -> f32
{
    assert(is_valid());
    f32 intensity;
    gfx.light.get_parameter(_id, _light_set_key, LightParameter::intensity, &intensity, sizeof(intensity));
    return intensity;
}

auto Light::color() const -> math::v3
{
    assert(is_valid());
    math::v3 color;
    gfx.light.get_parameter(_id, _light_set_key, LightParameter::color, &color, sizeof(color));
    return color;
}

auto Light::attenuation() const -> math::v3
{
    assert(is_valid());
    math::v3 attenuation;
    gfx.light.get_parameter(_id, _light_set_key, LightParameter::attenuation, &attenuation, sizeof(attenuation));
    return attenuation;
}

auto Light::range() const -> f32
{
    assert(is_valid());
    f32 range;
    gfx.light.get_parameter(_id, _light_set_key, LightParameter::range, &range, sizeof(range));
    return range;
}

auto Light::umbra() const -> f32
{
    assert(is_valid());
    f32 umbra;
    gfx.light.get_parameter(_id, _light_set_key, LightParameter::umbra, &umbra, sizeof(umbra));
    return umbra;
}

auto Light::penumbra() const -> f32
{
    assert(is_valid());
    f32 penumbra;
    gfx.light.get_parameter(_id, _light_set_key, LightParameter::penumbra, &penumbra, sizeof(penumbra));
    return penumbra;
}

auto Light::light_type() const -> Light::Type
{
    assert(is_valid());
    Type type;
    gfx.light.get_parameter(_id, _light_set_key, LightParameter::type, &type, sizeof(type));
    return type;
}

auto Light::entity_id() const -> id::id_type
{
    assert(is_valid());
    id::id_type id;
    gfx.light.get_parameter(_id, _light_set_key, LightParameter::entity_id, &id, sizeof(id));
    return id;
}

auto create_camera(CameraInitInfo info) -> Camera
{
    return gfx.camera.create(info);
}

auto remove_camera(CameraId id) -> void
{
    gfx.camera.remove(id);
}

auto Camera::up(math::v3 up) const -> void
{
    assert(is_valid());
    gfx.camera.set_parameter(_id, CameraParameter::up_vector, &up, sizeof(up));
}

auto Camera::field_of_view(f32 fov) const -> void
{
    assert(is_valid());
    gfx.camera.set_parameter(_id, CameraParameter::field_of_view, &fov, sizeof(fov));
}

auto Camera::aspect_ratio(f32 aspect_ratio) const -> void
{
    assert(is_valid());
    gfx.camera.set_parameter(_id, CameraParameter::aspect_ratio, &aspect_ratio, sizeof(aspect_ratio));
}

auto Camera::view_width(f32 width) const -> void
{
    assert(is_valid());
    gfx.camera.set_parameter(_id, CameraParameter::view_width, &width, sizeof(width));
}

auto Camera::view_height(f32 height) const -> void
{
    assert(is_valid());
    gfx.camera.set_parameter(_id, CameraParameter::view_height, &height, sizeof(height));
}

auto Camera::range(f32 near_z, f32 far_z) const -> void
{
    assert(is_valid());
    gfx.camera.set_parameter(_id, CameraParameter::near_z, &near_z, sizeof(near_z));
    gfx.camera.set_parameter(_id, CameraParameter::far_z, &far_z, sizeof(far_z));
}

auto Camera::view() const -> math::m4x4
{
    assert(is_valid());
    math::m4x4 matrix;
    gfx.camera.get_parameter(_id, CameraParameter::view, &matrix, sizeof(matrix));
    return matrix;
}

auto Camera::projection() const -> math::m4x4
{
    assert(is_valid());
    math::m4x4 matrix;
    gfx.camera.get_parameter(_id, CameraParameter::projection, &matrix, sizeof(matrix));
    return matrix;
}

auto Camera::inverse_projection() const -> math::m4x4
{
    assert(is_valid());
    math::m4x4 matrix;
    gfx.camera.get_parameter(_id, CameraParameter::inverse_projection, &matrix, sizeof(matrix));
    return matrix;
}

auto Camera::view_projection() const -> math::m4x4
{
    assert(is_valid());
    math::m4x4 matrix;
    gfx.camera.get_parameter(_id, CameraParameter::view_projection, &matrix, sizeof(matrix));
    return matrix;
}

auto Camera::inverse_view_projection() const -> math::m4x4
{
    assert(is_valid());
    math::m4x4 matrix;
    gfx.camera.get_parameter(_id, CameraParameter::inverse_view_projection, &matrix, sizeof(matrix));
    return matrix;
}

auto Camera::up() const -> math::v3
{
    assert(is_valid());
    math::v3 up_vector;
    gfx.camera.get_parameter(_id, CameraParameter::up_vector, &up_vector, sizeof(up_vector));
    return up_vector;
}

auto Camera::near_z() const -> f32
{
    assert(is_valid());
    f32 near_z;
    gfx.camera.get_parameter(_id, CameraParameter::near_z, &near_z, sizeof(near_z));
    return near_z;
}

auto Camera::far_z() const -> f32
{
    assert(is_valid());
    f32 far_z;
    gfx.camera.get_parameter(_id, CameraParameter::far_z, &far_z, sizeof(far_z));
    return far_z;
}

auto Camera::field_of_view() const -> f32
{
    assert(is_valid());
    f32 fov;
    gfx.camera.get_parameter(_id, CameraParameter::field_of_view, &fov, sizeof(fov));
    return fov;
}

auto Camera::aspect_ratio() const -> f32
{
    assert(is_valid());
    f32 aspect_ratio;
    gfx.camera.get_parameter(_id, CameraParameter::aspect_ratio, &aspect_ratio, sizeof(aspect_ratio));
    return aspect_ratio;
}

auto Camera::view_width() const -> f32
{
    assert(is_valid());
    f32 width;
    gfx.camera.get_parameter(_id, CameraParameter::view_width, &width, sizeof(width));
    return width;
}

auto Camera::view_height() const -> f32
{
    assert(is_valid());
    f32 height;
    gfx.camera.get_parameter(_id, CameraParameter::view_height, &height, sizeof(height));
    return height;
}

auto Camera::projection_type() const -> Camera::Type
{
    assert(is_valid());
    Camera::Type type;
    gfx.camera.get_parameter(_id, CameraParameter::type, &type, sizeof(type));
    return type;
}

auto Camera::entity_id() const -> id::id_type
{
    assert(is_valid());
    id::id_type entity_id;
    gfx.camera.get_parameter(_id, CameraParameter::entity_id, &entity_id, sizeof(entity_id));
    return entity_id;
}

auto add_mesh(const u8 *const data, id::id_type *const ids, u32 count) -> void
{
    gfx.resources.add_mesh(data, ids, count);
}

auto remove_mesh(const id::id_type *const ids, u32 count) -> void
{
    gfx.resources.remove_mesh(ids, count);
}

auto add_texture(const u8 *const data) -> id::id_type
{
    return gfx.resources.add_texture(data);
}

auto remove_texture(id::id_type id) -> void
{
    gfx.resources.remove_texture(id);
}

auto add_material(MaterialInitInfo info) -> id::id_type
{
    return gfx.resources.add_material(info);
}

auto remove_material(id::id_type id) -> void
{
    gfx.resources.remove_material(id);
}

auto add_render_item(
    id::id_type entity_id,
    id::id_type geometry_content_id,
    u32 material_count,
    const id::id_type *const material_ids) -> id::id_type
{
    return gfx.resources.add_render_item(entity_id, geometry_content_id, material_count, material_ids);
}

auto remove_render_item(id::id_type id) -> void
{
    gfx.resources.remove_render_item(id);
}
}
