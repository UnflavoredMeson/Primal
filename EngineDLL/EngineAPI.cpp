// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "../ContentTools/ToolsCommon.h"
#include "Common.h"
#include "CommonHeaders.h"
#include "Components/Entity.h"
#include "Components/Geometry.h"
#include "Components/Script.h"
#include "Components/Transform.h"
#include "Content/ContentToEngine.h"
#include "Graphics/Renderer.h"
#include "Platform/Platform.h"
#include "Platform/PlatformTypes.h"
#include "ShaderCompilation.h"
#include "Utilities/IOStream.h"
#include "Utilities/Threading.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>

using namespace primal;

namespace
{
HMODULE game_code_dll{nullptr};
using get_script_creator_fn = primal::script::detail::script_creator (*)(size_t);
get_script_creator_fn get_script_creator{nullptr};
using get_script_names_fn = LPSAFEARRAY (*)();
get_script_names_fn get_script_names{nullptr};

struct ViewportSurface : public graphics::RenderSurface
{
    graphics::Camera camera;
    utl::vector<id::id_type> geometry_ids;
};

utl::vector<ViewportSurface> surfaces;
utl::vector<u8> frame_info_buffer;

struct EngineInitError
{
    enum ErrorCode : u32
    {
        succeeded = 0,
        unknown,
        shader_compilation,
        graphics,
    };
};

struct ShaderData
{
    u32 type;
    u32 code_size;
    u32 byte_code_size;
    u32 errors_size;
    u32 assembly_size;
    u32 hash_size;
    u8 *code;
    u8 *byte_code_error_assembly_hash;
    const char *function_name;
    const char *extra_args;
};

struct ShaderGroupData
{
    u32 type;
    u32 count;
    u32 data_size;
    u8 *data;
};

auto patch_material_data(u8 const *data) -> u8 *
{
    utl::BlobStreamReader blob{data};
    const u32 texture_count{blob.read<u32>()};
    if (texture_count)
    {
        id::id_type *const texture_ids{reinterpret_cast<id::id_type *>(const_cast<u8 *>(blob.position()))};
        blob.skip(sizeof(id::id_type) * texture_count);
        *(reinterpret_cast<id::id_type **>(const_cast<u8 *>(blob.position()))) = texture_ids;
    }

    return const_cast<u8 *>(blob.position());
}

auto calculate_thresholds(
    const game_entity::EntityId *const entity_ids,
    f32 *const thresholds,
    u32 count,
    u32 surface_id) -> void
{
    const game_entity::Entity camera{game_entity::EntityId{surfaces[surface_id].camera.entity_id()}};

    math::v3 pos{camera.position()};
    const math::vec camera_pos{math::load(pos)};

    for (u32 i{0}; i < count; ++i)
    {
        assert(id::is_valid(entity_ids[i]));
        pos = game_entity::Entity{entity_ids[i]}.position();
        const math::vec entity_pos{math::load(pos)};
        const math::vec distance{math::length_est3(camera_pos - entity_pos)};
        math::store(thresholds[i], distance);
    }
}

auto create_one_game_entity(
    math::v3 position,
    math::v3 rotation,
    geometry::InitInfo *geometry_info,
    const char *script_name,
    math::v3 scale = {1.f, 1.f, 1.f}) -> game_entity::Entity
{
    transform::InitInfo transform_info{};
    const math::v4 rot_quat{math::quat_from_euler(rotation)};
    std::memcpy(&transform_info.rotation[0], &rot_quat.x, sizeof(transform_info.rotation));
    std::memcpy(&transform_info.position[0], &position.x, sizeof(transform_info.position));
    std::memcpy(&transform_info.scale[0], &scale.x, sizeof(transform_info.scale));

    script::InitInfo script_info{};
    if (script_name)
    {
        script_info.script_creator = script::detail::get_script_creator(script::detail::string_hash()(script_name));
        assert(script_info.script_creator);
    }

    game_entity::EntityInfo entity_info{};
    entity_info.transform = &transform_info;
    entity_info.script = &script_info;
    entity_info.geometry = geometry_info;
    game_entity::Entity ntt{game_entity::create(entity_info)};
    assert(ntt.is_valid());
    return ntt;
}

auto create_camera() -> graphics::Camera
{
    const game_entity::Entity ntt{create_one_game_entity({0, 1, 10}, {0, -math::pi, 0}, nullptr, nullptr)};
    return graphics::create_camera(graphics::PerspectiveCameraInitInfo{ntt.get_id()});
}

auto remove_camera(graphics::Camera &camera) -> void
{
    const game_entity::EntityId id{camera.entity_id()};
    graphics::remove_camera(camera.get_id());
    camera = {};
    game_entity::remove(id);
}

}

