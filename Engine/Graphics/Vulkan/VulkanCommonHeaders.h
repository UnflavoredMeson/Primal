// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "CommonHeaders.h"
#if PRIMAL_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif
#include "Core/Log.h"
#include "Graphics/Renderer.h"
#include "Platform/Window.h"
// The Vulkan headers (core API only) and the loaded function pointers. Window-system surface extensions (xlib,
// wayland, win32) are included by VulkanSurface.cpp alone, so their headers' macros don't leak into the engine.
#include "VulkanLoader.h"

// Vulkan Memory Allocator (Engine/ThirdParty/vma; declarations only, the implementation is
// compiled once in VulkanMemoryAllocator.cpp).
#include <vk_mem_alloc.h>

#include <cstdio>

// Breaks into the debugger (MSVC, clang and gcc spellings).
#if defined(_MSC_VER)
#define PRIMAL_VK_DEBUG_BREAK() __debugbreak()
#elif defined(__clang__)
#define PRIMAL_VK_DEBUG_BREAK() __builtin_debugtrap()
#else
#include <csignal>
#define PRIMAL_VK_DEBUG_BREAK() std::raise(SIGTRAP)
#endif

namespace primal::graphics::vulkan
{
// Same as d3d12::frame_buffer_count: the CPU can record up to this many frames ahead of the GPU.
constexpr u32 frame_buffer_count{3};
// Vulkan 1.3 core: dynamic rendering, synchronization2, maintenance4 (+ 1.2 timeline semaphores).
constexpr u32 minimum_api_version{VK_API_VERSION_1_3};

// Vulkan handles as u64 (debug names, deferred releases). Dispatchable handles are pointers; non-dispatchable handles
// are pointers in 64-bit builds and u64 in 32-bit ones, so a single kind of cast doesn't fit both.
template <typename T>
[[nodiscard]] auto handle_to_u64(T handle) -> u64
{
    if constexpr (std::is_pointer_v<T>)
    {
        return reinterpret_cast<u64>(handle);
    }
    else
    {
        return static_cast<u64>(handle);
    }
}

template <typename T>
[[nodiscard]] auto handle_from_u64(u64 value) -> T
{
    if constexpr (std::is_pointer_v<T>)
    {
        return reinterpret_cast<T>(value);
    }
    else
    {
        return static_cast<T>(value);
    }
}

namespace detail
{
// Logs a failed Vulkan call. Returns true if the call succeeded (result >= 0, which includes
// VK_SUBOPTIMAL_KHR and the other positive success codes).
inline auto check_result(VkResult result, const char *call, const char *file, u32 line) -> bool
{
    if (result >= 0)
    {
        return true;
    }
    log::error(log::Category::vulkan, "Error {} in {} ({}:{})", static_cast<s32>(result), call, file, line);
#ifdef _DEBUG
    PRIMAL_VK_DEBUG_BREAK();
#endif
    return false;
}
}
}

// Checks that a Vulkan call succeeded (logs and breaks into the debugger in debug builds).
// Unlike DXCall, it's an expression in all configurations: it evaluates to true on success, so
// it can be used as `if (!VkCall(vkCreateFoo(...))) return false;`.
#ifndef VkCall
#define VkCall(x) ::primal::graphics::vulkan::detail::check_result((x), #x, __FILE__, __LINE__)
#endif

#ifdef _DEBUG
// Names a Vulkan object for validation messages and RenderDoc (VK_EXT_debug_utils).
#define NAME_VK_OBJECT(handle, type, name)                                                                             \
    ::primal::graphics::vulkan::core::set_object_name(::primal::graphics::vulkan::handle_to_u64(handle), type, name)
#define NAME_VK_OBJECT_INDEXED(handle, type, n, name)                                                                  \
    {                                                                                                                  \
        char full_name[128];                                                                                           \
        if (std::snprintf(full_name, sizeof(full_name), "%s[%u]", name, static_cast<u32>(n)) > 0)                      \
            NAME_VK_OBJECT(handle, type, full_name);                                                                   \
    }
#else
#define NAME_VK_OBJECT(handle, type, name)
#define NAME_VK_OBJECT_INDEXED(handle, type, n, name)
#endif
