// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "ShaderCompilation.h"

#if PRIMAL_PLATFORM_WINDOWS
#include <wrl.h>
#include <dxcapi.h>
#include <d3d12shader.h>
#else
// DXC's Linux build: libdxcompiler.so plus a small COM emulation layer (WinAdapter.h).
#include <dxc/dxcapi.h>
#endif

#include "Graphics/Renderer.h"
#include "Content/ContentToEngine.h"
#include "Utilities/IOStream.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <iterator>
#include <string>

using namespace primal;

namespace
{

#if PRIMAL_PLATFORM_WINDOWS
template <typename T>
using com_ptr = Microsoft::WRL::ComPtr<T>;
#define DEBUG_BREAK() __debugbreak()
#else
// CComPtr from WinAdapter.h, with the Get() accessor of WRL's ComPtr.
template <typename T>
class com_ptr : public CComPtr<T>
{
  public:
    using CComPtr<T>::CComPtr; // NOLINT(modernize-use-noexcept): inherited constructors, misdiagnosed
    auto Get() const -> T *
    {
        return this->p;
    }
};
#if defined(__clang__)
#define DEBUG_BREAK() __builtin_debugtrap()
#else
#define DEBUG_BREAK() std::raise(SIGTRAP) // GCC has no __builtin_debugtrap (same as PRIMAL_VK_DEBUG_BREAK)
#endif
#endif

auto shader_log(const char *message) -> void
{
#if PRIMAL_PLATFORM_WINDOWS
    ::OutputDebugStringA(message);
#endif
    // Also on Windows: PrimalShaderCompiler runs as a build step, where only stderr reaches the build output.
    std::fputs(message, stderr);
}

}