extern utl::TicketMutex mutex;
auto to_quat(math::v3 euler_angles, bool is_degrees) -> math::v4;

EDITOR_INTERFACE auto GetEngineVersion(int *major, int *minor, int *revision) -> void
{
    *major = primal::version.major;
    *minor = primal::version.minor;
    *revision = primal::version.revision;
}

EDITOR_INTERFACE auto InitializeEngine() -> EngineInitError::ErrorCode
{
    const graphics::GraphicsPlatform platform{::selected_graphics_platform()};
    while (!::compile_shaders(platform))
    {
        // Pop up a message box allowing the user to retry compilation.
        if (::MessageBoxA(nullptr, "Failed to compile engine shaders.", "Shader Compilation Error", MB_RETRYCANCEL) !=
            IDRETRY)
        {
            return EngineInitError::shader_compilation;
        }
    }

    if (!graphics::initialize(platform))
    {
        return EngineInitError::graphics;
    }

    // Create a light-set for uninitialized light-set keys in the editor
    graphics::create_light_set(0);

    return EngineInitError::succeeded;
}

EDITOR_INTERFACE auto ShutdownEngine() -> void
{
    graphics::remove_light_set(0);
    graphics::shutdown();
}

EDITOR_INTERFACE auto LoadGameCodeDll(const char *dll_path) -> u32
{
    if (game_code_dll)
    {
        return FALSE;
    }
    game_code_dll = ::LoadLibraryA(dll_path);
    assert(game_code_dll);

    get_script_creator = (get_script_creator_fn)::GetProcAddress(game_code_dll, "get_script_creator");
    get_script_names = (get_script_names_fn)::GetProcAddress(game_code_dll, "get_script_names");

    return (game_code_dll && get_script_creator && get_script_names) ? TRUE : FALSE;
}

EDITOR_INTERFACE auto UnloadGameCodeDll() -> u32
{
    if (!game_code_dll)
    {
        return FALSE;
    }
    assert(game_code_dll);
    [[maybe_unused]] const int result{::FreeLibrary(game_code_dll)};
    assert(result);
    game_code_dll = nullptr;
    return TRUE;
}
EDITOR_INTERFACE auto GetScriptCreator(const char *name) -> script::detail::script_creator
{
    return (game_code_dll && get_script_creator) ? get_script_creator(script::detail::string_hash()(name)) : nullptr;
}

EDITOR_INTERFACE auto GetScriptNames() -> LPSAFEARRAY
{
    return (game_code_dll && get_script_names) ? get_script_names() : nullptr;
}

EDITOR_INTERFACE auto CreateRenderSurface(HWND host, s32 width, s32 height) -> u32
{
    const std::scoped_lock lock{mutex};

    assert(host);
    // NOTE: PlatformWin32.cpp tracks the mouse for child windows (WM_MOUSEHOVER/WM_MOUSELEAVE for the editor).
    const platform::NativeWindowHandle parent{
        platform::NativeWindowHandle::win32, nullptr, reinterpret_cast<u64>(host)};
    const platform::WindowInitInfo info{nullptr, &parent, nullptr, 0, 0, width, height};
    ViewportSurface surface{};
    surface.window = platform::create_window(&info);
    surface.surface = graphics::create_surface(surface.window);
    surface.camera = create_camera();
    assert(surface.window.is_valid());
    surfaces.emplace_back(surface);
    return static_cast<u32>(surfaces.size()) - 1;
}

EDITOR_INTERFACE auto RemoveRenderSurface(u32 id) -> void
{
    const std::scoped_lock lock{mutex};
    assert(id < surfaces.size());
    remove_camera(surfaces[id].camera);
    graphics::remove_surface(surfaces[id].surface.get_id());
    platform::remove_window(surfaces[id].window.get_id());
}

EDITOR_INTERFACE auto ResizeRenderSurface(u32 id) -> void
{
    const std::scoped_lock lock{mutex};
    assert(id < surfaces.size());
    const platform::Window &window{surfaces[id].window};
    window.resize(0, 0);
    surfaces[id].surface.resize(window.width(), window.height());
    surfaces[id].camera.aspect_ratio(static_cast<f32>(window.width()) / window.height());
}

