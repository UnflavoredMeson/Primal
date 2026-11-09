// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "VulkanCommonHeaders.h"

// Staging uploads (counterpart of D3D12Upload.h). Usage, from any thread:
//     upload::vulkan_upload_context context{ size };
//     memcpy(context.cpu_address(), data, size);
//     vkCmdCopyBuffer(context.command_buffer(), context.upload_buffer(), destination, ...);
//     context.end_upload(); // submits, waits for the copy, frees the staging buffer
// D3D12 uploads on a copy queue; here they go to the graphics queue (no queue family ownership
// transfers needed). end_upload() records a barrier that makes the copies visible to everything
// submitted later, so callers only transition images (to their final layout) themselves.
namespace primal::graphics::vulkan::upload
{

class VulkanUploadContext
{
  public:
    explicit VulkanUploadContext(u32 aligned_size);
    DISABLE_COPY_AND_MOVE(VulkanUploadContext);
    ~VulkanUploadContext()
    {
        assert(_frame_index == u32_invalid_id);
    }

    auto end_upload() -> void;

    [[nodiscard]] constexpr auto command_buffer() const -> VkCommandBuffer
    {
        return _cmd_buffer;
    }
    [[nodiscard]] constexpr auto upload_buffer() const -> VkBuffer
    {
        return _upload_buffer;
    }
    [[nodiscard]] constexpr auto cpu_address() const -> void *
    {
        return _cpu_address;
    }

  private:
    VkCommandBuffer _cmd_buffer{VK_NULL_HANDLE};
    VkBuffer _upload_buffer{VK_NULL_HANDLE};
    void *_cpu_address{nullptr};
    u32 _frame_index{u32_invalid_id};
};

auto initialize() -> bool;
auto shutdown() -> void;

}
