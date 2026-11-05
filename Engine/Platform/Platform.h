// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "CommonHeaders.h"
#include "Window.h"
#include <filesystem>

namespace primal::platform {

struct window_init_info;

window create_window(const window_init_info* const init_info = nullptr);
void remove_window(window_id id);

// Sets the working directory to the folder of the running executable.
// Returns the new working directory, or an empty path on failure.
std::filesystem::path set_current_directory_to_executable_path();
}
