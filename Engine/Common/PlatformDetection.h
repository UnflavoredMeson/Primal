// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once

// Platform detection. Use these instead of checking compiler-specific macros (_WIN64, __linux__, ...)
// directly. Each macro is always defined, as 0 or 1, so use #if (not #ifdef).
#if defined(_WIN64)
#define PRIMAL_PLATFORM_WINDOWS 1
#define PRIMAL_PLATFORM_LINUX 0
#elif defined(__linux__) && defined(__x86_64__)
#define PRIMAL_PLATFORM_WINDOWS 0
#define PRIMAL_PLATFORM_LINUX 1
#else
#error Unsupported platform. Primal supports 64-bit Windows and x86-64 Linux.
#endif

// Graphics backends compiled into the engine. Can be overridden by the build system.
#ifndef PRIMAL_GRAPHICS_D3D12
#define PRIMAL_GRAPHICS_D3D12 PRIMAL_PLATFORM_WINDOWS
#endif

// Vulkan defaults to Linux only, until the backend is added to the Windows projects.
#ifndef PRIMAL_GRAPHICS_VULKAN
#define PRIMAL_GRAPHICS_VULKAN PRIMAL_PLATFORM_LINUX
#endif
