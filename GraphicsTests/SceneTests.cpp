// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "Components/Entity.h"
#include "Components/Geometry.h"
#include "Components/Script.h"
#include "Content/ContentToEngine.h"
#include "EngineAPI/Light.h"
#include "Graphics/Renderer.h"
#include "ShaderCompilation.h"
#include "TestContent.h"
#include "TestSession.h"

#include <doctest.h>

#include <array>
#include <chrono>
#include <string>
#include <vector>

import primal.jobs;

using namespace primal;

// A small scene through the whole renderer: depth prepass, light culling, gpass and post-processing (EngineTest's
// renderer test and the 6e-6g drivers of the Vulkan port, as test cases). Everything in it is made here
// (TestContent.h): the three vertex formats of TestShader.hlsl, a geometry with two LODs and several submeshes,
// textured and untextured materials, directional, point, spot and ambient (IBL) lights in two light sets, and a
// script that moves an entity. A test passes when nothing crashes or asserts and the validation layer (with
// synchronization validation) reports nothing. Nobody looks at the picture.

namespace
{

constexpr f32 frame_time{1.f / 60.f};
constexpr u64 first_light_set{0};
constexpr u64 second_light_set{1};

class SpinScript : public script::EntityScript
{
  public:
    explicit SpinScript(game_entity::Entity entity)
        : script::EntityScript{entity}
    {
    }

    auto update(f32 dt) -> void override
    {
        _angle += dt * math::two_pi;
        const math::v3 rotation{0.f, _angle, 0.f};
        const math::v4 quaternion{math::quat_from_euler(rotation)};
        set_rotation(quaternion);
    }

  private:
    f32 _angle{0.f};
};

REGISTER_SCRIPT(SpinScript);

// The order TestShader.hlsl reads a textured material's textures in.
struct TextureUsage
{
    enum Usage : u32
    {
        ambient_occlusion,
        base_color,
        emissive,
        metal_rough,
        normal,

        count
    };
};

auto create_texture(const std::vector<u8> &blob) -> id::id_type
{
    const id::id_type id{content::create_resource(blob.data(), content::AssetType::texture)};
    REQUIRE(id::is_valid(id));
    return id;
}

auto create_geometry(std::span<const test_content::Lod> lods) -> id::id_type
{
    const std::vector<u8> blob{test_content::make_geometry(lods)};
    const id::id_type id{content::create_resource(blob.data(), content::AssetType::mesh)};
    REQUIRE(id::is_valid(id));
    return id;
}

class Scene
{
  public:
    Scene()
    {
        compile_shaders();
        create_textures();
        create_materials();
        create_geometries();
        create_entities();
        create_lights();
    }
    DISABLE_COPY_AND_MOVE(Scene);

    // NOTE: before the renderer shuts down (declare the Scene after the Session).
    ~Scene()
    {
        for (const graphics::Light light : _lights)
        {
            remove_light(light);
        }
        graphics::remove_light_set(first_light_set);
        graphics::remove_light_set(second_light_set);

        for (const game_entity::Entity entity : _entities)
        {
            game_entity::remove(entity.get_id());
        }
        for (const id::id_type id : _geometry_ids)
        {
            content::destroy_resource(id, content::AssetType::mesh);
        }
        for (const id::id_type id : _material_ids)
        {
            content::destroy_resource(id, content::AssetType::material);
        }
        for (const id::id_type id : _texture_ids)
        {
            content::destroy_resource(id, content::AssetType::texture);
        }
        for (const id::id_type id : _shader_group_ids)
        {
            content::remove_shader_group(id);
        }
    }

    [[nodiscard]] auto render_item_count() const -> u32
    {
        return static_cast<u32>(_entities.size());
    }

    [[nodiscard]] auto light_count() const -> u32
    {
        return static_cast<u32>(_lights.size());
    }

    [[nodiscard]] auto light(u32 index) -> graphics::Light
    {
        return _lights[index];
    }

