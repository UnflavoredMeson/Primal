// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "VulkanSurface.h"
#include "VulkanCore.h"
#include "Platform/PlatformTypes.h"
#include <algorithm>

#if PRIMAL_PLATFORM_LINUX
// Window-system surface extensions. Declare the few X11/Wayland types the Vulkan headers need
// instead of including Xlib.h / wayland-client.h: Xlib.h defines macros like None, Bool, Status
// and Always. (Same definitions as Xlib.h, so including both in one file would still compile.)
// NOLINTBEGIN(modernize-use-using, readability-identifier-naming): spelled like Xlib.h
typedef struct _XDisplay Display;
typedef unsigned long XID;
typedef XID Window;
typedef unsigned long VisualID;
// NOLINTEND(modernize-use-using, readability-identifier-naming)
#include <vulkan/vulkan_xlib.h>
struct wl_display;
struct wl_surface;
#include <vulkan/vulkan_wayland.h>
#endif

namespace primal::graphics::vulkan {
namespace {

// Preferred swapchain formats, in order. Rendering is in linear space; *_SRGB swapchain images
// encode to sRGB when written (d3d12 uses an FP16 scRGB back buffer instead).
constexpr VkSurfaceFormatKHR preferred_formats[]{
    { VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR },
    { VK_FORMAT_R8G8B8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR },
};

// Number of swapchain images to ask for: one being presented, one queued, one to render to.
constexpr u32 desired_image_count{ 3 };

VkSurfaceFormatKHR
choose_surface_format(VkPhysicalDevice physical_device, VkSurfaceKHR surface)
{
    u32 count{ 0 };
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &count, nullptr);
    utl::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &count, formats.data());
    assert(count);
    if (!count) return {};

    for (const auto&[preferred_format, color_space] : preferred_formats)
    {
        for (const VkSurfaceFormatKHR& format : formats)
        {
            if (format.format == preferred_format && format.colorSpace == color_space) return format;
        }
    }

    return formats[0];
}

