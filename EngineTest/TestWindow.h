// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "../Platform/Platform.h"
#include "../Platform/PlatformTypes.h"
#include "Test.h"

#include <cstdio>
#include <iterator>

using namespace primal;

platform::Window windows[4];

auto on_window_event(const platform::WindowEvent &event) -> void
{
    using platform::WindowEvent;
    switch (event.type)
    {
        case WindowEvent::closed:
        {
            bool all_closed{true};
            for (u32 i{0}; i < std::size(windows); ++i)
            {
                if (!windows[i].is_closed())
                {
                    all_closed = false;
                }
            }
            if (all_closed)
            {
                platform::post_quit();
            }
        }
        break;
        case WindowEvent::resized:
        {
            const platform::Window win{event.window};
            std::fprintf(
                stderr, "window %u resized: %u x %u\n", static_cast<u32>(event.window), win.width(), win.height());
        }
        break;
        case WindowEvent::key_down:
            if (event.key == input::InputCode::key_return && (event.modifiers & input::ModifierKey::alt))
            {
                const platform::Window win{event.window};
                win.set_fullscreen(!win.is_fullscreen());
            }
            break;
        default: break;
    }
}

class EngineTest : public Test
{
  public:
    auto initialize() -> bool override
    {
        const platform::WindowInitInfo info[]{
            {&::on_window_event, nullptr, "Test window 1", 100, 100, 400, 800},
            {&::on_window_event, nullptr, "Test window 2", 150, 150, 800, 400},
            {&::on_window_event, nullptr, "Test window 3", 200, 200, 400, 400},
            {&::on_window_event, nullptr, "Test window 4", 250, 250, 800, 600},
        };
        static_assert(std::size(info) == std::size(windows));

        for (u32 i{0}; i < std::size(windows); ++i)
        {
            windows[i] = platform::create_window(&info[i]);
            if (!windows[i].is_valid())
            {
                return false;
            }
        }
        return true;
    }

    auto run() -> void override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto shutdown() -> void override
    {
        for (u32 i{0}; i < std::size(windows); ++i)
        {
            if (windows[i].is_valid())
            {
                platform::remove_window(windows[i].get_id());
            }
        }
    }
};
