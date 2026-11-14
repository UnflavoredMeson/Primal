// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once

// The Vulkan functions the engine calls, loaded at run time. Nothing links the Vulkan loader: the library
// (vulkan-1.dll / libvulkan.so.1) is opened by loader::load_library(), and each function is fetched with
// vkGetInstanceProcAddr / vkGetDeviceProcAddr into a global pointer with the function's own name, so calls stay
// ::vkCreateBuffer(...). Device-level functions come from vkGetDeviceProcAddr and go straight to the driver (or the
// first layer), without the loader's trampoline.
//
// A function the engine starts to use goes in one of the lists below, by what its first parameter is:
// - global:   none (vkCreateInstance, vkEnumerateInstance*);
// - instance: VkInstance or VkPhysicalDevice (and vkGetDeviceProcAddr);
// - device:   VkDevice, VkQueue or VkCommandBuffer, incl. the required device extensions.
// FOR_VULKAN_DEBUG_UTILS_FUNCTIONS is loaded only when VK_EXT_debug_utils is enabled, and null otherwise: callers
// null-check them.
// The window-system surface functions (vkCreateXlibSurfaceKHR, ...) need platform types, so VulkanSurface.cpp
// fetches them itself with loader::instance_function().

#ifndef VK_NO_PROTOTYPES
#error "Build the Vulkan backend with VK_NO_PROTOTYPES (Engine/CMakeLists.txt, Engine/Vulkan.props)."
#endif
#include <vulkan/vulkan.h>

// NOLINTBEGIN(cppcoreguidelines-macro-usage)
#define FOR_VULKAN_GLOBAL_FUNCTIONS(DO)                                                                                \
    DO(PFN_vkEnumerateInstanceVersion, vkEnumerateInstanceVersion)                                                     \
    DO(PFN_vkEnumerateInstanceLayerProperties, vkEnumerateInstanceLayerProperties)                                     \
    DO(PFN_vkEnumerateInstanceExtensionProperties, vkEnumerateInstanceExtensionProperties)                             \
    DO(PFN_vkCreateInstance, vkCreateInstance)

#define FOR_VULKAN_INSTANCE_FUNCTIONS(DO)                                                                              \
    DO(PFN_vkDestroyInstance, vkDestroyInstance)                                                                       \
    DO(PFN_vkGetDeviceProcAddr, vkGetDeviceProcAddr)                                                                   \
    DO(PFN_vkEnumeratePhysicalDevices, vkEnumeratePhysicalDevices)                                                     \
    DO(PFN_vkGetPhysicalDeviceProperties, vkGetPhysicalDeviceProperties)                                               \
    DO(PFN_vkGetPhysicalDeviceFeatures2, vkGetPhysicalDeviceFeatures2)                                                 \
    DO(PFN_vkGetPhysicalDeviceFormatProperties, vkGetPhysicalDeviceFormatProperties)                                   \
    DO(PFN_vkGetPhysicalDeviceQueueFamilyProperties, vkGetPhysicalDeviceQueueFamilyProperties)                         \
    DO(PFN_vkEnumerateDeviceExtensionProperties, vkEnumerateDeviceExtensionProperties)                                 \
    DO(PFN_vkCreateDevice, vkCreateDevice)                                                                             \
    DO(PFN_vkDestroySurfaceKHR, vkDestroySurfaceKHR)                                                                   \
    DO(PFN_vkGetPhysicalDeviceSurfaceSupportKHR, vkGetPhysicalDeviceSurfaceSupportKHR)                                 \
    DO(PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR, vkGetPhysicalDeviceSurfaceCapabilitiesKHR)                       \
    DO(PFN_vkGetPhysicalDeviceSurfaceFormatsKHR, vkGetPhysicalDeviceSurfaceFormatsKHR)                                 \
    DO(PFN_vkGetPhysicalDeviceSurfacePresentModesKHR, vkGetPhysicalDeviceSurfacePresentModesKHR)