// Vsync: FIFO (always supported). Otherwise mailbox (no tearing, newest frame wins) or
// immediate (tearing, like DXGI_PRESENT_ALLOW_TEARING), falling back to FIFO.
VkPresentModeKHR
choose_present_mode(VkPhysicalDevice physical_device, VkSurfaceKHR surface, bool vsync)
{
    if (vsync) return VK_PRESENT_MODE_FIFO_KHR;

    u32 count{ 0 };
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &count, nullptr);
    utl::vector<VkPresentModeKHR> modes(count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &count, modes.data());

    constexpr VkPresentModeKHR preferred_modes[]{ VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR };
    for (const VkPresentModeKHR preferred : preferred_modes)
    {
        for (const VkPresentModeKHR mode : modes)
        {
            if (mode == preferred) return mode;
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

VkSurfaceKHR
create_platform_surface(const platform::native_window_handle& native)
{
    VkSurfaceKHR surface{ VK_NULL_HANDLE };
    switch (native.kind)
    {
#if PRIMAL_PLATFORM_LINUX
    case platform::native_window_handle::x11: {
        VkXlibSurfaceCreateInfoKHR info{ VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR };
        info.dpy = (Display*)native.display;
        info.window = (Window)native.window;
        VkCall(vkCreateXlibSurfaceKHR(core::instance(), &info, nullptr, &surface));
    }
    break;
    case platform::native_window_handle::wayland: {
        VkWaylandSurfaceCreateInfoKHR info{ VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR };
        info.display = (wl_display*)native.display;
        info.surface = (wl_surface*)native.window;
        VkCall(vkCreateWaylandSurfaceKHR(core::instance(), &info, nullptr, &surface));
    }
    break;
#endif
    // TODO: win32 (VK_KHR_win32_surface) once the Vulkan backend is built on Windows.
    default:
        std::fprintf(stderr, "Vulkan: unsupported native window kind %u\n", (u32)native.kind);
        break;
    }

    return surface;
}

} // anonymous namespace

bool
vulkan_surface::create()
{
    assert(_surface == VK_NULL_HANDLE);
    const platform::native_window_handle* const native{ _window.handle() };
    assert(native);

    // The platform layer must stop drawing to the window before a swapchain presents to it.
    _window.set_has_graphics_surface(true);

    _surface = create_platform_surface(*native);
    if (_surface == VK_NULL_HANDLE)
    {
        _window.set_has_graphics_surface(false);
        return false;
    }

    VkBool32 is_supported{ VK_FALSE };
    VkCall(vkGetPhysicalDeviceSurfaceSupportKHR(core::physical_device(), core::graphics_queue_family(), _surface, &is_supported));
    if (!is_supported)
    {
        std::fprintf(stderr, "Vulkan: the graphics queue can't present to this window.\n");
        return false;
    }

    return create_swapchain();
}

void
vulkan_surface::resize()
{
    assert(_surface);
    create_swapchain();
}

bool
vulkan_surface::needs_resize() const
{
    if (_out_of_date || _vsync != core::vsync_enabled()) return true;
    const math::u32v4 size{ _window.size() };
    return size.z - size.x != _extent.width || size.w - size.y != _extent.height;
}

bool
vulkan_surface::create_swapchain()
{
    const VkPhysicalDevice physical_device{ core::physical_device() };
    const VkDevice device{ core::device() };

    VkSurfaceCapabilitiesKHR caps{};
    if (!VkCall(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, _surface, &caps))) return false;

    // X11 reports the window size. Wayland reports 0xFFFFFFFF: the swapchain decides the size,
    // so use the window's size in pixels.
    VkExtent2D extent{ caps.currentExtent };
    if (extent.width == u32_invalid_id)
    {
        extent.width = std::clamp(_window.width(), caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = std::clamp(_window.height(), caps.minImageExtent.height, caps.maxImageExtent.height);
    }

    _vsync = core::vsync_enabled();
    _out_of_date = false;

    if (extent.width == 0 || extent.height == 0)
    {
        // Minimized. Keep the old swapchain (if any) and don't render until the window is visible.
        _extent = {};
        return true;
    }

    const VkSurfaceFormatKHR surface_format{ choose_surface_format(physical_device, _surface) };
    const VkPresentModeKHR present_mode{ choose_present_mode(physical_device, _surface, _vsync) };

    u32 image_count{ std::max(desired_image_count, caps.minImageCount) };
    if (caps.maxImageCount) image_count = std::min(image_count, caps.maxImageCount);
    image_count = std::min(image_count, max_image_count);

    VkCompositeAlphaFlagBitsKHR composite_alpha{ VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR };
    if (!(caps.supportedCompositeAlpha & composite_alpha))
    {
        composite_alpha = (VkCompositeAlphaFlagBitsKHR)(caps.supportedCompositeAlpha & ~(caps.supportedCompositeAlpha - 1)); // lowest set bit
    }

    VkSwapchainCreateInfoKHR info{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    info.surface = _surface;
    info.minImageCount = image_count;
    info.imageFormat = surface_format.format;
    info.imageColorSpace = surface_format.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = composite_alpha;
    info.presentMode = present_mode;
    info.clipped = VK_TRUE;
    info.oldSwapchain = _swapchain;

    VkSwapchainKHR swapchain{ VK_NULL_HANDLE };
    if (!VkCall(vkCreateSwapchainKHR(device, &info, nullptr, &swapchain))) return false;

    // NOTE: the caller flushed the GPU, so the old swapchain and its views can go right away.
    release_swapchain_resources();
    if (_swapchain) vkDestroySwapchainKHR(device, _swapchain, nullptr);
    _swapchain = swapchain;

    _format = surface_format.format;
    _color_space = surface_format.colorSpace;
    _present_mode = present_mode;
    _extent = extent;

    _image_count = 0;
    VkCall(vkGetSwapchainImagesKHR(device, _swapchain, &_image_count, nullptr));
    assert(_image_count <= max_image_count);
    _image_count = std::min(_image_count, max_image_count);
    VkCall(vkGetSwapchainImagesKHR(device, _swapchain, &_image_count, &_images[0]));

    for (u32 i{ 0 }; i < _image_count; ++i)
    {
        VkImageViewCreateInfo view_info{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        view_info.image = _images[i];
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = _format;
        view_info.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        VkCall(vkCreateImageView(device, &view_info, nullptr, &_image_views[i]));
        NAME_VK_OBJECT_INDEXED(_images[i], VK_OBJECT_TYPE_IMAGE, i, "Swapchain Image");
        NAME_VK_OBJECT_INDEXED(_image_views[i], VK_OBJECT_TYPE_IMAGE_VIEW, i, "Swapchain Image View");

        VkSemaphoreCreateInfo semaphore_info{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        VkCall(vkCreateSemaphore(device, &semaphore_info, nullptr, &_render_finished[i]));
        NAME_VK_OBJECT_INDEXED(_render_finished[i], VK_OBJECT_TYPE_SEMAPHORE, i, "Render Finished Semaphore");
    }

    _image_index = 0;
    DEBUG_OP(std::fprintf(stderr, "::Vulkan swapchain created: %u x %u, %u images, format %d, present mode %d\n",
                          _extent.width, _extent.height, _image_count, (s32)_format, (s32)_present_mode));
    return true;
}

bool
vulkan_surface::acquire_next_image(VkSemaphore image_acquired)
{
    if (!_swapchain || !_extent.width || !_extent.height) return false;

    const VkResult result{ vkAcquireNextImageKHR(core::device(), _swapchain, UINT64_MAX, image_acquired, VK_NULL_HANDLE, &_image_index) };
    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        // Not acquired, the semaphore won't be signaled. Recreate before the next frame.
        _out_of_date = true;
        return false;
    }
    if (result == VK_SUBOPTIMAL_KHR)
    {
        // Acquired (the semaphore will be signaled), so render and present this one.
        _out_of_date = true;
        return true;
    }

    return VkCall(result);
}

void
vulkan_surface::present(VkQueue queue)
{
    VkPresentInfoKHR info{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &_render_finished[_image_index];
    info.swapchainCount = 1;
    info.pSwapchains = &_swapchain;
    info.pImageIndices = &_image_index;

    const VkResult result{ vkQueuePresentKHR(queue, &info) };
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        _out_of_date = true;
    }
    else
    {
        VkCall(result);
    }
}

void
vulkan_surface::release_swapchain_resources()
{
    const VkDevice device{ core::device() };
    for (u32 i{ 0 }; i < max_image_count; ++i)
    {
        if (_image_views[i]) vkDestroyImageView(device, _image_views[i], nullptr);
        if (_render_finished[i]) vkDestroySemaphore(device, _render_finished[i], nullptr);
        _image_views[i] = VK_NULL_HANDLE;
        _render_finished[i] = VK_NULL_HANDLE;
        _images[i] = VK_NULL_HANDLE; // owned by the swapchain
    }
    _image_count = 0;
    _image_index = 0;
}

// NOTE: core::remove_surface() flushes the GPU first, so everything can be destroyed right away.
//       The window must still exist (remove the surface before the window).
void
vulkan_surface::release()
{
    if (_surface == VK_NULL_HANDLE) return;

    release_swapchain_resources();
    if (_swapchain) vkDestroySwapchainKHR(core::device(), _swapchain, nullptr);
    vkDestroySurfaceKHR(core::instance(), _surface, nullptr);
    _swapchain = VK_NULL_HANDLE;
    _surface = VK_NULL_HANDLE;
    _extent = {};

    _window.set_has_graphics_surface(false);
}

} // namespace primal::graphics::vulkan
