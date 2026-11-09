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
namespace
{

graphics::RenderSurface game_window{};

auto on_window_event(const platform::WindowEvent &event) -> void
{
    switch (event.type)
    {
        case platform::WindowEvent::closed: platform::post_quit(); break;
        case platform::WindowEvent::key_down:
            if (event.key == input::InputCode::key_return && (event.modifiers & input::ModifierKey::alt))
            {
                game_window.window.set_fullscreen(!game_window.window.is_fullscreen());
            }
            break;
        default: break;
    }
}

}

auto engine_initialize() -> bool
{
    if (!content::load_game())
    {
        return false;
    }

    const platform::WindowInitInfo info{
        &on_window_event, nullptr, "Primal Game" // TODO: get the game name from the loaded game file
    };

    game_window.window = platform::create_window(&info);
    return game_window.window.is_valid();
}

auto engine_update() -> void
{
    script::update(10.f);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

auto engine_shutdown() -> void
{
    if (game_window.window.is_valid())
    {
        platform::remove_window(game_window.window.get_id());
    }
    content::unload_game();
}
#endif
