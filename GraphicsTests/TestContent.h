// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "CommonHeaders.h"

#include <span>
#include <vector>

// Small hand-made content in the formats content::create_resource() reads, so the graphics tests don't need the
// imported assets in x64/ (they are made by ContentTools on Windows and aren't in the repo).
namespace test_content
{

// Vertex formats. The values are tools::elements::elements_type (ContentTools/Geometry.h) and the ELEMENTS_TYPE
// defines of EngineTest/TestShader.hlsl.
struct ElementsType
{
    enum Type : u32
    {
        position_only = 0,
        static_normal = 1,
        static_normal_texture = 3,
    };
};

struct Submesh
{
    ElementsType::Type elements_type{ElementsType::position_only};
    std::vector<primal::math::v3> positions;
    std::vector<primal::math::v3> normals;  // unit length, one per position
    std::vector<primal::math::v3> tangents; // unit length, one per position
    std::vector<primal::math::v2> uvs;      // one per position
    std::vector<u16> indices;               // triangle list
};

// A box around the origin: 24 vertices (4 per face), 36 indices.
[[nodiscard]] auto make_box(ElementsType::Type elements_type, primal::math::v3 half_extent) -> Submesh;
// A square in the xz plane around the origin, facing +y: quads_per_side^2 quads.
[[nodiscard]] auto make_plane(ElementsType::Type elements_type, f32 half_size, u32 quads_per_side) -> Submesh;

struct Lod
{
    f32 threshold{0.f};
    std::vector<Submesh> submeshes;
};

// A geometry for content::create_resource(..., AssetType::mesh). The LOD thresholds must increase.
[[nodiscard]] auto make_geometry(std::span<const Lod> lods) -> std::vector<u8>;

// Textures for content::create_resource(..., AssetType::texture).
// A square R8G8B8A8 checkerboard of two colors with a full mip chain. 'size' is a power of two.
[[nodiscard]] auto make_checkerboard_texture(u32 size, bool is_srgb, u32 color_a, u32 color_b) -> std::vector<u8>;
// A square R32G32B32A32_FLOAT cube map with 'mip_levels' mips, every texel 'color' scaled down a little per mip.
[[nodiscard]] auto make_cube_map(u32 size, u32 mip_levels, primal::math::v3 color) -> std::vector<u8>;
// A square R32G32_FLOAT texture standing in for the BRDF lookup table: (u, v) in each texel.
[[nodiscard]] auto make_brdf_lut(u32 size) -> std::vector<u8>;

}
