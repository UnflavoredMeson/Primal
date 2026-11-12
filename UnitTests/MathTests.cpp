// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "Utilities/Math.h"

#include <doctest.h>

#include <array>
#include <cmath>
#include <cstring>
#include <numbers>

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

// The SIMD functions (MathSimd.h) forward to the math library behind primal::math. These cases pin down the
// conventions the engine relies on, so that a change of library shows up here.

TEST_CASE("load and store round-trip the storage types")
{
    const math::v3 a{1.f, 2.f, 3.f};
    const math::v3 b{math::to_v3(math::load(a))};
    CHECK(b.x == 1.f);
    CHECK(b.y == 2.f);
    CHECK(b.z == 3.f);

    const math::v4 c{1.f, 2.f, 3.f, 4.f};
    math::v4a d{};
    math::store(d, math::load(c));
    CHECK(d.x == 1.f);
    CHECK(d.y == 2.f);
    CHECK(d.z == 3.f);
    CHECK(d.w == 4.f);

    const math::m4x4 m{1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f, 9.f, 10.f, 11.f, 12.f, 13.f, 14.f, 15.f, 16.f};
    math::m4x4 n{};
    math::store(n, math::load(m));
    for (u32 row{0}; row < 4; ++row)
    {
        for (u32 column{0}; column < 4; ++column)
        {
            CHECK(n.m[row][column] == m.m[row][column]);
        }
    }

    f32 x{0.f};
    math::store(x, math::vec_set(5.f, 6.f, 7.f, 8.f));
    CHECK(x == 5.f);
    CHECK(math::get_x(math::vec_replicate(9.f)) == 9.f);
    CHECK(math::get_x(math::vec_zero()) == 0.f);
}

TEST_CASE("vec operators work on components")
{
    const math::vec a{math::vec_set(1.f, 2.f, 3.f, 4.f)};
    const math::vec b{math::vec_set(4.f, 3.f, 2.f, 1.f)};

    const math::v4 sum{math::to_v4(a + b)};
    CHECK(sum.x == 5.f);
    CHECK(sum.w == 5.f);

    const math::v4 difference{math::to_v4(a - b)};
    CHECK(difference.x == -3.f);
    CHECK(difference.w == 3.f);

    const math::v4 scaled{math::to_v4(2.f * a * 0.5f + a * b)};
    CHECK(scaled.x == 5.f);
    CHECK(scaled.y == 8.f);
    CHECK(scaled.z == 9.f);
    CHECK(scaled.w == 8.f);
}

TEST_CASE("3-component vector functions")
{
    const math::vec x{math::vec_set(1.f, 0.f, 0.f, 0.f)};
    const math::vec y{math::vec_set(0.f, 1.f, 0.f, 0.f)};

    // Right-handed: x cross y is z.
    const math::v3 z{math::to_v3(math::cross3(x, y))};
    CHECK(z.x == 0.f);
    CHECK(z.y == 0.f);
    CHECK(z.z == 1.f);

    const math::vec v{math::vec_set(3.f, 4.f, 0.f, 100.f)};
    CHECK(math::get_x(math::dot3(v, v)) == 25.f);
    CHECK(math::get_x(math::length_sq3(v)) == 25.f);
    CHECK(math::is_equal(math::get_x(math::length_est3(v)), 5.f, 0.01f));
    CHECK(math::is_equal(math::get_x(math::reciprocal_length3(v)), 0.2f));

    const math::v3 n{math::to_v3(math::normalize3(v))};
    CHECK(math::is_equal(n.x, 0.6f));
    CHECK(math::is_equal(n.y, 0.8f));
    CHECK(n.z == 0.f);

    CHECK(math::near_equal4(v, v + math::vec_replicate(0.5f), math::vec_replicate(1.f)));
    CHECK_FALSE(math::near_equal4(v, v + math::vec_replicate(2.f), math::vec_replicate(1.f)));
}

TEST_CASE("quaternions: (x, y, z, w), Euler angles as pitch, yaw, roll")
{
    const math::v4 identity{math::to_v4(math::quat_identity())};
    CHECK(identity.x == 0.f);
    CHECK(identity.y == 0.f);
    CHECK(identity.z == 0.f);
    CHECK(identity.w == 1.f);

    // A yaw of 90 degrees: (0, sin 45, 0, cos 45).
    const f32 h{std::numbers::sqrt2_v<f32> * 0.5f};
    const math::v4 yaw{math::quat_from_euler(math::v3{0.f, math::half_pi, 0.f})};
    CHECK(math::is_equal(yaw.x, 0.f));
    CHECK(math::is_equal(yaw.y, h));
    CHECK(math::is_equal(yaw.z, 0.f));
    CHECK(math::is_equal(yaw.w, h));

    // The vec overload and an axis and angle give the same rotation.
    const math::vec yaw_vec{math::quat_from_euler(math::load(math::v3{0.f, math::half_pi, 0.f}))};
    const math::vec yaw_axis{math::quat_from_axis_angle(math::vec_set(0.f, 1.f, 0.f, 0.f), math::half_pi)};
    CHECK(math::near_equal4(yaw_vec, math::load(yaw), math::vec_replicate(math::epsilon)));
    CHECK(math::near_equal4(yaw_axis, math::load(yaw), math::vec_replicate(math::epsilon)));

    // It turns the x axis to -z (right-handed, counter-clockwise seen from +y).
    const math::v3 rotated{math::to_v3(math::rotate3(math::vec_set(1.f, 0.f, 0.f, 0.f), yaw_vec))};
    CHECK(math::is_equal(rotated.x, 0.f));
    CHECK(math::is_equal(rotated.y, 0.f));
    CHECK(math::is_equal(rotated.z, -1.f));

    // quat_mul(a, b) is a, then b: two quarter turns make a half turn.
    const math::v4 half_turn{math::to_v4(math::quat_mul(yaw_vec, yaw_vec))};
    CHECK(math::is_equal(half_turn.y, 1.f));
    CHECK(math::is_equal(half_turn.w, 0.f));
}

