// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "CommonHeaders.h"
#include "Components/Entity.h"
#include "Components/Geometry.h"
#include "Components/Script.h"
#include "Graphics/Renderer.h"
#include "Platform/Platform.h"
#include "Platform/PlatformTypes.h"

#include <chrono>
#include <functional>

// What the graphics tests share: the renderer session (start, stop, and the check that the Vulkan validation layer
// reported nothing), the windows with their surfaces and cameras, and the event loop.
namespace graphics_test
{

constexpr u32 max_views{4};

// A window with a graphics surface and, for the scene tests, a camera on its own entity.
struct View
{
    primal::platform::Window window{};
    primal::graphics::Surface surface{};
    primal::game_entity::Entity camera_entity{};
    primal::graphics::Camera camera{};
    u32 resize_count{0};   // 'resized' events seen
    bool is_closed{false}; // a 'closed' event was seen

    [[nodiscard]] auto is_valid() const -> bool
    {
        return surface.is_valid();
    }
};

// Processes the pending window events once. The tests never ask to quit, so this fails the test if the platform
// layer says the application should.
auto pump_events() -> void;
// Pumps events until done() is true. Returns false when it still isn't after the timeout.
[[nodiscard]] auto pump_events_until(
    const std::function<bool()> &done,
    std::chrono::milliseconds timeout = std::chrono::milliseconds{3000}) -> bool;

// An entity with a transform (rotation: pitch, yaw, roll in radians) and, optionally, geometry and a script.
[[nodiscard]] auto create_entity(
    primal::math::v3 position,
    primal::math::v3 rotation = {},
    primal::geometry::InitInfo *geometry_info = nullptr,
    primal::script::detail::script_creator script_creator = nullptr) -> primal::game_entity::Entity;

// The renderer, from graphics::initialize() to graphics::shutdown(). One at a time.
// A test ends with shutdown_and_check(); the destructor only cleans up after a failed REQUIRE.
class Session
{
  public:
    // Compiles the engine shaders if they changed and starts the Vulkan renderer. Vsync is turned off so the tests
    // don't wait for a display.
    Session();
    DISABLE_COPY_AND_MOVE(Session);
    ~Session();

    // A window of the given size with a surface; with_camera adds a camera entity that looks at the origin from
    // (0, 3, -10).
    auto add_view(const char *caption, s32 width, s32 height, bool with_camera) -> View &;
    auto remove_view(View &view) -> void;
    [[nodiscard]] auto view(u32 index) -> View &;

    // Pumps events and renders a frame without a camera or render items on every view, 'count' times.
    auto render_empty_frames(u32 count) -> void;

    // Removes the views, shuts the renderer down, and checks that validation reported no error or warning.
    auto shutdown_and_check() -> void;

  private:
    auto shutdown() -> void;

    bool _is_running{false};
};

}
