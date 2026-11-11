// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "VulkanCore.h"

#include "Graphics/Shaders/SharedTypes.h"
#include "VulkanCamera.h"
#include "VulkanContent.h"
#include "VulkanGPass.h"
#include "VulkanHelpers.h"
#include "VulkanLight.h"
#include "VulkanLightCulling.h"
#include "VulkanPostProcess.h"
#include "VulkanResources.h"
#include "VulkanShaders.h"
#include "VulkanSurface.h"
#include "VulkanUpload.h"

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdlib>

namespace primal::graphics::vulkan
{

auto log_message(const char *format, ...) -> void // NOLINT(modernize-avoid-variadic-functions): printf wrapper
{
    char buffer[1024];
    std::va_list args;
    va_start(args, format);
    std::va_list args_copy;
    va_copy(args_copy, args);
    const int length{std::vsnprintf(buffer, sizeof(buffer), format, args)};
    va_end(args);

    const char *message{buffer};
    std::unique_ptr<char[]> long_message;
    if (length >= static_cast<int>(sizeof(buffer)))
    {
        // Validation messages can be longer than the stack buffer.
        long_message = std::make_unique<char[]>(static_cast<size_t>(length) + 1);
        std::vsnprintf(long_message.get(), static_cast<size_t>(length) + 1, format, args_copy);
        message = long_message.get();
    }
    va_end(args_copy);
    if (length < 0)
    {
        return;
    }

    std::fputs(message, stderr);
#if PRIMAL_PLATFORM_WINDOWS
    ::OutputDebugStringA(message);
#endif
}

}

namespace primal::graphics::vulkan::core
{
namespace
{

// Graphics queue + per-frame command buffers, synchronized with a timeline semaphore
// (counterpart of d3d12_command; the timeline semaphore works like an ID3D12Fence).
class VulkanCommand
{
  public:
    VulkanCommand() = default;
    DISABLE_COPY_AND_MOVE(VulkanCommand);
    ~VulkanCommand()
    {
        assert(!_queue && !_fence);
    }

