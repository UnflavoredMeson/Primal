// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "Test.h"
#include "../Engine/Platform/PlatformTypes.h"
#include "../Engine/Platform/Platform.h"
#include "../Engine/Graphics/Renderer.h"
#include "../EngineDLL/ShaderCompilation.h"
#include <cstdio>
#include <iterator>

// 4 windows, each with a graphics surface. No camera or render items, so each frame is the gpass clear color
// (gray in debug builds, black in release) shown through post-processing.
//   Esc: close a window   Space: toggle vsync   F11: restart   Alt+Enter: fullscreen
// NOTE: Vulkan only for now. The D3D12 backend renders the full scene in render_surface() and
//       needs a camera, so it isn't usable with this test.

using namespace primal;

namespace surfaces_test {

graphics::render_surface test_surfaces[4]{};
bool is_restarting{ false };
time_it timer{};

bool test_initialize();
void test_shutdown();

graphics::render_surface*
find_surface(platform::window_id id)
{
    for (auto& surface : test_surfaces)
    {
        if (surface.window.is_valid() && surface.window.get_id() == id) return &surface;
    }
    return nullptr;
}

void
destroy_surface(graphics::render_surface& surface)
{
    graphics::render_surface temp{ surface };
    surface = {};
    // NOTE: the graphics surface goes first, it presents to the window.
    if (temp.surface.is_valid()) graphics::remove_surface(temp.surface.get_id());
    if (temp.window.is_valid()) platform::remove_window(temp.window.get_id());
}

void
on_window_event(const platform::window_event& event)
{
    using input::input_code;
    using platform::window_event;

    switch (event.type)
    {
    case window_event::closed: {
        bool all_closed{ true };
        for (auto& surface : test_surfaces)
        {
            if (!surface.window.is_valid()) continue;
            if (surface.window.is_closed())
                destroy_surface(surface);
            else
                all_closed = false;
        }
        if (all_closed && !is_restarting) platform::post_quit();
    }
    break;
    case window_event::resized:
        if (graphics::render_surface* const surface{ find_surface(event.window) })
        {
            const platform::window win{ surface->window };
            surface->surface.resize(win.width(), win.height());
            std::fprintf(stderr, "surface %u resized: %u x %u\n", (u32)(id::id_type)surface->surface.get_id(),
                         surface->surface.width(), surface->surface.height());
        }
        break;
    case window_event::key_down:
        if (event.key == input_code::key_escape)
        {
            platform::window{ event.window }.close();
        }
        else if (event.key == input_code::key_space)
        {
            bool vsync{ false };
            graphics::get_option(graphics::renderer_option::vsync, &vsync, sizeof(bool));
            vsync = !vsync;
            graphics::set_option(graphics::renderer_option::vsync, &vsync, sizeof(bool));
            std::fprintf(stderr, "vsync %s\n", vsync ? "on" : "off");
        }
        else if (event.key == input_code::key_f11)
        {
            is_restarting = true;
            test_shutdown();
            test_initialize();
        }
        else if (event.key == input_code::key_return && (event.modifiers & input::modifier_key::alt))
        {
            const platform::window win{ event.window };
            win.set_fullscreen(!win.is_fullscreen());
        }
        break;
    default:
        break;
    }
}

bool
test_initialize()
{
    // Post-processing needs the engine shaders (compiled only when the sources changed).
    // NOTE: on Windows, EngineTest.vcxproj always compiles TestRenderer.cpp, which includes ShaderCompilation.cpp.
    if (!compile_shaders(graphics::graphics_platform::vulkan))
    {
        std::fprintf(stderr, "Failed to compile the engine shaders.\n");
        return false;
    }

    if (!graphics::initialize(graphics::graphics_platform::vulkan)) return false;

    platform::window_init_info info[]{
        { &on_window_event, nullptr, "Surface 1", 100, 100, 400, 800 },
        { &on_window_event, nullptr, "Surface 2", 150, 150, 800, 400 },
        { &on_window_event, nullptr, "Surface 3", 200, 200, 400, 400 },
        { &on_window_event, nullptr, "Surface 4", 250, 250, 800, 600 },
    };
    static_assert(std::size(info) == std::size(test_surfaces));

    for (u32 i{ 0 }; i < std::size(test_surfaces); ++i)
    {
        test_surfaces[i].window = platform::create_window(&info[i]);
        if (!test_surfaces[i].window.is_valid()) return false;
        test_surfaces[i].surface = graphics::create_surface(test_surfaces[i].window);
        if (!test_surfaces[i].surface.is_valid()) return false;
    }

    is_restarting = false;
    return true;
}

void
test_shutdown()
{
    for (auto& surface : test_surfaces) destroy_surface(surface);
    graphics::shutdown();
}

} // namespace surfaces_test

class engine_test : public test
{
public:
    bool initialize() override { return surfaces_test::test_initialize(); }

    void run() override
    {
        surfaces_test::timer.begin();
        for (auto& surface : surfaces_test::test_surfaces)
        {
            if (surface.surface.is_valid()) surface.surface.render({});
        }
        surfaces_test::timer.end();
    }

    void shutdown() override { surfaces_test::test_shutdown(); }
};