// Assert that COM call to D3D API succeeded
#ifdef _DEBUG
#ifndef DXCall
#define DXCall(x)                                                                                                      \
    if (FAILED(x))                                                                                                     \
    {                                                                                                                  \
        char line_number[32];                                                                                          \
        snprintf(line_number, sizeof(line_number), "%u", __LINE__);                                                    \
        shader_log("Error in: ");                                                                                      \
        shader_log(__FILE__);                                                                                          \
        shader_log("\nLine: ");                                                                                        \
        shader_log(line_number);                                                                                       \
        shader_log("\n");                                                                                              \
        shader_log(#x);                                                                                                \
        shader_log("\n");                                                                                              \
        DEBUG_BREAK();                                                                                                 \
    }
#endif
#else
#ifndef DXCall
#define DXCall(x) x
#endif
#endif

namespace
{
// TODO: move some of this stuff into shader_compiler.

struct EngineShader
{
    enum Id : u32
    {
        fullscreen_triangle_vs = 0,
        post_process_ps,
        grid_frustums_cs,
        light_culling_cs,

        count
    };
};

struct EngineShaderInfo
{
    EngineShader::Id id;
    ShaderFileInfo info;
};

// NOTE: the HLSL sources are shared by the Direct3D 12 and Vulkan backends.
constexpr const char *shaders_source_path{"../../Engine/Graphics/Shaders/"};

constexpr EngineShaderInfo engine_shader_files[]{
    {EngineShader::fullscreen_triangle_vs,
     {"FullScreenTriangle.hlsl", "FullScreenTriangleVS", graphics::ShaderType::vertex}},
    {EngineShader::post_process_ps, {"PostProcess.hlsl", "PostProcessPS", graphics::ShaderType::pixel}},
    {EngineShader::grid_frustums_cs, {"GridFrustums.hlsl", "ComputeGridFrustumsCS", graphics::ShaderType::compute}},
    {EngineShader::light_culling_cs, {"CullLights.hlsl", "CullLightsCS", graphics::ShaderType::compute}},
};

static_assert(std::size(engine_shader_files) == EngineShader::count);

constexpr u32 shader_hash_length{16};
static_assert(content::CompiledShader::hash_length == shader_hash_length);

struct DxcCompiledShader
{
    com_ptr<IDxcBlob> byte_code;
    com_ptr<IDxcBlobUtf8> errors;
    com_ptr<IDxcBlobUtf8> assembly;
    u8 hash[shader_hash_length]{};
};

auto to_wstring(const char *c) -> std::wstring
{
    std::string s{c};
    return {s.begin(), s.end()};
}

// 128-bit FNV-1a. DXC only computes a shader hash for DXIL, so SPIR-V byte code is hashed here.
// The hash is used to identify byte code, not for security.
auto hash_byte_code(const u8 *const data, u64 size, u8 (&hash)[shader_hash_length]) -> void
{
    // offset basis 0x6c62272e07bb014262b821756295c58d, prime 2^88 + 0x13b
    u64 hi{0x6c62272e07bb0142ull};
    u64 lo{0x62b821756295c58dull};
    constexpr u64 prime_low{0x13b};
    for (u64 i{0}; i < size; ++i)
    {
        lo ^= data[i];
        // (hi, lo) *= prime (mod 2^128)
        const u64 lo_lo{(lo & 0xffff'ffffull) * prime_low};
        const u64 lo_hi{(lo >> 32) * prime_low};
        const u64 new_lo{lo_lo + (lo_hi << 32)};
        const u64 carry{(lo_hi >> 32) + (new_lo < lo_lo ? 1ull : 0ull)};
        hi = hi * prime_low + carry + (lo << 24);
        lo = new_lo;
    }
    std::memcpy(&hash[0], &lo, sizeof(lo));
    std::memcpy(&hash[sizeof(lo)], &hi, sizeof(hi));
}

class ShaderCompiler
{
  public:
    explicit ShaderCompiler(graphics::GraphicsPlatform platform)
        : _platform{platform}
    {
        HRESULT hr{S_OK};
        DXCall(hr = ::DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&_compiler)));
        if (FAILED(hr))
        {
            return;
        }
        DXCall(hr = ::DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&_utils)));
        if (FAILED(hr))
        {
            return;
        }
        DXCall(hr = _utils->CreateDefaultIncludeHandler(&_include_handler));
        if (FAILED(hr))
        {
            return;
        }
    }

    DISABLE_COPY_AND_MOVE(ShaderCompiler);

    auto is_valid() const -> bool
    {
        return _compiler && _utils && _include_handler;
    }

    // Full DXC argument list for a shader (used for the compiled shaders' up-to-date key).
    auto arguments(const ShaderFileInfo &info, utl::vector<std::wstring> &extra_args) -> utl::vector<std::wstring>
    {
        return get_args(info, extra_args);
    }

    // "major.minor[.commit_count+commit_hash]" of the loaded DXC, or empty if it doesn't say.
    auto version() const -> std::string
    {
        std::string result;
        com_ptr<IDxcVersionInfo> info{nullptr};
        if (SUCCEEDED(_compiler->QueryInterface(IID_PPV_ARGS(&info))) && info)
        {
            u32 major{0}, minor{0};
            if (SUCCEEDED(info->GetVersion(&major, &minor)))
            {
                result = std::to_string(major) + "." + std::to_string(minor);
            }
        }
        com_ptr<IDxcVersionInfo2> info2{nullptr};
        if (SUCCEEDED(_compiler->QueryInterface(IID_PPV_ARGS(&info2))) && info2)
        {
            u32 commit_count{0};
            char *commit_hash{nullptr};
            if (SUCCEEDED(info2->GetCommitInfo(&commit_count, &commit_hash)) && commit_hash)
            {
                result += "." + std::to_string(commit_count) + "+" + commit_hash;
                ::CoTaskMemFree(commit_hash);
            }
        }
        return result;
    }

    auto compile(
        u8 *data,
        u32 data_size,
        graphics::ShaderType::Type type,
        const char *function,
        utl::vector<std::wstring> &extra_args) -> DxcCompiledShader
    {
        assert(_compiler && _utils && _include_handler);
        assert(data && data_size && function);
        assert(type < graphics::ShaderType::count);
        HRESULT hr{S_OK};

        com_ptr<IDxcBlobEncoding> source_blob{nullptr};
        DXCall(hr = _utils->CreateBlob(data, data_size, 0, &source_blob));
        if (FAILED(hr))
        {
            return {};
        }
        assert(source_blob && source_blob->GetBufferSize());

        ShaderFileInfo info{};
        info.function = function;
        info.type = type;

        // TODO: do proper developer logging
        shader_log("Compiling ");
        shader_log(function);
        shader_log("\n");

        return compile(source_blob.Get(), get_args(info, extra_args));
    }

    auto compile(ShaderFileInfo info, const std::filesystem::path &full_path, utl::vector<std::wstring> &extra_args)
        -> DxcCompiledShader
    {
        assert(_compiler && _utils && _include_handler);
        HRESULT hr{S_OK};

        // Load the source file using Utils interface.
        com_ptr<IDxcBlobEncoding> source_blob{nullptr};
        DXCall(hr = _utils->LoadFile(full_path.wstring().c_str(), nullptr, &source_blob));
        if (FAILED(hr))
        {
            return {};
        }
        assert(source_blob && source_blob->GetBufferSize());

        shader_log("Compiling ");
        shader_log(info.file_name);
        shader_log(" : ");
        shader_log(info.function);
        shader_log("\n");

        return compile(source_blob.Get(), get_args(info, extra_args));
    }

    auto compile(IDxcBlobEncoding *source_blob, const utl::vector<std::wstring> &compiler_args) -> DxcCompiledShader
    {
        DxcBuffer buffer{};
        buffer.Encoding = DXC_CP_ACP; // auto-detect text format, I guess?
        buffer.Ptr = source_blob->GetBufferPointer();
        buffer.Size = source_blob->GetBufferSize();

        utl::vector<LPCWSTR> args;
        for (const auto &arg : compiler_args)
        {
            args.emplace_back(arg.c_str());
        }

        HRESULT hr{S_OK};
        com_ptr<IDxcResult> results{nullptr};
        DXCall(
            hr = _compiler->Compile(
                &buffer, args.data(), static_cast<u32>(args.size()), _include_handler.Get(), IID_PPV_ARGS(&results)));
        if (FAILED(hr))
        {
            return {};
        }

        com_ptr<IDxcBlobUtf8> errors{nullptr};
        DXCall(hr = results->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr));
        if (FAILED(hr))
        {
            return {};
        }

        if (errors && errors->GetStringLength())
        {
            shader_log("\nShader compilation error: \n");
            shader_log(errors->GetStringPointer());
        }
        else
        {
            shader_log(" [ Succeeded ]");
        }
        shader_log("\n");

        HRESULT status{S_OK};
        DXCall(hr = results->GetStatus(&status));
        if (FAILED(hr) || FAILED(status))
        {
            return {};
        }

        com_ptr<IDxcBlob> shader{nullptr};
        DXCall(hr = results->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shader), nullptr));
        if (FAILED(hr) || !shader)
        {
            return {};
        }

        DxcCompiledShader result{};

        if (is_spirv())
        {
            hash_byte_code(static_cast<const u8 *>(shader->GetBufferPointer()), shader->GetBufferSize(), result.hash);
        }
        else
        {
            com_ptr<IDxcBlob> hash{nullptr};
            DXCall(hr = results->GetOutput(DXC_OUT_SHADER_HASH, IID_PPV_ARGS(&hash), nullptr));
            if (FAILED(hr) || !hash)
            {
                return {};
            }
            DxcShaderHash *const hash_buffer{static_cast<DxcShaderHash *>(hash->GetBufferPointer())};
            // different source code could result in the same byte code, so we only care about byte code hash.
            assert(!(hash_buffer->Flags & DXC_HASHFLAG_INCLUDES_SOURCE));
            static_assert(sizeof(DxcShaderHash::HashDigest) == shader_hash_length);
            std::memcpy(&result.hash[0], &hash_buffer->HashDigest[0], shader_hash_length);
        }

        shader_log("Shader hash: ");
        for (u32 i{0}; i < shader_hash_length; ++i)
        {
            char hash_bytes[4]{}; // 2 chars for hex value, a space and termination 0.
            std::snprintf(hash_bytes, sizeof(hash_bytes), "%02x ", static_cast<u32>(result.hash[i]));
            shader_log(hash_bytes);
        }
        shader_log("\n");

        // NOTE: disassembly is optional (only used by the editor's shader view). DXC can only
        //       disassemble DXIL; use spirv-dis for SPIR-V.
        buffer.Ptr = shader->GetBufferPointer();
        buffer.Size = shader->GetBufferSize();
        buffer.Encoding = 0;
        com_ptr<IDxcResult> disasm_results{nullptr};
        if (!is_spirv() && SUCCEEDED(_compiler->Disassemble(&buffer, IID_PPV_ARGS(&disasm_results))) && disasm_results)
        {
            com_ptr<IDxcBlobUtf8> disassembly{nullptr};
            if (SUCCEEDED(disasm_results->GetOutput(DXC_OUT_DISASSEMBLY, IID_PPV_ARGS(&disassembly), nullptr)))
            {
                result.assembly.Attach(disassembly.Detach());
            }
        }

        // NOTE: Attach() takes over the reference. (Constructing from a raw pointer adds one,
        //       which leaked the blobs.)
        result.byte_code.Attach(shader.Detach());
        result.errors.Attach(errors.Detach());
        return result;
    }

  private:
    auto is_spirv() const -> bool
    {
        return _platform == graphics::GraphicsPlatform::vulkan;
    }

    auto get_args(const ShaderFileInfo &info, utl::vector<std::wstring> &extra_args) -> utl::vector<std::wstring>
    {
        utl::vector<std::wstring> args{};
        if (info.file_name)
        {
            args.emplace_back(to_wstring(info.file_name));
        }

        args.emplace_back(L"-E");
        args.emplace_back(to_wstring(info.function)); // Entry function
        args.emplace_back(L"-T");
        args.emplace_back(to_wstring(_profile_strings[static_cast<u32>(info.type)])); // Target profile
        args.emplace_back(L"-I");
        args.emplace_back(to_wstring(shaders_source_path)); // Include path
        args.emplace_back(L"-enable-16bit-types");
        args.emplace_back(DXC_ARG_ALL_RESOURCES_BOUND);
#if _DEBUG
        args.emplace_back(DXC_ARG_DEBUG);
        args.emplace_back(DXC_ARG_SKIP_OPTIMIZATIONS);
#else
        args.emplace_back(DXC_ARG_OPTIMIZATION_LEVEL3);
#endif
        args.emplace_back(DXC_ARG_WARNINGS_ARE_ERRORS);

        if (is_spirv())
        {
            add_spirv_args(args);
        }
        else
        {
            args.emplace_back(L"-Qstrip_reflect"); // Strip reflections into a separate blob
            args.emplace_back(L"-Qstrip_debug");   // Strip debug information into a separate blob
        }

        for (const auto &arg : extra_args)
        {
            args.emplace_back(arg.c_str());
        }

        return args;
    }

    // HLSL -> SPIR-V for the Vulkan backend.
    // https://shader-slang.org/slang/user-guide/spirv-target-specific
    static auto add_spirv_args(utl::vector<std::wstring> &args) -> void
    {
        args.emplace_back(L"-spirv");
        args.emplace_back(L"-fspv-target-env=vulkan1.3");
        // Every SPIR-V entry point is called "main". Compiled shaders don't store the HLSL function name, and
        // Vulkan pipelines need the entry point name (VkPipelineShaderStageCreateInfo::pName).
        args.emplace_back(L"-fspv-entrypoint-name=main");
        // Same buffer layout rules as Direct3D 12, so SharedTypes.h structs match on both backends.
        // Requires the scalarBlockLayout feature (core in Vulkan 1.2).
        args.emplace_back(L"-fvk-use-dx-layout");
        // register(xN, space0) -> descriptor set 0, binding = N + shift.
        const wchar_t *const shifts[][2]{
            {L"-fvk-b-shift", L"0"}, {L"-fvk-t-shift", L"100"}, {L"-fvk-u-shift", L"200"}, {L"-fvk-s-shift", L"300"}};
        for (const auto &shift : shifts)
        {
            args.emplace_back(shift[0]);
            args.emplace_back(shift[1]);
            args.emplace_back(L"0"); // register space
        }
        // SM 6.6 ResourceDescriptorHeap[] / SamplerDescriptorHeap[] -> one large descriptor array each
        // (binding 0 of set 1 / set 2). Needs VK_EXT_mutable_descriptor_type for the resource heap.
        args.emplace_back(L"-fvk-bind-resource-heap");
        args.emplace_back(L"0");
        args.emplace_back(L"1");
        args.emplace_back(L"-fvk-bind-sampler-heap");
        args.emplace_back(L"0");
        args.emplace_back(L"2");
    }

    // NOTE: Shader Model 6.x can also be used (AS and MS are only supported from SM6.5 on).
    constexpr static const char
        *_profile_strings[]{"vs_6_6", "hs_6_6", "ds_6_6", "gs_6_6", "ps_6_6", "cs_6_6", "as_6_6", "ms_6_6"};
    static_assert(std::size(_profile_strings) == graphics::ShaderType::count);

    com_ptr<IDxcCompiler3> _compiler{nullptr};
    com_ptr<IDxcUtils> _utils{nullptr};
    com_ptr<IDxcIncludeHandler> _include_handler{nullptr};
    graphics::GraphicsPlatform _platform;
};

