// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.

// The test runner of the graphics tests: doctest's implementation and main(). The tests are in the other .cpp
// files of this folder. They open windows and render with Vulkan, so they need a display and a Vulkan 1.3 device;
// a virtual display (Xvfb) and a software device (lavapipe) will do, which is what CI uses.
#define DOCTEST_CONFIG_IMPLEMENT
#include "Platform/Platform.h"

#include <doctest.h>

auto main(int argc, char **argv) -> int
{
    // The engine finds its shaders (and the tests find TestShader.hlsl) relative to the executable.
    if (primal::platform::set_current_directory_to_executable_path().empty())
    {
        return 1;
    }

    doctest::Context context{argc, argv};
    return context.run();
}