    // Updates the scripts and renders one frame on every view, alternating the light sets between the views.
    // lod_threshold picks the LOD of every geometry that has more than one.
    auto render_frame(graphics_test::Session &session, f32 lod_threshold = 0.f) -> void
    {
        graphics_test::pump_events();
        script::update(frame_time);

        const u32 item_count{render_item_count()};
        _render_item_ids.resize(item_count);
        _thresholds.assign(item_count, lod_threshold);
        if (item_count)
        {
            geometry::get_render_item_ids(_render_item_ids.data(), item_count);
        }

        for (u32 i{0}; i < graphics_test::max_views; ++i)
        {
            const graphics_test::View &view{session.view(i)};
            if (!view.is_valid())
            {
                continue;
            }
            graphics::FrameInfo info{};
            info.render_item_ids = _render_item_ids.data();
            info.render_item_count = item_count;
            info.thresholds = _thresholds.data();
            info.light_set_key = (i % 2) == 0 ? first_light_set : second_light_set;
            info.last_frame_time = frame_time * 1000.f;
            info.average_frame_time = frame_time * 1000.f;
            info.camera_id = view.camera.get_id();
            view.surface.render(info);
        }
    }

    auto render_frames(graphics_test::Session &session, u32 count, f32 lod_threshold = 0.f) -> void
    {
        for (u32 i{0}; i < count; ++i)
        {
            render_frame(session, lod_threshold);
        }
    }

    // Removes the entity that was created last, with its render item.
    auto remove_last_entity() -> void
    {
        REQUIRE_FALSE(_entities.empty());
        game_entity::remove(_entities.back().get_id());
        _entities.pop_back();
    }

    auto add_box_entity(math::v3 position) -> void
    {
        add_entity(_box_normal_id, {&_plain_material_id, 1}, position);
    }

    auto add_point_light(u64 light_set_key, math::v3 position, math::v3 color) -> graphics::Light
    {
        graphics::LightInitInfo info{};
        info.light_set_key = light_set_key;
        info.entity_id = graphics_test::create_entity(position).get_id();
        info.type = graphics::Light::point;
        info.intensity = 2.f;
        info.color = color;
        info.point_params.attenuation = {1.f, 1.f, 1.f};
        info.point_params.range = 4.f;
        return add_light(info);
    }

    // Removes the light that was created last.
    auto remove_last_light() -> void
    {
        REQUIRE_FALSE(_lights.empty());
        remove_light(_lights.back());
        _lights.pop_back();
    }

  private:
    auto compile_shaders() -> void
    {
        const char *const shader_path{"../../EngineTest/"};
        constexpr graphics::GraphicsPlatform platform{graphics::GraphicsPlatform::vulkan};

        // One vertex shader per vertex format; the format's elements type is the key.
        constexpr u32 keys[]{
            test_content::ElementsType::position_only,
            test_content::ElementsType::static_normal,
            test_content::ElementsType::static_normal_texture,
        };
        std::vector<std::unique_ptr<u8[]>> vertex_shaders;
        std::vector<const u8 *> vertex_shader_pointers;
        for (const u32 key : keys)
        {
            utl::vector<std::wstring> args{};
            args.emplace_back(L"-D");
            args.emplace_back(L"ELEMENTS_TYPE=" + std::to_wstring(key));
            vertex_shaders.emplace_back(
                ::compile_shader(
                    {"TestShader.hlsl", "TestShaderVS", graphics::ShaderType::vertex},
                    shader_path,
                    args,
                    false,
                    platform));
            REQUIRE(vertex_shaders.back() != nullptr);
            vertex_shader_pointers.push_back(vertex_shaders.back().get());
        }
        _vertex_shaders_id = content::add_shader_group(
            vertex_shader_pointers.data(), static_cast<u32>(vertex_shader_pointers.size()), &keys[0]);

        const auto add_pixel_shader = [&](const wchar_t *define) -> id::id_type
        {
            utl::vector<std::wstring> args{};
            if (define)
            {
                args.emplace_back(L"-D");
                args.emplace_back(define);
            }
            const std::unique_ptr<u8[]> shader{::compile_shader(
                {"TestShader.hlsl", "TestShaderPS", graphics::ShaderType::pixel}, shader_path, args, false, platform)};
            REQUIRE(shader != nullptr);
            const u8 *const pointer{shader.get()};
            return content::add_shader_group(&pointer, 1, &u32_invalid_id);
        };
        _pixel_shader_id = add_pixel_shader(nullptr);
        _textured_pixel_shader_id = add_pixel_shader(L"TEXTURED_MTL=1");

        _shader_group_ids = {_vertex_shaders_id, _pixel_shader_id, _textured_pixel_shader_id};
        for (const id::id_type id : _shader_group_ids)
        {
            REQUIRE(id::is_valid(id));
        }
    }

