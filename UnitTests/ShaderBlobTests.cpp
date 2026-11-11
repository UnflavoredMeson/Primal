// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "Content/ContentToEngine.h"

#include <doctest.h>

#include <array>
#include <cstring>
#include <initializer_list>
#include <vector>

using namespace primal;

// The engine shader file (shaders.bin, "PSHB"): content::get_engine_shaders() must accept a complete file of the
// current version and reject everything else, because both graphics backends use the records in place.

namespace
{

constexpr u64 header_size{sizeof(content::EngineShadersHeader)};
constexpr u64 record_header_size{content::CompiledShader::buffer_size(0)};

// Where get_engine_shaders() puts the records it finds. A fixed size (and not a std::vector the size of the shader
// count): GCC 16 -O3 reports a false -Wstringop-overflow inside vector::assign().
using shader_pointers = std::array<content::compiled_shader_ptr, 8>;

// A shaders.bin image in 8-byte aligned memory (get_engine_shaders() requires the alignment).
class ShaderBlob
{
  public:
    // One shader per size. Shader i has hash bytes i + 1 and byte code bytes 0x10 + i.
    explicit ShaderBlob(std::initializer_list<u64> byte_code_sizes)
    {
        u64 size{header_size};
        for (const u64 byte_code_size : byte_code_sizes)
        {
            size = aligned(size + content::CompiledShader::buffer_size(byte_code_size));
        }
        _storage.resize(size / sizeof(u64));
        _size = size;

        set_header({
            .magic = content::EngineShadersHeader::magic_value,
            .version = content::EngineShadersHeader::current_version,
            .shader_count = static_cast<u32>(byte_code_sizes.size()),
            .reserved = 0,
        });

        u64 offset{header_size};
        u8 index{0};
        for (const u64 byte_code_size : byte_code_sizes)
        {
            _record_offsets.push_back(offset);
            std::memcpy(data() + offset, &byte_code_size, sizeof(u64));
            std::memset(data() + offset + sizeof(u64), index + 1, content::CompiledShader::hash_length);
            std::memset(data() + offset + record_header_size, 0x10 + index, byte_code_size);
            offset = aligned(offset + content::CompiledShader::buffer_size(byte_code_size));
            ++index;
        }
    }

    [[nodiscard]] auto data() -> u8 *
    {
        return reinterpret_cast<u8 *>(_storage.data());
    }
    [[nodiscard]] auto size() const -> u64
    {
        return _size;
    }
    [[nodiscard]] auto record_offset(u32 index) const -> u64
    {
        return _record_offsets[index];
    }
    [[nodiscard]] auto header() -> content::EngineShadersHeader
    {
        content::EngineShadersHeader header{};
        std::memcpy(&header, data(), header_size);
        return header;
    }
    auto set_header(const content::EngineShadersHeader &header) -> void
    {
        std::memcpy(data(), &header, header_size);
    }
    auto set_byte_code_size(u32 index, u64 byte_code_size) -> void
    {
        std::memcpy(data() + _record_offsets[index], &byte_code_size, sizeof(u64));
    }

    // Parses the first 'size' bytes as a file with 'shader_count' shaders.
    [[nodiscard]] auto parse(u32 shader_count, u64 size, shader_pointers &shaders) -> bool
    {
        REQUIRE(shader_count <= shaders.size());
        shaders.fill(nullptr);
        return content::get_engine_shaders(data(), size, shaders.data(), shader_count);
    }
    [[nodiscard]] auto parse(u32 shader_count) -> bool
    {
        shader_pointers shaders{};
        return parse(shader_count, _size, shaders);
    }

  private:
    [[nodiscard]] static auto aligned(u64 value) -> u64
    {
        constexpr u64 alignment{content::EngineShadersHeader::record_alignment};
        return (value + alignment - 1) / alignment * alignment;
    }

    std::vector<u64> _storage;
    std::vector<u64> _record_offsets;
    u64 _size{0};
};

}

TEST_SUITE_BEGIN("content::get_engine_shaders");

TEST_CASE("the header is 16 bytes and starts with PSHB")
{
    CHECK(header_size == 16U);
    CHECK(record_header_size == sizeof(u64) + content::CompiledShader::hash_length);

    ShaderBlob blob{1};
    CHECK(std::memcmp(blob.data(), "PSHB", 4) == 0);
}

