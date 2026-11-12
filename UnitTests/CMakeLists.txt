# Copyright (c) 2026 Charles Smith
# Distributed under the MIT license. See the LICENSE file in the project root for more information.
#
# UnitTests: the automated tests (doctest). One executable runs every test and exits non-zero when one fails.
#   x64/<Config>/UnitTests                      run everything
#   x64/<Config>/UnitTests -ts="utl::vector"    one test suite (--help lists the options)
#   ctest --test-dir build/<preset>             the same through CTest

add_executable(UnitTests
    Main.cpp
    ContentLoaderTests.cpp
    EntityTests.cpp
    FrameTests.cpp
    FreeListTests.cpp
    IdTests.cpp
    InputTests.cpp
    IOStreamTests.cpp
    JobTests.cpp
    LogTests.cpp
    MathTests.cpp
    ShaderBlobTests.cpp
    VectorTests.cpp
)

# doctest is downloaded at configure time (cmake/PrimalDependencies.cmake).
target_link_libraries(UnitTests PRIVATE Engine primal::doctest)

set_target_properties(UnitTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/x64/$<CONFIG>"
)

add_test(NAME UnitTests COMMAND UnitTests)
