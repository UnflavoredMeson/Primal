// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "../EngineDLL/ShaderCompilation.h"
#include "CommonHeaders.h"
#include "Components/Entity.h"
#include "Components/Geometry.h"
#include "Content/ContentToEngine.h"
#include "Graphics/Renderer.h"
#include "Test.h"

#include <filesystem>
#include <iterator>

import primal.jobs;

#if TEST_RENDERER

using namespace primal;

auto create_one_game_entity(
    math::v3 position,
    math::v3 rotation,
    geometry::InitInfo *geometry_info,
    const char *script_name) -> game_entity::Entity;
auto remove_game_entity(game_entity::EntityId id) -> void;
auto read_file(const std::filesystem::path &, std::unique_ptr<u8[]> &, u64 &) -> bool;

namespace
{

id::id_type fan_model_id{id::invalid_id};
id::id_type int_model_id{id::invalid_id};
id::id_type lab_model_id{id::invalid_id};
id::id_type fembot_model_id{id::invalid_id};
id::id_type sphere_model_id{id::invalid_id};

game_entity::EntityId fan_entity_id{id::invalid_id};
game_entity::EntityId int_entity_id{id::invalid_id};
game_entity::EntityId lab_entity_id{id::invalid_id};
game_entity::EntityId fembot_entity_id{id::invalid_id};
game_entity::EntityId sphere_entity_ids[12];
// PRIMAL_TEST_EXTRA_SPHERES=<n>: n more spheres in a grid above the scene, to measure render preparation and
// recording with many render items parallel recording starts above 512 items).
utl::vector<game_entity::EntityId> extra_sphere_entity_ids;

struct TextureUsage
{
    enum Usage : u32
    {
        ambient_occlusion = 0,
        base_color,
        emissive,
        metal_rough,
        normal,

