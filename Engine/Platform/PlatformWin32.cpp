// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "CommonHeaders.h"
#if PRIMAL_PLATFORM_WINDOWS
#include "Input/InputWin32.h"
#include "Platform.h"
#include "PlatformTypes.h"

#include <string>

namespace primal::platform
{

namespace
{
struct WindowInfo
{
    HWND hwnd{nullptr};
    window_event_callback callback{nullptr};
    NativeWindowHandle native{};
    RECT client_area{0, 0, 1920, 1080};
    RECT fullscreen_area{};
    POINT top_left{0, 0};
    DWORD style{WS_VISIBLE};
    bool is_fullscreen{false};
    bool is_closed{false};
    bool is_tracking_mouse{false};
};

utl::free_list<WindowInfo> windows;
bool quit_requested{false};

auto get_from_id(WindowId id) -> WindowInfo &
{
    assert(windows[id].hwnd);
    return windows[id];
}

auto get_id_from_handle(HWND handle) -> WindowId
{
    return WindowId{static_cast<id::id_type>(::GetWindowLongPtr(handle, GWLP_USERDATA))};
}

auto get_from_handle(HWND handle) -> WindowInfo &
{
    return get_from_id(get_id_from_handle(handle));
}

auto to_wide(const char *utf8) -> std::wstring
{
    if (!utf8 || !*utf8)
    {
        return {};
    }
    const int length{::MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0)};
    if (length <= 0)
    {
        return {};
    }
    std::wstring wide(static_cast<size_t>(length), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide.data(), length);
    wide.resize(static_cast<size_t>(length) - 1); // drop the terminating null
    return wide;
}

// Lets a host (the editor) get WM_MOUSEHOVER/WM_MOUSELEAVE for child windows.
auto track_mouse(HWND hwnd) -> void
{
    TRACKMOUSEEVENT tme{};
    tme.cbSize = sizeof(TRACKMOUSEEVENT);
    tme.dwFlags = TME_HOVER | TME_LEAVE;
    tme.dwHoverTime = 10;
    tme.hwndTrack = hwnd;
    ::TrackMouseEvent(&tme);
}

auto is_key_message(UINT msg) -> bool
{
    return msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYUP;
}

bool resized{false};

// NOLINTNEXTLINE(modernize-use-trailing-return-type): calling convention (keeps the classic return type, T02)
LRESULT CALLBACK internal_window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
        case WM_NCCREATE:
        {
            // Put the window id in the user data field of window's data buffer.
            DEBUG_OP(::SetLastError(0));
            const WindowId id{windows.add()};
            windows[id].hwnd = hwnd;
            ::SetWindowLongPtr(hwnd, GWLP_USERDATA, static_cast<LONG_PTR>(id));
            assert(::GetLastError() == 0);
        }
        break;
        case WM_CLOSE:
        {
            // Hide instead of destroying, so the HWND stays valid until remove_window().
            WindowInfo &info{get_from_handle(hwnd)};
            info.is_closed = true;
            ::ShowWindow(hwnd, SW_HIDE);
            if (info.callback)
            {
                info.callback({WindowEvent::closed, get_id_from_handle(hwnd)});
            }
            return 0;
        }
        case WM_DESTROY: get_from_handle(hwnd).is_closed = true; break;
        case WM_SIZE: resized = (wparam != SIZE_MINIMIZED); break;
        case WM_MOUSEMOVE:
        {
            WindowInfo &info{get_from_handle(hwnd)};
            if ((info.style & WS_CHILD) && !info.is_tracking_mouse)
            {
                track_mouse(hwnd);
                info.is_tracking_mouse = true;
            }
        }
        break;
        case WM_MOUSELEAVE: get_from_handle(hwnd).is_tracking_mouse = false; break;
        default: break;
    }

    input::process_input_message(hwnd, msg, wparam, lparam);

    // Collect the event now, but send it last: the callback is allowed to remove this window.
    WindowEvent event{};
    bool has_event{false};

    if (resized && ::GetKeyState(VK_LBUTTON) >= 0)
    {
        WindowInfo &info{get_from_handle(hwnd)};
        assert(info.hwnd);
        ::GetClientRect(info.hwnd, info.is_fullscreen ? &info.fullscreen_area : &info.client_area);
        resized = false;
        event = {WindowEvent::resized, get_id_from_handle(hwnd)};
        has_event = true;
    }
    else if (is_key_message(msg))
    {
        const bool is_down{msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN};
        const bool is_repeat{is_down && (lparam & (1 << 30))};
        const u32 code{input::map_virtual_key(static_cast<u32>(wparam))};
        if (!is_repeat && code != u32_invalid_id)
        {
            event = {is_down ? WindowEvent::key_down : WindowEvent::key_up, get_id_from_handle(hwnd)};
            event.key = static_cast<input::InputCode::Code>(code);
            event.modifiers = input::modifier_keys();
            has_event = true;
        }
    }

    const window_event_callback callback{has_event ? get_from_handle(hwnd).callback : nullptr};

    LRESULT result{0};
    // Don't activate the (non-existent) window menu when Alt is pressed, and don't beep on Alt+Enter.
    if ((msg == WM_SYSCOMMAND && wparam == SC_KEYMENU) ||
        (msg == WM_SYSCHAR && wparam == VK_RETURN && (HIWORD(lparam) & KF_ALTDOWN)))
    {
        result = 0;
    }
    else
    {
        result = ::DefWindowProc(hwnd, msg, wparam, lparam);
    }

    if (callback)
    {
        callback(event);
    }
    return result;
}