// Get the path to the compiled shaders binary file.
auto engine_shaders_file_path(graphics::GraphicsPlatform platform) -> std::filesystem::path
{
    return std::filesystem::path{graphics::get_engine_shaders_path(platform)};
}

// Bump content::engine_shaders_header::current_version when the shaders.bin layout changes, and this when anything
// else that affects the output (but isn't a compiler argument) changes. Both are part of the key.
constexpr u32 compiled_shaders_key_version{1};

// Arguments for the engine shaders that aren't the same for every shader.
auto engine_shader_extra_args(EngineShader::Id id) -> utl::vector<std::wstring>
{
    utl::vector<std::wstring> extra_args{};
    if (id == EngineShader::grid_frustums_cs || id == EngineShader::light_culling_cs)
    {
        // TODO: get TILE_SIZE value from d3d12::delight
        extra_args.emplace_back(L"-D");
        extra_args.emplace_back(L"TILE_SIZE=32");
    }
    return extra_args;
}

// Identifies everything besides the sources that determines the compiled engine shaders: format version, DXC version
// and the full argument list of every shader. Stored next to shaders.bin (".key"); a mismatch means recompile.
// (Source changes are caught by timestamps.)
auto compiled_shaders_key(ShaderCompiler &compiler) -> std::string
{
    std::string text{
        "format " + std::to_string(content::EngineShadersHeader::current_version) + "." +
        std::to_string(compiled_shaders_key_version) + "\ndxc " + compiler.version() + "\n"};
    for (const auto &file : engine_shader_files)
    {
        utl::vector<std::wstring> extra_args{engine_shader_extra_args(file.id)};
        for (const auto &arg : compiler.arguments(file.info, extra_args))
        {
            for (const wchar_t c : arg)
            {
                text += static_cast<char>(c); // arguments are ASCII
            }
            text += ' ';
        }
        text += '\n';
    }

    u8 hash[shader_hash_length]{};
    hash_byte_code(reinterpret_cast<const u8 *>(text.data()), text.size(), hash);
    std::string key;
    for (const u8 b : hash)
    {
        char hex[3]{};
        std::snprintf(hex, sizeof(hex), "%02x", static_cast<u32>(b));
        key += hex;
    }
    return key;
}