EDITOR_INTERFACE auto GetWindowHandle(u32 id) -> HWND
{
    const std::scoped_lock lock{mutex};
    assert(id < surfaces.size());
    return reinterpret_cast<HWND>(surfaces[id].window.handle()->window);
}

EDITOR_INTERFACE auto CreateResource(u8 *data, content::AssetType::Type type) -> id::id_type
{
    if (type == content::AssetType::material)
    {
        data = patch_material_data(data);
    }

    assert(data && type < content::AssetType::count);
    return content::create_resource(data, type);
}

EDITOR_INTERFACE auto DestroyResource(id::id_type id, content::AssetType::Type type) -> void
{
    assert(id::is_valid(id) && type < content::AssetType::count);
    content::destroy_resource(id, type);
}

EDITOR_INTERFACE auto AddShaderGroup(ShaderGroupData const *data) -> id::id_type
{
    assert(data && data->type < graphics::ShaderType::count && data->count && data->data_size && data->data);
    const u32 count{data->count};

    // data->data =
    // {
    //    u32 keys[count];
    //    struct{
    //      u64 bytecode_length;
    //      u8  hash[hash_length];
    //      u8  bytecode[bytecode_length];
    //    } blocks[count];
    // }
    //
    utl::BlobStreamReader blob{data->data};
    const u32 *const keys{reinterpret_cast<const u32 *>(blob.position())};
    blob.skip(count * sizeof(u32)); // skip keys

    const u8 **shader_pointers{(const u8 **)alloca(count * sizeof(u8 *))};

    for (u32 i{0}; i < count; ++i)
    {

        // NOTE: byteCodeLength is a 64-bit value!
        const u32 block_size{sizeof(u64) + content::CompiledShader::hash_length + *(u32 *)blob.position()};
        shader_pointers[i] = blob.position();
        blob.skip(block_size);
    }

    assert(blob.position() == (data->data + data->data_size));

    return content::add_shader_group(shader_pointers, count, keys);
}

EDITOR_INTERFACE auto RemoveShaderGroup(id::id_type id) -> void
{
    content::remove_shader_group(id);
}

EDITOR_INTERFACE auto CompileShader(ShaderData *data) -> u32
{
    assert(data && data->code && data->code_size && data->function_name);
    ShaderFileInfo info{};
    info.function = data->function_name;
    info.type = static_cast<graphics::ShaderType::Type>(data->type);

    const utl::vector<std::string> extra_args{::split(data->extra_args, ';')};
    utl::vector<std::wstring> w_extra_args{};

    for (const auto &str : extra_args)
    {
        w_extra_args.emplace_back(::to_wstring(str.c_str()));
    }

    const std::unique_ptr<u8[]> compiled_shader{
        ::compile_shader(info, data->code, data->code_size, w_extra_args, true)};

    if (!compiled_shader)
    {
        return FALSE;
    }

    u64 buffer_size{0};

    {
        utl::BlobStreamReader blob{compiled_shader.get()};
        data->byte_code_size = static_cast<u32>(blob.read<u64>());
        data->hash_size = content::CompiledShader::hash_length;
        blob.skip(data->hash_size + data->byte_code_size);
        data->errors_size = static_cast<u32>(blob.read<u64>());
        data->assembly_size = static_cast<u32>(blob.read<u64>());
        buffer_size = data->byte_code_size + data->hash_size + data->errors_size + data->assembly_size;
    }

    assert(buffer_size);

    data->byte_code_error_assembly_hash = static_cast<u8 *>(::CoTaskMemAlloc(buffer_size));
    assert(data->byte_code_error_assembly_hash);

    {
        utl::BlobStreamReader blob{compiled_shader.get()};
        blob.skip(sizeof(u64)); // skip the size of byte-code buffer.
        blob.read(&data->byte_code_error_assembly_hash[buffer_size - data->hash_size], data->hash_size);
        blob.read(data->byte_code_error_assembly_hash, data->byte_code_size);
        blob.skip(2 * sizeof(u64)); // skip the size of error and assembly buffers.
        blob.read(&data->byte_code_error_assembly_hash[data->byte_code_size], data->errors_size + data->assembly_size);
    }

    return TRUE;
}