auto resize_window(const WindowInfo &info, const RECT &area) -> void
{
    // Adjust the window size for correct device size
    RECT window_rect{area};
    ::AdjustWindowRect(&window_rect, info.style, FALSE);

    const s32 width{window_rect.right - window_rect.left};
    const s32 height{window_rect.bottom - window_rect.top};

    ::MoveWindow(info.hwnd, info.top_left.x, info.top_left.y, width, height, true);
}

auto resize_window(WindowId id, u32 width, u32 height) -> void
{
    WindowInfo &info{get_from_id(id)};

    // NOTE: when we host the window in the level editor we just update
    //       the internal data (i.e. the client area dimensions).
    if (info.style & WS_CHILD)
    {
        ::GetClientRect(info.hwnd, &info.client_area);
    }
    else
    {
        // NOTE: we also resize while in fullscreen mode to support the case
        //       when the user changes the screen resolution.
        RECT &area{info.is_fullscreen ? info.fullscreen_area : info.client_area};
        area.bottom = area.top + height;
        area.right = area.left + width;

        resize_window(info, area);
    }
}

auto set_window_fullscreen(WindowId id, bool is_fullscreen) -> void
{
    WindowInfo &info{get_from_id(id)};

    if (info.is_fullscreen != is_fullscreen)
    {
        info.is_fullscreen = is_fullscreen;

        if (is_fullscreen)
        {
            // Store the current window dimensions so they can be restored
            // when switching out of fullscreen state.
            ::GetClientRect(info.hwnd, &info.client_area);
            RECT rect;
            ::GetWindowRect(info.hwnd, &rect);
            info.top_left.x = rect.left;
            info.top_left.y = rect.top;
            ::SetWindowLongPtr(info.hwnd, GWL_STYLE, 0);
            ::ShowWindow(info.hwnd, SW_MAXIMIZE);
        }
        else
        {
            ::SetWindowLongPtr(info.hwnd, GWL_STYLE, info.style);
            resize_window(info, info.client_area);
            ::ShowWindow(info.hwnd, SW_SHOWNORMAL);
        }
    }
}

auto is_window_fullscreen(WindowId id) -> bool
{
    return get_from_id(id).is_fullscreen;
}

auto get_window_handle(WindowId id) -> window_handle
{
    return &get_from_id(id).native;
}

auto set_window_caption(WindowId id, const char *caption) -> void
{
    const WindowInfo &info{get_from_id(id)};
    ::SetWindowTextW(info.hwnd, to_wide(caption).c_str());
}