auto compiled_shaders_key_path(graphics::GraphicsPlatform platform) -> std::filesystem::path
{
    std::filesystem::path path{engine_shaders_file_path(platform)};
    path += ".key";
    return path;
}

// NOTE: uses the error_code overloads of std::filesystem throughout: a missing source folder (executable started from
//       another directory) must fail compile_shaders(), not throw.
auto compiled_shaders_are_up_to_date(graphics::GraphicsPlatform platform, const std::string &key) -> bool
{
    std::error_code ec;
    const auto engine_shaders_path = engine_shaders_file_path(platform);
    const auto shaders_compilation_time = std::filesystem::last_write_time(engine_shaders_path, ec);
    if (ec)
    {
        return false; // no shaders.bin yet
    }

    {
        std::ifstream key_file{compiled_shaders_key_path(platform)};
        std::string stored_key;
        if (!key_file || !std::getline(key_file, stored_key) || stored_key != key)
        {
            return false;
        }
    }

    std::filesystem::directory_iterator it{shaders_source_path, ec};
    if (ec)
    {
        return false; // let compile_shaders() report the missing sources
    }
    for (const std::filesystem::directory_iterator end{}; it != end; it.increment(ec))
    {
        if (ec)
        {
            return false;
        }
        const auto source_time = it->last_write_time(ec);
        if (ec || source_time > shaders_compilation_time)
        {
            return false;
        }
    }

    return true;
}

