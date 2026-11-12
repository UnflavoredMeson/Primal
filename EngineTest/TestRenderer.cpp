// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "TestRenderer.h"

#include "../EngineDLL/ShaderCompilation.h"
#include "Components/Entity.h"
#include "Components/Geometry.h"
#include "Components/Script.h"
#include "Components/Transform.h"
#include "Content/ContentToEngine.h"
#include "Graphics/Renderer.h"
#include "Input/Input.h"
#include "Platform/Platform.h"
#include "Platform/PlatformTypes.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#if TEST_RENDERER

using namespace primal;

// Multithreading test worker spawn code ////////////////////////////////////////////
#define ENABLE_TEST_WORKERS 0

constexpr u32 num_threads{8};
bool shutdown{false};
std::thread workers[num_threads];

utl::vector<u8> buffer(1024 * 1024, 0);
// Test worker for upload context
auto buffer_test_worker() -> void
{
    while (!shutdown)
    {
        // auto* resource = graphics::d3d12::d3dx::create_buffer(buffer.data(), (u32)buffer.size());
        //  NOTE: We can also use core::release(resource) since we're not using the buffer for rendering.
        //        However, this is a nice test for deferred_release functionality.
        // graphics::d3d12::core::deferred_release(resource);
    }
}

template <class FnPtr, class... Args>
auto init_test_workers([[maybe_unused]] FnPtr &&fn, [[maybe_unused]] Args &&...args) -> void
{
#if ENABLE_TEST_WORKERS
    shutdown = false;
    for (auto &w : workers)
    {
        w = std::thread(std::forward<FnPtr>(fn), std::forward<Args>(args)...);
    }
#endif
}

auto joint_test_workers() -> void
{
#if ENABLE_TEST_WORKERS
    shutdown = true;
    for (auto &w : workers)
    {
        w.join();
    }
#endif
}
/////////////////////////////////////////////////////////////////////////////////////

struct CameraSurface
{
    game_entity::Entity entity;
    graphics::Camera camera;
    graphics::RenderSurface surface{};
};

CameraSurface surfaces[4]{};
TimeIt timer{};

bool resized{false};
bool is_restarting{false};
bool is_graphics_initialized{false};

utl::vector<id::id_type> render_item_id_cache;

auto destroy_camera_surface(CameraSurface &surface) -> void;
auto test_initialize() -> bool;
auto test_shutdown() -> void;
auto create_render_items() -> void;
auto destroy_render_items() -> void;
auto generate_lights() -> void;
auto remove_lights() -> void;
auto test_lights(f32 dt) -> void;

auto find_camera_surface(platform::WindowId id) -> CameraSurface *
{
    for (u32 i{0}; i < std::size(surfaces); ++i)
    {
        if (surfaces[i].surface.window.is_valid() && surfaces[i].surface.window.get_id() == id)
        {
            return &surfaces[i];
        }
    }
    return nullptr;
}

auto on_window_event(const platform::WindowEvent &event) -> void
{
    using input::InputCode;
    using platform::WindowEvent;

    switch (event.type)
    {
        case WindowEvent::closed:
        {
            bool all_closed{true};
            for (u32 i{0}; i < std::size(surfaces); ++i)
            {
                if (surfaces[i].surface.window.is_valid())
                {
                    if (surfaces[i].surface.window.is_closed())
                    {
                        ::destroy_camera_surface(surfaces[i]);
                    }
                    else
                    {
                        all_closed = false;
                    }
                }
            }
            if (all_closed && !is_restarting)
            {
                platform::post_quit();
            }
        }
        break;
        case WindowEvent::resized:
            if (CameraSurface const *const surface{::find_camera_surface(event.window)})
            {
                const platform::Window win{surface->surface.window};
                surface->surface.surface.resize(win.width(), win.height());
                surface->camera.aspect_ratio(static_cast<f32>(win.width()) / win.height());
            }
            break;
        case WindowEvent::key_down:
            if (event.key == InputCode::key_escape)
            {
                platform::Window{event.window}.close();
            }
            else if (event.key == InputCode::key_space)
            {
                bool vsync{false};
                graphics::get_option(graphics::RendererOption::vsync, &vsync, sizeof(bool));
                vsync = !vsync;
                graphics::set_option(graphics::RendererOption::vsync, &vsync, sizeof(bool));
            }
            else if (event.key == InputCode::key_f11)
            {
                is_restarting = true;
                ::test_shutdown();
                ::test_initialize();
            }
            else if (event.key == InputCode::key_return && (event.modifiers & input::ModifierKey::alt))
            {
                if (CameraSurface const *const surface{::find_camera_surface(event.window)})
                {
                    const platform::Window win{surface->surface.window};
                    win.set_fullscreen(!win.is_fullscreen());
                }
            }
            break;
        default: break;
    }
}

