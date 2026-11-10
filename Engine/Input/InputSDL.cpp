// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "InputSDL.h"
#if PRIMAL_PLATFORM_LINUX
#include "Input.h"

#include <SDL3/SDL.h>

namespace primal::input
{
namespace
{
// Mouse wheel delta for one notch, to match Win32 (WHEEL_DELTA).
constexpr f32 wheel_delta{120.f};

u32 held_modifiers{ModifierKey::none};

auto set_modifier(u32 flag, bool is_down) -> void
{
    if (is_down)
    {
        held_modifiers |= flag;
    }
    else
    {
        held_modifiers &= ~flag;
    }
}

// Win32 sends both the generic key (e.g. key_shift) and the left/right variant.
// Do the same here. The generic key stays down while either side is held.
auto set_modifier_key(
    InputCode::Code side_code,
    u32 side_flag,
    InputCode::Code generic_code,
    u32 generic_flags,
    bool is_down) -> void
{
    set_modifier(side_flag, is_down);
    set(InputSource::keyboard, side_code, {is_down ? 1.f : 0.f, 0.f, 0.f});
    set(InputSource::keyboard, generic_code, {(held_modifiers & generic_flags) ? 1.f : 0.f, 0.f, 0.f});
}

auto process_modifier(SDL_Scancode scancode, bool is_down) -> bool
{
    switch (scancode)
    {
        case SDL_SCANCODE_LSHIFT:
            set_modifier_key(
                InputCode::key_left_shift, ModifierKey::left_shift, InputCode::key_shift, ModifierKey::shift, is_down);
            return true;
        case SDL_SCANCODE_RSHIFT:
            set_modifier_key(
                InputCode::key_right_shift,
                ModifierKey::right_shift,
                InputCode::key_shift,
                ModifierKey::shift,
                is_down);
            return true;
        case SDL_SCANCODE_LCTRL:
            set_modifier_key(
                InputCode::key_left_control,
                ModifierKey::left_ctrl,
                InputCode::key_control,
                ModifierKey::ctrl,
                is_down);
            return true;
        case SDL_SCANCODE_RCTRL:
            set_modifier_key(
                InputCode::key_right_control,
                ModifierKey::right_ctrl,
                InputCode::key_control,
                ModifierKey::ctrl,
                is_down);
            return true;
        case SDL_SCANCODE_LALT:
            set_modifier_key(
                InputCode::key_left_alt, ModifierKey::left_alt, InputCode::key_alt, ModifierKey::alt, is_down);
            return true;
        case SDL_SCANCODE_RALT:
            set_modifier_key(
                InputCode::key_right_alt, ModifierKey::right_alt, InputCode::key_alt, ModifierKey::alt, is_down);
            return true;
        default: return false;
    }
}

auto mouse_button_code(u8 button) -> InputCode::Code
{
    switch (button)
    {
        case SDL_BUTTON_LEFT: return InputCode::mouse_left;
        case SDL_BUTTON_RIGHT: return InputCode::mouse_right;
        case SDL_BUTTON_MIDDLE: return InputCode::mouse_middle;
        default: return static_cast<InputCode::Code>(u32_invalid_id);
    }
}
}

auto map_scancode(u32 scancode) -> u32
{
    switch (static_cast<SDL_Scancode>(scancode))
    {
        case SDL_SCANCODE_BACKSPACE: return InputCode::key_backspace;
        case SDL_SCANCODE_TAB: return InputCode::key_tab;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER: return InputCode::key_return; // Win32 reports VK_RETURN for both
        case SDL_SCANCODE_LSHIFT: return InputCode::key_left_shift;
        case SDL_SCANCODE_RSHIFT: return InputCode::key_right_shift;
        case SDL_SCANCODE_LCTRL: return InputCode::key_left_control;
        case SDL_SCANCODE_RCTRL: return InputCode::key_right_control;
        case SDL_SCANCODE_LALT: return InputCode::key_left_alt;
        case SDL_SCANCODE_RALT: return InputCode::key_right_alt;
        case SDL_SCANCODE_PAUSE: return InputCode::key_pause;
        case SDL_SCANCODE_CAPSLOCK: return InputCode::key_capslock;
        case SDL_SCANCODE_ESCAPE: return InputCode::key_escape;
        case SDL_SCANCODE_SPACE: return InputCode::key_space;
        case SDL_SCANCODE_PAGEUP: return InputCode::key_page_up;
        case SDL_SCANCODE_PAGEDOWN: return InputCode::key_page_down;
        case SDL_SCANCODE_HOME: return InputCode::key_home;
        case SDL_SCANCODE_END: return InputCode::key_end;
        case SDL_SCANCODE_LEFT: return InputCode::key_left;
        case SDL_SCANCODE_UP: return InputCode::key_up;
        case SDL_SCANCODE_RIGHT: return InputCode::key_right;
        case SDL_SCANCODE_DOWN: return InputCode::key_down;
        case SDL_SCANCODE_PRINTSCREEN: return InputCode::key_print_screen;
        case SDL_SCANCODE_INSERT: return InputCode::key_insert;
        case SDL_SCANCODE_DELETE: return InputCode::key_delete;

        case SDL_SCANCODE_0: return InputCode::key_0;
        case SDL_SCANCODE_1: return InputCode::key_1;
        case SDL_SCANCODE_2: return InputCode::key_2;
        case SDL_SCANCODE_3: return InputCode::key_3;
        case SDL_SCANCODE_4: return InputCode::key_4;
        case SDL_SCANCODE_5: return InputCode::key_5;
        case SDL_SCANCODE_6: return InputCode::key_6;
        case SDL_SCANCODE_7: return InputCode::key_7;
        case SDL_SCANCODE_8: return InputCode::key_8;
        case SDL_SCANCODE_9: return InputCode::key_9;

        case SDL_SCANCODE_A: return InputCode::key_a;
        case SDL_SCANCODE_B: return InputCode::key_b;
        case SDL_SCANCODE_C: return InputCode::key_c;
        case SDL_SCANCODE_D: return InputCode::key_d;
        case SDL_SCANCODE_E: return InputCode::key_e;
        case SDL_SCANCODE_F: return InputCode::key_f;
        case SDL_SCANCODE_G: return InputCode::key_g;
        case SDL_SCANCODE_H: return InputCode::key_h;
        case SDL_SCANCODE_I: return InputCode::key_i;
        case SDL_SCANCODE_J: return InputCode::key_j;
        case SDL_SCANCODE_K: return InputCode::key_k;
        case SDL_SCANCODE_L: return InputCode::key_l;
        case SDL_SCANCODE_M: return InputCode::key_m;
        case SDL_SCANCODE_N: return InputCode::key_n;
        case SDL_SCANCODE_O: return InputCode::key_o;
        case SDL_SCANCODE_P: return InputCode::key_p;
        case SDL_SCANCODE_Q: return InputCode::key_q;
        case SDL_SCANCODE_R: return InputCode::key_r;
        case SDL_SCANCODE_S: return InputCode::key_s;
        case SDL_SCANCODE_T: return InputCode::key_t;
        case SDL_SCANCODE_U: return InputCode::key_u;
        case SDL_SCANCODE_V: return InputCode::key_v;
        case SDL_SCANCODE_W: return InputCode::key_w;
        case SDL_SCANCODE_X: return InputCode::key_x;
        case SDL_SCANCODE_Y: return InputCode::key_y;
        case SDL_SCANCODE_Z: return InputCode::key_z;

        case SDL_SCANCODE_KP_0: return InputCode::key_numpad_0;
        case SDL_SCANCODE_KP_1: return InputCode::key_numpad_1;
        case SDL_SCANCODE_KP_2: return InputCode::key_numpad_2;
        case SDL_SCANCODE_KP_3: return InputCode::key_numpad_3;
        case SDL_SCANCODE_KP_4: return InputCode::key_numpad_4;
        case SDL_SCANCODE_KP_5: return InputCode::key_numpad_5;
        case SDL_SCANCODE_KP_6: return InputCode::key_numpad_6;
        case SDL_SCANCODE_KP_7: return InputCode::key_numpad_7;
        case SDL_SCANCODE_KP_8: return InputCode::key_numpad_8;
        case SDL_SCANCODE_KP_9: return InputCode::key_numpad_9;

        case SDL_SCANCODE_KP_MULTIPLY: return InputCode::key_multiply;
        case SDL_SCANCODE_KP_PLUS: return InputCode::key_add;
        case SDL_SCANCODE_KP_MINUS: return InputCode::key_subtract;
        case SDL_SCANCODE_KP_PERIOD: return InputCode::key_decimal;
        case SDL_SCANCODE_KP_DIVIDE: return InputCode::key_divide;

        case SDL_SCANCODE_F1: return InputCode::key_f1;
        case SDL_SCANCODE_F2: return InputCode::key_f2;
        case SDL_SCANCODE_F3: return InputCode::key_f3;
        case SDL_SCANCODE_F4: return InputCode::key_f4;
        case SDL_SCANCODE_F5: return InputCode::key_f5;
        case SDL_SCANCODE_F6: return InputCode::key_f6;
        case SDL_SCANCODE_F7: return InputCode::key_f7;
        case SDL_SCANCODE_F8: return InputCode::key_f8;
        case SDL_SCANCODE_F9: return InputCode::key_f9;
        case SDL_SCANCODE_F10: return InputCode::key_f10;
        case SDL_SCANCODE_F11: return InputCode::key_f11;
        case SDL_SCANCODE_F12: return InputCode::key_f12;

        case SDL_SCANCODE_NUMLOCKCLEAR: return InputCode::key_numlock;
        case SDL_SCANCODE_SCROLLLOCK: return InputCode::key_scrollock;
        case SDL_SCANCODE_SEMICOLON: return InputCode::key_colon; // VK_OEM_1
        case SDL_SCANCODE_EQUALS: return InputCode::key_plus;     // VK_OEM_PLUS
        case SDL_SCANCODE_COMMA: return InputCode::key_comma;
        case SDL_SCANCODE_MINUS: return InputCode::key_minus;
        case SDL_SCANCODE_PERIOD: return InputCode::key_period;
        case SDL_SCANCODE_SLASH: return InputCode::key_question;             // VK_OEM_2
        case SDL_SCANCODE_LEFTBRACKET: return InputCode::key_bracket_open;   // VK_OEM_4
        case SDL_SCANCODE_BACKSLASH: return InputCode::key_pipe;             // VK_OEM_5
        case SDL_SCANCODE_RIGHTBRACKET: return InputCode::key_bracket_close; // VK_OEM_6
        case SDL_SCANCODE_APOSTROPHE: return InputCode::key_quote;           // VK_OEM_7
        case SDL_SCANCODE_GRAVE: return InputCode::key_tilde;                // VK_OEM_3
        default: return u32_invalid_id;
    }
}

auto modifier_keys() -> u32
{
    return held_modifiers;
}

auto process_input_event(const SDL_Event &event) -> void
{
    switch (event.type)
    {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        {
            const bool is_down{event.type == SDL_EVENT_KEY_DOWN};
            if (process_modifier(event.key.scancode, is_down))
            {
                break;
            }

            const u32 code{map_scancode(event.key.scancode)};
            if (code != u32_invalid_id)
            {
                set(InputSource::keyboard, static_cast<InputCode::Code>(code), {is_down ? 1.f : 0.f, 0.f, 0.f});
            }
        }
        break;
        case SDL_EVENT_MOUSE_MOTION:
        {
            const f32 x{event.motion.x};
            const f32 y{event.motion.y};
            set(InputSource::mouse, InputCode::mouse_position_x, {x, 0.f, 0.f});
            set(InputSource::mouse, InputCode::mouse_position_y, {y, 0.f, 0.f});
            set(InputSource::mouse, InputCode::mouse_position, {x, y, 0.f});
        }
        break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        {
            // NOTE: SDL captures the mouse while a button is held (SDL_HINT_MOUSE_AUTO_CAPTURE),
            //       like SetCapture()/ReleaseCapture() in InputWin32.cpp.
            const InputCode::Code code{mouse_button_code(event.button.button)};
            if (code != u32_invalid_id)
            {
                const f32 pressed{event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? 1.f : 0.f};
                set(InputSource::mouse, code, {event.button.x, event.button.y, pressed});
            }
        }
        break;
        case SDL_EVENT_MOUSE_WHEEL:
        {
            f32 notches{event.wheel.y};
            if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
            {
                notches = -notches;
            }
            set(InputSource::mouse, InputCode::mouse_wheel, {notches * wheel_delta, 0.f, 0.f});
        }
        break;
        default: break;
    }
}
}
#endif
