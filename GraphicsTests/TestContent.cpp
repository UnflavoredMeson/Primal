// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "TestContent.h"

#include "Content/ContentToEngine.h"
#include "Graphics/Renderer.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>

using namespace primal;

namespace test_content
{
namespace
{

// DXGI_FORMAT values (the texture files store them).
constexpr u32 format_r32g32b32a32_float{2};
constexpr u32 format_r32g32_float{16};
constexpr u32 format_r8g8b8a8_unorm{28};
constexpr u32 format_r8g8b8a8_unorm_srgb{29};

class ByteWriter
{
  public:
    template <typename T>
    auto write(const T &value) -> void
    {
        write(&value, sizeof(T));
    }

    auto write(const void *data, size_t size) -> void
    {
        // NOTE: not vector::insert(): GCC 16 -O2 reports a false -Wstringop-overflow inside it.
        const size_t offset{_bytes.size()};
        _bytes.resize(offset + size);
        std::memcpy(_bytes.data() + offset, data, size);
    }

    // Zero bytes up to a multiple of 4 (D3D12_STANDARD_MAXIMUM_ELEMENT_ALIGNMENT_BYTE_MULTIPLE).
    auto pad_to_4() -> void
    {
        _bytes.resize((_bytes.size() + 3) / 4 * 4);
    }

    [[nodiscard]] auto size() const -> size_t
    {
        return _bytes.size();
    }

    [[nodiscard]] auto take() -> std::vector<u8>
    {
        return std::move(_bytes);
    }

