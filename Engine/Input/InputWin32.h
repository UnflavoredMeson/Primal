// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "CommonHeaders.h"
#ifdef PRIMAL_PLATFORM_WINDOWS

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif // !WIN32_LEAN_AND_MEAN

#include <Windows.h>

namespace primal::input {

HRESULT process_input_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

// Maps a virtual key code to an input code. Returns u32_invalid_id for unmapped keys.
u32 map_virtual_key(u32 virtual_key);

// Currently held modifier keys as modifier_key::key flags.
u32 modifier_keys();

}

#endif // PRIMAL_PLATFORM_WINDOWS