auto save_compiled_shaders(
    const utl::vector<DxcCompiledShader> &shaders,
    graphics::GraphicsPlatform platform,
    const std::string &key) -> bool
{
    const auto engine_shaders_path = engine_shaders_file_path(platform);
    const auto key_path = compiled_shaders_key_path(platform);
    std::error_code ec;
    std::filesystem::remove(key_path, ec); // a stale key must not survive a failed write
    std::filesystem::create_directories(engine_shaders_path.parent_path(), ec);
    if (ec)
    {
        shader_log("Can't create the compiled shaders folder: ");
        shader_log(engine_shaders_path.parent_path().string().c_str());
        shader_log("\n");
        return false;
    }

    {
        std::ofstream file(engine_shaders_path, std::ios::out | std::ios::binary);
        if (!file)
        {
            shader_log("Can't write ");
            shader_log(engine_shaders_path.string().c_str());
            shader_log("\n");
            return false;
        }

        // Layout: see content::engine_shaders_header.
        content::EngineShadersHeader header{};
        header.magic = content::EngineShadersHeader::magic_value;
        header.version = content::EngineShadersHeader::current_version;
        header.shader_count = static_cast<u32>(shaders.size());
        file.write(reinterpret_cast<const char *>(&header), sizeof(header));

        constexpr u64 alignment{content::EngineShadersHeader::record_alignment};
        constexpr char padding[alignment]{};
        for (const auto &shader : shaders)
        {
            void *const byte_code{shader.byte_code->GetBufferPointer()};
            const u64 byte_code_length{shader.byte_code->GetBufferSize()};
            file.write(reinterpret_cast<const char *>(&byte_code_length), sizeof(byte_code_length));
            file.write(reinterpret_cast<const char *>(&shader.hash[0]), shader_hash_length);
            file.write(static_cast<char *>(byte_code), byte_code_length);
            const u64 record_size{content::CompiledShader::buffer_size(byte_code_length)};
            file.write(
                padding, static_cast<std::streamsize>(math::align_size_up<alignment>(record_size) - record_size));
        }
        if (!file.flush())
        {
            return false;
        }
    }

    // Written last: the key only exists next to a complete shaders.bin.
    std::ofstream key_file{key_path};
    key_file << key << '\n';
    return static_cast<bool>(key_file.flush());
}

