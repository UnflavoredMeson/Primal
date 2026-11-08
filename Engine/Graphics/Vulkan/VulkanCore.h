// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "VulkanCommonHeaders.h"
#include "VulkanHelpers.h"
#include <type_traits>

namespace primal::graphics::vulkan {
class descriptor_heap;
class constant_buffer;
} // namespace primal::graphics::vulkan

namespace primal::graphics::vulkan::core {

bool initialize();
void shutdown();

namespace detail {
// allocation: the VMA allocation of a buffer or image (destroyed together), or nullptr.
void deferred_release(VkObjectType type, u64 handle, VmaAllocation allocation = VK_NULL_HANDLE);

// Maps a Vulkan handle type to its VkObjectType. (64-bit handles are distinct pointer types.)
template<typename T>
consteval VkObjectType
object_type()
{
    if constexpr (std::is_same_v<T, VkSwapchainKHR>)
        return VK_OBJECT_TYPE_SWAPCHAIN_KHR;
    else if constexpr (std::is_same_v<T, VkSurfaceKHR>)
        return VK_OBJECT_TYPE_SURFACE_KHR;
    else if constexpr (std::is_same_v<T, VkImageView>)
        return VK_OBJECT_TYPE_IMAGE_VIEW;
    else if constexpr (std::is_same_v<T, VkImage>)
        return VK_OBJECT_TYPE_IMAGE;
    else if constexpr (std::is_same_v<T, VkBuffer>)
        return VK_OBJECT_TYPE_BUFFER;
    else if constexpr (std::is_same_v<T, VkSampler>)
        return VK_OBJECT_TYPE_SAMPLER;
    else if constexpr (std::is_same_v<T, VkPipeline>)
        return VK_OBJECT_TYPE_PIPELINE;
    else if constexpr (std::is_same_v<T, VkPipelineLayout>)
        return VK_OBJECT_TYPE_PIPELINE_LAYOUT;
    else if constexpr (std::is_same_v<T, VkSemaphore>)
        return VK_OBJECT_TYPE_SEMAPHORE;
    else if constexpr (std::is_same_v<T, VkDescriptorPool>)
        return VK_OBJECT_TYPE_DESCRIPTOR_POOL;
    else if constexpr (std::is_same_v<T, VkDescriptorSetLayout>)
        return VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT;
    else
        static_assert(sizeof(T) == 0, "Add the handle type here and to destroy_object() in VulkanCore.cpp.");
}
} // namespace detail

// Destroys a Vulkan object once the GPU can't be using it anymore (frame_buffer_count frames
// from now), like d3d12::core::deferred_release(). Sets the handle to VK_NULL_HANDLE.
template<typename T>
void
deferred_release(T& handle)
{
    if (handle != VK_NULL_HANDLE)
    {
        detail::deferred_release(detail::object_type<T>(), (u64)handle);
        handle = VK_NULL_HANDLE;
    }
}

// Same for a buffer or image and its VMA allocation (vmaDestroyBuffer/vmaDestroyImage).
template<typename T>
void
deferred_release(T& handle, VmaAllocation& allocation)
{
    static_assert(std::is_same_v<T, VkBuffer> || std::is_same_v<T, VkImage>);
    assert((handle == VK_NULL_HANDLE) == (allocation == VK_NULL_HANDLE));
    if (handle != VK_NULL_HANDLE)
    {
        detail::deferred_release(detail::object_type<T>(), (u64)handle, allocation);
        handle = VK_NULL_HANDLE;
        allocation = VK_NULL_HANDLE;
    }
}

void set_option(renderer_option::option option, const void* const parameter, u32 parameter_size);
void get_option(renderer_option::option option, void* const parameter, u32 parameter_size);

[[nodiscard]] VkInstance instance();
[[nodiscard]] VkPhysicalDevice physical_device();
[[nodiscard]] VkDevice device();
[[nodiscard]] VkQueue graphics_queue();
[[nodiscard]] u32 graphics_queue_family();
[[nodiscard]] VmaAllocator allocator();
// The bindless resource heap (counterpart of d3d12::core::srv_heap()).
[[nodiscard]] descriptor_heap& srv_heap();
// The current frame's constant buffer (cleared at the start of each frame).
[[nodiscard]] constant_buffer& cbuffer();
// The D3D12 static samplers as immutable samplers (vkx::root_parameter::sampler).
[[nodiscard]] VkSampler static_sampler(vkx::static_sampler::type type);
[[nodiscard]] u32 current_frame_index();
void set_deferred_releases_flag(u32 frame_idx);

[[nodiscard]] bool vsync_enabled();
// Names an object for validation messages and RenderDoc (no-op without VK_EXT_debug_utils).
void set_object_name(u64 handle, VkObjectType type, const char* name);

[[nodiscard]] surface create_surface(platform::window window);
void remove_surface(surface_id id);
void resize_surface(surface_id id, u32, u32);
[[nodiscard]] u32 surface_width(surface_id id);
[[nodiscard]] u32 surface_height(surface_id id);
void render_surface(surface_id id, frame_info info);
} // namespace primal::graphics::vulkan::core
