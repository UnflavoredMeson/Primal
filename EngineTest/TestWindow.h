// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "Test.h"
#include "../Platform/PlatformTypes.h"
#include "../Platform/Platform.h"
#include <cstdio>
#include <iterator>

using namespace primal;

platform::window _windows[4];

void on_window_event(const platform::window_event& event)
{
    using platform::window_event;
    switch (event.type)
    {
    case window_event::closed:
    {
        bool all_closed{ true };
        for (u32 i{ 0 }; i < std::size(_windows); ++i)
        {
            if (!_windows[i].is_closed())
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
    case window_event::resized:
    {
        const platform::window win{ event.window };
        std::fprintf(stderr, "window %u resized: %u x %u\n", (u32)(id::id_type)event.window, win.width(), win.height());
    }
    break;
    case window_event::key_down:
        if (event.key == input::input_code::key_return && (event.modifiers & input::modifier_key::alt))
        {
            const platform::window win{ event.window };
            win.set_fullscreen(!win.is_fullscreen());
        }
        break;
    default:
        break;
    }
}

class engine_test : public test
{
public:
    bool initialize() override
    {
        platform::window_init_info info[]
        {
            {&on_window_event, nullptr, "Test window 1", 100, 100, 400, 800},
            {&on_window_event, nullptr, "Test window 2", 150, 150, 800, 400},
            {&on_window_event, nullptr, "Test window 3", 200, 200, 400, 400},
            {&on_window_event, nullptr, "Test window 4", 250, 250, 800, 600},
        };
        static_assert(std::size(info) == std::size(_windows));

        for (u32 i{ 0 }; i < std::size(_windows); ++i)
        {
            _windows[i] = platform::create_window(&info[i]);
            if (!_windows[i].is_valid()) return false;
        }
        return true;
    }

    void run() override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    void shutdown() override
    {
        for (u32 i{ 0 }; i < std::size(_windows); ++i)
        {
            if (_windows[i].is_valid()) platform::remove_window(_windows[i].get_id());
        }
    }
};
