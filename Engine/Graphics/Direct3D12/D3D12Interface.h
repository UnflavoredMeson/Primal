// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
namespace primal::graphics
{
struct PlatformInterface;

namespace d3d12
{
auto get_platform_interface(PlatformInterface &pi) -> void;
}
}
