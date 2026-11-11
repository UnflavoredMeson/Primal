# Copyright (c) 2026 Charles Smith
# Distributed under the MIT license. See the LICENSE file in the project root for more information.
#
# GraphicsTests: the automated tests that open windows and render with Vulkan (doctest). They need a display and a
# Vulkan 1.3 device; a virtual display (Xvfb) with a software device (lavapipe) will do, which is what CI uses.
# All content is made by the tests themselves (TestContent.h), so the assets in x64/ aren't needed.
#   x64/<Config>/GraphicsTests                           run everything
#   x64/<Config>/GraphicsTests -ts="graphics scene"      one test suite (--help lists the options)
#   xvfb-run -a x64/<Config>/GraphicsTests               without showing windows
#   ctest --test-dir build/<preset> -L graphics          the same through CTest

add_executable(GraphicsTests
    Main.cpp
    SceneTests.cpp
    SurfaceTests.cpp
    TestContent.cpp
    TestSession.cpp
    WindowTests.cpp
)

# doctest is downloaded at configure time (cmake/PrimalDependencies.cmake). ShaderCompiler: the tests compile the
# engine shaders (when they changed) and EngineTest/TestShader.hlsl.
target_link_libraries(GraphicsTests PRIVATE Engine ShaderCompiler primal::doctest)

# Next to EngineTest (x64/<Config>/): the engine and the tests find the shaders relative to the executable.
set_target_properties(GraphicsTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/x64/$<CONFIG>"
)

# Labelled, so that "ctest -LE graphics" runs only the tests that need no display and no GPU.
add_test(NAME GraphicsTests COMMAND GraphicsTests)
set_tests_properties(GraphicsTests PROPERTIES LABELS graphics)