        count
    };
};

id::id_type texture_ids[TextureUsage::count];

id::id_type ibl_brdf_lut_id{id::invalid_id};
id::id_type ibl_diffuse_id{id::invalid_id};
id::id_type ibl_specular_id{id::invalid_id};

id::id_type vs_id{id::invalid_id};
id::id_type ps_id{id::invalid_id};
id::id_type textured_ps_id{id::invalid_id};
id::id_type default_mtl_id{id::invalid_id};
id::id_type fembot_mtl_id{id::invalid_id};

id::id_type pbr_mtl_ids[12];

graphics::Light ibl_light{};

[[nodiscard]] auto load_asset(const char *path, content::AssetType::Type type) -> id::id_type
{
    std::unique_ptr<u8[]> buffer;
    u64 size{0};
    ::read_file(path, buffer, size);

    const id::id_type asset_id{content::create_resource(buffer.get(), type)};
    assert(id::is_valid(asset_id));
    return asset_id;
}

[[nodiscard]] auto load_model(const char *path) -> id::id_type
{
    // load test model
    return load_asset(path, content::AssetType::mesh);
}

[[nodiscard]] auto load_texture(const char *path) -> id::id_type
{
    // load test texture
    return load_asset(path, content::AssetType::texture);
}

auto load_shaders() -> void
{
    // Let's say our material uses a vertex shader and a pixel shader.
    ShaderFileInfo info{};
    info.file_name = "TestShader.hlsl";
    info.function = "TestShaderVS";
    info.type = graphics::ShaderType::vertex;

    const char *shader_path{"../../EngineTest/"};
    // Same backend as graphics::initialize() (DXIL for Direct3D 12, SPIR-V for Vulkan).
    const graphics::GraphicsPlatform platform{::selected_graphics_platform()};

    std::wstring defines[]{L"ELEMENTS_TYPE=0", L"ELEMENTS_TYPE=1", L"ELEMENTS_TYPE=3"};
    utl::vector<u32> keys;
    // Values of tools::elements::elements_type (ContentTools/Geometry.h, which includes Windows-only headers):
    // position_only = 0, static_normal = 1, static_normal_texture = 3. Must match the ELEMENTS_TYPE defines above.
    keys.emplace_back(0u);
    keys.emplace_back(1u);
    keys.emplace_back(3u);

    utl::vector<std::wstring> extra_args{};
    utl::vector<std::unique_ptr<u8[]>> vertex_shaders;
    utl::vector<const u8 *> vertex_shader_pointers;
    for (u32 i{0}; i < std::size(defines); ++i)
    {
        extra_args.clear();
        extra_args.emplace_back(L"-D");
        extra_args.emplace_back(defines[i]);
        vertex_shaders.emplace_back(::compile_shader(info, shader_path, extra_args, false, platform));
        assert(vertex_shaders.back().get());
        vertex_shader_pointers.emplace_back(vertex_shaders.back().get());
    }

    extra_args.clear();
    info.function = "TestShaderPS";
    info.type = graphics::ShaderType::pixel;
    utl::vector<std::unique_ptr<u8[]>> pixel_shaders;

    pixel_shaders.emplace_back(::compile_shader(info, shader_path, extra_args, false, platform));
    assert(pixel_shaders.back().get());

    defines[0] = L"TEXTURED_MTL=1";
    extra_args.emplace_back(L"-D");
    extra_args.emplace_back(defines[0]);

    pixel_shaders.emplace_back(::compile_shader(info, shader_path, extra_args, false, platform));
    assert(pixel_shaders.back().get());

    vs_id = content::add_shader_group(
        vertex_shader_pointers.data(), static_cast<u32>(vertex_shader_pointers.size()), keys.data());

    const u8 *pixel_shader_pointers[]{pixel_shaders[0].get()};
    ps_id = content::add_shader_group(pixel_shader_pointers, 1, &u32_invalid_id);

    pixel_shader_pointers[0] = pixel_shaders[1].get();
    textured_ps_id = content::add_shader_group(pixel_shader_pointers, 1, &u32_invalid_id);
}

auto create_material() -> void
{
    assert(id::is_valid(vs_id) && id::is_valid(ps_id) && id::is_valid(textured_ps_id));
    graphics::MaterialInitInfo info{};
    info.shader_ids[graphics::ShaderType::vertex] = vs_id;
    info.shader_ids[graphics::ShaderType::pixel] = ps_id;
    info.type = graphics::MaterialType::opaque;
    default_mtl_id = content::create_resource(&info, content::AssetType::material);

    std::memset(pbr_mtl_ids, 0xff, sizeof(pbr_mtl_ids));
    const math::v2 metal_rough[std::size(pbr_mtl_ids)]{
        {0.f, 0.0f},
        {0.f, 0.2f},
        {0.f, 0.4f},
        {0.f, 0.6f},
        {0.f, 0.8f},
        {0.f, 1.f},
        {1.f, 0.0f},
        {1.f, 0.2f},
        {1.f, 0.4f},
        {1.f, 0.6f},
        {1.f, 0.8f},
        {1.f, 1.f},
    };
    graphics::MaterialSurface &s{info.surface};
    s.base_color[0] = s.base_color[1] = s.base_color[2] = 172;
    s.base_color[3] = 255;
    s.emissive[0] = 255;
    s.emissive[1] = 172;
    s.emissive[2] = 0;
    s.emissive_intensity = static_cast<u16>(0.f * (65535.f / 10000.f));

    for (u32 i{0}; i < std::size(pbr_mtl_ids); ++i)
    {
        s.metallic = static_cast<u8>(255 * metal_rough[i].x);
        s.roughness = static_cast<u8>(255 * metal_rough[i].y);
        pbr_mtl_ids[i] = content::create_resource(&info, content::AssetType::material);
    }

    info.shader_ids[graphics::ShaderType::pixel] = textured_ps_id;
    info.texture_count = TextureUsage::count;
    info.texture_ids = &texture_ids[0];
    fembot_mtl_id = content::create_resource(&info, content::AssetType::material);
}

auto create_ibl_light() -> void
{
    graphics::LightInitInfo info{};
    info.entity_id = 0;
    info.type = graphics::Light::ambient;
    info.ambient_params.brdf_lut_texture_id = ibl_brdf_lut_id;
    info.ambient_params.diffuse_texture_id = ibl_diffuse_id;
    info.ambient_params.specular_texture_id = ibl_specular_id;

    ibl_light = graphics::create_light(info);
}

auto remove_model(id::id_type model_id) -> void
{
    if (id::is_valid(model_id))
    {
        content::destroy_resource(model_id, content::AssetType::mesh);
    }
}
}

