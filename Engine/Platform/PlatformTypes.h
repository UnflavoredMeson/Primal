// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "CommonHeaders.h"
#include "Window.h"
#include "EngineAPI/Input.h"

#if PRIMAL_PLATFORM_WINDOWS
// NOTE: kept so code that relied on PlatformTypes.h pulling in Windows.h keeps compiling.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif // PRIMAL_PLATFORM_WINDOWS

namespace primal::platform
{

// Opaque native window handle. Graphics backends create their surfaces from this (an HWND for
// DXGI, or a VkSurfaceKHR via VK_KHR_win32/xlib/wayland_surface), never from SDL objects, so
// windows created by a host application (the editor) work the same way.
// NOTE: window::handle() points into the platform's window list: copy the struct if it's needed
//       after the next create_window()/remove_window().
struct NativeWindowHandle
{
    enum Kind : u32
    {
        none,
        win32,   // display: HINSTANCE,   window: HWND
        x11,     // display: Display*,    window: X11 Window (XID)
        wayland, // display: wl_display*, window: wl_surface*
    };

    Kind kind{none};
    void *display{nullptr};
    u64 window{0}; // the handle/pointer value cast to u64
};

using window_handle = const NativeWindowHandle *;

struct WindowEvent
{
    enum Type : u32
    {
        closed,   // the user asked to close the window (it's hidden; call remove_window() to destroy it)
        resized,  // the client area changed size (sent once a drag-resize finishes)
        key_down, // key repeats update the input state but aren't sent as events
        key_up,
    };

    Type type{};
    WindowId window{id::invalid_id};
    input::InputCode::Code key{}; // key_down / key_up
    u32 modifiers{0};             // input::modifier_key::key flags (key_down / key_up)
};

// Called from pump_events() (or the host's message loop for parented windows).
// It's safe to remove the window, or any other window, from the callback.
using window_event_callback = void (*)(const WindowEvent &);

struct WindowInitInfo
{
    window_event_callback callback{nullptr};
    window_handle parent{nullptr}; // host window for a child window (editor viewport)
    const char *caption{nullptr};  // UTF-8
    s32 left{0};
    s32 top{0};
    s32 width{1920};
    s32 height{1080};
};

// Processes all pending window and input events. Returns false once a quit was requested.
auto pump_events() -> bool;

// Asks the application to quit: pump_events() returns false from now on (like PostQuitMessage).
auto post_quit() -> void;
}
