// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "CommonHeaders.h"
#if PRIMAL_PLATFORM_WINDOWS
#include "Platform/PlatformTypes.h"
#include "Platform/Platform.h"

#ifndef  WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif // ! WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <crtdbg.h>

#ifndef USE_WITH_EDITOR

extern bool engine_initialize();
extern void engine_update();
extern void engine_shutdown();

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
#if _DEBUG
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    primal::platform::set_current_directory_to_executable_path();

    if (engine_initialize())
    {
        while (primal::platform::pump_events())
        {
            engine_update();
        }
    }
    engine_shutdown();
    return 0;
}

#endif // USE_WITH_EDITOR
#endif // PRIMAL_PLATFORM_WINDOWS