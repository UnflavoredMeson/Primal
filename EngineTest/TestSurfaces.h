// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "../Engine/Graphics/Renderer.h"
#include "../Engine/Platform/Platform.h"
#include "../Engine/Platform/PlatformTypes.h"
#include "../EngineDLL/ShaderCompilation.h"
#include "Test.h"

#include <cstdio>
#include <iterator>

// 4 windows, each with a graphics surface. No camera or render items, so each frame is the gpass clear color
// (gray in debug builds, black in release) shown through post-processing.
//   Esc: close a window   Space: toggle vsync   F11: restart   Alt+Enter: fullscreen
// NOTE: Vulkan only for now. The D3D12 backend renders the full scene in render_surface() and
//       needs a camera, so it isn't usable with this test.

using namespace primal;

namespace surfaces_test
{

graphics::RenderSurface test_surfaces[4]{};
bool is_restarting{false};
TimeIt timer{};

auto test_initialize() -> bool;
auto test_shutdown() -> void;

auto find_surface(platform::WindowId id) -> graphics::RenderSurface *
{
    for (auto &surface : test_surfaces)
    {
        if (surface.window.is_valid() && surface.window.get_id() == id)
        {
            return &surface;
        }
    }
    return nullptr;
}

auto destroy_surface(graphics::RenderSurface &surface) -> void
{
    const graphics::RenderSurface temp{surface};
    surface = {};
    // NOTE: the graphics surface goes first, it presents to the window.
    if (temp.surface.is_valid())
    {
        graphics::remove_surface(temp.surface.get_id());
    }
    if (temp.window.is_valid())
    {
        platform::remove_window(temp.window.get_id());
    }
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
            for (auto &surface : test_surfaces)
            {
                if (!surface.window.is_valid())
                {
                    continue;
                }
                if (surface.window.is_closed())
                {
                    destroy_surface(surface);
                }
                else
                {
                    all_closed = false;
                }
            }
            if (all_closed && !is_restarting)
            {
                platform::post_quit();
            }
        }
        break;
        case WindowEvent::resized:
            if (graphics::RenderSurface const *const surface{find_surface(event.window)})
            {
                const platform::Window win{surface->window};
                surface->surface.resize(win.width(), win.height());
                std::fprintf(
                    stderr,
                    "surface %u resized: %u x %u\n",
                    static_cast<u32>(surface->surface.get_id()),
                    surface->surface.width(),
                    surface->surface.height());
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
                std::fprintf(stderr, "vsync %s\n", vsync ? "on" : "off");
            }
            else if (event.key == InputCode::key_f11)
            {
                is_restarting = true;
                test_shutdown();
                test_initialize();
            }
            else if (event.key == InputCode::key_return && (event.modifiers & input::ModifierKey::alt))
            {
                const platform::Window win{event.window};
                win.set_fullscreen(!win.is_fullscreen());
            }
            break;
        default: break;
    }
}

auto test_initialize() -> bool
{
    // Post-processing needs the engine shaders (compiled only when the sources changed).
    if (!::compile_shaders(graphics::GraphicsPlatform::vulkan))
    {
        std::fprintf(stderr, "Failed to compile the engine shaders.\n");
        return false;
    }

    if (!graphics::initialize(graphics::GraphicsPlatform::vulkan))
    {
        return false;
    }

    const platform::WindowInitInfo info[]{
        {&on_window_event, nullptr, "Surface 1", 100, 100, 400, 800},
        {&on_window_event, nullptr, "Surface 2", 150, 150, 800, 400},
        {&on_window_event, nullptr, "Surface 3", 200, 200, 400, 400},
        {&on_window_event, nullptr, "Surface 4", 250, 250, 800, 600},
    };
    static_assert(std::size(info) == std::size(test_surfaces));

    for (u32 i{0}; i < std::size(test_surfaces); ++i)
    {
        test_surfaces[i].window = platform::create_window(&info[i]);
        if (!test_surfaces[i].window.is_valid())
        {
            return false;
        }
        test_surfaces[i].surface = graphics::create_surface(test_surfaces[i].window);
        if (!test_surfaces[i].surface.is_valid())
        {
            return false;
        }
    }

    is_restarting = false;
    return true;
}

auto test_shutdown() -> void
{
    for (auto &surface : test_surfaces)
    {
        destroy_surface(surface);
    }
    graphics::shutdown();
}

}

class EngineTest : public Test
{
  public:
    auto initialize() -> bool override
    {
        return surfaces_test::test_initialize();
    }

    auto run() -> void override
    {
        surfaces_test::timer.begin();
        for (const auto &surface : surfaces_test::test_surfaces)
        {
            if (surface.surface.is_valid())
            {
                surface.surface.render({});
            }
        }
        surfaces_test::timer.end();
    }

    auto shutdown() -> void override
    {
        surfaces_test::test_shutdown();
    }
};
