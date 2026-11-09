// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "VulkanUpload.h"
#include "VulkanCore.h"
#include <thread>

namespace primal::graphics::vulkan::upload
{
namespace
{

struct UploadFrame
{
    VkCommandPool cmd_pool{VK_NULL_HANDLE};
    VkCommandBuffer cmd_buffer{VK_NULL_HANDLE};
    VkBuffer upload_buffer{VK_NULL_HANDLE};
    VmaAllocation allocation{VK_NULL_HANDLE};
    void *cpu_address{nullptr};
    u64 fence_value{0};
    // Set while a context uses this frame (D3D12 marks it by setting upload_buffer to 1).
    bool in_use{false};

    auto wait_and_reset() -> void;

    auto release() -> void
    {
        wait_and_reset();
        if (cmd_pool)
            ::vkDestroyCommandPool(core::device(), cmd_pool, nullptr);
        cmd_pool = VK_NULL_HANDLE;
        cmd_buffer = VK_NULL_HANDLE;
    }

    [[nodiscard]] constexpr auto is_ready() const -> bool
    {
        return !in_use;
    }
};

constexpr u32 upload_frame_count{4};
UploadFrame upload_frames[upload_frame_count]{};
VkSemaphore upload_fence{VK_NULL_HANDLE}; // timeline semaphore (ID3D12Fence)
u64 upload_fence_value{0};
std::mutex frame_mutex{};
std::mutex fence_mutex{};

auto UploadFrame::wait_and_reset() -> void
{
    if (upload_fence && fence_value)
    {
        VkSemaphoreWaitInfo wait_info{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
        wait_info.semaphoreCount = 1;
        wait_info.pSemaphores = &upload_fence;
        wait_info.pValues = &fence_value;
        VkCall(::vkWaitSemaphores(core::device(), &wait_info, UINT64_MAX));
    }

    // The GPU is done with the staging buffer, so it can be destroyed right away (no deferred release).
    if (upload_buffer)
        ::vmaDestroyBuffer(core::allocator(), upload_buffer, allocation);
    upload_buffer = VK_NULL_HANDLE;
    allocation = VK_NULL_HANDLE;
    cpu_address = nullptr;
}

// NOTE: frames should be locked (frame_lock) before this function is called.
auto get_available_upload_frame(std::unique_lock<std::mutex> &frame_lock) -> u32
{
    assert(frame_lock.owns_lock());
    u32 index{u32_invalid_id};
    const u32 count{upload_frame_count};
    UploadFrame *const frames{&upload_frames[0]};

    for (u32 i{0}; i < count; ++i)
    {
        if (frames[i].is_ready())
        {
            index = i;
            break;
        }
    }

    // None of the frames were done uploading. Wait until one is.
    // NOTE: unlike D3D12 (where is_ready() checks a field the uploading thread clears), the other
    //       threads need frame_mutex to release a frame, so it's unlocked while waiting.
    while (index == u32_invalid_id)
    {
        frame_lock.unlock();
        std::this_thread::yield();
        frame_lock.lock();
        for (u32 i{0}; i < count; ++i)
        {
            if (frames[i].is_ready())
            {
                index = i;
                break;
            }
        }
    }

    return index;
}

auto init_failed() -> bool
{
    shutdown();
    return false;
}

}

VulkanUploadContext::VulkanUploadContext(u32 aligned_size)
{
    assert(upload_fence && aligned_size);
    {
        // We don't want to lock this function for longer than necessary. So, we scope this lock.
        std::unique_lock lock{frame_mutex};
        _frame_index = get_available_upload_frame(lock);
        assert(_frame_index != u32_invalid_id);
        // Before unlocking, we prevent other threads from picking this frame.
        upload_frames[_frame_index].in_use = true;
    }

    UploadFrame &frame{upload_frames[_frame_index]};

    // Upload heap: host-visible, write-combined, mapped.
    VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.size = aligned_size;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
    alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo allocation{};
    VkCall(
        ::vmaCreateBuffer(
            core::allocator(), &buffer_info, &alloc_info, &frame.upload_buffer, &frame.allocation, &allocation));
    frame.cpu_address = allocation.pMappedData;
    NAME_VK_OBJECT_INDEXED(frame.upload_buffer, VK_OBJECT_TYPE_BUFFER, aligned_size, "Upload Buffer - size");

    _cmd_buffer = frame.cmd_buffer;
    _upload_buffer = frame.upload_buffer;
    _cpu_address = frame.cpu_address;
    assert(_cmd_buffer && _upload_buffer && _cpu_address);

    VkCall(::vkResetCommandPool(core::device(), frame.cmd_pool, 0));
    VkCommandBufferBeginInfo begin_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VkCall(::vkBeginCommandBuffer(_cmd_buffer, &begin_info));
}

auto VulkanUploadContext::end_upload() -> void
{
    assert(_frame_index != u32_invalid_id);
    UploadFrame &frame{upload_frames[_frame_index]};
    const VkCommandBuffer cmd_buffer{frame.cmd_buffer};

    // Make the copies available and visible to all commands submitted after this one on the queue
    // (render frames). Pipeline barriers order against later submissions on the same queue.
    VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &barrier;
    ::vkCmdPipelineBarrier2(cmd_buffer, &dependency);

    VkCall(::vkEndCommandBuffer(cmd_buffer));

    {
        // The fence value must increase in submission order, so the submit is in the same lock.
        std::scoped_lock lock{fence_mutex};
        frame.fence_value = ++upload_fence_value;

        VkSemaphoreSubmitInfo signal_info{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        signal_info.semaphore = upload_fence;
        signal_info.value = frame.fence_value;
        signal_info.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        VkCommandBufferSubmitInfo cmd_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        cmd_info.commandBuffer = cmd_buffer;
        VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &cmd_info;
        submit.signalSemaphoreInfoCount = 1;
        submit.pSignalSemaphoreInfos = &signal_info;
        core::submit_to_graphics_queue(submit);
    }

    // Wait for the copy to finish. Then release the upload buffer.
    frame.wait_and_reset();
    {
        std::scoped_lock lock{frame_mutex};
        frame.in_use = false;
    }
    // This instance of upload context is now expired. Make sure we don't use it again.
    _cmd_buffer = VK_NULL_HANDLE;
    _upload_buffer = VK_NULL_HANDLE;
    _cpu_address = nullptr;
    _frame_index = u32_invalid_id;
}

auto initialize() -> bool
{
    const VkDevice device{core::device()};
    assert(device && !upload_fence);

    for (u32 i{0}; i < upload_frame_count; ++i)
    {
        UploadFrame &frame{upload_frames[i]};
        VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pool_info.queueFamilyIndex = core::graphics_queue_family();
        if (!VkCall(::vkCreateCommandPool(device, &pool_info, nullptr, &frame.cmd_pool)))
            return init_failed();

        VkCommandBufferAllocateInfo alloc_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        alloc_info.commandPool = frame.cmd_pool;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_info.commandBufferCount = 1;
        if (!VkCall(::vkAllocateCommandBuffers(device, &alloc_info, &frame.cmd_buffer)))
            return init_failed();

        NAME_VK_OBJECT_INDEXED(frame.cmd_pool, VK_OBJECT_TYPE_COMMAND_POOL, i, "Upload Command Pool");
        NAME_VK_OBJECT_INDEXED(frame.cmd_buffer, VK_OBJECT_TYPE_COMMAND_BUFFER, i, "Upload Command Buffer");
    }

    VkSemaphoreTypeCreateInfo timeline_info{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    timeline_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    semaphore_info.pNext = &timeline_info;
    if (!VkCall(::vkCreateSemaphore(device, &semaphore_info, nullptr, &upload_fence)))
        return init_failed();
    NAME_VK_OBJECT(upload_fence, VK_OBJECT_TYPE_SEMAPHORE, "Upload Fence (timeline)");

    return true;
}

auto shutdown() -> void
{
    for (u32 i{0}; i < upload_frame_count; ++i)
    {
        upload_frames[i].release();
        upload_frames[i].fence_value = 0;
        upload_frames[i].in_use = false;
    }

    if (upload_fence)
        ::vkDestroySemaphore(core::device(), upload_fence, nullptr);
    upload_fence = VK_NULL_HANDLE;
    upload_fence_value = 0;
}

}