    auto create_textures() -> void
    {
        using test_content::make_checkerboard_texture;
        _material_textures[TextureUsage::ambient_occlusion] =
            create_texture(make_checkerboard_texture(4, false, 0xffff'ffff, 0xffc0'c0c0));
        _material_textures[TextureUsage::base_color] =
            create_texture(make_checkerboard_texture(16, true, 0xff20'60e0, 0xffe0'e0e0));
        _material_textures[TextureUsage::emissive] =
            create_texture(make_checkerboard_texture(8, true, 0xff00'0000, 0xff10'2040));
        // Red is the metallic value, green the roughness.
        _material_textures[TextureUsage::metal_rough] =
            create_texture(make_checkerboard_texture(8, false, 0xff00'40ff, 0xff00'c000));
        // (0.5, 0.5, 1): straight up in tangent space.
        _material_textures[TextureUsage::normal] =
            create_texture(make_checkerboard_texture(4, false, 0xffff'8080, 0xffff'8080));

        _brdf_lut_id = create_texture(test_content::make_brdf_lut(16));
        _diffuse_ibl_id = create_texture(test_content::make_cube_map(4, 1, {0.3f, 0.35f, 0.5f}));
        _specular_ibl_id = create_texture(test_content::make_cube_map(16, 5, {0.6f, 0.6f, 0.7f}));

        _texture_ids.assign(_material_textures.begin(), _material_textures.end());
        _texture_ids.insert(_texture_ids.end(), {_brdf_lut_id, _diffuse_ibl_id, _specular_ibl_id});
    }

    auto create_materials() -> void
    {
        graphics::MaterialInitInfo info{};
        info.type = graphics::MaterialType::opaque;
        info.shader_ids[graphics::ShaderType::vertex] = _vertex_shaders_id;
        info.shader_ids[graphics::ShaderType::pixel] = _pixel_shader_id;
        const auto add_material = [&] -> id::id_type
        {
            const id::id_type id{content::create_resource(&info, content::AssetType::material)};
            REQUIRE(id::is_valid(id));
            _material_ids.push_back(id);
            return id;
        };

        _plain_material_id = add_material();

        info.surface.base_color[0] = 220;
        info.surface.base_color[1] = 180;
        info.surface.base_color[2] = 60;
        info.surface.metallic = 255;
        info.surface.roughness = 60;
        _metal_material_id = add_material();

        info.surface = {};
        info.surface.emissive[0] = 255;
        info.surface.emissive[1] = 120;
        info.surface.emissive_intensity = 300;
        _emissive_material_id = add_material();

        info.surface = {};
        info.shader_ids[graphics::ShaderType::pixel] = _textured_pixel_shader_id;
        info.texture_count = TextureUsage::count;
        info.texture_ids = _material_textures.data();
        _textured_material_id = add_material();
    }

    auto create_geometries() -> void
    {
        using test_content::ElementsType;
        using test_content::Lod;
        using test_content::make_box;
        using test_content::make_plane;

        const auto add_geometry = [&](std::span<const Lod> lods) -> id::id_type
        {
            const id::id_type id{create_geometry(lods)};
            _geometry_ids.push_back(id);
            return id;
        };

        // Single submesh geometries, one per vertex format.
        _floor_id = add_geometry({{Lod{0.f, {make_plane(ElementsType::static_normal, 12.f, 8)}}}});
        _box_position_id = add_geometry({{Lod{0.f, {make_box(ElementsType::position_only, {0.7f, 0.7f, 0.7f})}}}});
        _box_normal_id = add_geometry({{Lod{0.f, {make_box(ElementsType::static_normal, {0.8f, 1.2f, 0.8f})}}}});
        _box_textured_id = add_geometry({{Lod{0.f, {make_box(ElementsType::static_normal_texture, {1.f, 1.f, 1.f})}}}});

        // Two LODs: three submeshes with different vertex formats, then one. The submesh sizes aren't multiples of
        // each other, so the parts of the mesh buffer start at odd offsets.
        const Lod lods[]{
            Lod{0.f,
                {
                    make_box(ElementsType::static_normal_texture, {0.5f, 0.5f, 0.5f}),
                    make_plane(ElementsType::static_normal, 1.5f, 3),
                    make_box(ElementsType::position_only, {0.2f, 1.5f, 0.2f}),
                }},
            Lod{10.f, {make_box(ElementsType::static_normal, {0.6f, 0.6f, 0.6f})}},
        };
        _lod_geometry_id = add_geometry(lods);
    }

