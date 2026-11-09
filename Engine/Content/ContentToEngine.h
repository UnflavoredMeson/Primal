// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "CommonHeaders.h"

namespace primal::content
{

struct AssetType
{
    enum Type : u32
    {
        unknown = 0,
        animation,
        audio,
        material,
        mesh,
        skeleton,
        texture,

        count
    };
};

struct TextureFlags
{
    enum Flags : u32
    {
        is_hdr = 0x01,
        has_alpha = 0x02,
        is_premultiplied_alpha = 0x04,
        is_imported_as_normal_map = 0x08,
        is_cube_map = 0x10,
        is_volume_map = 0x20,
        is_srgb = 0x40,
    };
};

typedef struct CompiledShader
{
    static constexpr u32 hash_length{16};
    constexpr auto byte_code_size() const -> u64
    {
        return _byte_code_size;
    }
    constexpr auto hash() const -> const u8 *const
    {
        return &_hash[0];
    }
    constexpr auto byte_code() const -> const u8 *const
    {
        return &_byte_code;
    }
    constexpr auto buffer_size() const -> const u64
    {
        return sizeof(_byte_code_size) + hash_length + _byte_code_size;
    }
    constexpr static auto buffer_size(u64 size) -> u64
    {
        return sizeof(_byte_code_size) + hash_length + size;
    }

  private:
    u64 _byte_code_size;
    u8 _hash[hash_length];
    u8 _byte_code;
} const *compiled_shader_ptr;

// Layout of shaders.bin (written by EngineDLL/ShaderCompilation.cpp, read by both graphics backends):
// an engine_shaders_header, then shader_count compiled_shader records. Every record starts at an 8-byte aligned
// offset (zero padding after the byte code, also after the last record), so the records can be used in place.
struct EngineShadersHeader
{
    static constexpr u32 magic_value{0x42485350}; // "PSHB" (little endian)
    static constexpr u32 current_version{2};      // 1 = the old layout without header, records packed back to back
    static constexpr u64 record_alignment{sizeof(u64)};
    u32 magic;
    u32 version;
    u32 shader_count;
    u32 reserved;
};
static_assert(sizeof(EngineShadersHeader) % EngineShadersHeader::record_alignment == 0);

struct LodOffset
{
    u16 offset;
    u16 count;
};

auto create_resource(const void *const data, AssetType::Type type) -> id::id_type;
auto destroy_resource(id::id_type id, AssetType::Type type) -> void;

auto add_shader_group(const u8 *const *shaders, u32 num_shaders, const u32 *const keys) -> id::id_type;
auto remove_shader_group(id::id_type id) -> void;
auto get_shader(id::id_type id, u32 shader_key) -> compiled_shader_ptr;
// Finds the shader_count records in the contents of a shaders.bin file. Returns false if the blob isn't a complete
// shaders.bin of the current version with exactly shader_count shaders. The blob must be 8-byte aligned and must
// outlive the returned pointers.
auto get_engine_shaders(const u8 *const blob, u64 size, compiled_shader_ptr *const shaders, u32 shader_count) -> bool;

auto get_submesh_gpu_ids(id::id_type geometry_content_id, u32 id_count, id::id_type *const gpu_ids) -> void;
auto get_lod_offsets(
    const id::id_type *const geometry_ids,
    const f32 *const thresholds,
    u32 id_count,
    utl::vector<LodOffset> &offsets) -> void;
}
