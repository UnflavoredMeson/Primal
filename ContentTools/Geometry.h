// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "ToolsCommon.h"

namespace primal::tools
{

struct Vertex
{
    math::v4 tangent{};
    math::v4 joint_weights{};
    math::u32v4 joint_indices{u32_invalid_id, u32_invalid_id, u32_invalid_id, u32_invalid_id};
    math::v3 position{};
    math::v3 normal{};
    math::v2 uv{};
    u8 red{}, green{}, blue{};
    u8 pad;
};

namespace elements
{

struct ElementsType
{
    enum Type : u32
    {
        position_only = 0x00,
        static_normal = 0x01,
        static_normal_texture = 0x03,
        static_color = 0x04,
        skeletal = 0x08,
        skeletal_color = skeletal | static_color,
        skeletal_normal = skeletal | static_normal,
        skeletal_normal_color = skeletal_normal | static_color,
        skeletal_normal_texture = skeletal | static_normal_texture,
        skeletal_normal_texture_color = skeletal_normal_texture | static_color,
    };
};

struct StaticColor
{
    u8 color[3];
    u8 pad;
};

struct StaticNormal
{
    u8 color[3];
    u8 t_sign; // bit 0: tangent handedness, bit 1: tangent.z sign, bit 2: normal.z sign (0 means -1, 1 means +1).
    u16 normal[2];
};

struct StaticNormalTexture
{
    u8 color[3];
    u8 t_sign; // bit 0: tangent handedness, bit 1: tangent.z sign, bit 2: normal.z sign (0 means -1, 1 means +1).
    u16 normal[2];
    u16 tangent[2];
    math::v2 uv;
};

struct Skeletal
{
    u8 joint_weights[3]; // normalized joint weights for up to 4 joints.
    u8 pad;
    u16 joint_indices[4];
};

struct SkeletalColor
{
    u8 joint_weights[3]; // normalized joint weights for up to 4 joints.
    u8 pad;
    u16 joint_indices[4];
    u8 color[3];
    u8 pad2;
};

struct SkeletalNormal
{
    u8 joint_weights[3]; // normalized joint weights for up to 4 joints.
    u8 t_sign; // bit 0: tangent handedness, bit 1: tangent.z sign, bit 2: normal.z sign (0 means -1, 1 means +1).
    u16 joint_indices[4];
    u16 normal[2];
};

struct SkeletalNormalColor
{
    u8 joint_weights[3]; // normalized joint weights for up to 4 joints.
    u8 t_sign; // bit 0: tangent handedness, bit 1: tangent.z sign, bit 2: normal.z sign (0 means -1, 1 means +1).
    u16 joint_indices[4];
    u16 normal[2];
    u8 color[3];
    u8 pad;
};

struct SkeletalNormalTexture
{
    u8 joint_weights[3]; // normalized joint weights for up to 4 joints.
    u8 t_sign; // bit 0: tangent handedness, bit 1: tangent.z sign, bit 2: normal.z sign (0 means -1, 1 means +1).
    u16 joint_indices[4];
    u16 normal[2];
    u16 tangent[2];
    math::v2 uv;
};

struct SkeletalNormalTextureColor
{
    u8 joint_weights[3]; // normalized joint weights for up to 4 joints.
    u8 t_sign;           // bit 0: tangent handedness * (tangent.z sign), bit 1: normal.z sign (0 means -1, 1 means +1).
    u16 joint_indices[4];
    u16 normal[2];
    u16 tangent[2];
    math::v2 uv;
    u8 color[3];
    u8 pad;
};

}

struct Mesh
{
    // Initial data
    utl::vector<math::v3> positions;
    utl::vector<math::v3> normals;
    utl::vector<math::v4> tangents;
    utl::vector<math::v3> colors;
    utl::vector<utl::vector<math::v2>> uv_sets;
    utl::vector<u32> material_indices;
    utl::vector<u32> material_used;

    utl::vector<u32> raw_indices;

    // Intermediate data
    utl::vector<Vertex> vertices;
    utl::vector<u32> indices;

    // Output data
    std::string name;
    elements::ElementsType::Type elements_type;
    utl::vector<u8> position_buffer;
    utl::vector<u8> element_buffer;

    f32 lod_threshold{-1.f};
    u32 lod_id{u32_invalid_id};
};

struct LodGroup
{
    std::string name;
    utl::vector<Mesh> meshes;
};

struct Scene
{
    std::string name;
    utl::vector<LodGroup> lod_groups;
};

struct GeometryImportSettings
{
    f32 smoothing_angle;
    u8 calculate_normals;
    u8 calculate_tangents;
    u8 reverse_handedness;
    u8 import_embedded_textures;
    u8 import_animations;
    u8 coalesce_meshes;
};

struct SceneData
{
    u8 *buffer;
    u32 buffer_size;
    GeometryImportSettings settings;
};

auto process_scene(Scene &scene, const GeometryImportSettings &settings, Progression *const progression) -> void;
auto pack_data(const Scene &scene, SceneData &data) -> void;
auto coalesce_meshes(const LodGroup &lod, Mesh &combined_mesh, Progression *const progression) -> bool;
}
