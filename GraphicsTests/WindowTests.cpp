// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "Platform/Platform.h"
#include "Platform/PlatformTypes.h"
#include "TestSession.h"

#include <doctest.h>

#include <array>

using namespace primal;

// The platform layer's windows, without a renderer (EngineTest's window test, as test cases).
// NOTE: sizes are compared with each other, not with the numbers asked for: a window manager may scale a window
//       (HiDPI) or decide its size itself (tiling).

namespace
{

struct EventCounts
{
    u32 resized{0};
    u32 closed{0};
    platform::WindowId last_window{id::invalid_id};
};

EventCounts events{};

auto count_event(const platform::WindowEvent &event) -> void
{
    events.last_window = event.window;
    if (event.type == platform::WindowEvent::resized)
    {
        ++events.resized;
    }
    else if (event.type == platform::WindowEvent::closed)
    {
        ++events.closed;
    }
}

auto create_window(const char *caption, s32 width, s32 height) -> platform::Window
{
    const platform::WindowInitInfo info{&count_event, nullptr, caption, 60, 60, width, height};
    const platform::Window window{platform::create_window(&info)};
    REQUIRE_MESSAGE(window.is_valid(), "no window: the tests need a display (X11 or Wayland; Xvfb will do)");
    return window;
}

}

TEST_SUITE_BEGIN("platform::Window");

TEST_CASE("windows are created with a native handle and a size, and removed")
{
    std::array<platform::Window, 4> windows{};
    constexpr s32 sizes[4][2]{{200, 400}, {400, 200}, {200, 200}, {400, 300}};
    for (u32 i{0}; i < windows.size(); ++i)
    {
        windows[i] = create_window("Window test", sizes[i][0], sizes[i][1]);
    }
    graphics_test::pump_events();

    for (u32 i{0}; i < windows.size(); ++i)
    {
        CAPTURE(i);
        const platform::Window window{windows[i]};
        CHECK(window.width() > 0);
        CHECK(window.height() > 0);
        const math::u32v4 size{window.size()};
        CHECK(size.z - size.x == window.width());
        CHECK(size.w - size.y == window.height());
        CHECK_FALSE(window.is_closed());
        CHECK_FALSE(window.is_fullscreen());

        // What a graphics backend creates its surface from.
        const platform::NativeWindowHandle *const handle{window.handle()};
        REQUIRE(handle != nullptr);
        CHECK(handle->kind != platform::NativeWindowHandle::none);
        CHECK(handle->display != nullptr);
        CHECK(handle->window != 0);

        for (u32 j{0}; j < i; ++j)
        {
            CHECK(windows[j].get_id() != window.get_id());
            CHECK(windows[j].handle()->window != handle->window);
        }
    }

    // Wider windows were asked to be wider.
    CHECK(windows[1].width() > windows[0].width());
    CHECK(windows[0].height() > windows[1].height());

    for (const platform::Window window : windows)
    {
        platform::remove_window(window.get_id());
    }
    graphics_test::pump_events();
}

TEST_CASE("resize() changes the size and sends a resized event")
{
    const platform::Window window{create_window("Resize test", 300, 200)};
    graphics_test::pump_events();
    const u32 width_before{window.width()};
    const u32 height_before{window.height()};
    events = {};

    window.resize(500, 350);
    const bool resized{graphics_test::pump_events_until([] { return events.resized > 0; })};

    CHECK(resized);
    CHECK(events.last_window == window.get_id());
    CHECK(window.width() > width_before);
    CHECK(window.height() > height_before);

    platform::remove_window(window.get_id());
    graphics_test::pump_events();
}

TEST_CASE("close() hides the window and sends a closed event; the window stays until it is removed")
{
    const platform::Window first{create_window("Close test 1", 200, 200)};
    const platform::Window second{create_window("Close test 2", 200, 200)};
    graphics_test::pump_events();
    events = {};

    first.close();
    const bool closed{graphics_test::pump_events_until([] { return events.closed > 0; })};

    CHECK(closed);
    CHECK(events.closed == 1);
    CHECK(events.last_window == first.get_id());
    CHECK(first.is_closed());
    CHECK_FALSE(second.is_closed());
    // Still a window: only remove_window() destroys it.
    CHECK(first.handle() != nullptr);

    platform::remove_window(first.get_id());
    platform::remove_window(second.get_id());
    graphics_test::pump_events();
}

TEST_CASE("a caption with non-ASCII characters is accepted")
{
    const platform::Window window{create_window("Primal \xe2\x80\x93 f\xc3\xbcnf", 200, 200)};
    window.set_caption("\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e");
    graphics_test::pump_events();
    CHECK(window.is_valid());

    platform::remove_window(window.get_id());
    graphics_test::pump_events();
}

TEST_SUITE_END();