TEST_CASE("a complete file gives one record per shader, in place")
{
    // Sizes that end on and off the 8-byte record alignment.
    ShaderBlob blob{1, 8, 13, 24};
    shader_pointers shaders{};

    REQUIRE(blob.parse(4, blob.size(), shaders));

    constexpr u64 sizes[]{1, 8, 13, 24};
    for (u32 i{0}; i < 4; ++i)
    {
        // NOLINTNEXTLINE(misc-const-correctness): the variable is doctest's (reported with the MSVC headers only)
        REQUIRE_MESSAGE(shaders[i] != nullptr, "shader " << i);
        // In place: the record is a pointer into the blob, at an aligned offset.
        CHECK(reinterpret_cast<const u8 *>(shaders[i]) == blob.data() + blob.record_offset(i));
        CHECK(blob.record_offset(i) % content::EngineShadersHeader::record_alignment == 0);
        CHECK(shaders[i]->byte_code_size() == sizes[i]);
        CHECK(shaders[i]->buffer_size() == record_header_size + sizes[i]);
        CHECK(shaders[i]->hash()[0] == i + 1);
        CHECK(shaders[i]->hash()[content::CompiledShader::hash_length - 1] == i + 1);
        CHECK(shaders[i]->byte_code()[0] == 0x10 + i);
        CHECK(shaders[i]->byte_code()[sizes[i] - 1] == 0x10 + i);
    }
}

TEST_CASE("a file with another shader count is rejected")
{
    ShaderBlob blob{8, 8, 8};

    CHECK(blob.parse(3));
    CHECK_FALSE(blob.parse(2));
    CHECK_FALSE(blob.parse(4));
}

TEST_CASE("a wrong magic value or version is rejected")
{
    ShaderBlob blob{8, 5};
    const content::EngineShadersHeader good{blob.header()};
    REQUIRE(blob.parse(2));

    SUBCASE("magic")
    {
        content::EngineShadersHeader header{good};
        header.magic = 0x42485351;
        blob.set_header(header);
        CHECK_FALSE(blob.parse(2));
    }

    SUBCASE("version 1, the old layout without a header")
    {
        content::EngineShadersHeader header{good};
        header.version = 1;
        blob.set_header(header);
        CHECK_FALSE(blob.parse(2));
    }

    SUBCASE("a newer version")
    {
        content::EngineShadersHeader header{good};
        header.version = content::EngineShadersHeader::current_version + 1;
        blob.set_header(header);
        CHECK_FALSE(blob.parse(2));
    }

    SUBCASE("the reserved field is ignored")
    {
        content::EngineShadersHeader header{good};
        header.reserved = 0xffff'ffff;
        blob.set_header(header);
        CHECK(blob.parse(2));
    }
}

TEST_CASE("every truncated file is rejected")
{
    ShaderBlob blob{1, 8, 13};
    shader_pointers shaders{};
    REQUIRE(blob.parse(3, blob.size(), shaders));

    // Every shorter length: inside the header, inside a record header, inside byte code, and without the padding
    // after the last record.
    for (u64 size{0}; size < blob.size(); ++size)
    {
        // NOLINTNEXTLINE(misc-const-correctness): the variable is doctest's (reported with the MSVC headers only)
        CHECK_FALSE_MESSAGE(blob.parse(3, size, shaders), "size " << size);
    }
}

TEST_CASE("bytes after the last record are rejected")
{
    // The fourth record is only there to have memory after the first three.
    ShaderBlob blob{8, 8, 8, 64};
    content::EngineShadersHeader header{blob.header()};
    header.shader_count = 3;
    blob.set_header(header);
    shader_pointers shaders{};

    REQUIRE(blob.parse(3, blob.record_offset(3), shaders));
    CHECK_FALSE(blob.parse(3, blob.record_offset(3) + 8, shaders));
    CHECK_FALSE(blob.parse(3, blob.size(), shaders));
}

TEST_CASE("a record with a wrong byte code size is rejected")
{
    ShaderBlob blob{8, 16, 8};
    REQUIRE(blob.parse(3));

    SUBCASE("zero")
    {
        blob.set_byte_code_size(1, 0);
        CHECK_FALSE(blob.parse(3));
    }

    SUBCASE("larger than the rest of the file")
    {
        blob.set_byte_code_size(2, 9);
        CHECK_FALSE(blob.parse(3));
    }

    SUBCASE("so large that offset + size wraps around")
    {
        blob.set_byte_code_size(0, 0xffff'ffff'ffff'fff8ULL);
        CHECK_FALSE(blob.parse(3));
        blob.set_byte_code_size(0, 0xffff'ffff'ffff'ffffULL);
        CHECK_FALSE(blob.parse(3));
    }

    SUBCASE("smaller: the next record is no longer where the file says")
    {
        // Record 1 shrinks by 8 bytes, so record 2 would start inside record 1's old byte code. The bytes there
        // (0x11...) are a huge size.
        blob.set_byte_code_size(1, 8);
        CHECK_FALSE(blob.parse(3));
    }
}

TEST_SUITE_END();
