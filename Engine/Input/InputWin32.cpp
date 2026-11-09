// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "CommonHeaders.h"
#if PRIMAL_PLATFORM_WINDOWS

#include "InputWin32.h"
#include "Input.h"

namespace primal::input
{
namespace
{

constexpr u32 vk_mapping[256]{
    /* 0x00 */ u32_invalid_id,
    /* 0x01 */ InputCode::mouse_left,
    /* 0x02 */ InputCode::mouse_right,
    /* 0x03 */ u32_invalid_id,
    /* 0x04 */ InputCode::mouse_middle,
    /* 0x05 */ u32_invalid_id,
    /* 0x06 */ u32_invalid_id,
    /* 0x07 */ u32_invalid_id,
    /* 0x08 */ InputCode::key_backspace,
    /* 0x09 */ InputCode::key_tab,
    /* 0x0A */ u32_invalid_id,
    /* 0x0B */ u32_invalid_id,
    /* 0x0C */ u32_invalid_id,
    /* 0x0D */ InputCode::key_return,
    /* 0x0E */ u32_invalid_id,
    /* 0x0F */ u32_invalid_id,

    /* 0x10 */ InputCode::key_shift,
    /* 0x11 */ InputCode::key_control,
    /* 0x12 */ InputCode::key_alt,
    /* 0x13 */ InputCode::key_pause,
    /* 0x14 */ InputCode::key_capslock,
    /* 0x15 */ u32_invalid_id,
    /* 0x16 */ u32_invalid_id,
    /* 0x17 */ u32_invalid_id,
    /* 0x18 */ u32_invalid_id,
    /* 0x19 */ u32_invalid_id,
    /* 0x1A */ u32_invalid_id,
    /* 0x1B */ InputCode::key_escape,
    /* 0x1C */ u32_invalid_id,
    /* 0x1D */ u32_invalid_id,
    /* 0x1E */ u32_invalid_id,
    /* 0x1F */ u32_invalid_id,

    /* 0x20 */ InputCode::key_space,
    /* 0x21 */ InputCode::key_page_up,
    /* 0x22 */ InputCode::key_page_down,
    /* 0x23 */ InputCode::key_end,
    /* 0x24 */ InputCode::key_home,
    /* 0x25 */ InputCode::key_left,
    /* 0x26 */ InputCode::key_up,
    /* 0x27 */ InputCode::key_right,
    /* 0x28 */ InputCode::key_down,
    /* 0x29 */ u32_invalid_id,
    /* 0x2A */ u32_invalid_id,
    /* 0x2B */ u32_invalid_id,
    /* 0x2C */ InputCode::key_print_screen,
    /* 0x2D */ InputCode::key_insert,
    /* 0x2E */ InputCode::key_delete,
    /* 0x2F */ u32_invalid_id,

    /* 0x30 */ InputCode::key_0,
    /* 0x31 */ InputCode::key_1,
    /* 0x32 */ InputCode::key_2,
    /* 0x33 */ InputCode::key_3,
    /* 0x34 */ InputCode::key_4,
    /* 0x35 */ InputCode::key_5,
    /* 0x36 */ InputCode::key_6,
    /* 0x37 */ InputCode::key_7,
    /* 0x38 */ InputCode::key_8,
    /* 0x39 */ InputCode::key_9,
    /* 0x3A */ u32_invalid_id,
    /* 0x3B */ u32_invalid_id,
    /* 0x3C */ u32_invalid_id,
    /* 0x3D */ u32_invalid_id,
    /* 0x3E */ u32_invalid_id,
    /* 0x3F */ u32_invalid_id,

    /* 0x40 */ u32_invalid_id,
    /* 0x41 */ InputCode::key_a,
    /* 0x42 */ InputCode::key_b,
    /* 0x43 */ InputCode::key_c,
    /* 0x44 */ InputCode::key_d,
    /* 0x45 */ InputCode::key_e,
    /* 0x46 */ InputCode::key_f,
    /* 0x47 */ InputCode::key_g,
    /* 0x48 */ InputCode::key_h,
    /* 0x49 */ InputCode::key_i,
    /* 0x4A */ InputCode::key_j,
    /* 0x4B */ InputCode::key_k,
    /* 0x4C */ InputCode::key_l,
    /* 0x4D */ InputCode::key_m,
    /* 0x4E */ InputCode::key_n,
    /* 0x4F */ InputCode::key_o,

    /* 0x50 */ InputCode::key_p,
    /* 0x51 */ InputCode::key_q,
    /* 0x52 */ InputCode::key_r,
    /* 0x53 */ InputCode::key_s,
    /* 0x54 */ InputCode::key_t,
    /* 0x55 */ InputCode::key_u,
    /* 0x56 */ InputCode::key_v,
    /* 0x57 */ InputCode::key_w,
    /* 0x58 */ InputCode::key_x,
    /* 0x59 */ InputCode::key_y,
    /* 0x5A */ InputCode::key_z,
    /* 0x5B */ u32_invalid_id,
    /* 0x5C */ u32_invalid_id,
    /* 0x5D */ u32_invalid_id,
    /* 0x5E */ u32_invalid_id,
    /* 0x5F */ u32_invalid_id,

    /* 0x60 */ InputCode::key_numpad_0,
    /* 0x61 */ InputCode::key_numpad_1,
    /* 0x62 */ InputCode::key_numpad_2,
    /* 0x63 */ InputCode::key_numpad_3,
    /* 0x64 */ InputCode::key_numpad_4,
    /* 0x65 */ InputCode::key_numpad_5,
    /* 0x66 */ InputCode::key_numpad_6,
    /* 0x67 */ InputCode::key_numpad_7,
    /* 0x68 */ InputCode::key_numpad_8,
    /* 0x69 */ InputCode::key_numpad_9,
    /* 0x6A */ InputCode::key_multiply,
    /* 0x6B */ InputCode::key_add,
    /* 0x6C */ u32_invalid_id,
    /* 0x6D */ InputCode::key_subtract,
    /* 0x6E */ InputCode::key_decimal,
    /* 0x6F */ InputCode::key_divide,

    /* 0x70 */ InputCode::key_f1,
    /* 0x71 */ InputCode::key_f2,
    /* 0x72 */ InputCode::key_f3,
    /* 0x73 */ InputCode::key_f4,
    /* 0x74 */ InputCode::key_f5,
    /* 0x75 */ InputCode::key_f6,
    /* 0x76 */ InputCode::key_f7,
    /* 0x77 */ InputCode::key_f8,
    /* 0x78 */ InputCode::key_f9,
    /* 0x79 */ InputCode::key_f10,
    /* 0x7A */ InputCode::key_f11,
    /* 0x7B */ InputCode::key_f12,
    /* 0x7C */ u32_invalid_id,
    /* 0x7D */ u32_invalid_id,
    /* 0x7E */ u32_invalid_id,
    /* 0x7F */ u32_invalid_id,

    /* 0x80 */ u32_invalid_id,
    /* 0x81 */ u32_invalid_id,
    /* 0x82 */ u32_invalid_id,
    /* 0x83 */ u32_invalid_id,
    /* 0x84 */ u32_invalid_id,
    /* 0x85 */ u32_invalid_id,
    /* 0x86 */ u32_invalid_id,
    /* 0x87 */ u32_invalid_id,
    /* 0x88 */ u32_invalid_id,
    /* 0x89 */ u32_invalid_id,
    /* 0x8A */ u32_invalid_id,
    /* 0x8B */ u32_invalid_id,
    /* 0x8C */ u32_invalid_id,
    /* 0x8D */ u32_invalid_id,
    /* 0x8E */ u32_invalid_id,
    /* 0x8F */ u32_invalid_id,

    /* 0x90 */ InputCode::key_numlock,
    /* 0x91 */ InputCode::key_scrollock,
    /* 0x92 */ u32_invalid_id,
    /* 0x93 */ u32_invalid_id,
    /* 0x94 */ u32_invalid_id,
    /* 0x95 */ u32_invalid_id,
    /* 0x96 */ u32_invalid_id,
    /* 0x97 */ u32_invalid_id,
    /* 0x98 */ u32_invalid_id,
    /* 0x99 */ u32_invalid_id,
    /* 0x9A */ u32_invalid_id,
    /* 0x9B */ u32_invalid_id,
    /* 0x9C */ u32_invalid_id,
    /* 0x9D */ u32_invalid_id,
    /* 0x9E */ u32_invalid_id,
    /* 0x9F */ u32_invalid_id,

    /* 0xA0 */ u32_invalid_id,
    /* 0xA1 */ u32_invalid_id,
    /* 0xA2 */ u32_invalid_id,
    /* 0xA3 */ u32_invalid_id,
    /* 0xA4 */ u32_invalid_id,
    /* 0xA5 */ u32_invalid_id,
    /* 0xA6 */ u32_invalid_id,
    /* 0xA7 */ u32_invalid_id,
    /* 0xA8 */ u32_invalid_id,
    /* 0xA9 */ u32_invalid_id,
    /* 0xAA */ u32_invalid_id,
    /* 0xAB */ u32_invalid_id,
    /* 0xAC */ u32_invalid_id,
    /* 0xAD */ u32_invalid_id,
    /* 0xAE */ u32_invalid_id,
    /* 0xAF */ u32_invalid_id,

    /* 0xB0 */ u32_invalid_id,
    /* 0xB1 */ u32_invalid_id,
    /* 0xB2 */ u32_invalid_id,
    /* 0xB3 */ u32_invalid_id,
    /* 0xB4 */ u32_invalid_id,
    /* 0xB5 */ u32_invalid_id,
    /* 0xB6 */ u32_invalid_id,
    /* 0xB7 */ u32_invalid_id,
    /* 0xB8 */ u32_invalid_id,
    /* 0xB9 */ u32_invalid_id,
    /* 0xBA */ InputCode::key_colon,
    /* 0xBB */ InputCode::key_plus,
    /* 0xBC */ InputCode::key_comma,
    /* 0xBD */ InputCode::key_minus,
    /* 0xBE */ InputCode::key_period,
    /* 0xBF */ InputCode::key_question,

    /* 0xC0 */ InputCode::key_tilde,
    /* 0xC1 */ u32_invalid_id,
    /* 0xC2 */ u32_invalid_id,
    /* 0xC3 */ u32_invalid_id,
    /* 0xC4 */ u32_invalid_id,
    /* 0xC5 */ u32_invalid_id,
    /* 0xC6 */ u32_invalid_id,
    /* 0xC7 */ u32_invalid_id,
    /* 0xC8 */ u32_invalid_id,
    /* 0xC9 */ u32_invalid_id,
    /* 0xCA */ u32_invalid_id,
    /* 0xCB */ u32_invalid_id,
    /* 0xCC */ u32_invalid_id,
    /* 0xCD */ u32_invalid_id,
    /* 0xCE */ u32_invalid_id,
    /* 0xCF */ u32_invalid_id,

    /* 0xD0 */ u32_invalid_id,
    /* 0xD1 */ u32_invalid_id,
    /* 0xD2 */ u32_invalid_id,
    /* 0xD3 */ u32_invalid_id,
    /* 0xD4 */ u32_invalid_id,
    /* 0xD5 */ u32_invalid_id,
    /* 0xD6 */ u32_invalid_id,
    /* 0xD7 */ u32_invalid_id,
    /* 0xD8 */ u32_invalid_id,
    /* 0xD9 */ u32_invalid_id,
    /* 0xDA */ u32_invalid_id,
    /* 0xDB */ InputCode::key_bracket_open,
    /* 0xDC */ InputCode::key_pipe,
    /* 0xDD */ InputCode::key_bracket_close,
    /* 0xDE */ InputCode::key_quote,
    /* 0xDF */ u32_invalid_id,

    /* 0xE0 */ u32_invalid_id,
    /* 0xE1 */ u32_invalid_id,
    /* 0xE2 */ u32_invalid_id,
    /* 0xE3 */ u32_invalid_id,
    /* 0xE4 */ u32_invalid_id,
    /* 0xE5 */ u32_invalid_id,
    /* 0xE6 */ u32_invalid_id,
    /* 0xE7 */ u32_invalid_id,
    /* 0xE8 */ u32_invalid_id,
    /* 0xE9 */ u32_invalid_id,
    /* 0xEA */ u32_invalid_id,
    /* 0xEB */ u32_invalid_id,
    /* 0xEC */ u32_invalid_id,
    /* 0xED */ u32_invalid_id,
    /* 0xEE */ u32_invalid_id,
    /* 0xEF */ u32_invalid_id,

    /* 0xF0 */ u32_invalid_id,
    /* 0xF1 */ u32_invalid_id,
    /* 0xF2 */ u32_invalid_id,
    /* 0xF3 */ u32_invalid_id,
    /* 0xF4 */ u32_invalid_id,
    /* 0xF5 */ u32_invalid_id,
    /* 0xF6 */ u32_invalid_id,
    /* 0xF7 */ u32_invalid_id,
    /* 0xF8 */ u32_invalid_id,
    /* 0xF9 */ u32_invalid_id,
    /* 0xFA */ u32_invalid_id,
    /* 0xFB */ u32_invalid_id,
    /* 0xFC */ u32_invalid_id,
    /* 0xFD */ u32_invalid_id,
    /* 0xFE */ u32_invalid_id,
    /* 0xFF */ u32_invalid_id,
};

struct ModifierFlags
{
    enum Flags : u8
    {
        left_shift = 0x10,
        left_control = 0x20,
        left_alt = 0x40,

