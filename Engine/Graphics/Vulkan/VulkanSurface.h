// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "VulkanCommonHeaders.h"

namespace primal::graphics::vulkan
{

// A window's VkSurfaceKHR and swapchain (counterpart of d3d12_surface).
// NOTE: lives in a utl::free_list, which relocates its items bitwise: keep members trivially
//       relocatable (handles and fixed-size arrays, no self-pointers).
class VulkanSurface
{
  public:
    // Upper bound for the number of swapchain images (we ask for 3, drivers may give more).
    constexpr static u32 max_image_count{8};

    explicit VulkanSurface(platform::Window window)
        : _window{window}
    {
        assert(_window.handle());
    }
    DISABLE_COPY_AND_MOVE(VulkanSurface);
    ~VulkanSurface()
    {
        release();
    }

    // Creates the VkSurfaceKHR from the window's native handle and the swapchain.
    auto create() -> bool;
    // Recreates the swapchain for the window's current size and the vsync option.
    // NOTE: the caller must make sure the GPU is done with the swapchain images (core flushes).
    auto resize() -> void;

    // Acquires the next swapchain image; image_acquired is signaled when it can be rendered to.
    // Returns false if there's nothing to render to (minimized window, or out of date and
    // recreating failed). The caller must not wait on image_acquired then.
    [[nodiscard]] auto acquire_next_image(VkSemaphore image_acquired) -> bool;
    // Presents the acquired image once render_finished_semaphore() is signaled.
    auto present(VkQueue queue) -> void;

    // True if the swapchain doesn't match the window or the present mode anymore.
    [[nodiscard]] auto needs_resize() const -> bool;

    [[nodiscard]] constexpr auto width() const -> u32
    {
        return _extent.width;
    }
    [[nodiscard]] constexpr auto height() const -> u32
    {
        return _extent.height;
    }
    [[nodiscard]] constexpr auto extent() const -> VkExtent2D
    {
        return _extent;
    }
    [[nodiscard]] constexpr auto format() const -> VkFormat
    {
        return _format;
    }
    // The surface's light culler (delight), like d3d12_surface.
    [[nodiscard]] constexpr auto light_culling_id() const -> id::id_type
    {
        return _light_culling_id;
    }
    [[nodiscard]] constexpr auto image() const -> VkImage
    {
        return _images[_image_index];
    }
    [[nodiscard]] constexpr auto image_view() const -> VkImageView
    {
        return _image_views[_image_index];
    }
    // Signaled by the frame's submit, waited on by present. One per image: a semaphore can only be
    // reused once the presentation engine is done with it, which we know when that image is acquired again.
    [[nodiscard]] constexpr auto render_finished_semaphore() const -> VkSemaphore
    {
        return _render_finished[_image_index];
    }

  private:
    auto create_swapchain() -> bool;
    auto release_swapchain_resources() -> void;
    auto release() -> void;

    VkSurfaceKHR _surface{VK_NULL_HANDLE};
    VkSwapchainKHR _swapchain{VK_NULL_HANDLE};
    VkImage _images[max_image_count]{};
    VkImageView _image_views[max_image_count]{};
    VkSemaphore _render_finished[max_image_count]{};
    u32 _image_count{0};
    u32 _image_index{0};
    platform::Window _window;
    VkExtent2D _extent{};
    VkFormat _format{VK_FORMAT_UNDEFINED};
    VkColorSpaceKHR _color_space{VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    VkPresentModeKHR _present_mode{VK_PRESENT_MODE_FIFO_KHR};
    id::id_type _light_culling_id{id::invalid_id};
    bool _vsync{true};
    bool _out_of_date{false};
};
}
