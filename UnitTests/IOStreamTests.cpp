// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "Utilities/IOStream.h"

#include <doctest.h>

#include <array>
#include <cstring>

using namespace primal;

TEST_SUITE_BEGIN("utl::BlobStream");

TEST_CASE("values come back in the order and with the types they were written")
{
    std::array<u8, 64> buffer{};
    utl::BlobStreamWriter writer{buffer.data(), buffer.size()};

    writer.write<u8>(0xab);
    writer.write<u32>(0xdead'beef);
    writer.write<f32>(-1.5f);
    writer.write<u64>(0x0123'4567'89ab'cdefULL);
    writer.write<s16>(-2);
    writer.write<bool>(true);

    // Packed: no padding between the values.
    CHECK(writer.offset() == 1U + 4U + 4U + 8U + 2U + 1U);
    CHECK(writer.buffer_start() == buffer.data());
    CHECK(writer.buffer_end() == buffer.data() + buffer.size());
    CHECK(writer.position() == buffer.data() + writer.offset());

    utl::BlobStreamReader reader{buffer.data()};
    CHECK(reader.read<u8>() == 0xab);
    CHECK(reader.read<u32>() == 0xdead'beefU);
    CHECK(reader.read<f32>() == -1.5f);
    CHECK(reader.read<u64>() == 0x0123'4567'89ab'cdefULL);
    CHECK(reader.read<s16>() == -2);
    CHECK(reader.read<bool>());

    CHECK(reader.offset() == writer.offset());
    CHECK(reader.buffer_start() == buffer.data());
    CHECK(reader.position() == buffer.data() + reader.offset());
}

TEST_CASE("values are stored in the machine's byte order")
{
    std::array<u8, 4> buffer{};
    utl::BlobStreamWriter writer{buffer.data(), buffer.size()};
    writer.write<u32>(0x0403'0201);

    // x64 is little-endian. The content files rely on it.
    CHECK(buffer[0] == 1);
    CHECK(buffer[1] == 2);
    CHECK(buffer[2] == 3);
    CHECK(buffer[3] == 4);
}

TEST_CASE("byte and char buffers")
{
    constexpr std::array<u8, 5> bytes{1, 2, 3, 4, 5};
    constexpr char text[]{"primal"};

    std::array<u8, 32> buffer{};
    utl::BlobStreamWriter writer{buffer.data(), buffer.size()};
    writer.write(bytes.data(), bytes.size());
    writer.write(text, sizeof(text));
    CHECK(writer.offset() == bytes.size() + sizeof(text));

    utl::BlobStreamReader reader{buffer.data()};
    std::array<u8, 5> bytes_read{};
    std::array<u8, sizeof(text)> text_read{};
    reader.read(bytes_read.data(), bytes_read.size());
    reader.read(text_read.data(), text_read.size());

    CHECK(bytes_read == bytes);
    CHECK(std::memcmp(text_read.data(), text, sizeof(text)) == 0);
    CHECK(reader.offset() == writer.offset());
}

TEST_CASE("skip moves the position without touching the bytes")
{
    std::array<u8, 16> buffer{};
    buffer.fill(0xee);

    utl::BlobStreamWriter writer{buffer.data(), buffer.size()};
    writer.write<u16>(0x1111);
    writer.skip(6);
    writer.write<u16>(0x2222);
    CHECK(writer.offset() == 10U);

    for (u32 i{2}; i < 8; ++i)
    {
        CHECK(buffer[i] == 0xee);
    }

    utl::BlobStreamReader reader{buffer.data()};
    CHECK(reader.read<u16>() == 0x1111);
    reader.skip(6);
    CHECK(reader.offset() == 8U);
    CHECK(reader.read<u16>() == 0x2222);
}

TEST_CASE("a buffer can be filled to its last byte")
{
    std::array<u8, 8> buffer{};
    utl::BlobStreamWriter writer{buffer.data(), buffer.size()};
    writer.write<u32>(1);
    writer.write<u32>(2);

    CHECK(writer.position() == writer.buffer_end());
}

TEST_SUITE_END();
