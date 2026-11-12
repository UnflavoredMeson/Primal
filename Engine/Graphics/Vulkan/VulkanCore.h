// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "VulkanCommonHeaders.h"
#include "VulkanHelpers.h"
#include "VulkanInterface.h"

#include <type_traits>

namespace primal::graphics::vulkan::camera
{
class VulkanCamera;
}

namespace primal::graphics::vulkan
{
class DescriptorHeap;
class ConstantBuffer;

// Per-surface frame data for the render modules (counterpart of d3d12_frame_info).
struct VulkanFrameInfo
{
    const FrameInfo *info{nullptr};
    camera::VulkanCamera *camera{nullptr}; // null when frame_info has no camera (nothing is drawn then)
    vkx::BufferRange global_shader_data{}; // hlsl::GlobalShaderData in the frame's constant buffer
    u32 surface_width{0};
    u32 surface_height{0};
    id::id_type surface_id{id::invalid_id};
    id::id_type light_culling_id{id::invalid_id};
    u32 frame_index{0};
};
}

namespace primal::graphics::vulkan::core
{

auto initialize() -> bool;
auto shutdown() -> void;
[[nodiscard]] auto validation_stats() -> ValidationStats;

namespace detail
{
// allocation: the VMA allocation of a buffer or image (destroyed together), or nullptr.
auto deferred_release(VkObjectType type, u64 handle, VmaAllocation allocation = VK_NULL_HANDLE) -> void;

// Maps a Vulkan handle type to its VkObjectType. (64-bit handles are distinct pointer types.)
template <typename T>
consteval auto object_type() -> VkObjectType
{
    if constexpr (std::is_same_v<T, VkSwapchainKHR>)
    {
        return VK_OBJECT_TYPE_SWAPCHAIN_KHR;
    }
    else if constexpr (std::is_same_v<T, VkSurfaceKHR>)
    {
        return VK_OBJECT_TYPE_SURFACE_KHR;
    }
    else if constexpr (std::is_same_v<T, VkImageView>)
    {
        return VK_OBJECT_TYPE_IMAGE_VIEW;
    }
    else if constexpr (std::is_same_v<T, VkImage>)
    {
        return VK_OBJECT_TYPE_IMAGE;
    }
    else if constexpr (std::is_same_v<T, VkBuffer>)
    {
        return VK_OBJECT_TYPE_BUFFER;
    }
    else if constexpr (std::is_same_v<T, VkSampler>)
    {
        return VK_OBJECT_TYPE_SAMPLER;
    }
    else if constexpr (std::is_same_v<T, VkPipeline>)
    {
        return VK_OBJECT_TYPE_PIPELINE;
    }
    else if constexpr (std::is_same_v<T, VkPipelineLayout>)
    {
        return VK_OBJECT_TYPE_PIPELINE_LAYOUT;
    }
    else if constexpr (std::is_same_v<T, VkSemaphore>)
    {
        return VK_OBJECT_TYPE_SEMAPHORE;
    }
    else if constexpr (std::is_same_v<T, VkDescriptorPool>)
    {
        return VK_OBJECT_TYPE_DESCRIPTOR_POOL;
    }
    else if constexpr (std::is_same_v<T, VkDescriptorSetLayout>)
    {
        return VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT;
    }
    else
    {
        static_assert(sizeof(T) == 0, "Add the handle type here and to destroy_object() in VulkanCore.cpp.");
    }
}
}

// Destroys a Vulkan object once the GPU can't be using it anymore (frame_buffer_count frames
// from now), like d3d12::core::deferred_release(). Sets the handle to VK_NULL_HANDLE.
template <typename T>
auto deferred_release(T &handle) -> void
{
    if (handle != VK_NULL_HANDLE)
    {
        detail::deferred_release(detail::object_type<T>(), handle_to_u64(handle));
        handle = VK_NULL_HANDLE;
    }
}

// Same for a buffer or image and its VMA allocation (vmaDestroyBuffer/vmaDestroyImage).
template <typename T>
auto deferred_release(T &handle, VmaAllocation &allocation) -> void
{
    static_assert(std::is_same_v<T, VkBuffer> || std::is_same_v<T, VkImage>);
    assert((handle == VK_NULL_HANDLE) == (allocation == VK_NULL_HANDLE));
    if (handle != VK_NULL_HANDLE)
    {
        detail::deferred_release(detail::object_type<T>(), handle_to_u64(handle), allocation);
        handle = VK_NULL_HANDLE;
        allocation = VK_NULL_HANDLE;
    }
}

auto set_option(RendererOption::Option option, const void *const parameter, u32 parameter_size) -> void;
auto get_option(RendererOption::Option option, void *const parameter, u32 parameter_size) -> void;

[[nodiscard]] auto instance() -> VkInstance;
[[nodiscard]] auto physical_device() -> VkPhysicalDevice;
[[nodiscard]] auto device() -> VkDevice;
[[nodiscard]] auto graphics_queue() -> VkQueue;
// Thread-safe vkQueueSubmit2 on the graphics queue (render frames use the same queue and lock).
auto submit_to_graphics_queue(const VkSubmitInfo2 &submit) -> void;
[[nodiscard]] auto graphics_queue_family() -> u32;
[[nodiscard]] auto allocator() -> VmaAllocator;
// The bindless resource heap (counterpart of d3d12::core::srv_heap()).
[[nodiscard]] auto srv_heap() -> DescriptorHeap &;
// The current frame's constant buffer (cleared at the start of each frame).
[[nodiscard]] auto cbuffer() -> ConstantBuffer &;
// Alignment for the offset of a buffer range bound as a root SRV/UAV: max(16, minStorageBufferOffsetAlignment).
// D3D12 root SRVs only need 4 bytes, so data packed for D3D12 (mesh assets) is repacked with this alignment.
[[nodiscard]] auto storage_buffer_offset_alignment() -> u32;
// A zeroed storage buffer (256 KB) for root SRVs that have no buffer yet (D3D12 binds address 0 there).
[[nodiscard]] auto zero_buffer() -> vkx::BufferRange;
// The D3D12 static samplers as immutable samplers (vkx::root_parameter::sampler).
[[nodiscard]] auto static_sampler(vkx::StaticSampler::Type type) -> VkSampler;
[[nodiscard]] auto current_frame_index() -> u32;
auto set_deferred_releases_flag(u32 frame_idx) -> void;

// Secondary command buffers for recording in jobs. Each thread records into its own
// command pool per frame; a pool is reset when its frame starts again.
// - prepare_thread_command_pools(): called by the recording thread before it starts the jobs; creates the pools
//   the job system's threads need (jobs::worker_count() + one for a thread outside the pool).
// - begin_secondary(): from a job; returns a begun secondary command buffer of the calling thread's pool for the
//   current frame. The caller ends it.
auto prepare_thread_command_pools() -> void;
[[nodiscard]] auto begin_secondary(const VkCommandBufferInheritanceInfo &inheritance) -> VkCommandBuffer;
// PRIMAL_VULKAN_PARALLEL_RECORDING=0 turns parallel recording off (for measurements).
[[nodiscard]] auto parallel_recording_enabled() -> bool;

[[nodiscard]] auto vsync_enabled() -> bool;
// Names an object for validation messages and RenderDoc (no-op without VK_EXT_debug_utils).
auto set_object_name(u64 handle, VkObjectType type, const char *name) -> void;

[[nodiscard]] auto create_surface(platform::Window window) -> Surface;
auto remove_surface(SurfaceId id) -> void;
auto resize_surface(SurfaceId id, u32, u32) -> void;
[[nodiscard]] auto surface_width(SurfaceId id) -> u32;
[[nodiscard]] auto surface_height(SurfaceId id) -> u32;
auto render_surface(SurfaceId id, FrameInfo info) -> void;
}
