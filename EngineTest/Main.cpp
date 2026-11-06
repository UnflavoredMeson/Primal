// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "Test.h"

#ifdef _MSC_VER
#pragma comment(lib, "engine.lib")
#endif

#if TEST_ENTITY_COMPONENTS
#include "TestEntityComponents.h"
#elif TEST_WINDOW
#include "TestWindow.h"
#elif TEST_RENDERER
#include "TestRenderer.h"
#elif TEST_SURFACES
#include "TestSurfaces.h"
#else
#error One of the tests need to be enabled
#endif

#include "../Engine/Platform/PlatformTypes.h"
#include "../Engine/Platform/Platform.h"

int
run_test()
{
    primal::platform::set_current_directory_to_executable_path();
    engine_test test{};
    if (test.initialize())
    {
#if TEST_ENTITY_COMPONENTS
        test.run(); // runs its own console loop
#else
        while (primal::platform::pump_events())
        {
            test.run();
        }
#endif
    }
    test.shutdown();
    return 0;
}

#if PRIMAL_PLATFORM_WINDOWS
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
#if _DEBUG
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    // When a leak is detected, call _CrtSetBreakAlloc(N) with leak number N to
    // break at leak site.
#endif
    return run_test();
}
#elif PRIMAL_PLATFORM_LINUX
// NOTE: there is no _CrtSetDbgFlag on Linux. For leak checking, configure with
//       -DPRIMAL_ENABLE_SANITIZERS=ON (AddressSanitizer includes LeakSanitizer).
int main()
{
    return run_test();
}
#endif