// VK_EXT_debug_utils (enabled in debug builds when it's there). vkSetDebugUtilsObjectNameEXT takes a VkDevice but
// belongs to an instance extension, so it's fetched from the instance.
#define FOR_VULKAN_DEBUG_UTILS_FUNCTIONS(DO)                                                                           \
    DO(PFN_vkCreateDebugUtilsMessengerEXT, vkCreateDebugUtilsMessengerEXT)                                             \
    DO(PFN_vkDestroyDebugUtilsMessengerEXT, vkDestroyDebugUtilsMessengerEXT)                                           \
    DO(PFN_vkSetDebugUtilsObjectNameEXT, vkSetDebugUtilsObjectNameEXT)

#define FOR_VULKAN_DEVICE_FUNCTIONS(DO)                                                                                \
    DO(PFN_vkDestroyDevice, vkDestroyDevice)                                                                           \
    DO(PFN_vkGetDeviceQueue, vkGetDeviceQueue)                                                                         \
    DO(PFN_vkQueueSubmit2, vkQueueSubmit2)                                                                             \
    DO(PFN_vkQueueWaitIdle, vkQueueWaitIdle)                                                                           \
    DO(PFN_vkCreateSemaphore, vkCreateSemaphore)                                                                       \
    DO(PFN_vkDestroySemaphore, vkDestroySemaphore)                                                                     \
    DO(PFN_vkGetSemaphoreCounterValue, vkGetSemaphoreCounterValue)                                                     \
    DO(PFN_vkWaitSemaphores, vkWaitSemaphores)                                                                         \
    DO(PFN_vkCreateCommandPool, vkCreateCommandPool)                                                                   \
    DO(PFN_vkDestroyCommandPool, vkDestroyCommandPool)                                                                 \
    DO(PFN_vkResetCommandPool, vkResetCommandPool)                                                                     \
    DO(PFN_vkAllocateCommandBuffers, vkAllocateCommandBuffers)                                                         \
    DO(PFN_vkBeginCommandBuffer, vkBeginCommandBuffer)                                                                 \
    DO(PFN_vkEndCommandBuffer, vkEndCommandBuffer)                                                                     \
    DO(PFN_vkDestroyBuffer, vkDestroyBuffer)                                                                           \
    DO(PFN_vkDestroyImage, vkDestroyImage)                                                                             \
    DO(PFN_vkCreateImageView, vkCreateImageView)                                                                       \
    DO(PFN_vkDestroyImageView, vkDestroyImageView)                                                                     \
    DO(PFN_vkCreateSampler, vkCreateSampler)                                                                           \
    DO(PFN_vkDestroySampler, vkDestroySampler)                                                                         \
    DO(PFN_vkCreateShaderModule, vkCreateShaderModule)                                                                 \
    DO(PFN_vkDestroyShaderModule, vkDestroyShaderModule)                                                               \
    DO(PFN_vkCreatePipelineLayout, vkCreatePipelineLayout)                                                             \
    DO(PFN_vkDestroyPipelineLayout, vkDestroyPipelineLayout)                                                           \
    DO(PFN_vkCreateGraphicsPipelines, vkCreateGraphicsPipelines)                                                       \
    DO(PFN_vkCreateComputePipelines, vkCreateComputePipelines)                                                         \
    DO(PFN_vkDestroyPipeline, vkDestroyPipeline)                                                                       \
    DO(PFN_vkCreateDescriptorSetLayout, vkCreateDescriptorSetLayout)                                                   \
    DO(PFN_vkDestroyDescriptorSetLayout, vkDestroyDescriptorSetLayout)                                                 \
    DO(PFN_vkGetDescriptorSetLayoutSupport, vkGetDescriptorSetLayoutSupport)                                           \
    DO(PFN_vkCreateDescriptorPool, vkCreateDescriptorPool)                                                             \
    DO(PFN_vkDestroyDescriptorPool, vkDestroyDescriptorPool)                                                           \
    DO(PFN_vkAllocateDescriptorSets, vkAllocateDescriptorSets)                                                         \
    DO(PFN_vkUpdateDescriptorSets, vkUpdateDescriptorSets)                                                             \
    DO(PFN_vkCmdPipelineBarrier2, vkCmdPipelineBarrier2)                                                               \
    DO(PFN_vkCmdBeginRendering, vkCmdBeginRendering)                                                                   \
    DO(PFN_vkCmdEndRendering, vkCmdEndRendering)                                                                       \
    DO(PFN_vkCmdExecuteCommands, vkCmdExecuteCommands)                                                                 \
    DO(PFN_vkCmdSetViewport, vkCmdSetViewport)                                                                         \
    DO(PFN_vkCmdSetScissor, vkCmdSetScissor)                                                                           \
    DO(PFN_vkCmdBindPipeline, vkCmdBindPipeline)                                                                       \
    DO(PFN_vkCmdBindDescriptorSets, vkCmdBindDescriptorSets)                                                           \
    DO(PFN_vkCmdBindIndexBuffer, vkCmdBindIndexBuffer)                                                                 \
    DO(PFN_vkCmdDraw, vkCmdDraw)                                                                                       \
    DO(PFN_vkCmdDrawIndexed, vkCmdDrawIndexed)                                                                         \
    DO(PFN_vkCmdDispatch, vkCmdDispatch)                                                                               \
    DO(PFN_vkCmdCopyBuffer, vkCmdCopyBuffer)                                                                           \
    DO(PFN_vkCmdCopyBufferToImage, vkCmdCopyBufferToImage)                                                             \
    DO(PFN_vkCmdFillBuffer, vkCmdFillBuffer)                                                                           \
    /* VK_KHR_push_descriptor */                                                                                       \
    DO(PFN_vkCmdPushDescriptorSetKHR, vkCmdPushDescriptorSetKHR)                                                       \
    /* VK_KHR_swapchain */                                                                                             \
    DO(PFN_vkCreateSwapchainKHR, vkCreateSwapchainKHR)                                                                 \
    DO(PFN_vkDestroySwapchainKHR, vkDestroySwapchainKHR)                                                               \
    DO(PFN_vkGetSwapchainImagesKHR, vkGetSwapchainImagesKHR)                                                           \
    DO(PFN_vkAcquireNextImageKHR, vkAcquireNextImageKHR)                                                               \
    DO(PFN_vkQueuePresentKHR, vkQueuePresentKHR)