auto get_window_size(WindowId id) -> math::u32v4
{
    const WindowInfo &info{get_from_id(id)};
    const RECT &area{info.is_fullscreen ? info.fullscreen_area : info.client_area};
    return {
        static_cast<u32>(area.left),
        static_cast<u32>(area.top),
        static_cast<u32>(area.right),
        static_cast<u32>(area.bottom)};
}

auto is_window_closed(WindowId id) -> bool
{
    return get_from_id(id).is_closed;
}

auto close_window(WindowId id) -> void
{
    ::PostMessage(get_from_id(id).hwnd, WM_CLOSE, 0, 0);
}

auto set_window_has_graphics_surface(WindowId, bool) -> void
{
    // Nothing to do: the window class brush paints the background and DXGI presents over it.
}
}

auto create_window(const WindowInitInfo *init_info /* = nullptr */) -> Window
{
    HWND parent{(init_info && init_info->parent) ? reinterpret_cast<HWND>(init_info->parent->window) : nullptr};
    assert(!init_info || !init_info->parent || init_info->parent->kind == NativeWindowHandle::win32);

    // Setup a window class
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = internal_window_proc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = nullptr;
    wc.hIcon = ::LoadIcon(nullptr, IDI_APPLICATION);
    wc.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = ::CreateSolidBrush(RGB(26, 48, 76));
    wc.lpszMenuName = nullptr;
    wc.lpszClassName = L"PrimalWindow";
    wc.hIconSm = ::LoadIcon(nullptr, IDI_APPLICATION);

    // Register the window class
    ::RegisterClassExW(&wc);

    WindowInfo info{};
    info.callback = init_info ? init_info->callback : nullptr;
    info.client_area.right =
        (init_info && init_info->width) ? info.client_area.left + init_info->width : info.client_area.right;
    info.client_area.bottom =
        (init_info && init_info->height) ? info.client_area.top + init_info->height : info.client_area.bottom;
    info.style |= parent ? WS_CHILD : WS_OVERLAPPEDWINDOW;

    RECT rect{info.client_area};

    // adjust the window size for correct device size
    ::AdjustWindowRect(&rect, info.style, FALSE);

    const std::wstring caption{to_wide((init_info && init_info->caption) ? init_info->caption : "Primal Game")};
    const s32 left{init_info ? init_info->left : info.top_left.x};
    const s32 top{init_info ? init_info->top : info.top_left.y};
    const s32 width{rect.right - rect.left};
    const s32 height{rect.bottom - rect.top};

    // Create an instance of the window class
    info.hwnd = ::CreateWindowExW(
        0,                // extended style
        wc.lpszClassName, // window class name
        caption.c_str(),  // instance title
        info.style,       // window style
        left,
        top, // initial window position
        width,
        height,   // initial window dimensions
        parent,   // handle to parent window
        nullptr,  // handle to menu
        nullptr,  // intance of this application
        nullptr); // extra creation parameters

    if (info.hwnd)
    {
        info.native = {
            NativeWindowHandle::win32,
            reinterpret_cast<void *>(::GetModuleHandleW(nullptr)),
            reinterpret_cast<u64>(info.hwnd)};

        ::ShowWindow(info.hwnd, SW_SHOWNORMAL);
        ::UpdateWindow(info.hwnd);

        const WindowId id{get_id_from_handle(info.hwnd)};
        windows[id] = info;

        return Window{id};
    }
    return {};
}

auto remove_window(WindowId id) -> void
{
    const WindowInfo &info{get_from_id(id)};
    ::DestroyWindow(info.hwnd);
    windows.remove(id);
}

auto set_current_directory_to_executable_path() -> std::filesystem::path
{
    wchar_t path[MAX_PATH]{};
    const u32 length{::GetModuleFileNameW(nullptr, &path[0], MAX_PATH)};
    if (!length || ::GetLastError() == ERROR_INSUFFICIENT_BUFFER)
    {
        return {};
    }
    std::error_code ec;
    std::filesystem::current_path(std::filesystem::path{path}.parent_path(), ec);
    if (ec)
    {
        return {};
    }
    return std::filesystem::current_path();
}

auto pump_events() -> bool
{
    MSG msg{};
    while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
        if (msg.message == WM_QUIT)
        {
            quit_requested = true;
        }
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