    auto initialize(VkDevice device, u32 queue_family) -> bool
    {
        _device = device;
        _queue_family = queue_family;
        ::vkGetDeviceQueue(device, queue_family, 0, &_queue);
        NAME_VK_OBJECT(_queue, VK_OBJECT_TYPE_QUEUE, "GFX Queue");

        VkSemaphoreTypeCreateInfo timeline_info{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
        timeline_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        timeline_info.initialValue = 0;
        VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        semaphore_info.pNext = &timeline_info;
        if (!VkCall(::vkCreateSemaphore(device, &semaphore_info, nullptr, &_fence)))
        {
            return false;
        }
        NAME_VK_OBJECT(_fence, VK_OBJECT_TYPE_SEMAPHORE, "GFX Frame Fence (timeline)");

        for (u32 i{0}; i < frame_buffer_count; ++i)
        {
            CommandFrame &frame{_cmd_frames[i]};

            VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            pool_info.queueFamilyIndex = queue_family;
            if (!VkCall(::vkCreateCommandPool(device, &pool_info, nullptr, &frame.cmd_pool)))
            {
                return false;
            }
            NAME_VK_OBJECT_INDEXED(frame.cmd_pool, VK_OBJECT_TYPE_COMMAND_POOL, i, "GFX Command Pool");

            VkCommandBufferAllocateInfo alloc_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            alloc_info.commandPool = frame.cmd_pool;
            alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            alloc_info.commandBufferCount = 1;
            if (!VkCall(::vkAllocateCommandBuffers(device, &alloc_info, &frame.cmd_buffer)))
            {
                return false;
            }
            NAME_VK_OBJECT_INDEXED(frame.cmd_buffer, VK_OBJECT_TYPE_COMMAND_BUFFER, i, "GFX Command Buffer");

            const VkSemaphoreCreateInfo binary_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            if (!VkCall(::vkCreateSemaphore(device, &binary_info, nullptr, &frame.image_acquired)))
            {
                return false;
            }
            NAME_VK_OBJECT_INDEXED(frame.image_acquired, VK_OBJECT_TYPE_SEMAPHORE, i, "Image Acquired Semaphore");
        }

        return true;
    }

    // Wait for the current frame to be signaled, then reset and begin its command buffer.
    auto begin_frame() -> VkCommandBuffer
    {
        const CommandFrame &frame{_cmd_frames[_frame_index]};
        frame.wait(_device, _fence);
        VkCall(::vkResetCommandPool(_device, frame.cmd_pool, 0));

        VkCommandBufferBeginInfo begin_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VkCall(::vkBeginCommandBuffer(frame.cmd_buffer, &begin_info));
        return frame.cmd_buffer;
    }

    // Submit the frame's command buffer (after the swapchain image was acquired), present,
    // and signal the timeline semaphore with the new fence value.
    auto end_frame(VulkanSurface &surface) -> void
    {
        CommandFrame &frame{_cmd_frames[_frame_index]};
        VkCall(::vkEndCommandBuffer(frame.cmd_buffer));

        const u64 fence_value{++_fence_value};

        VkSemaphoreSubmitInfo wait_info{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        wait_info.semaphore = frame.image_acquired;
        // Everything before writing to the swapchain image may start before it's acquired.
        wait_info.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

        VkSemaphoreSubmitInfo signal_infos[2]{};
        signal_infos[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        signal_infos[0].semaphore = surface.render_finished_semaphore();
        signal_infos[0].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        signal_infos[1].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        signal_infos[1].semaphore = _fence;
        signal_infos[1].value = fence_value;
        signal_infos[1].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

        VkCommandBufferSubmitInfo cmd_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        cmd_info.commandBuffer = frame.cmd_buffer;

        VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submit.waitSemaphoreInfoCount = 1;
        submit.pWaitSemaphoreInfos = &wait_info;
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &cmd_info;
        submit.signalSemaphoreInfoCount = static_cast<u32>(std::size(signal_infos));
        submit.pSignalSemaphoreInfos = &signal_infos[0];
        {
            const std::scoped_lock lock{_queue_mutex};
            VkCall(::vkQueueSubmit2(_queue, 1, &submit, VK_NULL_HANDLE));

            // Presenting swap chain images happens in lockstep with frame buffers.
            surface.present(_queue);
        }

        frame.fence_value = fence_value;
        _frame_index = (_frame_index + 1) % frame_buffer_count;
    }

    // Wait until the GPU is done with all submitted frames, and the queue with its presents.
    // NOTE: the frame fences don't cover a present's wait on the render finished semaphore.
    //       Without VK_EXT_swapchain_maintenance1 (present fences), waiting for the queue to be
    //       idle is the only way to know the semaphores and swapchain images aren't in use anymore,
    //       which is needed before recreating or destroying a swapchain.
    auto flush() -> void
    {
        for (u32 i{0}; i < frame_buffer_count; ++i)
        {
            _cmd_frames[i].wait(_device, _fence);
        }
        if (_queue)
        {
            const std::scoped_lock lock{_queue_mutex};
            VkCall(::vkQueueWaitIdle(_queue));
        }
        _frame_index = 0;
    }

    auto release() -> void
    {
        if (!_device)
        {
            return;
        }
        flush();

        for (u32 i{0}; i < frame_buffer_count; ++i)
        {
            _cmd_frames[i].release(_device);
        }

        if (_fence)
        {
            ::vkDestroySemaphore(_device, _fence, nullptr);
        }
        _fence = VK_NULL_HANDLE;
        _fence_value = 0;
        _queue = VK_NULL_HANDLE;
        _device = VK_NULL_HANDLE;
    }

    // For other modules (upload) that submit to the same queue, possibly from other threads.
    // NOTE: a VkQueue must be externally synchronized (submits, presents, wait idle).
    auto submit(const VkSubmitInfo2 &submit) -> void
    {
        const std::scoped_lock lock{_queue_mutex};
        VkCall(::vkQueueSubmit2(_queue, 1, &submit, VK_NULL_HANDLE));
    }

    [[nodiscard]] constexpr auto queue() const -> VkQueue
    {
        return _queue;
    }
    [[nodiscard]] constexpr auto queue_family() const -> u32
    {
        return _queue_family;
    }
    [[nodiscard]] constexpr auto frame_index() const -> u32
    {
        return _frame_index;
    }
    [[nodiscard]] constexpr auto image_acquired_semaphore() const -> VkSemaphore
    {
        return _cmd_frames[_frame_index].image_acquired;
    }

  private:
    struct CommandFrame
    {
        VkCommandPool cmd_pool{VK_NULL_HANDLE};
        VkCommandBuffer cmd_buffer{VK_NULL_HANDLE};
        // Signaled by vkAcquireNextImageKHR. Reusable once this frame's fence value is reached,
        // because the frame's submit waited on it.
        VkSemaphore image_acquired{VK_NULL_HANDLE};
        u64 fence_value{0};

        auto wait(VkDevice device, VkSemaphore fence) const -> void
        {
            assert(device && fence);
            u64 completed{0};
            VkCall(::vkGetSemaphoreCounterValue(device, fence, &completed));
            // If the timeline value is still less than "fence_value", the GPU hasn't finished
            // executing this frame's command buffer yet.
            if (completed < fence_value)
            {
                VkSemaphoreWaitInfo wait_info{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
                wait_info.semaphoreCount = 1;
                wait_info.pSemaphores = &fence;
                wait_info.pValues = &fence_value;
                VkCall(::vkWaitSemaphores(device, &wait_info, UINT64_MAX));
            }
        }

        auto release(VkDevice device) -> void
        {
            // Destroying the pool frees its command buffers.
            if (cmd_pool)
            {
                ::vkDestroyCommandPool(device, cmd_pool, nullptr);
            }
            if (image_acquired)
            {
                ::vkDestroySemaphore(device, image_acquired, nullptr);
            }
            cmd_pool = VK_NULL_HANDLE;
            cmd_buffer = VK_NULL_HANDLE;
            image_acquired = VK_NULL_HANDLE;
            fence_value = 0;
        }
    };

    VkDevice _device{VK_NULL_HANDLE};
    VkQueue _queue{VK_NULL_HANDLE};
    std::mutex _queue_mutex;
    u32 _queue_family{u32_invalid_id};
    VkSemaphore _fence{VK_NULL_HANDLE};
    u64 _fence_value{0};
    CommandFrame _cmd_frames[frame_buffer_count]{};
    u32 _frame_index{0};
};

struct DeferredObject
{
    VkObjectType type;
    u64 handle;
    VmaAllocation allocation;
};

using surface_collection = utl::free_list<VulkanSurface>;

constexpr const char *validation_layer_name{"VK_LAYER_KHRONOS_validation"};

VkInstance vk_instance{VK_NULL_HANDLE};
VkDebugUtilsMessengerEXT debug_messenger{VK_NULL_HANDLE};
VkPhysicalDevice main_physical_device{VK_NULL_HANDLE};
VkDevice main_device{VK_NULL_HANDLE};
VulkanCommand gfx_command;
surface_collection surfaces;
VmaAllocator vma_allocator{VK_NULL_HANDLE};
VulkanBuffer zero_buffer_object{};
u32 storage_buffer_alignment{16}; // max(16, minStorageBufferOffsetAlignment), set by create_device()
DescriptorHeap srv_desc_heap;
ConstantBuffer constant_buffers[frame_buffer_count];
VkSampler static_samplers[vkx::StaticSampler::count]{};
vkx::VulkanBarrier resource_barriers{};

// Same sizes as the D3D12 backend.
constexpr u32 srv_heap_capacity{4096};
constexpr u32 constant_buffer_size{1024 * 1024};

PFN_vkSetDebugUtilsObjectNameEXT set_debug_utils_object_name{nullptr};

utl::vector<DeferredObject> deferred_releases[frame_buffer_count]{};
u32 deferred_releases_flag[frame_buffer_count]{};
std::mutex deferred_releases_mutex{};

// Validation messages since the last initialize() (validation is only on in debug builds). The error count is
// printed at shutdown; both stay readable afterwards through vulkan::validation_stats(). Atomic: the layer calls
// debug_callback() from whichever thread made the Vulkan call.
std::atomic<u32> validation_error_count{0};
std::atomic<u32> validation_warning_count{0};
bool validation_enabled{false};

struct Options
{
    bool enable_vsync{true}; // enable/disable vsync
    bool enable_dxr{false};  // enable/disable ray tracing (not supported yet)
    u32 msaa_samples{1};     // MSAA samples (1, 2, 4, 8)
} options;

auto failed_init() -> bool
{
    shutdown();
    return false;
}

// NOTE: classic return type: functions with a calling convention (VKAPI_CALL, WINAPI, CALLBACK) keep it (T02).
// NOLINTNEXTLINE(modernize-use-trailing-return-type): calling convention (keeps the classic return type, T02)
VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT *data,
    void *)
{
    const char *label{"info"};
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    {
        label = "ERROR";
        ++validation_error_count;
    }
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
        label = "warning";
        ++validation_warning_count;
    }
    log_message("::Vulkan %s: %s\n", label, data && data->pMessage ? data->pMessage : "");
    return VK_FALSE;
}

auto has_layer(const utl::vector<VkLayerProperties> &layers, const char *name) -> bool
{
    for (const auto &layer : layers)
    {
        if (std::strcmp(layer.layerName, name) == 0)
        {
            return true;
        }
    }
    return false;
}

auto has_extension(const utl::vector<VkExtensionProperties> &extensions, const char *name) -> bool
{
    for (const auto &extension : extensions)
    {
        if (std::strcmp(extension.extensionName, name) == 0)
        {
            return true;
        }
    }
    return false;
}

// Validation is on in debug builds if the layer is installed. PRIMAL_VULKAN_VALIDATION=0 turns it off.
auto use_validation() -> bool
{
#ifdef _DEBUG
    constexpr const char *name{"PRIMAL_VULKAN_VALIDATION"};
#ifdef _MSC_VER
    // std::getenv is deprecated by MSVC (C4996).
    char *env{nullptr};
    size_t size{0};
    if (_dupenv_s(&env, &size, name) != 0 || !env)
    {
        return true;
    }
    const bool enabled{env[0] != '0'};
    std::free(env);
    return enabled;
#else
    const char *const env{std::getenv(name)};
    return !env || env[0] != '0';
#endif
#else
    return false;
#endif
}

auto create_instance() -> bool
{
#if PRIMAL_PLATFORM_WINDOWS
    // vulkan-1.dll is delay-loaded (Engine/Vulkan.props) so D3D12-only machines can still start.
    // Load it here first: calling into a missing delay-loaded DLL raises an SEH exception.
    if (!::LoadLibraryW(L"vulkan-1.dll"))
    {
        log_message("Vulkan: vulkan-1.dll (the Vulkan loader) wasn't found.\n");
        return false;
    }
#endif

    u32 api_version{0};
    VkCall(::vkEnumerateInstanceVersion(&api_version));
    if (api_version < minimum_api_version)
    {
        log_message(
            "Vulkan: the loader supports Vulkan %u.%u, 1.3 is required.\n",
            VK_API_VERSION_MAJOR(api_version),
            VK_API_VERSION_MINOR(api_version));
        return false;
    }

    u32 count{0};
    ::vkEnumerateInstanceLayerProperties(&count, nullptr);
    utl::vector<VkLayerProperties> layers(count);
    ::vkEnumerateInstanceLayerProperties(&count, layers.data());

    ::vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    utl::vector<VkExtensionProperties> extensions(count);
    ::vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data());

    utl::vector<const char *> enabled_layers;
    utl::vector<const char *> enabled_extensions;
    enabled_extensions.emplace_back(VK_KHR_SURFACE_EXTENSION_NAME);

    // Window systems. The native handle of each window says which one it uses (see VulkanSurface.cpp).
    u32 window_system_count{0};
#if PRIMAL_PLATFORM_LINUX
    constexpr const char *window_system_extensions[]{"VK_KHR_xlib_surface", "VK_KHR_wayland_surface"};
#else
    constexpr const char *window_system_extensions[]{"VK_KHR_win32_surface"};
#endif
    for (const char *name : window_system_extensions)
    {
        if (has_extension(extensions, name))
        {
            enabled_extensions.emplace_back(name);
            ++window_system_count;
        }
    }
    if (!window_system_count)
    {
        log_message("Vulkan: no supported window system surface extension.\n");
        return false;
    }
    // Extra swapchain color spaces (FP16 scRGB like the D3D12 back buffer). Optional.
    if (has_extension(extensions, VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME))
    {
        enabled_extensions.emplace_back(VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME);
    }

    const bool validation{use_validation() && has_layer(layers, validation_layer_name)};
    const bool debug_utils{has_extension(extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)};
    // VK_EXT_layer_settings is implemented by the validation layer, so it's listed with the layer's
    // extensions.
    bool layer_settings{false};
    if (validation)
    {
        layer_settings = has_extension(extensions, VK_EXT_LAYER_SETTINGS_EXTENSION_NAME);
        if (!layer_settings)
        {
            u32 layer_extension_count{0};
            ::vkEnumerateInstanceExtensionProperties(validation_layer_name, &layer_extension_count, nullptr);
            utl::vector<VkExtensionProperties> layer_extensions(layer_extension_count);
            ::vkEnumerateInstanceExtensionProperties(
                validation_layer_name, &layer_extension_count, layer_extensions.data());
            layer_settings = has_extension(layer_extensions, VK_EXT_LAYER_SETTINGS_EXTENSION_NAME);
        }
    }
    if (validation)
    {
        enabled_layers.emplace_back(validation_layer_name);
    }
#ifdef _DEBUG
    if (debug_utils)
    {
        enabled_extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    if (use_validation() && !validation)
    {
        log_message("Warning: Vulkan validation layer is not available. Install vulkan-validation-layers.\n");
    }
#endif
    if (layer_settings)
    {
        enabled_extensions.emplace_back(VK_EXT_LAYER_SETTINGS_EXTENSION_NAME);
    }

    VkApplicationInfo app_info{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app_info.pApplicationName = "Primal";
    app_info.pEngineName = "Primal";
    app_info.engineVersion = VK_MAKE_API_VERSION(0, version.major, version.minor, version.revision);
    app_info.apiVersion = minimum_api_version;

    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &app_info;
    info.enabledLayerCount = static_cast<u32>(enabled_layers.size());
    info.ppEnabledLayerNames = enabled_layers.data();
    info.enabledExtensionCount = static_cast<u32>(enabled_extensions.size());
    info.ppEnabledExtensionNames = enabled_extensions.data();

    // Synchronization validation (hazards between commands/submits), the counterpart of the
    // D3D12 debug layer's resource state checks.
    const VkBool32 enable{VK_TRUE};
    const VkLayerSettingEXT settings[]{
        {validation_layer_name, "validate_sync", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &enable},
    };
    VkLayerSettingsCreateInfoEXT settings_info{VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT};
    settings_info.settingCount = static_cast<u32>(std::size(settings));
    settings_info.pSettings = &settings[0];

    // Messages from vkCreateInstance/vkDestroyInstance go to this messenger.
    VkDebugUtilsMessengerCreateInfoEXT messenger_info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    messenger_info.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    messenger_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    messenger_info.pfnUserCallback = debug_callback;

    const void **next{&info.pNext};
    if (validation && debug_utils)
    {
        *next = &messenger_info;
        next = &messenger_info.pNext;
    }
    if (layer_settings)
    {
        *next = &settings_info;
    }

    if (!VkCall(::vkCreateInstance(&info, nullptr, &vk_instance)))
    {
        return false;
    }
    // Without VK_EXT_debug_utils the layer's messages don't reach debug_callback(), so they aren't counted.
    validation_enabled = validation && debug_utils;
    if (validation)
    {
        log_message("::Vulkan validation layer enabled%s\n", layer_settings ? " (+ synchronization validation)" : "");
    }

#ifdef _DEBUG
    if (debug_utils)
    {
        set_debug_utils_object_name = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
            ::vkGetInstanceProcAddr(vk_instance, "vkSetDebugUtilsObjectNameEXT"));
        if (validation)
        {
            const auto create_messenger{reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                ::vkGetInstanceProcAddr(vk_instance, "vkCreateDebugUtilsMessengerEXT"))};
            // messenger_info was part of the vkCreateInstance pNext chain (pNext may point at settings_info);
            // vkCreateDebugUtilsMessengerEXT requires pNext == nullptr.
            messenger_info.pNext = nullptr;
            if (create_messenger)
            {
                VkCall(create_messenger(vk_instance, &messenger_info, nullptr, &debug_messenger));
            }
        }
    }
#endif

    return true;
}

struct DeviceFeatures
{
    VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT mutable_descriptor{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT};
    VkPhysicalDeviceVulkan13Features vulkan13{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        &mutable_descriptor};
    VkPhysicalDeviceVulkan12Features vulkan12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &vulkan13};
    VkPhysicalDeviceVulkan11Features vulkan11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, &vulkan12};
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &vulkan11};

