// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "VulkanCommonHeaders.h"

namespace primal::graphics::vulkan {

// A window's VkSurfaceKHR and swapchain (counterpart of d3d12_surface).
// NOTE: lives in a utl::free_list, which relocates its items bitwise: keep members trivially
//       relocatable (handles and fixed-size arrays, no self-pointers).
class vulkan_surface
{
public:
    // Upper bound for the number of swapchain images (we ask for 3, drivers may give more).
    constexpr static u32 max_image_count{ 8 };

    explicit vulkan_surface(platform::window window)
        : _window{ window }
    {
        assert(_window.handle());
    }
    DISABLE_COPY_AND_MOVE(vulkan_surface);
    ~vulkan_surface() { release(); }

    // Creates the VkSurfaceKHR from the window's native handle and the swapchain.
    bool create();
    // Recreates the swapchain for the window's current size and the vsync option.
    // NOTE: the caller must make sure the GPU is done with the swapchain images (core flushes).
    void resize();

    // Acquires the next swapchain image; image_acquired is signaled when it can be rendered to.
    // Returns false if there's nothing to render to (minimized window, or out of date and
    // recreating failed). The caller must not wait on image_acquired then.
    [[nodiscard]] bool acquire_next_image(VkSemaphore image_acquired);
    // Presents the acquired image once render_finished_semaphore() is signaled.
    void present(VkQueue queue);

    // True if the swapchain doesn't match the window or the present mode anymore.
    [[nodiscard]] bool needs_resize() const;

    [[nodiscard]] constexpr u32 width() const { return _extent.width; }
    [[nodiscard]] constexpr u32 height() const { return _extent.height; }
    [[nodiscard]] constexpr VkExtent2D extent() const { return _extent; }
    [[nodiscard]] constexpr VkFormat format() const { return _format; }
    [[nodiscard]] constexpr VkImage image() const { return _images[_image_index]; }
    [[nodiscard]] constexpr VkImageView image_view() const { return _image_views[_image_index]; }
    // Signaled by the frame's submit, waited on by present. One per image: a semaphore can only be
    // reused once the presentation engine is done with it, which we know when that image is acquired again.
    [[nodiscard]] constexpr VkSemaphore render_finished_semaphore() const { return _render_finished[_image_index]; }

private:
    bool create_swapchain();
    void release_swapchain_resources();
    void release();

    VkSurfaceKHR _surface{ VK_NULL_HANDLE };
    VkSwapchainKHR _swapchain{ VK_NULL_HANDLE };
    VkImage _images[max_image_count]{};
    VkImageView _image_views[max_image_count]{};
    VkSemaphore _render_finished[max_image_count]{};
    u32 _image_count{ 0 };
    u32 _image_index{ 0 };
    platform::window _window;
    VkExtent2D _extent{};
    VkFormat _format{ VK_FORMAT_UNDEFINED };
    VkColorSpaceKHR _color_space{ VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
    VkPresentModeKHR _present_mode{ VK_PRESENT_MODE_FIFO_KHR };
    bool _vsync{ true };
    bool _out_of_date{ false };
};
} // namespace primal::graphics::vulkan