  private:
    std::vector<u8> _bytes;
};

// [-1, 1] -> 16 bits, as TestShader.hlsl unpacks it (value * 2 / 65535 - 1).
auto pack_unit_float(f32 value) -> u16
{
    return static_cast<u16>(std::lround((std::clamp(value, -1.f, 1.f) + 1.f) * 0.5f * 65535.f));
}

auto element_size(ElementsType::Type type) -> u32
{
    switch (type)
    {
        case ElementsType::static_normal: return 8;          // u32 color_t_sign, u16 normal[2]
        case ElementsType::static_normal_texture: return 20; // + u16 tangent[2], f32 uv[2]
        default: return 0;
    }
}

auto write_submesh_data(ByteWriter &writer, const Submesh &mesh) -> void
{
    const size_t vertex_count{mesh.positions.size()};
    assert(vertex_count && vertex_count < (1 << 16) && !mesh.indices.empty() && mesh.indices.size() % 3 == 0);

    writer.write(mesh.positions.data(), vertex_count * sizeof(math::v3));
    writer.pad_to_4();

    if (mesh.elements_type != ElementsType::position_only)
    {
        assert(mesh.normals.size() == vertex_count);
        for (size_t i{0}; i < vertex_count; ++i)
        {
            const math::v3 &normal{mesh.normals[i]};
            // The top byte holds the signs the 16-bit x and y can't: bit 2 normal z, bit 1 tangent z, bit 0
            // handedness. The low 24 bits are the vertex color.
            u32 signs{normal.z >= 0.f ? 0x04U : 0U};
            if (mesh.elements_type == ElementsType::static_normal_texture)
            {
                assert(mesh.tangents.size() == vertex_count && mesh.uvs.size() == vertex_count);
                signs |= (mesh.tangents[i].z >= 0.f ? 0x02U : 0U) | 0x01U;
            }
            writer.write<u32>((signs << 24) | 0x00ff'ffffU);
            writer.write<u16>(pack_unit_float(normal.x));
            writer.write<u16>(pack_unit_float(normal.y));
            if (mesh.elements_type == ElementsType::static_normal_texture)
            {
                writer.write<u16>(pack_unit_float(mesh.tangents[i].x));
                writer.write<u16>(pack_unit_float(mesh.tangents[i].y));
                writer.write<math::v2>(mesh.uvs[i]);
            }
        }
        writer.pad_to_4();
    }

    // 16-bit indices: the engine uses them for meshes with fewer than 65536 vertices.
    writer.write(mesh.indices.data(), mesh.indices.size() * sizeof(u16));
    writer.pad_to_4();
}

auto add_quad(Submesh &mesh, math::v3 center, math::v3 normal, math::v3 tangent, f32 half_u, f32 half_v) -> void
{
    const math::vec n{math::load(normal)};
    const math::vec t{math::load(tangent)};
    const math::vec b{math::cross3(n, t)};
    const math::vec c{math::load(center)};

    const u16 first{static_cast<u16>(mesh.positions.size())};
    constexpr f32 corners[4][2]{{-1.f, -1.f}, {-1.f, 1.f}, {1.f, 1.f}, {1.f, -1.f}};
    for (const auto &corner : corners)
    {
        math::v3 position{};
        math::store(position, c + t * (corner[0] * half_u) + b * (corner[1] * half_v));
        mesh.positions.push_back(position);
        mesh.normals.push_back(normal);
        mesh.tangents.push_back(tangent);
        mesh.uvs.emplace_back(corner[0] * 0.5f + 0.5f, corner[1] * 0.5f + 0.5f);
    }

    for (const u16 index : {0, 1, 2, 0, 2, 3})
    {
        mesh.indices.push_back(static_cast<u16>(first + index));
    }
}

auto mip_size(u32 size, u32 mip) -> u32
{
    return std::max(size >> mip, 1U);
}

auto write_texture_header(ByteWriter &writer, u32 size, u32 array_size, u32 flags, u32 mip_levels, u32 format) -> void
{
    writer.write<u32>(size); // width
    writer.write<u32>(size); // height
    writer.write<u32>(array_size);
    writer.write<u32>(flags);
    writer.write<u32>(mip_levels);
    writer.write<u32>(format);
}

}

auto make_box(ElementsType::Type elements_type, math::v3 half_extent) -> Submesh
{
    Submesh mesh{.elements_type = elements_type};
    const f32 x{half_extent.x};
    const f32 y{half_extent.y};
    const f32 z{half_extent.z};
    add_quad(mesh, {x, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, z, y);
    add_quad(mesh, {-x, 0.f, 0.f}, {-1.f, 0.f, 0.f}, {0.f, 0.f, -1.f}, z, y);
    add_quad(mesh, {0.f, y, 0.f}, {0.f, 1.f, 0.f}, {1.f, 0.f, 0.f}, x, z);
    add_quad(mesh, {0.f, -y, 0.f}, {0.f, -1.f, 0.f}, {-1.f, 0.f, 0.f}, x, z);
    add_quad(mesh, {0.f, 0.f, z}, {0.f, 0.f, 1.f}, {-1.f, 0.f, 0.f}, x, y);
    add_quad(mesh, {0.f, 0.f, -z}, {0.f, 0.f, -1.f}, {1.f, 0.f, 0.f}, x, y);
    return mesh;
}

auto make_plane(ElementsType::Type elements_type, f32 half_size, u32 quads_per_side) -> Submesh
{
    assert(quads_per_side);
    Submesh mesh{.elements_type = elements_type};
    const f32 quad_half_size{half_size / static_cast<f32>(quads_per_side)};
    for (u32 row{0}; row < quads_per_side; ++row)
    {
        for (u32 column{0}; column < quads_per_side; ++column)
        {
            const f32 center_x{-half_size + quad_half_size * static_cast<f32>(2 * column + 1)};
            const f32 center_z{-half_size + quad_half_size * static_cast<f32>(2 * row + 1)};
            add_quad(mesh, {center_x, 0.f, center_z}, {0.f, 1.f, 0.f}, {1.f, 0.f, 0.f}, quad_half_size, quad_half_size);
        }
    }
    return mesh;
}

auto make_geometry(std::span<const Lod> lods) -> std::vector<u8>
{
    assert(!lods.empty());
    ByteWriter writer{};
    writer.write<u32>(static_cast<u32>(lods.size()));
    for (const Lod &lod : lods)
    {
        writer.write<f32>(lod.threshold);
    }
    for (const Lod &lod : lods)
    {
        assert(!lod.submeshes.empty());
        writer.write<u32>(static_cast<u32>(lod.submeshes.size()));
    }

    ByteWriter mesh_data{};
    for (const Lod &lod : lods)
    {
        for (const Submesh &mesh : lod.submeshes)
        {
            write_submesh_data(mesh_data, mesh);
        }
    }
    writer.write<u32>(static_cast<u32>(mesh_data.size()));
    const std::vector<u8> mesh_bytes{mesh_data.take()};
    writer.write(mesh_bytes.data(), mesh_bytes.size());

    for (const Lod &lod : lods)
    {
        for (const Submesh &mesh : lod.submeshes)
        {
            writer.write<u32>(element_size(mesh.elements_type));
            writer.write<u32>(static_cast<u32>(mesh.positions.size()));
            writer.write<u32>(static_cast<u32>(mesh.indices.size()));
            writer.write<u32>(mesh.elements_type);
            writer.write<u32>(graphics::PrimitiveTopology::triangle_list);
        }
    }

    return writer.take();
}

auto make_checkerboard_texture(u32 size, bool is_srgb, u32 color_a, u32 color_b) -> std::vector<u8>
{
    assert(size && (size & (size - 1)) == 0);
    u32 mip_levels{1};
    while ((size >> mip_levels) != 0)
    {
        ++mip_levels;
    }

    ByteWriter writer{};
    write_texture_header(
        writer,
        size,
        1,
        is_srgb ? content::TextureFlags::is_srgb : 0U,
        mip_levels,
        is_srgb ? format_r8g8b8a8_unorm_srgb : format_r8g8b8a8_unorm);
    for (u32 mip{0}; mip < mip_levels; ++mip)
    {
        const u32 mip_width{mip_size(size, mip)};
        const u32 row_pitch{mip_width * static_cast<u32>(sizeof(u32))};
        writer.write<u32>(row_pitch);
        writer.write<u32>(row_pitch * mip_width); // slice pitch
        for (u32 y{0}; y < mip_width; ++y)
        {
            for (u32 x{0}; x < mip_width; ++x)
            {
                writer.write<u32>(((x + y) % 2) == 0 ? color_a : color_b);
            }
        }
    }
    return writer.take();
}

auto make_cube_map(u32 size, u32 mip_levels, math::v3 color) -> std::vector<u8>
{
    assert(size && mip_levels && (size >> (mip_levels - 1)) != 0);
    constexpr u32 face_count{6};

    ByteWriter writer{};
    write_texture_header(
        writer,
        size,
        face_count,
        content::TextureFlags::is_cube_map | content::TextureFlags::is_hdr,
        mip_levels,
        format_r32g32b32a32_float);
    for (u32 face{0}; face < face_count; ++face)
    {
        for (u32 mip{0}; mip < mip_levels; ++mip)
        {
            const u32 mip_width{mip_size(size, mip)};
            const u32 row_pitch{mip_width * static_cast<u32>(sizeof(math::v4))};
            writer.write<u32>(row_pitch);
            writer.write<u32>(row_pitch * mip_width);
            // A little darker per mip and a little different per face.
            const f32 scale{(1.f - 0.1f * static_cast<f32>(mip)) * (0.7f + 0.05f * static_cast<f32>(face))};
            const math::v4 texel{color.x * scale, color.y * scale, color.z * scale, 1.f};
            for (u32 i{0}; i < mip_width * mip_width; ++i)
            {
                writer.write<math::v4>(texel);
            }
        }
    }
    return writer.take();
}

auto make_brdf_lut(u32 size) -> std::vector<u8>
{
    assert(size > 1);
    ByteWriter writer{};
    write_texture_header(writer, size, 1, content::TextureFlags::is_hdr, 1, format_r32g32_float);
    const u32 row_pitch{size * static_cast<u32>(sizeof(math::v2))};
    writer.write<u32>(row_pitch);
    writer.write<u32>(row_pitch * size);
    for (u32 y{0}; y < size; ++y)
    {
        for (u32 x{0}; x < size; ++x)
        {
            // Never 0: TestShader.hlsl divides by the first channel.
            const f32 u{(static_cast<f32>(x) + 0.5f) / static_cast<f32>(size)};
            const f32 v{(static_cast<f32>(y) + 0.5f) / static_cast<f32>(size)};
            writer.write<math::v2>({u, v});
        }
    }
    return writer.take();
}

}
