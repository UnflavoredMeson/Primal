// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "CommonHeaders.h"
#include "Core/Log.h"
#include "Graphics/Renderer.h"
#include "Platform/Window.h"

// Skip definition of min/max macros in windows.h
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

#include <array>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d12.lib")

namespace primal::graphics::d3d12
{
constexpr u32 frame_buffer_count{3};
using id3d12_device = ID3D12Device8;
using id3d12_graphics_command_list = ID3D12GraphicsCommandList6;
}

// Assert that COM call to D3D API succeeded
#ifdef _DEBUG
#ifndef DXCall
#define DXCall(x)                                                                                                      \
    if (FAILED(x))                                                                                                     \
    {                                                                                                                  \
        primal::log::error(primal::log::Category::d3d12, "D3D12 call failed: {}", #x);                                 \
        __debugbreak();                                                                                                \
    }
#endif
#else
#ifndef DXCall
#define DXCall(x) x
#endif
#endif

#ifdef _DEBUG
namespace primal::graphics::d3d12::detail
{
// Logs the name of a new object.
inline auto log_object_created(const wchar_t *name) -> void
{
    std::array<char, 256> utf8{};
    ::WideCharToMultiByte(CP_UTF8, 0, name, -1, utf8.data(), static_cast<int>(utf8.size() - 1), nullptr, nullptr);
    log::debug(log::Category::d3d12, "Object created: {}", utf8.data());
}
}

// Sets the name of the COM object and logs it.
#define NAME_D3D12_OBJECT(obj, name)                                                                                   \
    obj->SetName(name);                                                                                                \
    primal::graphics::d3d12::detail::log_object_created(name);
// The indexed variant will include the index in the name of the object
#define NAME_D3D12_OBJECT_INDEXED(obj, n, name)                                                                        \
    {                                                                                                                  \
        wchar_t full_name[128];                                                                                        \
        if (swprintf_s(full_name, L"%s[%llu]", name, static_cast<u64>(n)) > 0)                                         \
        {                                                                                                              \
            obj->SetName(full_name);                                                                                   \
            primal::graphics::d3d12::detail::log_object_created(full_name);                                            \
        }                                                                                                              \
    }
#else
#define NAME_D3D12_OBJECT(x, name)
#define NAME_D3D12_OBJECT_INDEXED(x, n, name)
#endif