    DeviceFeatures() = default;
    DISABLE_COPY_AND_MOVE(DeviceFeatures); // the pNext chain points into this struct
};

auto get_required_features(const DeviceFeatures &supported, DeviceFeatures &enabled, bool log_missing) -> bool
{
    bool ok{true};
    const auto require = [&](VkBool32 is_supported, VkBool32 &enable, const char *name)
    {
        enable = VK_TRUE;
        if (!is_supported)
        {
            if (log_missing)
            {
                log_message("Vulkan: missing device feature %s\n", name);
            }
            ok = false;
        }
    };
#define REQUIRE(level, name) require(supported.level.name, enabled.level.name, #name)
    // Vulkan 1.0
    REQUIRE(features.features, shaderInt16); // -enable-16bit-types
    REQUIRE(features.features, samplerAnisotropy);
    REQUIRE(features.features, textureCompressionBC); // BC1-7 textures (always available in D3D12)
    REQUIRE(features.features, multiDrawIndirect);    // ExecuteIndirect equivalent
    REQUIRE(features.features, drawIndirectFirstInstance);
    // Vulkan 1.1
    REQUIRE(vulkan11, storageBuffer16BitAccess); // -enable-16bit-types
    REQUIRE(vulkan11, uniformAndStorageBuffer16BitAccess);
    REQUIRE(vulkan11, shaderDrawParameters);
    // Vulkan 1.2
    REQUIRE(vulkan12, timelineSemaphore);  // frame fences
    REQUIRE(vulkan12, scalarBlockLayout);  // -fvk-use-dx-layout
    REQUIRE(vulkan12, shaderFloat16);      // -enable-16bit-types
    REQUIRE(vulkan12, descriptorIndexing); // ResourceDescriptorHeap[]
    REQUIRE(vulkan12, runtimeDescriptorArray);
    REQUIRE(vulkan12, descriptorBindingPartiallyBound);
    REQUIRE(vulkan12, descriptorBindingUpdateUnusedWhilePending);
    REQUIRE(vulkan12, descriptorBindingSampledImageUpdateAfterBind);
    REQUIRE(vulkan12, descriptorBindingStorageImageUpdateAfterBind);
    REQUIRE(vulkan12, descriptorBindingStorageBufferUpdateAfterBind);
    REQUIRE(vulkan12, descriptorBindingUniformBufferUpdateAfterBind);
    REQUIRE(vulkan12, shaderSampledImageArrayNonUniformIndexing);
    REQUIRE(vulkan12, shaderStorageBufferArrayNonUniformIndexing);
    REQUIRE(vulkan12, drawIndirectCount);
    REQUIRE(vulkan12, bufferDeviceAddress);
    // Vulkan 1.3
    REQUIRE(vulkan13, dynamicRendering); // no render pass objects
    REQUIRE(vulkan13, synchronization2);
    REQUIRE(vulkan13, maintenance4);
    // VK_EXT_mutable_descriptor_type: typed views aliasing ResourceDescriptorHeap[]
    REQUIRE(mutable_descriptor, mutableDescriptorType);
#undef REQUIRE
    return ok;
}