        right_shift = 0x01,
        right_control = 0x02,
        right_alt = 0x04,
    };
};

u8 modifier_keys_state{0};

auto set_modifier_input(u8 virtual_key, InputCode::Code code, ModifierFlags::Flags flags) -> void
{
    if (::GetKeyState(virtual_key) < 0)
    {
        set(InputSource::keyboard, code, {1.f, 0.f, 0.f});
        modifier_keys_state |= flags;
    }
    else if (modifier_keys_state & flags)
    {
        set(InputSource::keyboard, code, {0.f, 0.f, 0.f});
        modifier_keys_state &= ~flags;
    }
}

auto set_modifier_inputs(InputCode::Code code) -> void
{
    if (code == InputCode::key_shift)
    {
        set_modifier_input(VK_LSHIFT, InputCode::key_left_shift, ModifierFlags::left_shift);
        set_modifier_input(VK_RSHIFT, InputCode::key_right_shift, ModifierFlags::right_shift);
    }
    else if (code == InputCode::key_control)
    {
        set_modifier_input(VK_LCONTROL, InputCode::key_left_control, ModifierFlags::left_control);
        set_modifier_input(VK_RCONTROL, InputCode::key_right_control, ModifierFlags::right_control);
    }
    else if (code == InputCode::key_alt)
    {
        set_modifier_input(VK_LMENU, InputCode::key_left_alt, ModifierFlags::left_alt);
        set_modifier_input(VK_RMENU, InputCode::key_right_alt, ModifierFlags::right_alt);
    }
}

constexpr auto get_mouse_position(LPARAM lparam) -> math::v2
{
    return {(f32)((s16)(lparam & 0x0000ffff)), (f32)((s16)(lparam >> 16))};
}

}

auto map_virtual_key(u32 virtual_key) -> u32
{
    return virtual_key <= 0xff ? vk_mapping[virtual_key] : u32_invalid_id;
}

auto modifier_keys() -> u32
{
    u32 keys{ModifierKey::none};
    if (::GetKeyState(VK_LSHIFT) < 0)
    {
        keys |= ModifierKey::left_shift;
    }
    if (::GetKeyState(VK_RSHIFT) < 0)
    {
        keys |= ModifierKey::right_shift;
    }
    if (::GetKeyState(VK_LCONTROL) < 0)
    {
        keys |= ModifierKey::left_ctrl;
    }
    if (::GetKeyState(VK_RCONTROL) < 0)
    {
        keys |= ModifierKey::right_ctrl;
    }
    if (::GetKeyState(VK_LMENU) < 0)
    {
        keys |= ModifierKey::left_alt;
    }
    if (::GetKeyState(VK_RMENU) < 0)
    {
        keys |= ModifierKey::right_alt;
    }
    return keys;
}

auto process_input_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) -> HRESULT
{
    switch (msg)
    {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        {
            assert(wparam <= 0xff);
            const InputCode::Code code{vk_mapping[wparam & 0xff]};
            if (code != u32_invalid_id)
            {
                set(InputSource::keyboard, code, {1.f, 0.f, 0.f});
                set_modifier_inputs(code);
            }
        }
        break;
        case WM_KEYUP:
        case WM_SYSKEYUP:
        {
            assert(wparam <= 0xff);
            const InputCode::Code code{vk_mapping[wparam & 0xff]};
            if (code != u32_invalid_id)
            {
                set(InputSource::keyboard, code, {0.f, 0.f, 0.f});
                set_modifier_inputs(code);
            }
        }
        break;
        case WM_MOUSEMOVE:
        {
            const math::v2 pos{get_mouse_position(lparam)};
            set(InputSource::mouse, InputCode::mouse_position_x, {pos.x, 0.f, 0.f});
            set(InputSource::mouse, InputCode::mouse_position_y, {pos.y, 0.f, 0.f});
            set(InputSource::mouse, InputCode::mouse_position, {pos.x, pos.y, 0.f});
        }
        break;
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        {
            ::SetCapture(hwnd);
            const InputCode::Code code{
                msg == WM_LBUTTONDOWN   ? InputCode::mouse_left
                : msg == WM_RBUTTONDOWN ? InputCode::mouse_right
                                        : InputCode::mouse_middle};
            const math::v2 pos{get_mouse_position(lparam)};
            set(InputSource::mouse, code, {pos.x, pos.y, 1.f});
        }
        break;
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
        {
            ::ReleaseCapture();
            const InputCode::Code code{
                msg == WM_LBUTTONUP   ? InputCode::mouse_left
                : msg == WM_RBUTTONUP ? InputCode::mouse_right
                                      : InputCode::mouse_middle};
            const math::v2 pos{get_mouse_position(lparam)};
            set(InputSource::mouse, code, {pos.x, pos.y, 0.f});
        }
        break;
        case WM_MOUSEWHEEL:
        {
            set(InputSource::mouse, InputCode::mouse_wheel, {(f32)(GET_WHEEL_DELTA_WPARAM(wparam)), 0.f, 0.f});
        }
        break;
        default: break;
    }

    return S_OK;
}
}

#endif
