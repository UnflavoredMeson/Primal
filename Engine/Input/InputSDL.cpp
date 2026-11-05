// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "InputSDL.h"
#if PRIMAL_PLATFORM_LINUX
#include "Input.h"
#include <SDL3/SDL.h>


namespace primal::input {
    namespace {
        // Mouse wheel delta for one notch, to match Win32 (WHEEL_DELTA).
        constexpr f32 wheel_delta{120.f};

        u32 held_modifiers{modifier_key::none};

        void
        set_modifier(u32 flag, bool is_down) {
            if (is_down) held_modifiers |= flag;
            else held_modifiers &= ~flag;
        }

        // Win32 sends both the generic key (e.g. key_shift) and the left/right variant.
        // Do the same here. The generic key stays down while either side is held.
        void
        set_modifier_key(input_code::code side_code, u32 side_flag, input_code::code generic_code, u32 generic_flags,
                         bool is_down) {
            set_modifier(side_flag, is_down);
            set(input_source::keyboard, side_code, {is_down ? 1.f : 0.f, 0.f, 0.f});
            set(input_source::keyboard, generic_code, {(held_modifiers & generic_flags) ? 1.f : 0.f, 0.f, 0.f});
        }

        bool
        process_modifier(SDL_Scancode scancode, bool is_down) {
            switch (scancode) {
                case SDL_SCANCODE_LSHIFT: set_modifier_key(input_code::key_left_shift, modifier_key::left_shift,
                                                           input_code::key_shift, modifier_key::shift, is_down);
                    return true;
                case SDL_SCANCODE_RSHIFT: set_modifier_key(input_code::key_right_shift, modifier_key::right_shift,
                                                           input_code::key_shift, modifier_key::shift, is_down);
                    return true;
                case SDL_SCANCODE_LCTRL: set_modifier_key(input_code::key_left_control, modifier_key::left_ctrl,
                                                          input_code::key_control, modifier_key::ctrl, is_down);
                    return true;
                case SDL_SCANCODE_RCTRL: set_modifier_key(input_code::key_right_control, modifier_key::right_ctrl,
                                                          input_code::key_control, modifier_key::ctrl, is_down);
                    return true;
                case SDL_SCANCODE_LALT: set_modifier_key(input_code::key_left_alt, modifier_key::left_alt,
                                                         input_code::key_alt, modifier_key::alt, is_down);
                    return true;
                case SDL_SCANCODE_RALT: set_modifier_key(input_code::key_right_alt, modifier_key::right_alt,
                                                         input_code::key_alt, modifier_key::alt, is_down);
                    return true;
                default: return false;
            }
        }

        input_code::code
        mouse_button_code(u8 button) {
            switch (button) {
                case SDL_BUTTON_LEFT: return input_code::mouse_left;
                case SDL_BUTTON_RIGHT: return input_code::mouse_right;
                case SDL_BUTTON_MIDDLE: return input_code::mouse_middle;
                default: return (input_code::code) u32_invalid_id;
            }
        }
    } // anonymous namespace

