// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "Graphics/Renderer.h"
#include "TestSession.h"

#include <doctest.h>

using namespace primal;

// Surfaces without a camera or render items: each frame is the gpass clear color shown through post-processing
// (EngineTest's surfaces test and the 6c/6d drivers of the Vulkan port, as test cases). A test passes when nothing
// crashes or asserts and the validation layer (with synchronization validation) reports nothing.

namespace
{

constexpr u32 frame_count{20};

auto add_four_views(graphics_test::Session &session) -> void
{
    session.add_view("Surface 1", 200, 400, false);
    session.add_view("Surface 2", 400, 200, false);
    session.add_view("Surface 3", 200, 200, false);
    session.add_view("Surface 4", 400, 300, false);
}

}

TEST_SUITE_BEGIN("graphics::Surface");

TEST_CASE("the renderer starts and stops without a surface")
{
    graphics_test::Session session{};
    session.shutdown_and_check();
}

TEST_CASE("four surfaces render and have the size of their windows")
{
    graphics_test::Session session{};
    add_four_views(session);

    session.render_empty_frames(frame_count);

    for (u32 i{0}; i < graphics_test::max_views; ++i)
    {
        CAPTURE(i);
        const graphics_test::View &view{session.view(i)};
        REQUIRE(view.is_valid());
        CHECK(view.surface.width() == view.window.width());
        CHECK(view.surface.height() == view.window.height());
    }

    session.shutdown_and_check();
}

TEST_CASE("a surface follows its window when the window is resized")
{
    graphics_test::Session session{};
    add_four_views(session);
    session.render_empty_frames(frame_count);

    graphics_test::View &view{session.view(1)};
    const u32 width_before{view.surface.width()};
    const u32 height_before{view.surface.height()};

    SUBCASE("larger")
    {
        view.resize_count = 0;
        view.window.resize(640, 480);
        const bool resized{graphics_test::pump_events_until([&] { return view.resize_count > 0; })};
        session.render_empty_frames(frame_count);

        CHECK(resized);
        CHECK(view.surface.width() > width_before);
        CHECK(view.surface.height() > height_before);
    }

    SUBCASE("smaller")
    {
        view.resize_count = 0;
        view.window.resize(160, 120);
        const bool resized{graphics_test::pump_events_until([&] { return view.resize_count > 0; })};
        session.render_empty_frames(frame_count);

        CHECK(resized);
        CHECK(view.surface.width() < width_before);
        CHECK(view.surface.height() < height_before);
    }

    SUBCASE("several times in a row, rendering in between")
    {
        constexpr s32 sizes[][2]{{500, 300}, {220, 260}, {640, 360}, {300, 300}};
        for (const auto &size : sizes)
        {
            view.resize_count = 0;
            view.window.resize(static_cast<u32>(size[0]), static_cast<u32>(size[1]));
            const bool resized{graphics_test::pump_events_until([&] { return view.resize_count > 0; })};
            CHECK(resized);
            session.render_empty_frames(3);
        }
    }

    CHECK(view.surface.width() == view.window.width());
    CHECK(view.surface.height() == view.window.height());
    session.shutdown_and_check();
}

TEST_CASE("surfaces are removed and created while the others keep rendering")
{
    graphics_test::Session session{};
    add_four_views(session);
    session.render_empty_frames(frame_count);

    session.remove_view(session.view(0));
    session.remove_view(session.view(2));
    session.render_empty_frames(frame_count);
    CHECK_FALSE(session.view(0).is_valid());
    CHECK(session.view(1).is_valid());

    // The free slots are used again.
    const graphics_test::View &added{session.add_view("Surface 5", 320, 240, false)};
    CHECK(&added == &session.view(0));
    session.render_empty_frames(frame_count);

    session.shutdown_and_check();
}

TEST_CASE("vsync is switched while rendering")
{
    graphics_test::Session session{};
    session.add_view("Vsync 1", 320, 240, false);
    session.add_view("Vsync 2", 240, 320, false);
    session.render_empty_frames(5);

    for (const bool vsync : {true, false, true, false})
    {
        graphics::set_option(graphics::RendererOption::vsync, &vsync, sizeof(vsync));
        bool value{!vsync};
        graphics::get_option(graphics::RendererOption::vsync, &value, sizeof(value));
        CHECK(value == vsync);
        // Each switch re-creates the swapchains (another present mode).
        session.render_empty_frames(5);
    }

    session.shutdown_and_check();
}

TEST_CASE("the renderer is restarted with its surfaces")
{
    for (u32 round{0}; round < 3; ++round)
    {
        CAPTURE(round);
        graphics_test::Session session{};
        add_four_views(session);
        session.render_empty_frames(frame_count);
        session.shutdown_and_check();
    }
}

TEST_SUITE_END();