auto create_render_items() -> void
{
    // NOTE: you can get these models if you're a patreon or ko-fi supporter of Primal Engine.
    //       https://www.patreon.com/collection/270663
    //       https://ko-fi.com/gameengineseries/shop
    //       Use the editor to import the scene and put the 3 models in this location.
    //       You can replace them with any model that's available to you.
    assert(std::filesystem::exists("../../x64/lab_model.model"));
    assert(std::filesystem::exists("../../x64/fan_model.model"));
    assert(std::filesystem::exists("../../x64/int_model.model"));
    assert(std::filesystem::exists("../../x64/fembot_model.model"));

    std::memset(&texture_ids[0], 0xff, sizeof(id::id_type) * std::size(texture_ids));

    // Background jobs: loading reads files and creates GPU resources (the old version had a thread per asset).
    const TimeIt::clock::time_point load_start{TimeIt::clock::now()};
    jobs::Counter loads;
    const auto load{[&loads](auto function) { jobs::run(loads, function, jobs::Lane::background); }};
    load([] { texture_ids[TextureUsage::ambient_occlusion] = load_texture("../../x64/ambient_occlusion.texture"); });
    load([] { texture_ids[TextureUsage::base_color] = load_texture("../../x64/base_color.texture"); });
    load([] { texture_ids[TextureUsage::emissive] = load_texture("../../x64/emissive.texture"); });
    load([] { texture_ids[TextureUsage::metal_rough] = load_texture("../../x64/metal_rough.texture"); });
    load([] { texture_ids[TextureUsage::normal] = load_texture("../../x64/normal.texture"); });

    load([] { ibl_brdf_lut_id = load_texture("../../x64/IBL/brdf_lut.texture"); });

    load([] { ibl_diffuse_id = load_texture("../../x64/IBL/Set4/diffuse.texture"); });
    load([] { ibl_specular_id = load_texture("../../x64/IBL/Set4/specular.texture"); });

    load([] { lab_model_id = load_model("../../x64/lab_model.model"); });
    load([] { fan_model_id = load_model("../../x64/fan_model.model"); });
    load([] { int_model_id = load_model("../../x64/int_model.model"); });
    load([] { fembot_model_id = load_model("../../x64/fembot_model.model"); });
    load([] { sphere_model_id = load_model("../../x64/sphere_model.model"); });
    load([] { load_shaders(); });
    jobs::wait(loads);

    const auto load_time{std::chrono::duration<f32, std::milli>(TimeIt::clock::now() - load_start)};
    log::info(
        log::Category::test, "Test content loaded in {:.1f} ms ({} threads)", load_time.count(), jobs::worker_count());

    create_ibl_light();

    // NOTE: we need shaders to be ready before creating materials
    create_material();
    id::id_type materials[]{default_mtl_id};
    id::id_type fembot_materials[]{fembot_mtl_id, fembot_mtl_id};

    geometry::InitInfo geometry_info{};
    geometry_info.material_count = std::size(materials);
    geometry_info.material_ids = &materials[0];

    geometry_info.geometry_content_id = lab_model_id;
    lab_entity_id = ::create_one_game_entity({}, {}, &geometry_info, nullptr).get_id();

    geometry_info.geometry_content_id = fan_model_id;
    fan_entity_id = ::create_one_game_entity({-10.47f, 5.93f, -6.7f}, {}, &geometry_info, "FanScript").get_id();

    geometry_info.geometry_content_id = int_model_id;
    int_entity_id = ::create_one_game_entity({0.f, 1.3f, -6.6f}, {}, &geometry_info, "WibblyWobblyScript").get_id();

    geometry_info.geometry_content_id = fembot_model_id;
    geometry_info.material_count = std::size(fembot_materials);
    geometry_info.material_ids = &fembot_materials[0];
    fembot_entity_id =
        ::create_one_game_entity({-6.f, 0.f, 10.f}, {0.f, math::pi, 0.f}, &geometry_info, "RotatorScript").get_id();

    geometry_info.geometry_content_id = sphere_model_id;
    geometry_info.material_count = 1;
    for (u32 i{0}; i < std::size(sphere_entity_ids); ++i)
    {
        const id::id_type id{pbr_mtl_ids[i]};
        id::id_type sphere_mtls[]{id};
        geometry_info.material_ids = &sphere_mtls[0];
        const f32 x{-6.f + i % 6};
        const f32 y{(i < 6) ? 7.f : 5.5f};
        const f32 z = x;
        sphere_entity_ids[i] = ::create_one_game_entity({x, y, z}, {}, &geometry_info, nullptr).get_id();
    }

    const u32 extra_count{::test_env_u32("PRIMAL_TEST_EXTRA_SPHERES", 0)};
    if (extra_count)
    {
        // A square grid, 1.5 apart, centred over the scene at a height of 12.
        u32 side{1};
        while (side * side < extra_count)
        {
            ++side;
        }
        extra_sphere_entity_ids.resize(extra_count);
        for (u32 i{0}; i < extra_count; ++i)
        {
            id::id_type sphere_mtls[]{pbr_mtl_ids[i % std::size(pbr_mtl_ids)]};
            geometry_info.material_ids = &sphere_mtls[0];
            const f32 x{(static_cast<f32>(i % side) - static_cast<f32>(side) * 0.5f) * 1.5f};
            const f32 z{(static_cast<f32>(i / side) - static_cast<f32>(side) * 0.5f) * 1.5f};
            extra_sphere_entity_ids[i] = ::create_one_game_entity({x, 12.f, z}, {}, &geometry_info, nullptr).get_id();
        }
        log::info(log::Category::test, "{} extra spheres (PRIMAL_TEST_EXTRA_SPHERES)", extra_count);
    }
}