constexpr const char *required_device_extensions[]{
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME,
    // Root CBVs/SRVs/UAVs (core in Vulkan 1.4, but 1.3 is the minimum).
    VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
};

// Returns the index of a queue family that supports graphics and compute, or u32_invalid_id.
// NOTE: presenting is checked per window (vulkan_surface::create()).
auto find_graphics_queue_family(VkPhysicalDevice physical_device) -> u32
{
    u32 count{0};
    ::vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, nullptr);
    utl::vector<VkQueueFamilyProperties> families(count);
    ::vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, families.data());
    constexpr VkQueueFlags required{VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT};
    for (u32 i{0}; i < count; ++i)
    {
        if ((families[i].queueFlags & required) == required)
        {
            return i;
        }
    }
    return u32_invalid_id;
}

auto is_device_suitable(VkPhysicalDevice physical_device, bool log_missing) -> bool
{
    VkPhysicalDeviceProperties properties{};
    ::vkGetPhysicalDeviceProperties(physical_device, &properties);
    if (properties.apiVersion < minimum_api_version)
    {
        if (log_missing)
        {
            log_message(
                "Vulkan: %s supports Vulkan %u.%u, 1.3 is required.\n",
                properties.deviceName,
                VK_API_VERSION_MAJOR(properties.apiVersion),
                VK_API_VERSION_MINOR(properties.apiVersion));
        }
        return false;
    }

    u32 count{0};
    ::vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &count, nullptr);
    utl::vector<VkExtensionProperties> extensions(count);
    ::vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &count, extensions.data());
    bool ok{true};
    for (const char *name : required_device_extensions)
    {
        if (!has_extension(extensions, name))
        {
            if (log_missing)
            {
                log_message("Vulkan: %s doesn't support %s\n", properties.deviceName, name);
            }
            ok = false;
        }
    }
    if (!ok)
    {
        return false;
    }

    DeviceFeatures supported{};
    ::vkGetPhysicalDeviceFeatures2(physical_device, &supported.features);
    DeviceFeatures enabled{};
    if (!get_required_features(supported, enabled, log_missing))
    {
        return false;
    }

    return find_graphics_queue_family(physical_device) != u32_invalid_id;
}