    auto add_entity(
        id::id_type geometry_id,
        std::span<id::id_type> materials,
        math::v3 position,
        script::detail::script_creator script_creator = nullptr) -> void
    {
        geometry::InitInfo geometry_info{};
        geometry_info.geometry_content_id = geometry_id;
        geometry_info.material_count = static_cast<u32>(materials.size());
        geometry_info.material_ids = materials.data();
        _entities.push_back(graphics_test::create_entity(position, {}, &geometry_info, script_creator));
    }

    auto create_entities() -> void
    {
        add_entity(_floor_id, {&_plain_material_id, 1}, {0.f, 0.f, 0.f});
        add_entity(_box_position_id, {&_emissive_material_id, 1}, {-4.f, 0.7f, 0.f});
        add_entity(_box_normal_id, {&_metal_material_id, 1}, {-1.5f, 1.2f, 1.f});
        add_entity(
            _box_textured_id,
            {&_textured_material_id, 1},
            {1.5f, 1.f, 0.f},
            &script::detail::create_script<SpinScript>);
        // One material per submesh, over all LODs.
        id::id_type lod_materials[]{
            _textured_material_id, _metal_material_id, _emissive_material_id, _plain_material_id};
        add_entity(_lod_geometry_id, lod_materials, {4.f, 1.5f, 1.f}, &script::detail::create_script<SpinScript>);
    }

    auto add_light(graphics::LightInitInfo info) -> graphics::Light
    {
        const graphics::Light light{graphics::create_light(info)};
        REQUIRE(light.is_valid());
        _lights.push_back(light);
        return light;
    }

    static auto remove_light(graphics::Light light) -> void
    {
        const game_entity::EntityId entity_id{light.entity_id()};
        graphics::remove_light(light.get_id(), light.light_set_key());
        game_entity::remove(entity_id);
    }

    auto create_lights() -> void
    {
        graphics::create_light_set(first_light_set);
        graphics::create_light_set(second_light_set);

        for (const u64 light_set_key : {first_light_set, second_light_set})
        {
            graphics::LightInitInfo info{};
            info.light_set_key = light_set_key;

            // Image based lighting for both sets.
            info.entity_id = graphics_test::create_entity({}).get_id();
            info.type = graphics::Light::ambient;
            info.ambient_params.diffuse_texture_id = _diffuse_ibl_id;
            info.ambient_params.specular_texture_id = _specular_ibl_id;
            info.ambient_params.brdf_lut_texture_id = _brdf_lut_id;
            add_light(info);

            info.entity_id = graphics_test::create_entity({}, {0.9f, 0.4f, 0.f}).get_id();
            info.type = graphics::Light::directional;
            info.color = {1.f, 0.95f, 0.9f};
            info.directional_params = {};
            add_light(info);
        }

        // Cullable lights over the floor: point and spot lights in the first set, a few in the second.
        for (s32 x{-3}; x <= 3; ++x)
        {
            for (s32 z{-2}; z <= 2; ++z)
            {
                const math::v3 position{static_cast<f32>(x) * 2.5f, 2.f, static_cast<f32>(z) * 2.5f};
                const math::v3 color{
                    0.3f + 0.1f * static_cast<f32>(x + 3), 0.5f, 0.3f + 0.15f * static_cast<f32>(z + 2)};
                if ((x + z) % 2 == 0)
                {
                    add_point_light(first_light_set, position, color);
                }
                else
                {
                    graphics::LightInitInfo info{};
                    info.light_set_key = first_light_set;
                    // Pointing down.
                    info.entity_id = graphics_test::create_entity(position, {math::half_pi, 0.f, 0.f}).get_id();
                    info.type = graphics::Light::spot;
                    info.intensity = 3.f;
                    info.color = color;
                    info.spot_params.attenuation = {1.f, 1.f, 1.f};
                    info.spot_params.range = 6.f;
                    info.spot_params.umbra = 0.3f;
                    info.spot_params.penumbra = 0.6f;
                    add_light(info);
                }
            }
        }
        add_point_light(second_light_set, {-2.f, 1.5f, 0.f}, {1.f, 0.2f, 0.2f});
        add_point_light(second_light_set, {2.f, 1.5f, 0.f}, {0.2f, 0.2f, 1.f});
    }

