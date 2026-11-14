// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "VulkanLoader.h"

#include "Common/PlatformDetection.h"
#include "Core/Log.h"

#if PRIMAL_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

namespace primal::graphics::vulkan::loader
{
namespace
{

#if PRIMAL_PLATFORM_WINDOWS
constexpr const char *library_name{"vulkan-1.dll"};
#else
constexpr const char *library_name{"libvulkan.so.1"};
#endif

void *library{nullptr};

auto open_library() -> void *
{
#if PRIMAL_PLATFORM_WINDOWS
    return static_cast<void *>(::LoadLibraryW(L"vulkan-1.dll"));
#else
    // Some distributions only ship the unversioned name with the development package, and the other way around.
    void *handle{::dlopen(library_name, RTLD_NOW | RTLD_LOCAL)};
    if (!handle)
    {
        handle = ::dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    }
    return handle;
#endif
}

auto get_proc_addr(void *handle) -> PFN_vkGetInstanceProcAddr
{
#if PRIMAL_PLATFORM_WINDOWS
    // FARPROC → void(*)() → the real type: a cast through void(*)() is the one -Wcast-function-type allows.
    const FARPROC proc{::GetProcAddress(static_cast<HMODULE>(handle), "vkGetInstanceProcAddr")};
    return reinterpret_cast<PFN_vkGetInstanceProcAddr>(reinterpret_cast<void (*)()>(proc));
#else
    return reinterpret_cast<PFN_vkGetInstanceProcAddr>(::dlsym(handle, "vkGetInstanceProcAddr"));
#endif
}

auto report_missing(const char *name) -> void
{
    log::error(log::Category::vulkan, "{} isn't available (Vulkan library or driver too old?).", name);
}

}

auto load_library() -> bool
{
    if (!library)
    {
        library = open_library();
        if (!library)
        {
            log::error(log::Category::vulkan, "{} (the Vulkan loader) wasn't found.", library_name);
            return false;
        }
    }

    ::vkGetInstanceProcAddr = get_proc_addr(library);
    if (!::vkGetInstanceProcAddr)
    {
        report_missing("vkGetInstanceProcAddr");
        return false;
    }

    bool ok{true};
#define PRIMAL_VK_LOAD_FUNCTION(type, name)                                                                            \
    ::name = reinterpret_cast<type>(::vkGetInstanceProcAddr(VK_NULL_HANDLE, #name));                                   \
    if (!::name)                                                                                                       \
    {                                                                                                                  \
        report_missing(#name);                                                                                         \
        ok = false;                                                                                                    \
    }
    FOR_VULKAN_GLOBAL_FUNCTIONS(PRIMAL_VK_LOAD_FUNCTION)
#undef PRIMAL_VK_LOAD_FUNCTION
    return ok;
}

auto load_instance_functions(VkInstance instance) -> bool
{
    bool ok{true};
#define PRIMAL_VK_LOAD_FUNCTION(type, name)                                                                            \
    ::name = reinterpret_cast<type>(::vkGetInstanceProcAddr(instance, #name));                                         \
    if (!::name)                                                                                                       \
    {                                                                                                                  \
        report_missing(#name);                                                                                         \
        ok = false;                                                                                                    \
    }
    FOR_VULKAN_INSTANCE_FUNCTIONS(PRIMAL_VK_LOAD_FUNCTION)
#undef PRIMAL_VK_LOAD_FUNCTION
    return ok;
}

auto load_debug_utils_functions(VkInstance instance) -> void
{
#define PRIMAL_VK_LOAD_FUNCTION(type, name) ::name = reinterpret_cast<type>(::vkGetInstanceProcAddr(instance, #name));
    FOR_VULKAN_DEBUG_UTILS_FUNCTIONS(PRIMAL_VK_LOAD_FUNCTION)
#undef PRIMAL_VK_LOAD_FUNCTION
}

auto load_device_functions(VkDevice device) -> bool
{
    bool ok{true};
#define PRIMAL_VK_LOAD_FUNCTION(type, name)                                                                            \
    ::name = reinterpret_cast<type>(::vkGetDeviceProcAddr(device, #name));                                             \
    if (!::name)                                                                                                       \
    {                                                                                                                  \
        report_missing(#name);                                                                                         \
        ok = false;                                                                                                    \
    }
    FOR_VULKAN_DEVICE_FUNCTIONS(PRIMAL_VK_LOAD_FUNCTION)
#undef PRIMAL_VK_LOAD_FUNCTION
    return ok;
}

auto reset_functions() -> void
{
#define PRIMAL_VK_RESET_FUNCTION(type, name) ::name = nullptr;
    FOR_VULKAN_INSTANCE_FUNCTIONS(PRIMAL_VK_RESET_FUNCTION)
    FOR_VULKAN_DEBUG_UTILS_FUNCTIONS(PRIMAL_VK_RESET_FUNCTION)
    FOR_VULKAN_DEVICE_FUNCTIONS(PRIMAL_VK_RESET_FUNCTION)
#undef PRIMAL_VK_RESET_FUNCTION
}

}
