// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "CommonHeaders.h"
#include "Window.h"
#include <filesystem>

namespace primal::platform
{

struct WindowInitInfo;

auto create_window(const WindowInitInfo *const init_info = nullptr) -> Window;
auto remove_window(WindowId id) -> void;

// Sets the working directory to the folder of the running executable.
// Returns the new working directory, or an empty path on failure.
auto set_current_directory_to_executable_path() -> std::filesystem::path;
}