auto pack_compiled_shader(DxcCompiledShader compiled_shader, bool include_errors_and_disassembly)
    -> std::unique_ptr<u8[]>
{
    if (compiled_shader.byte_code && compiled_shader.byte_code->GetBufferPointer() &&
        compiled_shader.byte_code->GetBufferSize())
    {
        const u64 errors_length{
            compiled_shader.errors ? static_cast<u64>(compiled_shader.errors->GetStringLength()) : 0};
        const u64 assembly_length{
            compiled_shader.assembly ? static_cast<u64>(compiled_shader.assembly->GetStringLength()) : 0};
        const u64 extra_size{
            include_errors_and_disassembly ? sizeof(u64) + sizeof(u64) + errors_length + assembly_length : 0};
        const u64 buffer_size{
            sizeof(u64) + content::CompiledShader::hash_length + compiled_shader.byte_code->GetBufferSize() +
            extra_size};
        std::unique_ptr<u8[]> buffer{std::make_unique<u8[]>(buffer_size)};
        utl::BlobStreamWriter blob{buffer.get(), buffer_size};
        blob.write(static_cast<u64>(compiled_shader.byte_code->GetBufferSize()));
        blob.write(compiled_shader.hash, content::CompiledShader::hash_length);
        blob.write(
            static_cast<u8 *>(compiled_shader.byte_code->GetBufferPointer()),
            compiled_shader.byte_code->GetBufferSize());
        if (include_errors_and_disassembly)
        {
            blob.write(errors_length);
            blob.write(assembly_length);
            if (errors_length)
            {
                blob.write(compiled_shader.errors->GetStringPointer(), errors_length);
            }
            if (assembly_length)
            {
                blob.write(compiled_shader.assembly->GetStringPointer(), assembly_length);
            }
        }

        assert(blob.offset() == buffer_size);
        return buffer;
    }

    return {};
}

}

