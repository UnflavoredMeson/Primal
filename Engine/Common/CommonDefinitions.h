// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
// The macros and the Version struct were moved here from Arash Khatami's CommonHeaders.h.
#pragma once

// What every engine header may rely on: the engine-wide macros and the engine version. The low-level headers
// (Id.h, Vector.h, FreeList.h, Math.h) include this file instead of CommonHeaders.h, which includes them.

#ifdef _MSC_VER
#pragma warning(disable : 4530) // disable exception warning
#endif

#ifndef DISABLE_COPY
#define DISABLE_COPY(T)                                                                                                \
    explicit T(const T &) = delete;                                                                                    \
    auto operator=(const T &)->T & = delete;
#endif

#ifndef DISABLE_MOVE
#define DISABLE_MOVE(T)                                                                                                \
    explicit T(T &&) = delete;                                                                                         \
    auto operator=(T &&)->T & = delete;
#endif

#ifndef DISABLE_COPY_AND_MOVE
#define DISABLE_COPY_AND_MOVE(T) DISABLE_COPY(T) DISABLE_MOVE(T)
#endif

// A C function the editor calls across a DLL boundary: EngineDLL's and ContentTools' exports, and the exports of a
// game DLL (built with USE_WITH_EDITOR) that EngineDLL calls.
#ifndef EDITOR_INTERFACE
#if defined(_MSC_VER)
#define EDITOR_INTERFACE extern "C" __declspec(dllexport)
#else
#define EDITOR_INTERFACE extern "C" __attribute__((visibility("default")))
#endif
#endif

// Keeps a function out of line (hot loops calling a rarely-run function). Goes first: PRIMAL_NOINLINE auto f() -> T
#if defined(_MSC_VER)
#define PRIMAL_NOINLINE __declspec(noinline)
#else
#define PRIMAL_NOINLINE __attribute__((noinline))
#endif

#ifdef _DEBUG
#define DEBUG_OP(x) x
#else
#define DEBUG_OP(x)
#endif

namespace primal
{
constexpr const struct Version
{
    const int major{0};
    const int minor{0};
    const int revision{1};
} version;
}
