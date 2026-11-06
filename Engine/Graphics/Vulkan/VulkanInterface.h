// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
namespace primal::graphics {
struct platform_interface;

namespace vulkan {
void get_platform_interface(platform_interface& pi);
}
} // namespace primal::graphics
