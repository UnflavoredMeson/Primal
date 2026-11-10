// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "CommonHeaders.h"
#if PRIMAL_PLATFORM_LINUX
#include "Input/InputSDL.h"
#include "Platform.h"
#include "PlatformTypes.h"

#include <SDL3/SDL.h>

#include <cstdio>

// Linux counterpart of PlatformWin32.cpp, using SDL3 for standalone windows.
// Graphics backends must use window::handle() (a native_window_handle), not SDL objects.

namespace primal::platform
{
namespace
{
struct WindowInfo
{
    SDL_Window *sdl_window{nullptr};
    window_event_callback callback{nullptr};
    NativeWindowHandle native{};
    bool is_fullscreen{false};
    bool is_closed{false};
    bool has_graphics_surface{false};
};

constexpr auto window_id_property = "primal.window_id";

// Background color used until a graphics surface draws to the window.
// Same as the window class brush in PlatformWin32.cpp: RGB(26, 48, 76).
constexpr u8 background_color[3]{26, 48, 76};

utl::free_list<WindowInfo> windows;
u32 window_count{0};
bool quit_requested{false};
bool is_pumping_events{false};

auto get_from_id(u32 id) -> WindowInfo &
{
    assert(windows[id].sdl_window);
    return windows[id];
}

// Returns false if the SDL window isn't one of ours (or doesn't exist anymore).
auto get_id_from_sdl(SDL_WindowID sdl_id, WindowId &id) -> bool
{
    SDL_Window *const sdl_window{::SDL_GetWindowFromID(sdl_id)};
    if (!sdl_window)
    {
        return false;
    }
    const s64 value{::SDL_GetNumberProperty(::SDL_GetWindowProperties(sdl_window), window_id_property, -1)};
    if (value < 0)
    {
        return false;
    }
    id = WindowId{static_cast<id::id_type>(value)};
    return true;
}

auto init_video() -> bool
{
    if (::SDL_WasInit(SDL_INIT_VIDEO))
    {
        return true;
    }

    // Like Win32: closing the last window doesn't quit. The application decides (post_quit()).
    ::SDL_SetHint(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE, "0");
    // The background is painted through SDL's window surface. By default that's backed by an
    // SDL_Renderer (OpenGL), which recreates the X11 window with another visual and so changes
    // its XID after we handed out the native handle. The software framebuffer (XShm / wl_shm)
    // leaves the window alone.
    ::SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0");
    if (!::SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        std::fprintf(stderr, "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: %s\n", ::SDL_GetError());
        return false;
    }
    return true;
}

// NOTE: deferred while pump_events() runs: an event callback may remove all windows and then
//       create new ones (e.g. a restart), and SDL must stay initialized for the rest of the loop.
auto shutdown_video() -> void
{
    if (window_count == 0 && !is_pumping_events && ::SDL_WasInit(SDL_INIT_VIDEO))
    {
        ::SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
}

auto query_native_handle(SDL_Window *sdl_window, NativeWindowHandle &handle) -> bool
{
    const SDL_PropertiesID props{::SDL_GetWindowProperties(sdl_window)};
    const char *const driver{::SDL_GetCurrentVideoDriver()};
    handle = {};

    if (driver && ::SDL_strcmp(driver, "x11") == 0)
    {
        handle.kind = NativeWindowHandle::x11;
        handle.display = ::SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
        handle.window = static_cast<u64>(::SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
    }
    else if (driver && ::SDL_strcmp(driver, "wayland") == 0)
    {
        handle.kind = NativeWindowHandle::wayland;
        handle.display = ::SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
        handle.window =
            reinterpret_cast<u64>(::SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr));
    }
    else
    {
        std::fprintf(stderr, "Unsupported SDL video driver: %s\n", driver ? driver : "(none)");
        return false;
    }

    return handle.display && handle.window;
}

// NOTE: stops once a graphics backend presents to the window (set_window_has_graphics_surface).
auto paint_background(const WindowInfo &info) -> void
{
    if (info.has_graphics_surface)
    {
        return;
    }
    SDL_Surface *const surface{::SDL_GetWindowSurface(info.sdl_window)};
    if (!surface)
    {
        return;
    }
    const u32 color{::SDL_MapSurfaceRGB(surface, background_color[0], background_color[1], background_color[2])};
    ::SDL_FillSurfaceRect(surface, nullptr, color);
    ::SDL_UpdateWindowSurface(info.sdl_window);
}

auto send_event(const WindowInfo &info, const WindowEvent &event) -> void
{
    if (info.callback)
    {
        info.callback(event);
    }
}

auto process_window_event(const SDL_Event &e) -> void
{
    WindowId id{};
    if (!get_id_from_sdl(e.window.windowID, id))
    {
        return;
    }
    WindowInfo &info{get_from_id(id)};

    switch (e.type)
    {
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            // Hide instead of destroying, so the native handle stays valid until remove_window().
            // (On Win32 the HWND is destroyed here, and is_closed() becomes true.)
            info.is_closed = true;
            ::SDL_HideWindow(info.sdl_window);
            send_event(info, {WindowEvent::closed, id});
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            paint_background(info);
            send_event(info, {WindowEvent::resized, id});
            break;
        case SDL_EVENT_WINDOW_EXPOSED: paint_background(info); break;
        case SDL_EVENT_WINDOW_ENTER_FULLSCREEN: info.is_fullscreen = true; break;
        case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN: info.is_fullscreen = false; break;
        default: break;
    }
}

auto process_key_event(const SDL_Event &e) -> void
{
    input::process_input_event(e);

    // Key repeats update the input state above, but aren't sent as window events.
    if (e.key.repeat)
    {
        return;
    }

    WindowId id{};
    if (!get_id_from_sdl(e.key.windowID, id))
    {
        return;
    }
    const u32 code{input::map_scancode(e.key.scancode)};
    if (code == u32_invalid_id)
    {
        return;
    }

    WindowEvent event{e.type == SDL_EVENT_KEY_DOWN ? WindowEvent::key_down : WindowEvent::key_up, id};
    event.key = static_cast<input::InputCode::Code>(code);
    event.modifiers = input::modifier_keys();
    send_event(get_from_id(id), event);
}

auto resize_window(WindowId id, u32 width, u32 height) -> void
{
    const WindowInfo &info{get_from_id(id)};
    // NOTE: like PlatformWin32.cpp, this also resizes while in fullscreen mode.
    ::SDL_SetWindowSize(info.sdl_window, static_cast<int>(width), static_cast<int>(height));
}

auto set_window_fullscreen(WindowId id, bool is_fullscreen) -> void
{
    WindowInfo &info{get_from_id(id)};
    // Borderless desktop fullscreen, like PlatformWin32.cpp (no display mode change).
    if (info.is_fullscreen != is_fullscreen && ::SDL_SetWindowFullscreen(info.sdl_window, is_fullscreen))
    {
        info.is_fullscreen = is_fullscreen;
    }
}

auto is_window_fullscreen(WindowId id) -> bool
{
    return get_from_id(id).is_fullscreen;
}

auto get_window_handle(WindowId id) -> window_handle
{
    const WindowInfo &info{get_from_id(id)};
    // The native handle must stay valid for the window's lifetime (graphics surfaces are made from it).
    assert(
        info.native.kind != NativeWindowHandle::x11 ||
        info.native.window == static_cast<u64>(::SDL_GetNumberProperty(
                                  ::SDL_GetWindowProperties(info.sdl_window), SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0)));
    return &info.native;
}

auto set_window_caption(WindowId id, const char *caption) -> void
{
    ::SDL_SetWindowTitle(get_from_id(id).sdl_window, caption);
}

auto get_window_size(WindowId id) -> math::u32v4
{
    // Client area in pixels as { left, top, right, bottom }, like GetClientRect() on Win32.
    int width{0}, height{0};
    ::SDL_GetWindowSizeInPixels(get_from_id(id).sdl_window, &width, &height);
    return {0, 0, static_cast<u32>(width), static_cast<u32>(height)};
}

auto is_window_closed(WindowId id) -> bool
{
    return get_from_id(id).is_closed;
}

auto close_window(WindowId id) -> void
{
    // Handled in pump_events() like a close request from the user (Win32: PostMessage(WM_CLOSE)).
    SDL_Event e{};
    e.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
    e.window.windowID = ::SDL_GetWindowID(get_from_id(id).sdl_window);
    ::SDL_PushEvent(&e);
}

auto set_window_has_graphics_surface(WindowId id, bool has_surface) -> void
{
    WindowInfo &info{get_from_id(id)};
    if (info.has_graphics_surface == has_surface)
    {
        return;
    }
    info.has_graphics_surface = has_surface;
    if (has_surface)
    {
        // SDL's software framebuffer presents to the same X11 window / wl_surface, which
        // conflicts with a Vulkan swapchain. It has to go before the swapchain is created.
        ::SDL_DestroyWindowSurface(info.sdl_window);
    }
    else
    {
        paint_background(info);
    }
}
}

auto create_window(const WindowInitInfo *init_info /* = nullptr */) -> Window
{
    if (init_info && init_info->parent)
    {
        assert(init_info->parent->kind == NativeWindowHandle::x11);
        assert(false && "Parented windows are not supported on Linux yet.");
        return {};
    }

    if (!init_video())
    {
        return {};
    }

    const char *const caption{(init_info && init_info->caption) ? init_info->caption : "Primal Game"};
    const s32 width{(init_info && init_info->width) ? init_info->width : 1920};
    const s32 height{(init_info && init_info->height) ? init_info->height : 1080};

    const SDL_PropertiesID props{::SDL_CreateProperties()};
    ::SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, caption);
    ::SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, width);
    ::SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, height);
    if (init_info)
    {
        // NOTE: Wayland compositors ignore window positions.
        ::SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, init_info->left);
        ::SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, init_info->top);
    }
    ::SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true);
    SDL_Window *const sdl_window{::SDL_CreateWindowWithProperties(props)};
    ::SDL_DestroyProperties(props);

    if (!sdl_window)
    {
        std::fprintf(stderr, "SDL_CreateWindowWithProperties failed: %s\n", ::SDL_GetError());
        shutdown_video();
        return {};
    }

    WindowInfo info{};
    info.sdl_window = sdl_window;
    info.callback = init_info ? init_info->callback : nullptr;
    if (!query_native_handle(sdl_window, info.native))
    {
        ::SDL_DestroyWindow(sdl_window);
        shutdown_video();
        return {};
    }

    const WindowId id{windows.add(info)};
    ++window_count;
    ::SDL_SetNumberProperty(
        ::SDL_GetWindowProperties(sdl_window), window_id_property, static_cast<s64>(static_cast<id::id_type>(id)));
    paint_background(windows[id]);
    return Window{id};
}

