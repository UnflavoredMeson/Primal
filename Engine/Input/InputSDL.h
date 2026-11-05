// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once

#include "CommonHeaders.h"
#if PRIMAL_PLATFORM_LINUX
#include "EngineAPI/Input.h"

union SDL_Event;

namespace primal::input {
    // Updates the input state from an SDL keyboard, mouse button, mouse motion or mouse wheel event.
    // Other events are ignored.
    void process_input_event(const SDL_Event &event);

    // Maps an SDL_Scancode to an input code. Returns u32_invalid_id for unmapped keys.
    u32 map_scancode(u32 scancode);

    // Currently held modifier keys as modifier_key::key flags.
    u32 modifier_keys();
}

#endif // PRIMAL_PLATFORM_LINUX
