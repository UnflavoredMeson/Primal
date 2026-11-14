// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.

// Compiles the Vulkan Memory Allocator implementation (single-header library, Engine/ThirdParty/vma)
// in its own translation unit, with its warnings silenced.
#include "VulkanCommonHeaders.h"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif

// No static Vulkan functions (VK_NO_PROTOTYPES): VMA loads its own through the vkGet*ProcAddr pointers that
// core::create_allocator() passes in (VulkanLoader.h).
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
