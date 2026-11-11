// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "PrimitiveTypes.h"

namespace primal::graphics
{
struct PlatformInterface;

namespace vulkan
{
auto get_platform_interface(PlatformInterface &pi) -> void;

// What the validation layer reported since the last graphics::initialize(). For the automated tests
// (GraphicsTests/): the counts stay readable after graphics::shutdown(), which is when leaked objects are reported.
// NOTE: validation is only on in debug builds, when the layer is installed and PRIMAL_VULKAN_VALIDATION isn't 0.
struct ValidationStats
{
    bool is_enabled{false};
    u32 error_count{0};
    u32 warning_count{0};
};
[[nodiscard]] auto validation_stats() -> ValidationStats;
}
}