    id::id_type _vertex_shaders_id{id::invalid_id};
    id::id_type _pixel_shader_id{id::invalid_id};
    id::id_type _textured_pixel_shader_id{id::invalid_id};
    std::array<id::id_type, 3> _shader_group_ids{};

    std::array<id::id_type, TextureUsage::count> _material_textures{};
    id::id_type _brdf_lut_id{id::invalid_id};
    id::id_type _diffuse_ibl_id{id::invalid_id};
    id::id_type _specular_ibl_id{id::invalid_id};
    std::vector<id::id_type> _texture_ids;

    id::id_type _plain_material_id{id::invalid_id};
    id::id_type _metal_material_id{id::invalid_id};
    id::id_type _emissive_material_id{id::invalid_id};
    id::id_type _textured_material_id{id::invalid_id};
    std::vector<id::id_type> _material_ids;

    id::id_type _floor_id{id::invalid_id};
    id::id_type _box_position_id{id::invalid_id};
    id::id_type _box_normal_id{id::invalid_id};
    id::id_type _box_textured_id{id::invalid_id};
    id::id_type _lod_geometry_id{id::invalid_id};
    std::vector<id::id_type> _geometry_ids;

    // Every entity here has a geometry component, so there is one render item per entity.
    std::vector<game_entity::Entity> _entities;
    std::vector<graphics::Light> _lights;

    std::vector<id::id_type> _render_item_ids;
    std::vector<f32> _thresholds;
};

auto add_two_views(graphics_test::Session &session) -> void
{
    session.add_view("Scene 1", 480, 320, true);
    session.add_view("Scene 2", 320, 400, true);
}

}

TEST_SUITE_BEGIN("graphics scene");

TEST_CASE("a lit scene renders on two views with different light sets")
{
    graphics_test::Session session{};
    add_two_views(session);
    {
        Scene scene{};
        CHECK(scene.render_item_count() == 5);
        scene.render_frames(session, 30);
    }
    session.shutdown_and_check();
}

TEST_CASE("the scene renders with either LOD")
{
    graphics_test::Session session{};
    add_two_views(session);
    {
        Scene scene{};
        scene.render_frames(session, 10, 0.f);
        // At or above the second LOD's threshold.
        scene.render_frames(session, 10, 10.f);
        scene.render_frames(session, 10, 100.f);
        scene.render_frames(session, 10, 5.f);
    }
    session.shutdown_and_check();
}

TEST_CASE("the scene follows a window that is resized")
{
    graphics_test::Session session{};
    add_two_views(session);
    {
        Scene scene{};
        scene.render_frames(session, 10);

        // Larger than anything so far: the gpass buffers and the light culling grid grow.
        graphics_test::View &view{session.view(0)};
        view.resize_count = 0;
        view.window.resize(800, 600);
        CHECK(graphics_test::pump_events_until([&] { return view.resize_count > 0; }));
        scene.render_frames(session, 10);
        CHECK(view.surface.width() == view.window.width());
        CHECK(view.surface.height() == view.window.height());

        view.resize_count = 0;
        view.window.resize(260, 200);
        CHECK(graphics_test::pump_events_until([&] { return view.resize_count > 0; }));
        scene.render_frames(session, 10);
        CHECK(view.surface.width() == view.window.width());
    }
    session.shutdown_and_check();
}

