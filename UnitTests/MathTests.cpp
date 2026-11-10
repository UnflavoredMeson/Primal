// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "Utilities/Math.h"

#include <doctest.h>

#include <array>
#include <cstring>

using namespace primal;

TEST_SUITE_BEGIN("math");

namespace
{

// CRC-32C (Castagnoli), bit by bit, without the usual inversions: what the SSE4.2 instruction computes.
auto crc32c_reference(const u8 *data, u64 size) -> u64
{
    u32 crc{0};
    for (u64 i{0}; i < size; ++i)
    {
        crc ^= data[i];
        for (u32 bit{0}; bit < 8; ++bit)
        {
            crc = (crc >> 1) ^ (0x82f6'3b78U & (0U - (crc & 1U)));
        }
    }
    return crc;
}

}

TEST_CASE("is_equal compares within epsilon")
{
    CHECK(math::is_equal(1.f, 1.f));
    CHECK(math::is_equal(1.f, 1.f + math::epsilon * 0.5f));
    CHECK(math::is_equal(1.f + math::epsilon * 0.5f, 1.f));
    CHECK_FALSE(math::is_equal(1.f, 1.001f));
    CHECK(math::is_equal(1.f, 1.5f, 1.f));
    CHECK_FALSE(math::is_equal(-1.f, 1.f));
}

TEST_CASE("clamp")
{
    CHECK(math::clamp(5, 0, 10) == 5);
    CHECK(math::clamp(-5, 0, 10) == 0);
    CHECK(math::clamp(50, 0, 10) == 10);
    CHECK(math::clamp(3, 3, 3) == 3);
    CHECK(math::clamp(0.25f, 0.f, 1.f) == 0.25f);
    CHECK(math::clamp(1.25f, 0.f, 1.f) == 1.f);
}

TEST_CASE("pack_unit_float maps [0, 1] to all values of the given bits")
{
    CHECK(math::pack_unit_float<8>(0.f) == 0U);
    CHECK(math::pack_unit_float<8>(1.f) == 255U);
    CHECK(math::pack_unit_float<8>(0.5f) == 128U);
    CHECK(math::pack_unit_float<16>(1.f) == 65535U);
    CHECK(math::pack_unit_float<1>(0.49f) == 0U);
    CHECK(math::pack_unit_float<1>(0.51f) == 1U);
}

TEST_CASE("unpack_to_unit_float reverses pack_unit_float")
{
    CHECK(math::unpack_to_unit_float<8>(0) == 0.f);
    CHECK(math::unpack_to_unit_float<8>(255) == 1.f);

    for (u32 i{0}; i < 256; ++i)
    {
        CHECK(math::pack_unit_float<8>(math::unpack_to_unit_float<8>(i)) == i);
    }
    for (u32 i{0}; i < 65536; i += 97)
    {
        CHECK(math::pack_unit_float<16>(math::unpack_to_unit_float<16>(i)) == i);
    }
}

TEST_CASE("pack_float and unpack_to_float work on a range")
{
    CHECK(math::pack_float<8>(-1.f, -1.f, 1.f) == 0U);
    CHECK(math::pack_float<8>(1.f, -1.f, 1.f) == 255U);
    CHECK(math::unpack_to_float<8>(0, -1.f, 1.f) == -1.f);
    CHECK(math::unpack_to_float<8>(255, -1.f, 1.f) == 1.f);

    // 16 bits over a range of 200: a round trip is closer than one step.
    constexpr f32 step{200.f / 65535.f};
    for (s32 i{-8}; i <= 8; ++i)
    {
        const f32 f{static_cast<f32>(i) * 12.5f};
        const f32 unpacked{math::unpack_to_float<16>(math::pack_float<16>(f, -100.f, 100.f), -100.f, 100.f)};
        CHECK(math::is_equal(f, unpacked, step));
    }
}

TEST_CASE("align_size_up and align_size_down, alignment known at compile time")
{
    CHECK(math::align_size_up<16>(0) == 0U);
    CHECK(math::align_size_up<16>(1) == 16U);
    CHECK(math::align_size_up<16>(16) == 16U);
    CHECK(math::align_size_up<16>(17) == 32U);
    CHECK(math::align_size_up<1>(17) == 17U);

    CHECK(math::align_size_down<16>(0) == 0U);
    CHECK(math::align_size_down<16>(15) == 0U);
    CHECK(math::align_size_down<16>(16) == 16U);
    CHECK(math::align_size_down<16>(31) == 16U);
    CHECK(math::align_size_down<1>(31) == 31U);
}

TEST_CASE("align_size_up and align_size_down, alignment known at run time")
{
    for (u64 alignment{1}; alignment <= 4096; alignment <<= 1)
    {
        for (u64 size{0}; size < 3 * alignment; size += (alignment / 3) + 1)
        {
            const u64 up{math::align_size_up(size, alignment)};
            const u64 down{math::align_size_down(size, alignment)};

            CHECK(up % alignment == 0U);
            CHECK(up >= size);
            CHECK(up - size < alignment);
            CHECK(down % alignment == 0U);
            CHECK(down <= size);
            CHECK(size - down < alignment);
        }
    }

    // Past 32 bits.
    CHECK(math::align_size_up(0x1'0000'0001ULL, 0x1'0000'0000ULL) == 0x2'0000'0000ULL);
}

TEST_CASE("log2 rounds up")
{
    CHECK(math::log2(0) == 0);
    CHECK(math::log2(1) == 0);
    CHECK(math::log2(2) == 1);
    CHECK(math::log2(3) == 2);
    CHECK(math::log2(4) == 2);
    CHECK(math::log2(5) == 3);
    CHECK(math::log2(1024) == 10);
    CHECK(math::log2(1025) == 11);
    CHECK(math::log2(u64{1} << 63) == 63);
    CHECK(math::log2(u64_invalid_id) == 64);
}

TEST_CASE("calc_crc32_u64 is CRC-32C over whole 8-byte blocks")
{
    alignas(u64) std::array<u8, 64> data{};
    for (u32 i{0}; i < data.size(); ++i)
    {
        data[i] = static_cast<u8>(i * 37 + 11);
    }

    CHECK(math::calc_crc32_u64(data.data(), 8) == crc32c_reference(data.data(), 8));
    CHECK(math::calc_crc32_u64(data.data(), 64) == crc32c_reference(data.data(), 64));

    SUBCASE("the bytes after the last whole block are ignored")
    {
        CHECK(math::calc_crc32_u64(data.data(), 15) == math::calc_crc32_u64(data.data(), 8));
        CHECK(math::calc_crc32_u64(data.data(), 63) == math::calc_crc32_u64(data.data(), 56));
    }

    SUBCASE("one changed bit changes the result")
    {
        const u64 before{math::calc_crc32_u64(data.data(), 64)};
        data[40] ^= 0x10;
        CHECK(math::calc_crc32_u64(data.data(), 64) != before);
    }
}

TEST_CASE("to_radians and to_degrees")
{
    const math::v3 radians{math::to_radians({180.f, 90.f, -45.f})};
    CHECK(math::is_equal(radians.x, math::pi));
    CHECK(math::is_equal(radians.y, math::half_pi));
    CHECK(math::is_equal(radians.z, -math::pi * 0.25f));

    const math::v3 degrees{math::to_degrees(radians)};
    CHECK(math::is_equal(degrees.x, 180.f, 1e-3f));
    CHECK(math::is_equal(degrees.y, 90.f, 1e-3f));
    CHECK(math::is_equal(degrees.z, -45.f, 1e-3f));
}

TEST_SUITE_END();
