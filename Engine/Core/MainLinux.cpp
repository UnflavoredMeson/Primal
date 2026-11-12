// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "CommonHeaders.h"
#if PRIMAL_PLATFORM_LINUX
#include "Core/Log.h"
#include "Platform/Platform.h"
#include "Platform/PlatformTypes.h"

// Linux counterpart of MainWin32.cpp: entry point of a game executable linked with the engine.
// NOTE: an executable that defines its own main() (e.g. EngineTest) doesn't pull this in,
//       because the linker only uses archive members that resolve undefined symbols.

#ifndef USE_WITH_EDITOR

extern auto engine_initialize() -> bool;

extern auto engine_update() -> void;

extern auto engine_shutdown() -> void;

auto main() -> int
{
    // NOTE: for leak checking, configure with -DPRIMAL_ENABLE_SANITIZERS=ON.
    primal::platform::set_current_directory_to_executable_path();
    primal::log::initialize();
    primal::log::open_file("primal.log");

    if (::engine_initialize())
    {
        while (primal::platform::pump_events())
        {
            ::engine_update();
        }
    }
    ::engine_shutdown();
    primal::log::shutdown();
    primal::log::close_file();
    return 0;
}

#endif
#endif
