// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#if !defined(SHIPPING)
#include "Content/ContentLoader.h"
#include "Components/Script.h"
#include "Platform/PlatformTypes.h"
#include "Platform/Platform.h"
#include "Graphics/Renderer.h"
#include <thread>

using namespace primal;
namespace {

    graphics::render_surface game_window{};

    void on_window_event(const platform::window_event& event)
    {
        switch (event.type)
        {
            case platform::window_event::closed:
                platform::post_quit();
                break;
            case platform::window_event::key_down:
                if (event.key == input::input_code::key_return && (event.modifiers & input::modifier_key::alt))
                {
                    game_window.window.set_fullscreen(!game_window.window.is_fullscreen());
                }
                break;
            default:
                break;
        }
    }

} // anonymous namespace

bool
engine_initialize()
{
    if (!content::load_game()) return false;

    platform::window_init_info info
    {
        &on_window_event, nullptr, "Primal Game" // TODO: get the game name from the loaded game file
    };

    game_window.window = platform::create_window(&info);
    if (!game_window.window.is_valid()) return false;

    return true;
}

void
engine_update()
{
    script::update(10.f);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

void
engine_shutdown()
{
    if (game_window.window.is_valid()) platform::remove_window(game_window.window.get_id());
    content::unload_game();
}
#endif // !defined(SHIPPING)