auto compile_shader(
    ShaderFileInfo info,
    u8 *code,
    u32 code_size,
    utl::vector<std::wstring> &extra_args,
    bool include_errors_and_disassembly /*=false*/,
    graphics::GraphicsPlatform platform /*=default_shader_platform*/) -> std::unique_ptr<u8[]>
{
    assert(!info.file_name && info.function && code && code_size);

    // NOTE: according to marcelolr (https://github.com/Microsoft/DirectXShaderCompiler/issues/79)
    //       "creating compiler instances is pretty cheap, so it's probably not worth the hassle of caching / sharing
    //       them."
    return pack_compiled_shader(
        ShaderCompiler{platform}.compile(code, code_size, info.type, info.function, extra_args),
        include_errors_and_disassembly);
}

auto compile_shader(
    ShaderFileInfo info,
    const char *file_path,
    utl::vector<std::wstring> &extra_args,
    bool include_errors_and_disassembly /*=false*/,
    graphics::GraphicsPlatform platform /*=default_shader_platform*/) -> std::unique_ptr<u8[]>
{
    std::filesystem::path full_path{file_path};
    full_path += info.file_name;
    std::error_code ec;
    if (!std::filesystem::exists(full_path, ec))
    {
        return {};
    }

    // NOTE: according to marcelolr (https://github.com/Microsoft/DirectXShaderCompiler/issues/79)
    //       "creating compiler instances is pretty cheap, so it's probably not worth the hassle of caching / sharing
    //       them."
    return pack_compiled_shader(
        ShaderCompiler{platform}.compile(info, full_path, extra_args), include_errors_and_disassembly);
}

auto selected_graphics_platform() -> graphics::GraphicsPlatform
{
    constexpr const char *name{"PRIMAL_GRAPHICS"};
    std::string value;
#ifdef _MSC_VER
    // std::getenv is deprecated by MSVC (C4996).
    char *env{nullptr};
    size_t size{0};
    if (_dupenv_s(&env, &size, name) == 0 && env)
    {
        value = env;
        std::free(env);
    }
#else
    if (const char *const env{std::getenv(name)})
    {
        value = env;
    }
#endif

    if (value.empty())
    {
        return default_shader_platform;
    }
    if (value == "vulkan")
    {
        return graphics::GraphicsPlatform::vulkan;
    }
    if (value == "d3d12")
    {
        return graphics::GraphicsPlatform::direct3d12;
    }
    std::fprintf(
        stderr, "PRIMAL_GRAPHICS=%s is not \"vulkan\" or \"d3d12\"; using the default backend.\n", value.c_str());
    return default_shader_platform;
}

auto compile_shaders(graphics::GraphicsPlatform platform /*=default_shader_platform*/) -> bool
{
    ShaderCompiler compiler{platform};
    if (!compiler.is_valid())
    {
        return false;
    }
    const std::string key{compiled_shaders_key(compiler)};
    if (compiled_shaders_are_up_to_date(platform, key))
    {
        return true;
    }

    utl::vector<DxcCompiledShader> shaders;
    std::filesystem::path full_path{};

    for (const auto &file : engine_shader_files)
    {
        full_path = shaders_source_path;
        full_path += file.info.file_name;
        std::error_code ec;
        if (!std::filesystem::exists(full_path, ec))
        {
            shader_log("Engine shader source not found: ");
            shader_log(full_path.string().c_str());
            shader_log("\n");
            return false;
        }

        utl::vector<std::wstring> extra_args{engine_shader_extra_args(file.id)};
        DxcCompiledShader compiled_shader{compiler.compile(file.info, full_path, extra_args)};
        if (compiled_shader.byte_code && compiled_shader.byte_code->GetBufferPointer() &&
            compiled_shader.byte_code->GetBufferSize())
        {
            shaders.emplace_back(std::move(compiled_shader));
        }
        else
        {
            return false;
        }
    }

    return save_compiled_shaders(shaders, platform, key);
}
