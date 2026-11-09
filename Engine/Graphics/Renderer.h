// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "CommonHeaders.h"
#include "Platform/Window.h"
#include "EngineAPI/Camera.h"
#include "EngineAPI/Light.h"

namespace primal::graphics
{

struct RendererOption
{
    enum Option : u64
    {
        vsync,
        raytracing,
        msaa,

        count
    };
};

struct FrameInfo
{
    id::id_type *render_item_ids{nullptr};
    f32 *thresholds{nullptr};
    u64 light_set_key{0};
    f32 last_frame_time{16.7f};
    f32 average_frame_time{16.7f};
    u32 render_item_count{0};
    graphics::CameraId camera_id{id::invalid_id}; // qualified: a member named like its type (GCC -Wchanges-meaning)
};

DEFINE_TYPED_ID(SurfaceId);

class Surface
{
  public:
    constexpr explicit Surface(SurfaceId id)
        : _id{id}
    {
    }
    constexpr Surface() = default;
    constexpr auto get_id() const -> SurfaceId
    {
        return _id;
    }
    constexpr auto is_valid() const -> bool
    {
        return id::is_valid(_id);
    }

    auto resize(u32 width, u32 height) const -> void;
    auto width() const -> u32;
    auto height() const -> u32;
    auto render(FrameInfo info) const -> void;

  private:
    SurfaceId _id{id::invalid_id};
};

struct RenderSurface
{
    platform::Window window{};
    primal::graphics::Surface surface{};
};

struct DirectionalLightParams
{
};

struct PointLightParams
{
    math::v3 attenuation;
    f32 range;
};

struct SpotLightParams
{
    math::v3 attenuation;
    f32 range;
    // Umbra angle in radians [0, pi)
    f32 umbra;
    // Penumbra angle in radians [umbra, pi)
    f32 penumbra;
};

struct AmbientParams
{
    id::id_type diffuse_texture_id;
    id::id_type specular_texture_id;
    id::id_type brdf_lut_texture_id;
};

struct LightInitInfo
{
    u64 light_set_key{0};
    id::id_type entity_id{id::invalid_id};
    Light::Type type{};
    f32 intensity{1.f};
    math::v3 color{1.f, 1.f, 1.f};
    union
    {
        DirectionalLightParams directional_params;
        PointLightParams point_params;
        SpotLightParams spot_params;
        graphics::AmbientParams ambient_params;
    };
    bool is_enabled{true};
};

struct LightParameter
{
    enum Parameter : u32
    {
        is_enabled,
        intensity,
        color,
        attenuation,
        range,
        umbra,
        penumbra,
        type,
        entity_id,

        count
    };
};

struct CameraParameter
{
    enum Parameter : u32
    {
        up_vector,
        field_of_view,
        aspect_ratio,
        view_width,
        view_height,
        near_z,
        far_z,
        view,
        projection,
        inverse_projection,
        view_projection,
        inverse_view_projection,
        type,
        entity_id,

        count
    };
};

struct CameraInitInfo
{
    id::id_type entity_id{id::invalid_id};
    Camera::Type type{};
    math::v3 up;
    union
    {
        f32 field_of_view;
        f32 view_width;
    };
    union
    {
        f32 aspect_ratio;
        f32 view_height;
    };
    f32 near_z;
    f32 far_z;
};

struct PerspectiveCameraInitInfo : public CameraInitInfo
{
    explicit PerspectiveCameraInitInfo(id::id_type id)
    {
        assert(id::is_valid(id));
        entity_id = id;
        type = Camera::perspective;
        up = {0.f, 1.f, 0.f};
        field_of_view = 0.25f;
        aspect_ratio = 16.f / 10.f;
        near_z = 0.1f;
        far_z = 100.f;
    }
};

struct OrthographicCameraInitInfo : public CameraInitInfo
{
    explicit OrthographicCameraInitInfo(id::id_type id)
    {
        assert(id::is_valid(id));
        entity_id = id;
        type = Camera::orthographic;
        up = {0.f, 1.f, 0.f};
        view_width = 1920;
        view_height = 1080;
        near_z = 0.01f;
        far_z = 1000.f;
    }
};

struct ShaderFlags
{
    enum Flags : u32
    {
        none = 0x0,
        vertex = 0x01,
        hull = 0x02,
        domain = 0x04,
        geometry = 0x08,
        pixel = 0x10,
        compute = 0x20,
        amplification = 0x40,
        mesh = 0x80,
    };
};

struct ShaderType
{
    enum Type : u32
    {
        vertex = 0,
        hull,
        domain,
        geometry,
        pixel,
        compute,
        amplification,
        mesh,