// NOLINTEND(cppcoreguidelines-macro-usage)

// The pointers, in the global namespace like the prototypes VK_NO_PROTOTYPES leaves out. Set only while the renderer
// starts and shuts down; every other thread just reads them.
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
#define PRIMAL_VK_DECLARE_FUNCTION(type, name) inline type name{nullptr};
inline PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr{nullptr};
FOR_VULKAN_GLOBAL_FUNCTIONS(PRIMAL_VK_DECLARE_FUNCTION)
FOR_VULKAN_INSTANCE_FUNCTIONS(PRIMAL_VK_DECLARE_FUNCTION)
FOR_VULKAN_DEBUG_UTILS_FUNCTIONS(PRIMAL_VK_DECLARE_FUNCTION)
FOR_VULKAN_DEVICE_FUNCTIONS(PRIMAL_VK_DECLARE_FUNCTION)
#undef PRIMAL_VK_DECLARE_FUNCTION
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

namespace primal::graphics::vulkan::loader
{
// Opens the Vulkan library and loads vkGetInstanceProcAddr and the global functions. False (and logged) when the
// library isn't there, e.g. on a D3D12-only machine. The library stays open until the process ends: unloading it
// while drivers and layers may still have threads or atexit handlers isn't worth the trouble.
[[nodiscard]] auto load_library() -> bool;
// Loads the instance-level functions (after vkCreateInstance). False when a required one is missing; every missing
// one is logged by name.
[[nodiscard]] auto load_instance_functions(VkInstance instance) -> bool;
// Loads the VK_EXT_debug_utils functions. Call it only when the instance was created with the extension: for one
// that isn't enabled, vkGetInstanceProcAddr may still return a pointer that mustn't be called.
auto load_debug_utils_functions(VkInstance instance) -> void;
// Loads the device-level functions (after vkCreateDevice).
[[nodiscard]] auto load_device_functions(VkDevice device) -> bool;
// Clears every instance- and device-level pointer (after vkDestroyInstance), so a call after shutdown crashes
// at once instead of calling into a destroyed instance.
auto reset_functions() -> void;

// An instance-level function that isn't in the lists above (null when it's missing).
template <typename T>
[[nodiscard]] auto instance_function(VkInstance instance, const char *name) -> T
{
    return reinterpret_cast<T>(::vkGetInstanceProcAddr(instance, name));
}
}