EDITOR_INTERFACE auto SetGeometryIds(u32 surface_id, id::id_type const *geometry_ids, u32 count) -> void
{
    const std::scoped_lock lock{mutex};
    assert(surface_id < surfaces.size());

    ViewportSurface &surface{surfaces[surface_id]};
    surface.geometry_ids.resize(count);

    if (count)
    {
        std::memcpy(surface.geometry_ids.data(), geometry_ids, count * sizeof(id::id_type));
    }
}

EDITOR_INTERFACE auto RenderFrame(u32 surface_id, id::id_type camera_id, u64 light_set) -> void
{
    const std::scoped_lock lock{mutex};
    assert(surface_id < surfaces.size());

    const ViewportSurface &surface{surfaces[surface_id]};
    const u32 count{static_cast<u32>(surface.geometry_ids.size())};
    const u64 item_id_buffer_size{sizeof(id::id_type) * count};
    const u64 threshold_buffer_size{sizeof(f32) * count};
    const u64 entity_id_buffer_size{sizeof(game_entity::EntityId) * count};

    frame_info_buffer.resize(item_id_buffer_size + threshold_buffer_size + entity_id_buffer_size);

    id::id_type *item_ids{reinterpret_cast<id::id_type *>(frame_info_buffer.data())};
    f32 *thresholds{reinterpret_cast<f32 *>(frame_info_buffer.data() + item_id_buffer_size)};
    game_entity::EntityId *entity_ids{reinterpret_cast<game_entity::EntityId *>(
        frame_info_buffer.data() + item_id_buffer_size + threshold_buffer_size)};

    if (count)
    {
        const id::id_type *const geometry_ids{surface.geometry_ids.data()};
        geometry::get_entity_ids(geometry_ids, entity_ids, count);
        geometry::get_render_item_ids(geometry_ids, item_ids, count);
        calculate_thresholds(entity_ids, thresholds, count, surface_id);
    }

    graphics::FrameInfo info{};
    info.render_item_ids = item_ids;
    info.render_item_count = count;
    info.thresholds = thresholds;
    info.camera_id = !id::is_valid(camera_id) ? surface.camera.get_id() : graphics::CameraId{camera_id};
    info.light_set_key = light_set;

    surface.surface.render(info);
}

EDITOR_INTERFACE auto UpdateEditorCamera(
    u32 surface_id,
    f32 pos_x,
    f32 pos_y,
    f32 pos_z,
    f32 rot_x,
    f32 rot_y,
    f32 rot_z) -> void
{
    const std::scoped_lock lock{mutex};
    assert(surface_id < surfaces.size());
    const id::id_type entity_id{surfaces[surface_id].camera.entity_id()};
    assert(id::is_valid(entity_id));
    transform::ComponentCache cache{};
    cache.position = {pos_x, pos_y, pos_z};
    cache.rotation = ::to_quat({rot_x, rot_y, rot_z}, false);
    cache.flags = transform::ComponentFlags::position | transform::ComponentFlags::rotation;
    cache.id = transform::TransformId{entity_id};

    transform::update(&cache, 1);
}

EDITOR_INTERFACE auto SetCameraRange(u32 surface_id, f32 near_z, f32 far_z) -> void
{
    const std::scoped_lock lock{mutex};
    assert(near_z > 0 && far_z >= near_z);
    assert(surface_id < surfaces.size());
    surfaces[surface_id].camera.range(near_z, far_z);
}

EDITOR_INTERFACE auto SetCameraFoV(u32 surface_id, f32 fov) -> void
{
    const std::scoped_lock lock{mutex};
    assert(fov > 0);
    assert(surface_id < surfaces.size());
    fov *= (1.f / 180.f);
    surfaces[surface_id].camera.field_of_view(fov);
}

EDITOR_INTERFACE auto CreateLightSet(const char *light_set_key) -> u64
{
    const std::scoped_lock lock{mutex};
    assert(light_set_key && light_set_key[0]);
    const u64 key{std::hash<std::string>()(light_set_key)};
    graphics::create_light_set(key);
    return key;
}

EDITOR_INTERFACE auto RemoveLightSet(u64 light_set_key) -> void
{
    const std::scoped_lock lock{mutex};
    graphics::remove_light_set(light_set_key);
}

