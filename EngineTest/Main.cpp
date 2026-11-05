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
#else
#error One of the tests need to be enabled
#endif

#include <filesystem>

#if PRIMAL_PLATFORM_WINDOWS
#include <Windows.h>

// TODO: duplicate!
std::filesystem::path
set_current_directory_to_executable_path()
{
    // set the working directory to the executable path
    wchar_t path[MAX_PATH];
    const uint32_t length{ GetModuleFileName(0, &path[0], MAX_PATH) };
    if (!length || GetLastError() == ERROR_INSUFFICIENT_BUFFER) return {};
    std::filesystem::path p{ path };
    std::filesystem::current_path(p.parent_path());
    return std::filesystem::current_path();
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
#if _DEBUG
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    // When a leak is detected, call _CrtSetBreakAlloc(N) with leak number N to
    // break at leak site.
#endif

    set_current_directory_to_executable_path();
    engine_test test{};
    if (test.initialize())
    {
        MSG msg{};
        bool is_running{ true };
        while (is_running)
        {
            while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
                is_running &= (msg.message != WM_QUIT);
            }

            test.run();
        }
    }
    test.shutdown();
    return 0;
}

#elif PRIMAL_PLATFORM_LINUX

// TODO: duplicate!
std::filesystem::path
set_current_directory_to_executable_path()
{
    // set the working directory to the executable path
    std::error_code ec;
    const std::filesystem::path p{ std::filesystem::read_symlink("/proc/self/exe", ec) };
    if (ec) return {};
    std::filesystem::current_path(p.parent_path(), ec);
    if (ec) return {};
    return std::filesystem::current_path();
}

// NOTE: there is no _CrtSetDbgFlag on Linux. For leak checking, configure with
//       -DPRIMAL_ENABLE_SANITIZERS=ON (AddressSanitizer includes LeakSanitizer).
int main()
{
    set_current_directory_to_executable_path();
    engine_test test{};

    if (test.initialize())
    {
        //TODO: replace with the platform event pump once the platform layer is ported
        test.run();
    }

    test.shutdown();
}
#endif