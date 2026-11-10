// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once

#include "CommonHeaders.h"

#include <bit>
#include <immintrin.h> // _mm_crc32_u64 (SSE4.2)
#include <DirectXMath.h>

#include "../Utilities/MathTypes.h"

namespace primal::math
{

[[nodiscard]] constexpr auto is_equal(f32 a, f32 b, f32 eps = epsilon) -> bool
{
    f32 diff{a - b};
    if (diff < 0.f)
    {
        diff = -diff;
    }
    return diff < eps;
}

template <typename T>
[[nodiscard]] constexpr auto clamp(T value, T min, T max) -> T
{
    assert(min <= max);
    return (value < min) ? min : (value > max) ? max : value;
}

template <u32 bits>
[[nodiscard]] constexpr auto pack_unit_float(f32 f) -> u32
{
    static_assert(bits && bits <= sizeof(u32) * 8);
    assert(f >= 0.f && f <= 1.f);
    constexpr f32 intervals{static_cast<f32>((static_cast<u32>(1) << bits) - 1)};
    return static_cast<u32>(intervals * f + 0.5f); // NOLINT(bugprone-incorrect-roundings): f is in [0, 1] (asserted)
}

template <u32 bits>
[[nodiscard]] constexpr auto unpack_to_unit_float(u32 i) -> f32
{
    static_assert(bits && bits <= sizeof(u32) * 8);
    assert(i < (static_cast<u32>(1) << bits));
    constexpr f32 intervals{static_cast<f32>((static_cast<u32>(1) << bits) - 1)};
    return static_cast<f32>(i) / intervals;
}

template <u32 bits>
[[nodiscard]] constexpr auto pack_float(f32 f, f32 min, f32 max) -> u32
{
    assert(min < max);
    assert(f <= max && f >= min);
    const f32 distance{(f - min) / (max - min)};
    return pack_unit_float<bits>(distance);
}

template <u32 bits>
[[nodiscard]] constexpr auto unpack_to_float(u32 i, f32 min, f32 max) -> f32
{
    assert(min < max);
    return unpack_to_unit_float<bits>(i) * (max - min) + min;
}

// Align by rounding up. Will result in a multiple of 'alignment' that is greater than or equal to 'size'.
template <u64 alignment>
[[nodiscard]] constexpr auto align_size_up(u64 size) -> u64
{
    static_assert(alignment, "Alignment must be non-zero.");
    constexpr u64 mask{alignment - 1};
    static_assert(!(alignment & mask), "Alignment should be a power of 2.");
    return ((size + mask) & ~mask);
}

// Align by rounding down. Will result in a multiple of 'alignment' that is less than or equal to 'size'.
template <u64 alignment>
[[nodiscard]] constexpr auto align_size_down(u64 size) -> u64
{
    static_assert(alignment, "Alignment must be non-zero.");
    constexpr u64 mask{alignment - 1};
    static_assert(!(alignment & mask), "Alignment should be a power of 2.");
    return (size & ~mask);
}

// Align by rounding up. Will result in a multiple of 'alignment' that is greater than or equal to 'size'.
[[nodiscard]] constexpr auto align_size_up(u64 size, u64 alignment) -> u64
{
    assert(alignment && "Alignment must be non-zero.");
    const u64 mask{alignment - 1};
    assert(!(alignment & mask) && "Alignment should be a power of 2.");
    return ((size + mask) & ~mask);
}

// Align by rounding down. Will result in a multiple of 'alignment' that is less than or equal to 'size'.
[[nodiscard]] constexpr auto align_size_down(u64 size, u64 alignment) -> u64
{
    assert(alignment && "Alignment must be non-zero.");
    const u64 mask{alignment - 1};
    assert(!(alignment & mask) && "Alignment should be a power of 2.");
    return (size & ~mask);
}

[[nodiscard]] constexpr auto calc_crc32_u64(const u8 *const data, u64 size) -> u64
{
    assert(size >= sizeof(u64));
    u64 crc{0};
    const u8 *at{data};
    const u8 *const end{data + align_size_down<sizeof(u64)>(size)};
    while (at < end)
    {
        crc = _mm_crc32_u64(crc, *(reinterpret_cast<const u64 *>(at)));
        at += sizeof(u64);
    }

    return crc;
}

// Returns ceil(log2(value)): the exact log for powers of two, otherwise rounded up.
// Returns 0 for value 0 and 1.
[[nodiscard]] constexpr auto log2(u64 value) -> u8
{
    return value ? static_cast<u8>(std::bit_width(value - 1)) : static_cast<u8>(0);
}

constexpr auto to_radians(math::v3 degrees) -> math::v3
{
    return {degrees.x * to_rad, degrees.y * to_rad, degrees.z * to_rad};
}

constexpr auto to_degrees(math::v3 radians) -> math::v3
{
    return {radians.x * to_deg, radians.y * to_deg, radians.z * to_deg};
}

}