auto remove_window(WindowId id) -> void
{
    const WindowInfo &info{get_from_id(id)};
    ::SDL_DestroyWindow(info.sdl_window);
    windows.remove(id);
    assert(window_count > 0);
    --window_count;
    shutdown_video();
}

auto set_current_directory_to_executable_path() -> std::filesystem::path
{
    std::error_code ec;
    const std::filesystem::path path{std::filesystem::read_symlink("/proc/self/exe", ec)};
    if (ec)
    {
        return {};
    }
    std::filesystem::current_path(path.parent_path(), ec);
    if (ec)
    {
        return {};
    }
    return std::filesystem::current_path();
}

auto pump_events() -> bool
{
    if (::SDL_WasInit(SDL_INIT_EVENTS))
    {
        is_pumping_events = true;
        SDL_Event e;
        while (::SDL_PollEvent(&e))
        {
            if (e.type == SDL_EVENT_QUIT)
            {
                quit_requested = true;
            }
            else if (e.type >= SDL_EVENT_WINDOW_FIRST && e.type <= SDL_EVENT_WINDOW_LAST)
            {
                process_window_event(e);
            }
            else if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP)
            {
                process_key_event(e);
            }
            else
            {
                input::process_input_event(e);
            }
        }
        is_pumping_events = false;
        shutdown_video();
    }

    return !quit_requested;
}

auto post_quit() -> void
{
    quit_requested = true;
}
}

#include "IncludeWindowCpp.h"

#endif
