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

# DirectXMath includes <sal.h>, which only exists on Windows. Provide a minimal stub elsewhere.
add_library(primal_sal_stub INTERFACE)
add_library(primal::sal ALIAS primal_sal_stub)
target_include_directories(primal_sal_stub SYSTEM INTERFACE "${PROJECT_SOURCE_DIR}/Engine/ThirdParty/sal")
