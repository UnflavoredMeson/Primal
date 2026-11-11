// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "TestSession.h"

#include "Components/Transform.h"
#include "Graphics/Vulkan/VulkanInterface.h"
#include "ShaderCompilation.h"

#include <doctest.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <thread>

using namespace primal;

namespace graphics_test
{
namespace
{

// NOTE: global, because the window event callback is a plain function.
std::array<View, max_views> views{};

// CI sets PRIMAL_TEST_REQUIRE_VALIDATION=1: a debug build that runs without the validation layer (not installed,
// or turned off) then fails, instead of passing without having checked anything.
auto is_validation_required() -> bool
{
#ifdef _DEBUG
    constexpr const char *name{"PRIMAL_TEST_REQUIRE_VALIDATION"};
#ifdef _MSC_VER
    // std::getenv is deprecated by MSVC (C4996).
    char *env{nullptr};
    size_t size{0};
    if (_dupenv_s(&env, &size, name) != 0 || !env)
    {
        return false;
    }
    const bool required{env[0] == '1'};
    std::free(env);
    return required;
#else
    const char *const env{std::getenv(name)};
    return env && env[0] == '1';
#endif
#else
    // Validation is only on in debug builds.
    return false;
#endif
}

auto find_view(platform::WindowId id) -> View *
{
    for (View &view : views)
    {
        if (view.window.is_valid() && view.window.get_id() == id)
        {
            return &view;
        }
    }
    return nullptr;
}

auto on_window_event(const platform::WindowEvent &event) -> void
{
    View *const view{find_view(event.window)};
    if (!view)
    {
        return;
    }

    switch (event.type)
    {
        case platform::WindowEvent::resized:
        {
            const u32 width{view->window.width()};
            const u32 height{view->window.height()};
            ++view->resize_count;
            if (view->surface.is_valid())
            {
                view->surface.resize(width, height);
            }
            if (view->camera.is_valid() && height)
            {
                view->camera.aspect_ratio(static_cast<f32>(width) / static_cast<f32>(height));
            }
        }
        break;
        case platform::WindowEvent::closed: view->is_closed = true; break;
        default: break;
    }
}

auto destroy_view(View &view) -> void
{
    const View temp{view};
    view = {};
    // NOTE: the graphics surface goes first, it presents to the window.
    if (temp.surface.is_valid())
    {
        graphics::remove_surface(temp.surface.get_id());
    }
    if (temp.window.is_valid())
    {
        platform::remove_window(temp.window.get_id());
    }
    if (temp.camera.is_valid())
    {
        graphics::remove_camera(temp.camera.get_id());
    }
    if (temp.camera_entity.is_valid())
    {
        game_entity::remove(temp.camera_entity.get_id());
    }
}

}

auto pump_events() -> void
{
    REQUIRE(platform::pump_events());
}

auto pump_events_until(const std::function<bool()> &done, std::chrono::milliseconds timeout) -> bool
{
    const auto deadline{std::chrono::steady_clock::now() + timeout};
    while (true)
    {
        pump_events();
        if (done())
        {
            return true;
        }
        if (std::chrono::steady_clock::now() >= deadline)
        {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
}

auto create_entity(
    math::v3 position,
    math::v3 rotation,
    geometry::InitInfo *geometry_info,
    script::detail::script_creator script_creator) -> game_entity::Entity
{
    transform::InitInfo transform_info{};
    const DirectX::XMVECTOR quat{DirectX::XMQuaternionRotationRollPitchYawFromVector(DirectX::XMLoadFloat3(&rotation))};
    math::v4 rot_quat{};
    DirectX::XMStoreFloat4(&rot_quat, quat);
    std::memcpy(&transform_info.rotation[0], &rot_quat.x, sizeof(transform_info.rotation));
    std::memcpy(&transform_info.position[0], &position.x, sizeof(transform_info.position));

    script::InitInfo script_info{script_creator};
    const game_entity::EntityInfo entity_info{
        .transform = &transform_info,
        .script = script_creator ? &script_info : nullptr,
        .geometry = geometry_info,
    };
    const game_entity::Entity entity{game_entity::create(entity_info)};
    REQUIRE(entity.is_valid());
    return entity;
}

Session::Session()
{
    REQUIRE_MESSAGE(
        ::compile_shaders(graphics::GraphicsPlatform::vulkan), "the engine shaders (shaders.bin) didn't compile");
    REQUIRE_MESSAGE(
        graphics::initialize(graphics::GraphicsPlatform::vulkan),
        "the Vulkan renderer didn't start (is there a Vulkan 1.3 device? The log above says what is missing)");
    _is_running = true;

    const bool vsync{false};
    graphics::set_option(graphics::RendererOption::vsync, &vsync, sizeof(vsync));
}

Session::~Session()
{
    shutdown();
}

auto Session::add_view(const char *caption, s32 width, s32 height, bool with_camera) -> View &
{
    const auto free_view{
        std::ranges::find_if(views, [](const View &candidate) { return !candidate.window.is_valid(); })};
    REQUIRE_MESSAGE(free_view != views.end(), "all views are in use");
    View &view{*free_view};

    const platform::WindowInitInfo info{&on_window_event, nullptr, caption, 50, 50, width, height};
    view.window = platform::create_window(&info);
    REQUIRE_MESSAGE(view.window.is_valid(), "no window: the tests need a display (X11 or Wayland; Xvfb will do)");
    view.surface = graphics::create_surface(view.window);
    REQUIRE(view.surface.is_valid());

    if (with_camera)
    {
        view.camera_entity = create_entity({0.f, 3.f, -10.f}, {0.25f, 0.f, 0.f});
        view.camera = graphics::create_camera(graphics::PerspectiveCameraInitInfo{view.camera_entity.get_id()});
        REQUIRE(view.camera.is_valid());
        view.camera.aspect_ratio(static_cast<f32>(view.window.width()) / static_cast<f32>(view.window.height()));
    }
    return view;
}

auto Session::remove_view(View &view) -> void
{
    destroy_view(view);
}

auto Session::view(u32 index) -> View &
{
    REQUIRE(index < max_views);
    return views[index];
}

auto Session::render_empty_frames(u32 count) -> void
{
    for (u32 i{0}; i < count; ++i)
    {
        pump_events();
        for (const View &view : views)
        {
            if (view.is_valid())
            {
                view.surface.render({});
            }
        }
    }
}

auto Session::shutdown() -> void
{
    if (!_is_running)
    {
        return;
    }
    _is_running = false;
    for (View &view : views)
    {
        destroy_view(view);
    }
    graphics::shutdown();
}

auto Session::shutdown_and_check() -> void
{
    REQUIRE(_is_running);
    shutdown();

    // After shutdown(): destroying the device is when the layer reports what was leaked.
    const graphics::vulkan::ValidationStats stats{graphics::vulkan::validation_stats()};
    CHECK_MESSAGE(stats.error_count == 0, "validation errors were reported: see the '::Vulkan ERROR' lines above");
    CHECK_MESSAGE(stats.warning_count == 0, "validation warnings were reported: see the '::Vulkan warning' lines");
    if (is_validation_required())
    {
        CHECK_MESSAGE(stats.is_enabled, "PRIMAL_TEST_REQUIRE_VALIDATION=1, but the validation layer wasn't active");
    }
}

}