TEST_CASE("entities and lights are added, changed and removed while rendering")
{
    graphics_test::Session session{};
    add_two_views(session);
    {
        Scene scene{};
        scene.render_frames(session, 5);

        SUBCASE("render items")
        {
            for (u32 i{0}; i < 6; ++i)
            {
                scene.add_box_entity({-5.f + 2.f * static_cast<f32>(i), 3.f, 3.f});
                scene.render_frames(session, 2);
            }
            CHECK(scene.render_item_count() == 11);
            // Removed resources are released a few frames later: render through that.
            while (scene.render_item_count() > 2)
            {
                scene.remove_last_entity();
                scene.render_frames(session, 2);
            }
            scene.render_frames(session, 5);
        }

        SUBCASE("lights")
        {
            const u32 light_count{scene.light_count()};
            for (u32 i{0}; i < 8; ++i)
            {
                scene.add_point_light(
                    (i % 2) == 0 ? first_light_set : second_light_set,
                    {-6.f + 1.5f * static_cast<f32>(i), 1.f, -2.f},
                    {1.f, 1.f, 0.5f});
                scene.render_frames(session, 2);
            }
            CHECK(scene.light_count() == light_count + 8);

            // Change every light a little, every frame.
            for (u32 frame{0}; frame < 10; ++frame)
            {
                for (u32 i{0}; i < scene.light_count(); ++i)
                {
                    const graphics::Light light{scene.light(i)};
                    if (light.light_type() == graphics::Light::ambient)
                    {
                        continue;
                    }
                    light.intensity(0.5f + 0.1f * static_cast<f32>((frame + i) % 10));
                    light.color({0.2f + 0.08f * static_cast<f32>(frame), 0.6f, 0.9f});
                    light.is_enabled(((frame + i) % 4) != 0);
                }
                scene.render_frame(session);
            }

            while (scene.light_count() > light_count / 2)
            {
                scene.remove_last_light();
                scene.render_frame(session);
            }
            scene.render_frames(session, 5);
        }

        SUBCASE("a view")
        {
            session.remove_view(session.view(1));
            scene.render_frames(session, 5);
            session.add_view("Scene 3", 400, 240, true);
            session.add_view("Scene 4", 240, 240, true);
            scene.render_frames(session, 10);
        }
    }
    session.shutdown_and_check();
}

TEST_CASE("the renderer is restarted with a scene")
{
    for (u32 round{0}; round < 3; ++round)
    {
        CAPTURE(round);
        graphics_test::Session session{};
        add_two_views(session);
        {
            Scene scene{};
            scene.render_frames(session, 15);
        }
        session.shutdown_and_check();
    }
}

TEST_CASE("a scene with 1100 objects is prepared on several threads")
{
    // More objects than one batch of fill_per_object_data() and transform::update_matrices() (1024 each), so the job
    // system's workers take part (when it has any).
    graphics_test::Session session{};
    add_two_views(session);
    {
        Scene scene{};
        constexpr u32 columns{40};
        for (u32 i{0}; i < 1100; ++i)
        {
            const u32 row{i / columns};
            const u32 column{i % columns};
            scene.add_box_entity({static_cast<f32>(column) - 20.f, 0.5f, static_cast<f32>(row) - 14.f});
        }
        CHECK(scene.render_item_count() == 1105);
        scene.render_frames(session, 10);
    }
    session.shutdown_and_check();
}

// Not a test: render preparation in parallel. Run it with
//     x64/Release/GraphicsTests -tc="benchmark*" --no-skip
// and compare PRIMAL_JOBS=0 (one thread) with the default. A frame's CPU time includes waiting for the GPU, so
// keep the window small; the constant buffer (1 MB a frame) limits the scene to about 2,000 objects.
TEST_CASE("benchmark: a frame with 1500 objects" * doctest::skip())
{
    graphics_test::Session session{};
    session.add_view("Benchmark", 320, 240, true);
    {
        Scene scene{};
        constexpr u32 side{39}; // 39 x 39 = 1,521 boxes
        for (u32 x{0}; x < side; ++x)
        {
            for (u32 z{0}; z < side; ++z)
            {
                scene.add_box_entity({static_cast<f32>(x) - 20.f, 0.5f, static_cast<f32>(z) - 20.f});
            }
        }
        scene.render_frames(session, 30);

        constexpr u32 frames{300};
        const auto start{std::chrono::steady_clock::now()};
        scene.render_frames(session, frames);
        const std::chrono::duration<double, std::milli> total{std::chrono::steady_clock::now() - start};
        MESSAGE(
            "threads: " << jobs::worker_count() << ", render items: " << scene.render_item_count()
                        << ", per frame: " << total.count() / frames << " ms");
    }
    session.shutdown_and_check();
}

TEST_SUITE_END();
