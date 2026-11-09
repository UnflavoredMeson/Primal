// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "D3D12CommonHeaders.h"

namespace primal::graphics::d3d12::upload
{

class D3D12UploadContext
{
  public:
    D3D12UploadContext(u32 aligned_size);
    DISABLE_COPY_AND_MOVE(D3D12UploadContext);
    ~D3D12UploadContext()
    {
        assert(_frame_index == u32_invalid_id);
    }

    auto end_upload() -> void;

    [[nodiscard]] constexpr auto command_list() const -> id3d12_graphics_command_list *
    {
        return _cmd_list;
    }
    [[nodiscard]] constexpr auto upload_buffer() const -> ID3D12Resource *
    {
        return _upload_buffer;
    }
    [[nodiscard]] constexpr auto cpu_address() const -> void *
    {
        return _cpu_address;
    }

  private:
    DEBUG_OP(D3D12UploadContext() = default);
    id3d12_graphics_command_list *_cmd_list{nullptr};
    ID3D12Resource *_upload_buffer{nullptr};
    void *_cpu_address{nullptr};
    u32 _frame_index{u32_invalid_id};
};

auto initialize() -> bool;
auto shutdown() -> void;
}
