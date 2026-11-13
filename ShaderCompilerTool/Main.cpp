// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.

// PrimalShaderCompiler: compiles the engine shaders (shaders/<backend>/shaders.bin next to the executable) as a build
// step, so shader errors fail the build. Same up-to-date check as at run time (source timestamps + shaders.bin.key),
// so it's a no-op when nothing changed.
//
// Usage: PrimalShaderCompiler [d3d12] [vulkan]   (no arguments: the platform's default backend)

#include "Core/Log.h"
#include "Platform/Platform.h"
#include "ShaderCompilation.h"

#include <cstring>

using namespace primal;

namespace
{

auto platform_name(graphics::GraphicsPlatform platform) -> const char *
{
    return platform == graphics::GraphicsPlatform::vulkan ? "vulkan" : "d3d12";
}

}

auto main(int argc, char *const *argv) -> int
{
    // The build reads this tool's output: compiler errors must arrive as the compiler wrote them.
    log::set_console_style(log::ConsoleStyle::plain);

    utl::vector<graphics::GraphicsPlatform> platforms;
    for (int i{1}; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "vulkan") == 0)
        {
            platforms.emplace_back(graphics::GraphicsPlatform::vulkan);
        }
#if PRIMAL_PLATFORM_WINDOWS
        else if (std::strcmp(argv[i], "d3d12") == 0)
        {
            platforms.emplace_back(graphics::GraphicsPlatform::direct3d12);
        }
#endif
        else
        {
#if PRIMAL_PLATFORM_WINDOWS
            constexpr const char *usage{"PrimalShaderCompiler [d3d12] [vulkan]"};
#else
            constexpr const char *usage{"PrimalShaderCompiler [vulkan]"};
#endif
            log::error(log::Category::tools, "PrimalShaderCompiler: unknown backend '{}'", argv[i]);
            log::error(log::Category::tools, "Usage: {}", usage);
            return 2;
        }
    }
    if (platforms.empty())
    {
        platforms.emplace_back(default_shader_platform);
    }

    // The shader source and output paths are relative to the executable's folder (x64/<Config>/), like at run time.
    platform::set_current_directory_to_executable_path();

    for (const graphics::GraphicsPlatform platform : platforms)
    {
        if (!::compile_shaders(platform))
        {
            log::error(
                log::Category::tools,
                "PrimalShaderCompiler: error: failed to compile the engine shaders for {}",
                platform_name(platform));
            return 1;
        }
    }
    return 0;
}