// Get the most performing GPU that supports everything we need (discrete > integrated > others).
// NOTE: like d3d12's determine_main_adapter(), this could later let the user choose.
auto determine_main_physical_device() -> VkPhysicalDevice
{
    u32 count{0};
    ::vkEnumeratePhysicalDevices(vk_instance, &count, nullptr);
    utl::vector<VkPhysicalDevice> devices(count);
    ::vkEnumeratePhysicalDevices(vk_instance, &count, devices.data());

    const auto rank = [](VkPhysicalDeviceType type)
    {
        switch (type)
        {
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return 3;
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return 2;
            case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return 1;
            default: return 0; // CPU (lavapipe) and others
        }
    };

    VkPhysicalDevice best{VK_NULL_HANDLE};
    s32 best_rank{-1};
    for (const VkPhysicalDevice device : devices)
    {
        if (!is_device_suitable(device, false))
        {
            continue;
        }
        VkPhysicalDeviceProperties properties{};
        ::vkGetPhysicalDeviceProperties(device, &properties);
        if (rank(properties.deviceType) > best_rank)
        {
            best = device;
            best_rank = rank(properties.deviceType);
        }
    }

    if (!best)
    {
        // Say why each device was rejected.
        for (const VkPhysicalDevice device : devices)
        {
            is_device_suitable(device, true);
        }
    }

    return best;
}

auto create_device() -> bool
{
    main_physical_device = determine_main_physical_device();
    if (!main_physical_device)
    {
        log_message("Vulkan: no suitable GPU found.\n");
        return false;
    }

    VkPhysicalDeviceProperties properties{};
    ::vkGetPhysicalDeviceProperties(main_physical_device, &properties);
    log_message(
        "::Vulkan device: %s (Vulkan %u.%u.%u)\n",
        properties.deviceName,
        VK_API_VERSION_MAJOR(properties.apiVersion),
        VK_API_VERSION_MINOR(properties.apiVersion),
        VK_API_VERSION_PATCH(properties.apiVersion));

    // Offsets of storage buffer ranges (root SRVs/UAVs) must be multiples of minStorageBufferOffsetAlignment (4 on
    // AMD/Mesa, 16 on NVIDIA, at most 256). D3D12 only needs 4.
    storage_buffer_alignment = std::max(static_cast<u32>(properties.limits.minStorageBufferOffsetAlignment), 16u);

    const u32 queue_family{find_graphics_queue_family(main_physical_device)};
    const f32 priority{1.f};
    VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue_info.queueFamilyIndex = queue_family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;

    DeviceFeatures supported{};
    ::vkGetPhysicalDeviceFeatures2(main_physical_device, &supported.features);
    DeviceFeatures enabled{};
    get_required_features(supported, enabled, true);

    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    info.pNext = &enabled.features;
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queue_info;
    info.enabledExtensionCount = static_cast<u32>(std::size(required_device_extensions));
    info.ppEnabledExtensionNames = &required_device_extensions[0];

    if (!VkCall(::vkCreateDevice(main_physical_device, &info, nullptr, &main_device)))
    {
        return false;
    }
    NAME_VK_OBJECT(main_device, VK_OBJECT_TYPE_DEVICE, "Main Vulkan Device");
    vkx::load_device_functions(main_device);

    return gfx_command.initialize(main_device, queue_family);
}

auto create_allocator() -> bool
{
    VmaAllocatorCreateInfo info{};
    info.vulkanApiVersion = minimum_api_version;
    info.physicalDevice = main_physical_device;
    info.device = main_device;
    info.instance = vk_instance;
    return VkCall(::vmaCreateAllocator(&info, &vma_allocator));
}

// A zeroed GPU buffer bound wherever a root SRV has no buffer yet (D3D12 binds address 0 there): light buffers before
// a light set has lights, element buffers of position-only meshes. 256 KB covers a 4K surface's light grid (120 x 68
// tiles of uint2).
auto create_zero_buffer() -> bool
{
    constexpr u32 size{256 * 1024};
    const std::unique_ptr<u8[]> zeros{std::make_unique<u8[]>(size)}; // value-initialized
    VulkanBufferInitInfo info{};
    info.data = zeros.get();
    info.size = size;
    info.alignment = sizeof(math::v4);
    info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    zero_buffer_object = VulkanBuffer{info, false};
    NAME_VK_OBJECT(zero_buffer_object.buffer(), VK_OBJECT_TYPE_BUFFER, "Zero Buffer");
    return zero_buffer_object.buffer() != VK_NULL_HANDLE;
}