auto create_one_game_entity(
    math::v3 position,
    math::v3 rotation,
    geometry::InitInfo *geometry_info,
    const char *script_name) -> game_entity::Entity
{
    transform::InitInfo transform_info{};
    const math::v4 rot_quat{math::quat_from_euler(rotation)};
    std::memcpy(&transform_info.rotation[0], &rot_quat.x, sizeof(transform_info.rotation));
    std::memcpy(&transform_info.position[0], &position.x, sizeof(transform_info.position));

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

auto remove_game_entity(game_entity::EntityId id) -> void
{
    game_entity::remove(id);
}

auto read_file(const std::filesystem::path &path, std::unique_ptr<u8[]> &data, u64 &size) -> bool
{
    if (!std::filesystem::exists(path))
    {
        return false;
    }

    size = std::filesystem::file_size(path);
    assert(size);
    if (!size)
    {
        return false;
    }
    data = std::make_unique<u8[]>(size);
    std::ifstream file{path, std::ios::in | std::ios::binary};
    if (!file || !file.read(reinterpret_cast<char *>(data.get()), size))
    {
        file.close();
        return false;
    }

    file.close();
    return true;
}

auto create_camera_surface(CameraSurface &surface, platform::WindowInitInfo info) -> void
{
    surface.surface.window = platform::create_window(&info);
    surface.surface.surface = graphics::create_surface(surface.surface.window);
    surface.entity = ::create_one_game_entity({13.76f, 3.f, -1.1f}, {-0.137f, -1.70f, 0.f}, nullptr, "CameraScript");
    // surface.entity = create_one_game_entity({ -5.49f, 1.73f, 9.26f }, { 0.19f, 5.61f, 0.f }, nullptr,
    // "CameraScript");
    surface.camera = graphics::create_camera(graphics::PerspectiveCameraInitInfo{surface.entity.get_id()});
    surface.camera.aspect_ratio(static_cast<f32>(surface.surface.window.width()) / surface.surface.window.height());
}

auto destroy_camera_surface(CameraSurface &surface) -> void
{
    const CameraSurface temp{surface};
    surface = {};

    if (temp.surface.surface.is_valid())
    {
        graphics::remove_surface(temp.surface.surface.get_id());
    }
    if (temp.surface.window.is_valid())
    {
        platform::remove_window(temp.surface.window.get_id());
    }
    if (temp.camera.is_valid())
    {
        graphics::remove_camera(temp.camera.get_id());
    }
    if (temp.entity.is_valid())
    {
        game_entity::remove(temp.entity.get_id());
    }
}

auto test_initialize() -> bool
{
    const graphics::GraphicsPlatform platform{::selected_graphics_platform()};
    while (!::compile_shaders(platform))
    {
        log::error(log::Category::test, "Failed to compile the engine shaders.");
#if PRIMAL_PLATFORM_WINDOWS
        // Pop up a message box allowing the user to retry compilation.
        if (::MessageBox(nullptr, L"Failed to compile engine shaders.", L"Shader Compilation Error", MB_RETRYCANCEL) !=
            IDRETRY)
        {
            return false;
        }
#else
        return false;
#endif
    }

    if (!graphics::initialize(platform))
    {
        return false;
    }
    is_graphics_initialized = true;

    const platform::WindowInitInfo info[]{
        {&::on_window_event, nullptr, "Render window 1", 100, 100, 400, 800},
        {&::on_window_event, nullptr, "Render window 2", 150, 150, 800, 400},
        {&::on_window_event, nullptr, "Render window 3", 200, 200, 400, 400},
        {&::on_window_event, nullptr, "Render window 4", 250, 250, 800, 600},
    };
    static_assert(std::size(info) == std::size(surfaces));

    for (u32 i{0}; i < std::size(surfaces); ++i)
    {
        ::create_camera_surface(surfaces[i], info[i]);
    }

    ::init_test_workers(::buffer_test_worker);

    ::generate_lights();

    ::create_render_items();

    render_item_id_cache.resize(4 + 12);
    geometry::get_render_item_ids(render_item_id_cache.data(), static_cast<u32>(render_item_id_cache.size()));

    input::InputSource source{};
    source.binding = std::hash<std::string>()("move");
    source.source_type = input::InputSource::keyboard;
    source.code = input::InputCode::key_a;
    source.multiplier = 1.f;
    source.axis = input::Axis::x;
    input::bind(source);

    source.code = input::InputCode::key_d;
    source.multiplier = -1.f;
    input::bind(source);

    source.code = input::InputCode::key_w;
    source.multiplier = 1.f;
    source.axis = input::Axis::z;
    input::bind(source);

    source.code = input::InputCode::key_s;
    source.multiplier = -1.f;
    input::bind(source);

    source.code = input::InputCode::key_q;
    source.multiplier = -1.f;
    source.axis = input::Axis::y;
    input::bind(source);

    source.code = input::InputCode::key_e;
    source.multiplier = 1.f;
    input::bind(source);

    is_restarting = false;
    return true;
}

auto test_shutdown() -> void
{
    // NOTE: shader compilation or graphics::initialize() failed: nothing was created.
    if (!is_graphics_initialized)
    {
        return;
    }
    is_graphics_initialized = false;

    input::unbind(std::hash<std::string>()("move"));
    ::destroy_render_items();
    ::remove_lights();
    ::joint_test_workers();

    for (u32 i{0}; i < std::size(surfaces); ++i)
    {
        ::destroy_camera_surface(surfaces[i]);
    }

    graphics::shutdown();
}

auto EngineTest::initialize() -> bool
{
    return ::test_initialize();
}

auto EngineTest::run() -> void
{
    [[maybe_unused]] static u32 counter{0}; // used by the commented-out light set switch below
    static const u32 light_set_key{0};
    ++counter;
    // if ((counter % 90) == 0) light_set_key = (light_set_key + 1) % 2;

    timer.begin();
    const f32 dt{timer.dt_avg()};
    script::update(dt);
    // test_lights(dt);
    for (u32 i{0}; i < std::size(surfaces); ++i)
    {
        if (surfaces[i].surface.surface.is_valid())
        {
            f32 thresholds[4 + 12]{};

            graphics::FrameInfo info{};
            info.render_item_ids = render_item_id_cache.data() + 0;
            info.render_item_count = 4 + 12 - 0;
            info.thresholds = &thresholds[0];
            info.light_set_key = light_set_key;
            info.average_frame_time = dt;
            info.camera_id = surfaces[i].camera.get_id();

            assert(std::size(thresholds) >= info.render_item_count);
            surfaces[i].surface.surface.render(info);
        }
    }
    timer.end();
}

auto EngineTest::shutdown() -> void
{
    ::test_shutdown();
}

#endif
