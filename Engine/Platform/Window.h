// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "CommonHeaders.h"

namespace primal::platform
{

DEFINE_TYPED_ID(WindowId);
struct NativeWindowHandle;

class Window
{
  public:
    constexpr explicit Window(WindowId id)
        : _id{id}
    {
    }
    constexpr Window() = default;
    constexpr auto get_id() const -> WindowId
    {
        return _id;
    }
    constexpr auto is_valid() const -> bool
    {
        return id::is_valid(_id);
    }

    auto set_fullscreen(bool is_fullscreen) const -> void;
    auto is_fullscreen() const -> bool;
    auto handle() const -> const NativeWindowHandle *;
    auto set_caption(const char *caption) const -> void; // UTF-8
    auto size() const -> math::u32v4;
    auto resize(u32 width, u32 height) const -> void;
    auto width() const -> u32;
    auto height() const -> u32;
    auto is_closed() const -> bool;
    // Same as the user closing the window: it's hidden and a window_event::closed is sent.
    auto close() const -> void;
    // Called by the graphics backend when a surface starts/stops presenting to the window.
    // Until then the platform layer paints the window's background itself (Linux/SDL only).
    auto set_has_graphics_surface(bool has_surface) const -> void;

  private:
    WindowId _id{id::invalid_id};
};
}