auto create_static_samplers() -> bool
{
    VkPhysicalDeviceProperties properties{};
    ::vkGetPhysicalDeviceProperties(main_physical_device, &properties);
    constexpr const char *names[]{"Static Sampler - point", "Static Sampler - linear", "Static Sampler - anisotropic"};
    static_assert(std::size(names) == vkx::StaticSampler::count);

    for (u32 i{0}; i < vkx::StaticSampler::count; ++i)
    {
        VkSamplerCreateInfo info{vkx::sampler_info(static_cast<vkx::StaticSampler::Type>(i))};
        info.maxAnisotropy = std::min(info.maxAnisotropy, properties.limits.maxSamplerAnisotropy);
        if (!VkCall(::vkCreateSampler(main_device, &info, nullptr, &static_samplers[i])))
        {
            return false;
        }
        NAME_VK_OBJECT(static_samplers[i], VK_OBJECT_TYPE_SAMPLER, names[i]);
    }
    return true;
}

auto destroy_object(const DeferredObject &object) -> void
{
    const VkDevice device{main_device};
    switch (object.type)
    {
        case VK_OBJECT_TYPE_SWAPCHAIN_KHR:
            ::vkDestroySwapchainKHR(device, handle_from_u64<VkSwapchainKHR>(object.handle), nullptr);
            break;
        case VK_OBJECT_TYPE_SURFACE_KHR:
            ::vkDestroySurfaceKHR(vk_instance, handle_from_u64<VkSurfaceKHR>(object.handle), nullptr);
            break;
        case VK_OBJECT_TYPE_IMAGE_VIEW:
            ::vkDestroyImageView(device, handle_from_u64<VkImageView>(object.handle), nullptr);
            break;
        case VK_OBJECT_TYPE_IMAGE:
            if (object.allocation)
            {
                ::vmaDestroyImage(vma_allocator, handle_from_u64<VkImage>(object.handle), object.allocation);
            }
            else
            {
                ::vkDestroyImage(device, handle_from_u64<VkImage>(object.handle), nullptr);
            }
            break;
        case VK_OBJECT_TYPE_BUFFER:
            if (object.allocation)
            {
                ::vmaDestroyBuffer(vma_allocator, handle_from_u64<VkBuffer>(object.handle), object.allocation);
            }
            else
            {
                ::vkDestroyBuffer(device, handle_from_u64<VkBuffer>(object.handle), nullptr);
            }
            break;
        case VK_OBJECT_TYPE_DESCRIPTOR_POOL:
            ::vkDestroyDescriptorPool(device, handle_from_u64<VkDescriptorPool>(object.handle), nullptr);
            break;
        case VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT:
            ::vkDestroyDescriptorSetLayout(device, handle_from_u64<VkDescriptorSetLayout>(object.handle), nullptr);
            break;
        case VK_OBJECT_TYPE_SAMPLER:
            ::vkDestroySampler(device, handle_from_u64<VkSampler>(object.handle), nullptr);
            break;
        case VK_OBJECT_TYPE_PIPELINE:
            ::vkDestroyPipeline(device, handle_from_u64<VkPipeline>(object.handle), nullptr);
            break;
        case VK_OBJECT_TYPE_PIPELINE_LAYOUT:
            ::vkDestroyPipelineLayout(device, handle_from_u64<VkPipelineLayout>(object.handle), nullptr);
            break;
        case VK_OBJECT_TYPE_SEMAPHORE:
            ::vkDestroySemaphore(device, handle_from_u64<VkSemaphore>(object.handle), nullptr);
            break;
        default: assert(false && "Vulkan: deferred release of an unsupported object type."); break;
    }
}

auto process_deferred_releases(u32 frame_idx, bool force_release = false) -> void
{
    const std::scoped_lock lock{deferred_releases_mutex};

    // NOTE: The objects could still be in use in previous frames. So we wait
    //       another round before destroying them.
    //       force_release is used during shutdown, where we don't have to wait.
    if (!force_release && ++deferred_releases_flag[frame_idx] < frame_buffer_count)
    {
        return;
    }

    deferred_releases_flag[frame_idx] = 0;

    srv_desc_heap.process_deferred_free(frame_idx);

    utl::vector<DeferredObject> &objects{deferred_releases[frame_idx]};
    for (const auto &object : objects)
    {
        destroy_object(object);
    }
    objects.clear();
}

// Counterpart of d3d12's get_d3d12_frame_info(): fills GlobalShaderData in the frame's constant buffer.
auto get_vulkan_frame_info(
    const FrameInfo &info,
    ConstantBuffer &cbuffer,
    const VulkanSurface &surface,
    SurfaceId id,
    u32 frame_idx) -> VulkanFrameInfo
{
    hlsl::GlobalShaderData data{};

    using namespace DirectX;
    camera::VulkanCamera *camera{nullptr};
    if (id::is_valid(info.camera_id))
    {
        camera = &camera::get(info.camera_id);
        camera->update();
        XMStoreFloat4x4A(&data.View, camera->view());
        XMStoreFloat4x4A(&data.Projection, camera->projection());
        XMStoreFloat4x4A(&data.InvProjection, camera->inverse_projection());
        XMStoreFloat4x4A(&data.ViewProjection, camera->view_projection());
        XMStoreFloat4x4A(&data.InvViewProjection, camera->inverse_view_projection());
        XMStoreFloat3(&data.CameraPosition, camera->position());
        XMStoreFloat3(&data.CameraDirection, camera->direction());
        // A scene (camera) needs a light set, like in D3D12 (light::* assert that it exists).
        data.NumDirectionalLights = light::non_cullable_light_count(info.light_set_key);
        data.AmbientLight = light::ambient_light(info.light_set_key);
    }
    else
    {
        // No camera (the SURFACES test): identity matrices; nothing is drawn, post-processing shows the gpass clear
        // color.
        const XMMATRIX identity{XMMatrixIdentity()};
        XMStoreFloat4x4A(&data.View, identity);
        XMStoreFloat4x4A(&data.Projection, identity);
        XMStoreFloat4x4A(&data.InvProjection, identity);
        XMStoreFloat4x4A(&data.ViewProjection, identity);
        XMStoreFloat4x4A(&data.InvViewProjection, identity);
        data.CameraDirection = {0.f, 0.f, -1.f};
        // No lights (NumDirectionalLights 0) and no ambient light (what light::ambient_light() returns without one).
        data.AmbientLight = {-1.f, u32_invalid_id, u32_invalid_id, u32_invalid_id};
    }
    data.ViewWidth = static_cast<f32>(surface.width());
    data.ViewHeight = static_cast<f32>(surface.height());
    data.DeltaTime = info.average_frame_time;

    // NOTE: be careful not to read from this buffer. Reads are really really slow (write-combined memory).
    hlsl::GlobalShaderData *const shader_data{cbuffer.allocate<hlsl::GlobalShaderData>()};
    // TODO: handle the case when cbuffer is full.
    assert(shader_data);
    std::memcpy(shader_data, &data, sizeof(hlsl::GlobalShaderData));

    VulkanFrameInfo vulkan_info{};
    vulkan_info.info = &info;
    vulkan_info.camera = camera;
    vulkan_info.global_shader_data = cbuffer.range(shader_data);
    vulkan_info.surface_width = surface.width();
    vulkan_info.surface_height = surface.height();
    vulkan_info.surface_id = static_cast<id::id_type>(id);
    vulkan_info.light_culling_id = surface.light_culling_id();
    vulkan_info.frame_index = frame_idx;
    return vulkan_info;
}

}