    u32
    map_scancode(u32 scancode) {
        switch ((SDL_Scancode) scancode) {
            case SDL_SCANCODE_BACKSPACE: return input_code::key_backspace;
            case SDL_SCANCODE_TAB: return input_code::key_tab;
            case SDL_SCANCODE_RETURN: return input_code::key_return;
            case SDL_SCANCODE_KP_ENTER: return input_code::key_return; // Win32 reports VK_RETURN for both
            case SDL_SCANCODE_LSHIFT: return input_code::key_left_shift;
            case SDL_SCANCODE_RSHIFT: return input_code::key_right_shift;
            case SDL_SCANCODE_LCTRL: return input_code::key_left_control;
            case SDL_SCANCODE_RCTRL: return input_code::key_right_control;
            case SDL_SCANCODE_LALT: return input_code::key_left_alt;
            case SDL_SCANCODE_RALT: return input_code::key_right_alt;
            case SDL_SCANCODE_PAUSE: return input_code::key_pause;
            case SDL_SCANCODE_CAPSLOCK: return input_code::key_capslock;
            case SDL_SCANCODE_ESCAPE: return input_code::key_escape;
            case SDL_SCANCODE_SPACE: return input_code::key_space;
            case SDL_SCANCODE_PAGEUP: return input_code::key_page_up;
            case SDL_SCANCODE_PAGEDOWN: return input_code::key_page_down;
            case SDL_SCANCODE_HOME: return input_code::key_home;
            case SDL_SCANCODE_END: return input_code::key_end;
            case SDL_SCANCODE_LEFT: return input_code::key_left;
            case SDL_SCANCODE_UP: return input_code::key_up;
            case SDL_SCANCODE_RIGHT: return input_code::key_right;
            case SDL_SCANCODE_DOWN: return input_code::key_down;
            case SDL_SCANCODE_PRINTSCREEN: return input_code::key_print_screen;
            case SDL_SCANCODE_INSERT: return input_code::key_insert;
            case SDL_SCANCODE_DELETE: return input_code::key_delete;

            case SDL_SCANCODE_0: return input_code::key_0;
            case SDL_SCANCODE_1: return input_code::key_1;
            case SDL_SCANCODE_2: return input_code::key_2;
            case SDL_SCANCODE_3: return input_code::key_3;
            case SDL_SCANCODE_4: return input_code::key_4;
            case SDL_SCANCODE_5: return input_code::key_5;
            case SDL_SCANCODE_6: return input_code::key_6;
            case SDL_SCANCODE_7: return input_code::key_7;
            case SDL_SCANCODE_8: return input_code::key_8;
            case SDL_SCANCODE_9: return input_code::key_9;

            case SDL_SCANCODE_A: return input_code::key_a;
            case SDL_SCANCODE_B: return input_code::key_b;
            case SDL_SCANCODE_C: return input_code::key_c;
            case SDL_SCANCODE_D: return input_code::key_d;
            case SDL_SCANCODE_E: return input_code::key_e;
            case SDL_SCANCODE_F: return input_code::key_f;
            case SDL_SCANCODE_G: return input_code::key_g;
            case SDL_SCANCODE_H: return input_code::key_h;
            case SDL_SCANCODE_I: return input_code::key_i;
            case SDL_SCANCODE_J: return input_code::key_j;
            case SDL_SCANCODE_K: return input_code::key_k;
            case SDL_SCANCODE_L: return input_code::key_l;
            case SDL_SCANCODE_M: return input_code::key_m;
            case SDL_SCANCODE_N: return input_code::key_n;
            case SDL_SCANCODE_O: return input_code::key_o;
            case SDL_SCANCODE_P: return input_code::key_p;
            case SDL_SCANCODE_Q: return input_code::key_q;
            case SDL_SCANCODE_R: return input_code::key_r;
            case SDL_SCANCODE_S: return input_code::key_s;
            case SDL_SCANCODE_T: return input_code::key_t;
            case SDL_SCANCODE_U: return input_code::key_u;
            case SDL_SCANCODE_V: return input_code::key_v;
            case SDL_SCANCODE_W: return input_code::key_w;
            case SDL_SCANCODE_X: return input_code::key_x;
            case SDL_SCANCODE_Y: return input_code::key_y;
            case SDL_SCANCODE_Z: return input_code::key_z;

            case SDL_SCANCODE_KP_0: return input_code::key_numpad_0;
            case SDL_SCANCODE_KP_1: return input_code::key_numpad_1;
            case SDL_SCANCODE_KP_2: return input_code::key_numpad_2;
            case SDL_SCANCODE_KP_3: return input_code::key_numpad_3;
            case SDL_SCANCODE_KP_4: return input_code::key_numpad_4;
            case SDL_SCANCODE_KP_5: return input_code::key_numpad_5;
            case SDL_SCANCODE_KP_6: return input_code::key_numpad_6;
            case SDL_SCANCODE_KP_7: return input_code::key_numpad_7;
            case SDL_SCANCODE_KP_8: return input_code::key_numpad_8;
            case SDL_SCANCODE_KP_9: return input_code::key_numpad_9;

            case SDL_SCANCODE_KP_MULTIPLY: return input_code::key_multiply;
            case SDL_SCANCODE_KP_PLUS: return input_code::key_add;
            case SDL_SCANCODE_KP_MINUS: return input_code::key_subtract;
            case SDL_SCANCODE_KP_PERIOD: return input_code::key_decimal;
            case SDL_SCANCODE_KP_DIVIDE: return input_code::key_divide;

            case SDL_SCANCODE_F1: return input_code::key_f1;
            case SDL_SCANCODE_F2: return input_code::key_f2;
            case SDL_SCANCODE_F3: return input_code::key_f3;
            case SDL_SCANCODE_F4: return input_code::key_f4;
            case SDL_SCANCODE_F5: return input_code::key_f5;
            case SDL_SCANCODE_F6: return input_code::key_f6;
            case SDL_SCANCODE_F7: return input_code::key_f7;
            case SDL_SCANCODE_F8: return input_code::key_f8;
            case SDL_SCANCODE_F9: return input_code::key_f9;
            case SDL_SCANCODE_F10: return input_code::key_f10;
            case SDL_SCANCODE_F11: return input_code::key_f11;
            case SDL_SCANCODE_F12: return input_code::key_f12;

            case SDL_SCANCODE_NUMLOCKCLEAR: return input_code::key_numlock;
            case SDL_SCANCODE_SCROLLLOCK: return input_code::key_scrollock;
            case SDL_SCANCODE_SEMICOLON: return input_code::key_colon; // VK_OEM_1
            case SDL_SCANCODE_EQUALS: return input_code::key_plus; // VK_OEM_PLUS
            case SDL_SCANCODE_COMMA: return input_code::key_comma;
            case SDL_SCANCODE_MINUS: return input_code::key_minus;
            case SDL_SCANCODE_PERIOD: return input_code::key_period;
            case SDL_SCANCODE_SLASH: return input_code::key_question; // VK_OEM_2
            case SDL_SCANCODE_LEFTBRACKET: return input_code::key_bracket_open; // VK_OEM_4
            case SDL_SCANCODE_BACKSLASH: return input_code::key_pipe; // VK_OEM_5
            case SDL_SCANCODE_RIGHTBRACKET: return input_code::key_bracket_close; // VK_OEM_6
            case SDL_SCANCODE_APOSTROPHE: return input_code::key_quote; // VK_OEM_7
            case SDL_SCANCODE_GRAVE: return input_code::key_tilde; // VK_OEM_3
            default: return u32_invalid_id;
        }
    }