TEST_CASE("matrices: row vectors, translation in the last row")
{
    const math::vec scale{math::vec_set(2.f, 2.f, 2.f, 0.f)};
    const math::vec translation{math::vec_set(10.f, 20.f, 30.f, 0.f)};
    const math::mat world{math::affine_transform(scale, math::vec_zero(), math::quat_identity(), translation)};

    math::m4x4 stored{};
    math::store(stored, world);
    CHECK(stored._11 == 2.f);
    CHECK(stored._41 == 10.f);
    CHECK(stored._42 == 20.f);
    CHECK(stored._43 == 30.f);
    CHECK(stored._44 == 1.f);

    // Scale first, then translate.
    const math::vec point{math::vec_set(1.f, 1.f, 1.f, 0.f)};
    const math::v3 moved{math::to_v3(math::transform3(point, world))};
    CHECK(moved.x == 12.f);
    CHECK(moved.y == 22.f);
    CHECK(moved.z == 32.f);

    const math::v3 back{math::to_v3(math::transform3(math::load(moved), math::inverse(world)))};
    CHECK(math::is_equal(back.x, 1.f));
    CHECK(math::is_equal(back.y, 1.f));
    CHECK(math::is_equal(back.z, 1.f));

    math::m4x4 product{};
    math::store(product, math::mul(world, math::inverse(world)));
    math::m4x4 identity{};
    math::store(identity, math::mat_identity());
    for (u32 row{0}; row < 4; ++row)
    {
        for (u32 column{0}; column < 4; ++column)
        {
            CHECK(math::is_equal(product.m[row][column], identity.m[row][column]));
            CHECK(identity.m[row][column] == (row == column ? 1.f : 0.f));
        }
    }

    math::mat changed{math::mat_identity()};
    math::set_row(changed, 3, translation);
    math::store(stored, changed);
    CHECK(stored._41 == 10.f);
    CHECK(stored._44 == 0.f);

    const math::m3x3 rows{1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f, 9.f};
    math::store(stored, math::load(rows));
    CHECK(stored._12 == 2.f);
    CHECK(stored._33 == 9.f);
    CHECK(stored._44 == 1.f);
}

TEST_CASE("view and projection: right-handed, depth 0 to 1, reversed Z when near and far are swapped")
{
    constexpr f32 near_z{1.f};
    constexpr f32 far_z{100.f};

    // The camera looks down -z; view space keeps that direction.
    const math::mat view{math::look_to_rh(
        math::vec_set(0.f, 0.f, 5.f, 0.f), math::vec_set(0.f, 0.f, -1.f, 0.f), math::vec_set(0.f, 1.f, 0.f, 0.f))};
    const math::v3 in_view{math::to_v3(math::transform3(math::vec_zero(), view))};
    CHECK(math::is_equal(in_view.x, 0.f));
    CHECK(math::is_equal(in_view.y, 0.f));
    CHECK(math::is_equal(in_view.z, -5.f));

    const auto depth{
        [](const math::mat &projection, f32 distance) -> f32
        {
            math::v4 clip{};
            math::store(clip, math::transform3(math::vec_set(0.f, 0.f, -distance, 0.f), projection));
            return clip.z / clip.w;
        }};

    const math::mat perspective{math::perspective_fov_rh(math::half_pi, 16.f / 9.f, near_z, far_z)};
    CHECK(math::is_equal(depth(perspective, near_z), 0.f));
    CHECK(math::is_equal(depth(perspective, far_z), 1.f));

    const math::mat reversed{math::perspective_fov_rh(math::half_pi, 16.f / 9.f, far_z, near_z)};
    CHECK(math::is_equal(depth(reversed, near_z), 1.f));
    CHECK(math::is_equal(depth(reversed, far_z), 0.f));

    const math::mat orthographic{math::orthographic_rh(10.f, 10.f, far_z, near_z)};
    CHECK(math::is_equal(depth(orthographic, near_z), 1.f));
    CHECK(math::is_equal(depth(orthographic, far_z), 0.f));
}

TEST_CASE("sin, cos and acos are close to the <cmath> functions")
{
    for (s32 i{-31}; i <= 31; ++i)
    {
        const f32 angle{static_cast<f32>(i) * 0.1f};
        CHECK(math::is_equal(math::sin(angle), std::sin(angle), 1e-4f));
        CHECK(math::is_equal(math::cos(angle), std::cos(angle), 1e-4f));
    }

    for (s32 i{-20}; i <= 20; ++i)
    {
        const f32 value{static_cast<f32>(i) * 0.05f};
        CHECK(math::is_equal(math::acos(value), std::acos(value), 1e-3f));
    }
}

TEST_SUITE_END();
