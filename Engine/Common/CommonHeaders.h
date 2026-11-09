// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
// Include guard as well: headers included below include this file back, and when an IDE (clangd) opens this file on
// its own, #pragma once doesn't protect the main file ("redefinition of 'Version'").
#ifndef PRIMAL_COMMON_HEADERS_H
#define PRIMAL_COMMON_HEADERS_H

#include "PlatformDetection.h"

#ifdef _MSC_VER
#pragma warning(disable : 4530) // disable exception warning
#endif

// C/C++ headers
// NOTE: don't put here any headers that include std::vector or std::deque
#include <cstdint>
#include <cstring>
#include <assert.h>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <cstring>

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

// common headers
#include "PrimitiveTypes.h"
#include "../Utilities/Utilities.h"
#include "../Common/Id.h"
#include "../Utilities/Math.h"

#endif