namespace detail
{
auto deferred_release(VkObjectType type, u64 handle, VmaAllocation allocation) -> void
{
    const std::scoped_lock lock{deferred_releases_mutex};
    const u32 frame_idx{current_frame_index()};
    deferred_releases[frame_idx].push_back({type, handle, allocation});
    deferred_releases_flag[frame_idx] = 1;
}
}

auto initialize() -> bool
{
    if (main_device)
    {
        shutdown();
    }
    // NOTE: not in shutdown(): destroying the device and the instance is when leaked objects are reported, and the
    //       tests read the counts after it.
    validation_error_count = 0;
    validation_warning_count = 0;
    validation_enabled = false;

    if (!create_instance())
    {
        return failed_init();
    }
    if (!create_device())
    {
        return failed_init();
    }
    if (!create_allocator())
    {
        return failed_init();
    }
    if (!create_static_samplers())
    {
        return failed_init();
    }
    if (!srv_desc_heap.initialize(srv_heap_capacity))
    {
        return failed_init();
    }

    for (u32 i{0}; i < frame_buffer_count; ++i)
    {
        new (&constant_buffers[i]) ConstantBuffer{ConstantBuffer::get_default_init_info(constant_buffer_size)};
        if (!constant_buffers[i].buffer())
        {
            return failed_init();
        }
        NAME_VK_OBJECT_INDEXED(constant_buffers[i].buffer(), VK_OBJECT_TYPE_BUFFER, i, "Global Constant Buffer");
    }

    // initialize modules
    // NOTE: upload goes first (D3D12: after fx): modules may upload data while they initialize (gpass does).
    if (!(upload::initialize() && create_zero_buffer() && shaders::initialize() && gpass::initialize() &&
          fx::initialize() && content::initialize() && delight::initialize()))
    {
        return failed_init();
    }
    return true;
}

auto shutdown() -> void
{
    gfx_command.release();

    for (u32 i{0}; i < frame_buffer_count; ++i)
    {
        process_deferred_releases(i, true);
    }

    // shutdown modules
    delight::shutdown();
    content::shutdown();
    fx::shutdown();
    gpass::shutdown();
    shaders::shutdown();
    zero_buffer_object.release();
    upload::shutdown();

    for (u32 i{0}; i < frame_buffer_count; ++i)
    {
        constant_buffers[i].release();
    }

    // NOTE: modules may defer releases (and descriptor frees) while shutting down. Process those once
    //       more, then release the heap, which defers its own pool and layout.
    process_deferred_releases(0, true);
    srv_desc_heap.release();
    process_deferred_releases(0, true);

    assert(surfaces.empty() && "Vulkan: remove all surfaces before shutting down the renderer.");

    for (VkSampler &sampler : static_samplers)
    {
        if (sampler)
        {
            ::vkDestroySampler(main_device, sampler, nullptr);
        }
        sampler = VK_NULL_HANDLE;
    }

    if (vma_allocator)
    {
        ::vmaDestroyAllocator(vma_allocator);
    }
    vma_allocator = VK_NULL_HANDLE;

    if (main_device)
    {
        ::vkDestroyDevice(main_device, nullptr);
    }
    main_device = VK_NULL_HANDLE;
    main_physical_device = VK_NULL_HANDLE;

    if (debug_messenger)
    {
        const auto destroy_messenger{reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            ::vkGetInstanceProcAddr(vk_instance, "vkDestroyDebugUtilsMessengerEXT"))};
        if (destroy_messenger)
        {
            destroy_messenger(vk_instance, debug_messenger, nullptr);
        }
        debug_messenger = VK_NULL_HANDLE;
    }
    set_debug_utils_object_name = nullptr;

    if (vk_instance)
    {
        ::vkDestroyInstance(vk_instance, nullptr);
    }
    vk_instance = VK_NULL_HANDLE;

    if (const u32 error_count{validation_error_count})
    {
        log_message("::Vulkan: %u validation error(s) were reported.\n", error_count);
    }
}

auto validation_stats() -> ValidationStats
{
    return {
        .is_enabled = validation_enabled,
        .error_count = validation_error_count,
        .warning_count = validation_warning_count,
    };
}

auto set_option(RendererOption::Option option, const void *const parameter, [[maybe_unused]] u32 parameter_size) -> void
{
    assert(option < RendererOption::count);
    assert(parameter && parameter_size);

    switch (option)
    {
        case RendererOption::vsync:
        {
            assert(parameter_size == sizeof(bool));
            // NOTE: surfaces fall back to FIFO (vsync) if the window system has no mailbox/immediate
            //       mode. They pick up the change on their next frame (vulkan_surface::needs_resize()).
            options.enable_vsync = *static_cast<const bool *>(parameter);
        }
        break;
        case RendererOption::raytracing:
        {
            assert(parameter_size == sizeof(bool));
            options.enable_dxr = *static_cast<const bool *>(parameter);
        }
        break;
        case RendererOption::msaa:
        {
            assert(parameter_size == sizeof(u32));
            const u32 msaa_samples{*static_cast<const u32 *>(parameter)};
            if (msaa_samples == 1 || msaa_samples == 2 || msaa_samples == 4 || msaa_samples == 8)
            {
                options.msaa_samples = msaa_samples;
            }
        }
        break;
        default: break;
    }
}

