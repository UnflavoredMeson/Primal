// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
namespace primal::graphics
{
struct PlatformInterface;

namespace vulkan
{
auto get_platform_interface(PlatformInterface &pi) -> void;
}
}