auto extra_render_item_count() -> u32
{
    return static_cast<u32>(extra_sphere_entity_ids.size());
}

auto destroy_render_items() -> void
{
    ::remove_game_entity(lab_entity_id);
    ::remove_game_entity(fan_entity_id);
    ::remove_game_entity(int_entity_id);
    ::remove_game_entity(fembot_entity_id);

    for (u32 i{0}; i < std::size(sphere_entity_ids); ++i)
    {
        ::remove_game_entity(sphere_entity_ids[i]);
    }
    for (const game_entity::EntityId id : extra_sphere_entity_ids)
    {
        ::remove_game_entity(id);
    }
    extra_sphere_entity_ids.clear();

    remove_model(lab_model_id);
    remove_model(fan_model_id);
    remove_model(int_model_id);
    remove_model(fembot_model_id);
    remove_model(sphere_model_id);

    if (ibl_light.is_valid())
    {
        graphics::remove_light(ibl_light.get_id(), 0);
    }

    // remove material
    if (id::is_valid(default_mtl_id))
    {
        content::destroy_resource(default_mtl_id, content::AssetType::material);
    }

    if (id::is_valid(fembot_mtl_id))
    {
        content::destroy_resource(fembot_mtl_id, content::AssetType::material);
    }

    for (const id::id_type id : pbr_mtl_ids)
    {
        if (id::is_valid(id))
        {
            content::destroy_resource(id, content::AssetType::material);
        }
    }

    // remove textures
    for (const id::id_type id : texture_ids)
    {
        if (id::is_valid(id))
        {
            content::destroy_resource(id, content::AssetType::texture);
        }
    }

    if (id::is_valid(ibl_brdf_lut_id))
    {
        content::destroy_resource(ibl_brdf_lut_id, content::AssetType::texture);
    }

    if (id::is_valid(ibl_diffuse_id))
    {
        content::destroy_resource(ibl_diffuse_id, content::AssetType::texture);
    }

    if (id::is_valid(ibl_specular_id))
    {
        content::destroy_resource(ibl_specular_id, content::AssetType::texture);
    }

    // remove shaders and textures
    if (id::is_valid(vs_id))
    {
        content::remove_shader_group(vs_id);
    }

    if (id::is_valid(ps_id))
    {
        content::remove_shader_group(ps_id);
    }

    if (id::is_valid(textured_ps_id))
    {
        content::remove_shader_group(textured_ps_id);
    }
}
#endif