// data = {
//  u32         type;
//  f32         intensity;
//  math::v3    color;
//  u32         is_enabled;
//  math::u32v3 diffuse, specular, brdf_lut; (texture ids for ambient lights)
//  f32         range; (for point and spot lights)
//  math::v3    attenuation; (for point and spot lights)
//  f32         umbra; (for spot lights)
//  f32         penumbra; (for spot lights)
// }
//
EDITOR_INTERFACE auto CreateLight(
    id::id_type entity_id,
    u64 light_set_key,
    const u8 *const data,
    [[maybe_unused]] u32 data_size) -> id::id_type
{
    const std::scoped_lock lock{mutex};
    assert(id::is_valid(entity_id) && data && data_size);
    utl::BlobStreamReader blob{data};
    graphics::LightInitInfo info{};
    info.light_set_key = light_set_key;
    info.entity_id = entity_id;
    info.type = static_cast<graphics::Light::Type>(blob.read<id::id_type>());
    info.intensity = blob.read<f32>();
    info.color = {blob.read<f32>(), blob.read<f32>(), blob.read<f32>()};
    info.is_enabled = blob.read<u32>() != 0;

    if (info.type == graphics::Light::ambient)
    {
        info.ambient_params.diffuse_texture_id = blob.read<id::id_type>();
        info.ambient_params.specular_texture_id = blob.read<id::id_type>();
        info.ambient_params.brdf_lut_texture_id = blob.read<id::id_type>();
    }
    else if (info.type == graphics::Light::point)
    {
        info.point_params.range = blob.read<f32>();
        info.point_params.attenuation = {blob.read<f32>(), blob.read<f32>(), blob.read<f32>()};
    }
    else if (info.type == graphics::Light::spot)
    {
        info.spot_params.range = blob.read<f32>();
        info.spot_params.attenuation = {blob.read<f32>(), blob.read<f32>(), blob.read<f32>()};
        info.spot_params.umbra = blob.read<f32>() * math::to_rad;
        info.spot_params.penumbra = blob.read<f32>() * math::to_rad;
    }

    return graphics::create_light(info).get_id();
}

EDITOR_INTERFACE auto RemoveLight(id::id_type light_id, u64 light_set_key) -> void
{
    const std::scoped_lock lock{mutex};
    assert(id::is_valid(light_id));
    graphics::remove_light(graphics::LightId{light_id}, light_set_key);
}

EDITOR_INTERFACE auto GetLightIsEnabled(id::id_type const *ids, u64 const *light_set_keys, u32 *is_enabled, u32 count)
    -> void
{
    const std::scoped_lock lock{mutex};
    assert(ids && light_set_keys && is_enabled && count);

    for (u32 i{0}; i < count; ++i)
    {
        assert(id::is_valid(ids[i]));
        const graphics::Light light{graphics::LightId{ids[i]}, light_set_keys[i]};
        is_enabled[i] = light.is_enabled() ? 1 : 0;
    }
}

EDITOR_INTERFACE auto GetLightIntensity(id::id_type const *ids, u64 const *light_set_keys, f32 *intensities, u32 count)
    -> void
{
    const std::scoped_lock lock{mutex};
    assert(ids && light_set_keys && intensities && count);

    for (u32 i{0}; i < count; ++i)
    {
        assert(id::is_valid(ids[i]));
        const graphics::Light light{graphics::LightId{ids[i]}, light_set_keys[i]};
        intensities[i] = light.intensity();
    }
}

EDITOR_INTERFACE auto GetLightRange(id::id_type const *ids, u64 const *light_set_keys, f32 *ranges, u32 count) -> void
{
    const std::scoped_lock lock{mutex};
    assert(ids && light_set_keys && ranges && count);

    for (u32 i{0}; i < count; ++i)
    {
        assert(id::is_valid(ids[i]));
        const graphics::Light light{graphics::LightId{ids[i]}, light_set_keys[i]};
        ranges[i] = light.range();
    }
}

EDITOR_INTERFACE auto GetLightConeAngles(
    id::id_type const *ids,
    u64 const *light_set_keys,
    f32 *umbras,
    f32 *penumbras,
    u32 count) -> void
{
    const std::scoped_lock lock{mutex};
    assert(ids && light_set_keys && umbras && penumbras && count);

    for (u32 i{0}; i < count; ++i)
    {
        assert(id::is_valid(ids[i]));
        const graphics::Light light{graphics::LightId{ids[i]}, light_set_keys[i]};
        umbras[i] = light.umbra() * math::to_deg;
        penumbras[i] = light.penumbra() * math::to_deg;
    }
}