auto get_option(RendererOption::Option option, void *const parameter, [[maybe_unused]] u32 parameter_size) -> void
{
    assert(option < RendererOption::count);
    assert(parameter && parameter_size);
    switch (option)
    {
        case RendererOption::vsync:
            assert(parameter_size == sizeof(bool));
            *static_cast<bool *>(parameter) = options.enable_vsync;
            break;
        case RendererOption::raytracing:
            assert(parameter_size == sizeof(bool));
            *static_cast<bool *>(parameter) = options.enable_dxr;
            break;
        case RendererOption::msaa:
            assert(parameter_size == sizeof(u32));
            *static_cast<u32 *>(parameter) = options.msaa_samples;
            break;
        default: break;
    }
}

auto instance() -> VkInstance
{
    return vk_instance;
}

auto physical_device() -> VkPhysicalDevice
{
    return main_physical_device;
}

auto device() -> VkDevice
{
    return main_device;
}

auto graphics_queue() -> VkQueue
{
    return gfx_command.queue();
}

auto submit_to_graphics_queue(const VkSubmitInfo2 &submit) -> void
{
    gfx_command.submit(submit);
}

auto graphics_queue_family() -> u32
{
    return gfx_command.queue_family();
}

auto allocator() -> VmaAllocator
{
    return vma_allocator;
}

auto srv_heap() -> DescriptorHeap &
{
    return srv_desc_heap;
}

auto cbuffer() -> ConstantBuffer &
{
    return constant_buffers[current_frame_index()];
}

auto zero_buffer() -> vkx::BufferRange
{
    return zero_buffer_object.range();
}

auto storage_buffer_offset_alignment() -> u32
{
    return storage_buffer_alignment;
}

auto static_sampler(vkx::StaticSampler::Type type) -> VkSampler
{
    assert(type < vkx::StaticSampler::count);
    return static_samplers[type];
}

auto current_frame_index() -> u32
{
    return gfx_command.frame_index();
}

auto set_deferred_releases_flag(u32 frame_idx) -> void
{
    const std::scoped_lock lock{deferred_releases_mutex};
    deferred_releases_flag[frame_idx] = 1;
}

auto vsync_enabled() -> bool
{
    return options.enable_vsync;
}

auto set_object_name([[maybe_unused]] u64 handle, [[maybe_unused]] VkObjectType type, [[maybe_unused]] const char *name)
    -> void
{
    if (!set_debug_utils_object_name || !main_device || !handle)
    {
        return;
    }
    VkDebugUtilsObjectNameInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
    info.objectType = type;
    info.objectHandle = handle;
    info.pObjectName = name;
    set_debug_utils_object_name(main_device, &info);
}

auto create_surface(platform::Window window) -> Surface
{
    const SurfaceId id{surfaces.add(window)};
    if (!surfaces[id].create())
    {
        surfaces.remove(id);
        return {};
    }
    return Surface{id};
}

auto remove_surface(SurfaceId id) -> void
{
    gfx_command.flush();
    surfaces.remove(id);
}

auto resize_surface(SurfaceId id, u32, u32) -> void
{
    // NOTE: SDL sends a resize event when a window is first shown, at the size the swapchain was just created with.
    //       Skip the flush + recreate then (render_surface() checks again every frame anyway).
    if (!surfaces[id].needs_resize())
    {
        return;
    }
    gfx_command.flush();
    surfaces[id].resize();
}

auto surface_width(SurfaceId id) -> u32
{
    return surfaces[id].width();
}

auto surface_height(SurfaceId id) -> u32
{
    return surfaces[id].height();
}

auto render_surface(SurfaceId id, FrameInfo info) -> void
{
    VulkanSurface &surface{surfaces[id]};

    // Window resized, swapchain out of date, or vsync toggled.
    if (surface.needs_resize())
    {
        gfx_command.flush();
        surface.resize();
    }

    // Wait for the GPU to finish with this frame's command buffer and reset it.
    const VkCommandBuffer cmd{gfx_command.begin_frame()};
    const u32 frame_idx{current_frame_index()};

    if (deferred_releases_flag[frame_idx])
    {
        process_deferred_releases(frame_idx);
    }

    ConstantBuffer &cb{constant_buffers[frame_idx]};
    cb.clear();

    // Nothing to render to (minimized or out of date). The frame is skipped; its command
    // buffer is reset by the next begin_frame().
    if (!surface.acquire_next_image(gfx_command.image_acquired_semaphore()))
    {
        return;
    }

    const VulkanFrameInfo vulkan_info{get_vulkan_frame_info(info, cb, surface, id, frame_idx)};

    gpass::set_size({vulkan_info.surface_width, vulkan_info.surface_height});
    vkx::VulkanBarrier &barriers{resource_barriers};

    // Record commands
    vkx::set_viewport(cmd, vulkan_info.surface_width, vulkan_info.surface_height);

    // Depth prepass
    gpass::add_transitions_for_depth_prepass(barriers);
    barriers.apply(cmd);
    gpass::depth_prepass(cmd, vulkan_info);

    // Geometry and lighting pass
    gpass::add_transitions_for_gpass(barriers);
    if (vulkan_info.camera)
    {
        // No camera (SURFACES test): nothing is drawn, so there's no light set or culling either.
        light::update_light_buffers(vulkan_info);
        delight::cull_lights(cmd, vulkan_info, barriers);
    }
    barriers.apply(cmd);
    gpass::render(cmd, vulkan_info);

    // Post-process
    gpass::add_transitions_for_post_process(barriers);
    // The swapchain image is acquired at the color attachment output stage (end_frame()'s semaphore
    // wait), so the barrier's first scope starts there (D3D12: PRESENT -> RENDER_TARGET).
    constexpr vkx::ResourceState acquired{
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED};
    barriers.add(surface.image(), VK_IMAGE_ASPECT_COLOR_BIT, acquired, vkx::resource_states.render_target);
    barriers.apply(cmd);
    // Will write to the current back buffer, so back buffer is a render target
    fx::post_process(cmd, vulkan_info, surface.image_view(), surface.format());
    // after post process
    vkx::transition_image(
        cmd,
        surface.image(),
        VK_IMAGE_ASPECT_COLOR_BIT,
        vkx::resource_states.render_target,
        vkx::resource_states.present);

    // Done recording commands. Now submit, present and signal the fence value for this frame.
    gfx_command.end_frame(surface);
}
}