        count
    };
};

struct MaterialType
{
    enum Type : u32
    {
        opaque,
        // transparent, unlit, clear_coat, cloth, skin, foliage, hair, etc.

        count
    };
};

struct MaterialSurface
{
    u8 base_color[4]{255, 255, 255, 255};
    u8 emissive[3]{0, 0, 0};
    u8 metallic{0};
    u8 roughness{225};
    u8 input_mask{0}; // A set bit means to use a texture for the corresponding surface property
    u16 emissive_intensity{0};
};

struct MaterialInitInfo
{
    id::id_type *texture_ids;
    MaterialSurface surface;
    MaterialType::Type type;
    u32 texture_count; // NOTE: textures are optional, so, texture count may be 0 and texture_ids may be nullptr.
    id::id_type shader_ids[ShaderType::count]{
        id::invalid_id,
        id::invalid_id,
        id::invalid_id,
        id::invalid_id,
        id::invalid_id,
        id::invalid_id,
        id::invalid_id,
        id::invalid_id};
};

struct PrimitiveTopology
{
    enum Type : u32
    {
        point_list = 1,
        line_list,
        line_strip,
        triangle_list,
        triangle_strip,

        count
    };
};

#ifndef PRIMAL_PLUS
enum class GraphicsPlatform : u32
{
    direct3d12 = 0,
    vulkan = 1,
};
#else
#include "Graphics/GraphicsPlatform.h"
#endif

auto initialize(GraphicsPlatform platform) -> bool;
auto shutdown() -> void;

auto set_option(RendererOption::Option option, const void *const parameter, u32 parameter_size) -> void;
auto get_option(RendererOption::Option option, void *const parameter, u32 parameter_size) -> void;

// Get the location of compiled engine shaders relative to the executable's path.
// The path is for the graphics API that's currently in use.
auto get_engine_shaders_path() -> const char *;

// Get the location of compiled engine shaders, for the specified platform, relative to the executable's path.
// The path is for the graphics API that's currently in use.
auto get_engine_shaders_path(GraphicsPlatform platform) -> const char *;

auto create_surface(platform::Window window) -> Surface;
auto remove_surface(SurfaceId id) -> void;

auto create_light_set(u64 light_set_key) -> void;
auto remove_light_set(u64 light_set_key) -> void;

auto create_light(LightInitInfo info) -> Light;
auto remove_light(LightId id, u64 light_set_key) -> void;

auto create_camera(CameraInitInfo info) -> Camera;
auto remove_camera(CameraId id) -> void;

auto add_mesh(const u8 *const data, id::id_type *const ids, u32 count) -> void;
auto remove_mesh(const id::id_type *const ids, u32 count) -> void;

auto add_texture(const u8 *const data) -> id::id_type;
auto remove_texture(id::id_type id) -> void;

auto add_material(MaterialInitInfo info) -> id::id_type;
auto remove_material(id::id_type id) -> void;

auto add_render_item(
    id::id_type entity_id,
    id::id_type geometry_content_id,
    u32 material_count,
    const id::id_type *const material_ids) -> id::id_type;
auto remove_render_item(id::id_type id) -> void;
}
