# Copyright (c) 2026 Charles Smith
# Distributed under the MIT license. See the LICENSE file in the project root for more information.
#
# Third-party dependencies for the Linux build.

include(FetchContent)

# DirectXMath (header-only, portable). Uses a system install if one is found, otherwise fetches a pinned release.
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(DirectXMath
    URL      https://github.com/microsoft/DirectXMath/archive/refs/tags/jun2026.tar.gz
    URL_HASH SHA256=cb46002679b16a25fd11d0a2a13f6a7c7bf9c51bf04258fb5485e5b839da8474
    FIND_PACKAGE_ARGS CONFIG NAMES directxmath DirectXMath
)
FetchContent_MakeAvailable(DirectXMath)

if(NOT TARGET Microsoft::DirectXMath)
    add_library(Microsoft::DirectXMath ALIAS DirectXMath)
endif()
# Third-party headers: include them as system headers so their warnings (e.g. GCC -Wunused-but-set-variable in
# DirectXMathMatrix.inl) aren't reported. (FetchContent's SYSTEM option doesn't reach this INTERFACE target.)
if(TARGET DirectXMath)
    get_target_property(_dxmath_includes DirectXMath INTERFACE_INCLUDE_DIRECTORIES)
    set_target_properties(DirectXMath PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_dxmath_includes}")
endif()

# doctest (one header) for UnitTests. Downloaded, not vendored. UnitTests/UnitTests.vcxproj downloads the same file
# with the same hash: change the version in both.
FetchContent_Declare(doctest
    URL      https://raw.githubusercontent.com/doctest/doctest/v2.5.3/doctest/doctest.h
    URL_HASH SHA256=cfd518a3ef90f67e1f3ba514df23fb3627437de1a2feeba78cf5062a40021421
    DOWNLOAD_NO_EXTRACT TRUE
)
FetchContent_MakeAvailable(doctest)
add_library(primal_doctest INTERFACE)
add_library(primal::doctest ALIAS primal_doctest)
# SYSTEM: no warnings from it.
target_include_directories(primal_doctest SYSTEM INTERFACE "${doctest_SOURCE_DIR}")

# SDL3 (system package) for standalone windows, input and the event loop.
find_package(SDL3 3.2 REQUIRED CONFIG COMPONENTS SDL3-shared)

# DirectXMath includes <sal.h>, which only exists on Windows. Provide a minimal stub elsewhere.
add_library(primal_sal_stub INTERFACE)
add_library(primal::sal ALIAS primal_sal_stub)
target_include_directories(primal_sal_stub SYSTEM INTERFACE "${PROJECT_SOURCE_DIR}/Engine/ThirdParty/sal")

# Vulkan loader + headers (system packages: vulkan-icd-loader, vulkan-headers).
find_package(Vulkan 1.3 REQUIRED)