EDITOR_INTERFACE auto GetLightColor(
    id::id_type const *ids,
    u64 const *light_set_keys,
    f32 *r,
    f32 *g,
    f32 *b,
    u32 count) -> void
{
    const std::scoped_lock lock{mutex};
    assert(ids && light_set_keys && r && g && b && count);

    for (u32 i{0}; i < count; ++i)
    {
        assert(id::is_valid(ids[i]));
        const graphics::Light light{graphics::LightId{ids[i]}, light_set_keys[i]};
        const math::v3 color{light.color()};
        r[i] = color.x;
        g[i] = color.y;
        b[i] = color.z;
    }
}

EDITOR_INTERFACE auto GetLightAttenuation(
    id::id_type const *ids,
    u64 const *light_set_keys,
    f32 *a,
    f32 *b,
    f32 *c,
    u32 count) -> void
{
    const std::scoped_lock lock{mutex};
    assert(ids && light_set_keys && a && b && c && count);

    for (u32 i{0}; i < count; ++i)
    {
        assert(id::is_valid(ids[i]));
        const graphics::Light light{graphics::LightId{ids[i]}, light_set_keys[i]};
        const math::v3 attenuation{light.attenuation()};
        a[i] = attenuation.x;
        b[i] = attenuation.y;
        c[i] = attenuation.z;
    }
}

EDITOR_INTERFACE auto SetLightIsEnabled(
    id::id_type const *ids,
    u64 const *light_set_keys,
    u32 const *is_enabled,
    u32 count) -> void
{
    const std::scoped_lock lock{mutex};
    assert(ids && light_set_keys && is_enabled && count);

    for (u32 i{0}; i < count; ++i)
    {
        assert(id::is_valid(ids[i]));
        const graphics::Light light{graphics::LightId{ids[i]}, light_set_keys[i]};
        light.is_enabled(is_enabled[i] != 0);
    }
}

EDITOR_INTERFACE auto SetLightIntensity(
    id::id_type const *ids,
    u64 const *light_set_keys,
    f32 const *intensities,
    u32 count) -> void
{
    const std::scoped_lock lock{mutex};
    assert(ids && light_set_keys && intensities && count);

    for (u32 i{0}; i < count; ++i)
    {
        assert(id::is_valid(ids[i]));
        const graphics::Light light{graphics::LightId{ids[i]}, light_set_keys[i]};
        light.intensity(intensities[i]);
    }
}

EDITOR_INTERFACE auto SetLightRange(id::id_type const *ids, u64 const *light_set_keys, f32 const *ranges, u32 count)
    -> void
{
    const std::scoped_lock lock{mutex};
    assert(ids && light_set_keys && ranges && count);

    for (u32 i{0}; i < count; ++i)
    {
        assert(id::is_valid(ids[i]));
        const graphics::Light light{graphics::LightId{ids[i]}, light_set_keys[i]};
        light.range(ranges[i]);
    }
}

EDITOR_INTERFACE auto SetLightConeAngles(
    id::id_type const *ids,
    u64 const *light_set_keys,
    f32 const *umbras,
    f32 const *penumbras,
    u32 count) -> void
{
    const std::scoped_lock lock{mutex};
    assert(ids && light_set_keys && umbras && penumbras && count);

    for (u32 i{0}; i < count; ++i)
    {
        assert(id::is_valid(ids[i]));
        const graphics::Light light{graphics::LightId{ids[i]}, light_set_keys[i]};
        light.cone_angles(umbras[i] * math::to_rad, penumbras[i] * math::to_rad);
    }
}

EDITOR_INTERFACE auto SetLightColor(
    id::id_type const *ids,
    u64 const *light_set_keys,
    f32 const *r,
    f32 const *g,
    f32 const *b,
    u32 count) -> void
{
    const std::scoped_lock lock{mutex};
    assert(ids && light_set_keys && r && g && b && count);

    for (u32 i{0}; i < count; ++i)
    {
        assert(id::is_valid(ids[i]));
        const graphics::Light light{graphics::LightId{ids[i]}, light_set_keys[i]};
        light.color({r[i], g[i], b[i]});
    }
}

EDITOR_INTERFACE auto SetLightAttenuation(
    id::id_type const *ids,
    u64 const *light_set_keys,
    f32 const *a,
    f32 const *b,
    f32 const *c,
    u32 count) -> void
{
    const std::scoped_lock lock{mutex};
    assert(ids && light_set_keys && a && b && c && count);

    for (u32 i{0}; i < count; ++i)
    {
        assert(id::is_valid(ids[i]));
        const graphics::Light light{graphics::LightId{ids[i]}, light_set_keys[i]};
        light.attenuation({a[i], b[i], c[i]});
    }
}
