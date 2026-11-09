// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "D3D12CommonHeaders.h"

namespace primal::graphics::d3d12
{
class D3D12Surface
{
  public:
    // NOTE:
    // https://learn.microsoft.com/en-us/windows/win32/direct3darticles/high-dynamic-range#option-1-use-fp16-pixel-format-and-scrgb-color-space
    constexpr static DXGI_FORMAT default_back_buffer_format{DXGI_FORMAT_R16G16B16A16_FLOAT};
    constexpr static u32 buffer_count{4};

    explicit D3D12Surface(platform::Window window)
        : _window{window}
    {
        assert(_window.handle());
    }
#if USE_STL_VECTOR
    DISABLE_COPY(D3D12Surface);
    constexpr D3D12Surface(D3D12Surface &&o)
        : _swap_chain{o._swap_chain}
        , _window{o._window}
        , _current_bb_index{o._current_bb_index}
        , _viewport{o._viewport}
        , _scissor_rect{o._scissor_rect}
        , _light_culling_id{o._light_culling_id}
    {
        for (u32 i{0}; i < buffer_count; ++i)
        {
            _render_target_data[i] = o._render_target_data[i];
        }

        o.reset();
    }

    constexpr D3D12Surface &operator=(D3D12Surface &&o)
    {
        assert(this != &o);
        if (this != &o)
        {
            release();
            move(o);
        }

        return *this;
    }
#else
    DISABLE_COPY_AND_MOVE(D3D12Surface);
#endif
    ~D3D12Surface()
    {
        release();
    }

    auto create_swap_chain(IDXGIFactory7 *factory, ID3D12CommandQueue *cmd_queue) -> void;
    auto present() const -> void;
    auto resize() -> void;

    [[nodiscard]] constexpr auto width() const -> u32
    {
        return (u32)_viewport.Width;
    }
    [[nodiscard]] constexpr auto height() const -> u32
    {
        return (u32)_viewport.Height;
    }
    [[nodiscard]] constexpr auto back_buffer() const -> ID3D12Resource *
    {
        return _render_target_data[_current_bb_index].resource;
    }
    [[nodiscard]] constexpr auto rtv() const -> D3D12_CPU_DESCRIPTOR_HANDLE
    {
        return _render_target_data[_current_bb_index].rtv.cpu;
    }
    [[nodiscard]] constexpr auto viewport() const -> const D3D12_VIEWPORT &
    {
        return _viewport;
    }
    [[nodiscard]] constexpr auto scissor_rect() const -> const D3D12_RECT &
    {
        return _scissor_rect;
    }
    [[nodiscard]] constexpr auto light_culling_id() const -> id::id_type
    {
        return _light_culling_id;
    }

  private:
    auto finalize() -> void;
    auto release() -> void;

#if USE_STL_VECTOR
    constexpr auto move(D3D12Surface &o) -> void
    {
        _swap_chain = o._swap_chain;
        for (u32 i{0}; i < buffer_count; ++i)
        {
            _render_target_data[i] = o._render_target_data[i];
        }
        _window = o._window;
        _current_bb_index = o._current_bb_index;
        _viewport = o._viewport;
        _scissor_rect = o._scissor_rect;
        _light_culling_id = o._light_culling_id;

        o.reset();
    }

    constexpr auto reset() -> void
    {
        _swap_chain = nullptr;
        for (u32 i{0}; i < buffer_count; ++i)
        {
            _render_target_data[i] = {};
        }
        _window = {};
        _current_bb_index = 0;
        _viewport = {};
        _scissor_rect = {};
        _light_culling_id = id::invalid_id;
    }
#endif

    struct RenderTargetData
    {
        ID3D12Resource *resource{nullptr};
        DescriptorHandle rtv{};
    };

    // NOTE: when adding new member data here, don't forget to update the move constructor
    //       as well as the move() and reset() functions. This is to have the correct behavior
    //       when using std::vector (from STL)
    IDXGISwapChain4 *_swap_chain{nullptr};
    RenderTargetData _render_target_data[buffer_count]{};
    platform::Window _window;
    mutable u32 _current_bb_index{0};
    D3D12_VIEWPORT _viewport{};
    D3D12_RECT _scissor_rect{};
    id::id_type _light_culling_id{id::invalid_id};
};
}
