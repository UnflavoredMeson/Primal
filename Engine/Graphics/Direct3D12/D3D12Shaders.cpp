// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "D3D12Shaders.h"
#include "Content/ContentLoader.h"
#include "Content/ContentToEngine.h"

namespace primal::graphics::d3d12::shaders {
namespace {

// Each element in this array points to an offset within the shaders blob.
content::compiled_shader_ptr engine_shaders[engine_shader::count]{};

// This is a chunk of memory that contains all compiled engine shaders.
// The blob is an array of shader byte code consisting of a u64 size, a hash and an array of bytes.
// NOTE: in shaders.bin the records are packed back to back, so a record's u64 size is only 8-byte aligned
//       when the previous byte code sizes happen to be multiples of 8 (they're multiples of 4). Reading a
//       misaligned compiled_shader is undefined behavior, so the records are copied to 8-byte aligned
//       offsets in this blob (same loader as VulkanShaders.cpp).
std::unique_ptr<u8[]> engine_shaders_blob{};

bool
load_engine_shaders()
{
    assert(!engine_shaders_blob);
    std::unique_ptr<u8[]> file_blob{};
    u64 size{ 0 };
    const bool loaded{ content::load_engine_shaders(file_blob, size) };
    if (!loaded || !file_blob || !size)
    {
        OutputDebugStringA("D3D12: couldn't load the engine shaders (shaders/d3d12/shaders.bin). Compile them with "
                           "compile_shaders(graphics_platform::direct3d12).\n");
        return false;
    }

    // Find the records (sizes are read with memcpy: they may be misaligned in the file).
    u64 file_offsets[engine_shader::count]{};
    u64 record_sizes[engine_shader::count]{};
    u64 offset{ 0 };
    u64 aligned_size{ 0 };
    u32 index{ 0 };
    while (offset < size)
    {
        if (index >= engine_shader::count || offset + sizeof(u64) > size) break;
        u64 byte_code_size{ 0 };
        memcpy(&byte_code_size, &file_blob[offset], sizeof(u64));
        file_offsets[index] = offset;
        record_sizes[index] = content::compiled_shader::buffer_size(byte_code_size);
        offset += record_sizes[index];
        aligned_size = math::align_size_up<sizeof(u64)>(aligned_size) + record_sizes[index];
        ++index;
    }
    if (offset != size || index != engine_shader::count)
    {
        OutputDebugStringA("D3D12: shaders/d3d12/shaders.bin is corrupt or from another version (delete it to recompile).\n");
        return false;
    }

    engine_shaders_blob = std::make_unique<u8[]>(aligned_size);
    u64 aligned_offset{ 0 };
    for (u32 i{ 0 }; i < engine_shader::count; ++i)
    {
        aligned_offset = math::align_size_up<sizeof(u64)>(aligned_offset);
        memcpy(&engine_shaders_blob[aligned_offset], &file_blob[file_offsets[i]], record_sizes[i]);
        content::compiled_shader_ptr& shader{ engine_shaders[i] };
        assert(!shader);
        shader = reinterpret_cast<const content::compiled_shader_ptr>(&engine_shaders_blob[aligned_offset]);
        aligned_offset += record_sizes[i];
    }
    assert(aligned_offset == aligned_size);

    return true;
}

} // anonymous namespace

bool
initialize()
{
    return load_engine_shaders();
}

void
shutdown()
{
    for (u32 i{ 0 }; i < engine_shader::count; ++i)
    {
        engine_shaders[i] = {};
    }
    engine_shaders_blob.reset();
}

D3D12_SHADER_BYTECODE
get_engine_shader(engine_shader::id id)
{
    assert(id < engine_shader::count);
    const content::compiled_shader_ptr shader{ engine_shaders[id] };
    assert(shader && shader->byte_code_size());
    return { shader->byte_code(), shader->byte_code_size() };
}

}