    u32
    modifier_keys() {
        return held_modifiers;
    }

    void
    process_input_event(const SDL_Event &event) {
        switch (event.type) {
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP: {
                const bool is_down{event.type == SDL_EVENT_KEY_DOWN};
                if (process_modifier(event.key.scancode, is_down)) break;

                const u32 code{map_scancode(event.key.scancode)};
                if (code != u32_invalid_id) {
                    set(input_source::keyboard, (input_code::code) code, {is_down ? 1.f : 0.f, 0.f, 0.f});
                }
            }
            break;
            case SDL_EVENT_MOUSE_MOTION: {
                const f32 x{event.motion.x};
                const f32 y{event.motion.y};
                set(input_source::mouse, input_code::mouse_position_x, {x, 0.f, 0.f});
                set(input_source::mouse, input_code::mouse_position_y, {y, 0.f, 0.f});
                set(input_source::mouse, input_code::mouse_position, {x, y, 0.f});
            }
            break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP: {
                // NOTE: SDL captures the mouse while a button is held (SDL_HINT_MOUSE_AUTO_CAPTURE),
                //       like SetCapture()/ReleaseCapture() in InputWin32.cpp.
                const input_code::code code{mouse_button_code(event.button.button)};
                if (code != u32_invalid_id) {
                    const f32 pressed{event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? 1.f : 0.f};
                    set(input_source::mouse, code, {event.button.x, event.button.y, pressed});
                }
            }
            break;
            case SDL_EVENT_MOUSE_WHEEL: {
                f32 notches{event.wheel.y};
                if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) notches = -notches;
                set(input_source::mouse, input_code::mouse_wheel, {notches * wheel_delta, 0.f, 0.f});
            }
            break;
            default:
                break;
        }
    }
}
#endif // PRIMAL_PLATFORM_LINUX
