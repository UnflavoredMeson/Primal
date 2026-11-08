// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "D3D12Shaders.h"
#include "Content/ContentLoader.h"
#include "Content/ContentToEngine.h"

namespace primal::graphics::d3d12::shaders {
namespace {

// Each element in this array points to an offset within the shaders blob.
content::compiled_shader_ptr engine_shaders[engine_shader::count]{};

// The contents of shaders.bin: a header and the compiled engine shaders (see content::engine_shaders_header).
// The records are 8-byte aligned in the file, so engine_shaders point straight into this blob.
std::unique_ptr<u8[]> engine_shaders_blob{};

bool
load_engine_shaders()
{
    assert(!engine_shaders_blob);
    u64 size{ 0 };
    const bool loaded{ content::load_engine_shaders(engine_shaders_blob, size) };
    if (!loaded || !engine_shaders_blob || !size)
    {
        OutputDebugStringA("D3D12: couldn't load the engine shaders (shaders/d3d12/shaders.bin). Compile them with "
                           "compile_shaders(graphics_platform::direct3d12).\n");
        engine_shaders_blob.reset();
        return false;
    }

    if (!content::get_engine_shaders(engine_shaders_blob.get(), size, &engine_shaders[0], engine_shader::count))
    {
        OutputDebugStringA("D3D12: shaders/d3d12/shaders.bin is corrupt or from another version (delete it to recompile).\n");
        engine_shaders_blob.reset();
        for (auto& shader : engine_shaders) shader = {};
        return false;
    }

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