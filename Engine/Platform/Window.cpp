// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#if INCLUDE_WINDOW_CPP
#include "Window.h"

namespace primal::platform
{

auto Window::set_fullscreen(bool is_fullscreen) const -> void
{
    assert(is_valid());
    set_window_fullscreen(_id, is_fullscreen);
}

auto Window::is_fullscreen() const -> bool
{
    assert(is_valid());
    return is_window_fullscreen(_id);
}

auto Window::handle() const -> const NativeWindowHandle *
{
    assert(is_valid());
    return get_window_handle(_id);
}

auto Window::set_caption(const char *caption) const -> void
{
    assert(is_valid());
    set_window_caption(_id, caption);
}

auto Window::size() const -> math::u32v4
{
    assert(is_valid());
    return get_window_size(_id);
}

auto Window::resize(u32 width, u32 height) const -> void
{
    assert(is_valid());
    resize_window(_id, width, height);
}

auto Window::width() const -> u32
{
    math::u32v4 s{size()};
    return s.z - s.x;
}

auto Window::height() const -> u32
{
    math::u32v4 s{size()};
    return s.w - s.y;
}

auto Window::is_closed() const -> bool
{
    assert(is_valid());
    return is_window_closed(_id);
}
auto Window::close() const -> void
{
    assert(is_valid());
    close_window(_id);
}

auto Window::set_has_graphics_surface(bool has_surface) const -> void
{
    assert(is_valid());
    set_window_has_graphics_surface(_id, has_surface);
}
}
#endif // INCLUDE_WINDOW_CPP
