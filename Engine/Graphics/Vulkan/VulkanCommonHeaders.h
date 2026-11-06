// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "CommonHeaders.h"
#include "Graphics/Renderer.h"
#include "Platform/Window.h"

// NOTE: only the core API here. Window-system surface extensions (xlib, wayland, win32) are
//       included by VulkanSurface.cpp alone, so their headers' macros don't leak into the engine.
#include <vulkan/vulkan.h>

#include <cstdio>

namespace primal::graphics::vulkan {
// Same as d3d12::frame_buffer_count: the CPU can record up to this many frames ahead of the GPU.
constexpr u32 frame_buffer_count{ 3 };
// Vulkan 1.3 core: dynamic rendering, synchronization2, maintenance4 (+ 1.2 timeline semaphores).
constexpr u32 minimum_api_version{ VK_API_VERSION_1_3 };

namespace detail {
// Logs a failed Vulkan call. Returns true if the call succeeded (result >= 0, which includes
// VK_SUBOPTIMAL_KHR and the other positive success codes).
inline bool
check_result(VkResult result, const char* call, const char* file, u32 line)
{
    if (result >= 0) return true;
    std::fprintf(stderr, "Vulkan error %d in: %s\nLine: %u\n%s\n", (s32)result, file, line, call);
#ifdef _DEBUG
    __builtin_debugtrap();
#endif
    return false;
}
} // namespace detail
} // namespace primal::graphics::vulkan

// Checks that a Vulkan call succeeded (logs and breaks into the debugger in debug builds).
// Unlike DXCall, it's an expression in all configurations: it evaluates to true on success, so
// it can be used as `if (!VkCall(vkCreateFoo(...))) return false;`.
#ifndef VkCall
#define VkCall(x) ::primal::graphics::vulkan::detail::check_result((x), #x, __FILE__, __LINE__)
#endif

#ifdef _DEBUG
// Names a Vulkan object for validation messages and RenderDoc (VK_EXT_debug_utils).
#define NAME_VK_OBJECT(handle, type, name) ::primal::graphics::vulkan::core::set_object_name((u64)(handle), type, name)
#define NAME_VK_OBJECT_INDEXED(handle, type, n, name)                                  \
    {                                                                                  \
        char full_name[128];                                                           \
        if (std::snprintf(full_name, sizeof(full_name), "%s[%u]", name, (u32)(n)) > 0) \
            NAME_VK_OBJECT(handle, type, full_name);                                   \
    }
#else
#define NAME_VK_OBJECT(handle, type, name)
#define NAME_VK_OBJECT_INDEXED(handle, type, n, name)
#endif // _DEBUG
