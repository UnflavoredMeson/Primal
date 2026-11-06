// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "CommonHeaders.h"

namespace primal::platform {

DEFINE_TYPED_ID(window_id);
struct native_window_handle;

class window
{
public:
    constexpr explicit window(window_id id) : _id{ id } {}
    constexpr window() = default;
    constexpr window_id get_id() const { return _id; }
    constexpr bool is_valid() const { return id::is_valid(_id); }

    void set_fullscreen(bool is_fullscreen) const;
    bool is_fullscreen() const;
    const native_window_handle* handle() const;
    void set_caption(const char* caption) const; // UTF-8
    math::u32v4 size() const;
    void resize(u32 width, u32 height) const;
    u32 width() const;
    u32 height() const;
    bool is_closed() const;
    // Same as the user closing the window: it's hidden and a window_event::closed is sent.
    void close() const;
    // Called by the graphics backend when a surface starts/stops presenting to the window.
    // Until then the platform layer paints the window's background itself (Linux/SDL only).
    void set_has_graphics_surface(bool has_surface) const;
private:
    window_id _id{ id::invalid_id };
};
}
