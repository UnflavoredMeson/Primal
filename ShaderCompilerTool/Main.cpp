// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.

// PrimalShaderCompiler: compiles the engine shaders (shaders/<backend>/shaders.bin next to the executable) as a build
// step, so shader errors fail the build. Same up-to-date check as at run time (source timestamps + shaders.bin.key),
// so it's a no-op when nothing changed.
//
// Usage: PrimalShaderCompiler [d3d12] [vulkan]   (no arguments: the platform's default backend)

#include "ShaderCompilation.h"
#include "Platform/Platform.h"
#include <cstdio>
#include <cstring>

using namespace primal;

namespace {

const char*
platform_name(graphics::graphics_platform platform)
{
    return platform == graphics::graphics_platform::vulkan ? "vulkan" : "d3d12";
}

} // anonymous namespace

int
main(int argc, char** argv)
{
    utl::vector<graphics::graphics_platform> platforms;
    for (int i{ 1 }; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "vulkan") == 0) platforms.emplace_back(graphics::graphics_platform::vulkan);
#if PRIMAL_PLATFORM_WINDOWS
        else if (std::strcmp(argv[i], "d3d12") == 0) platforms.emplace_back(graphics::graphics_platform::direct3d12);
#endif
        else
        {
#if PRIMAL_PLATFORM_WINDOWS
            constexpr const char* usage{ "PrimalShaderCompiler [d3d12] [vulkan]" };
#else
            constexpr const char* usage{ "PrimalShaderCompiler [vulkan]" };
#endif
            std::fprintf(stderr, "PrimalShaderCompiler: unknown backend '%s'\nUsage: %s\n", argv[i], usage);
            return 2;
        }
    }
    if (platforms.empty()) platforms.emplace_back(default_shader_platform);

    // The shader source and output paths are relative to the executable's folder (x64/<Config>/), like at run time.
    platform::set_current_directory_to_executable_path();

    for (const graphics::graphics_platform platform : platforms)
    {
        if (!compile_shaders(platform))
        {
            std::fprintf(stderr, "PrimalShaderCompiler: error: failed to compile the engine shaders for %s\n",
                         platform_name(platform));
            return 1;
        }
    }
    return 0;
}
