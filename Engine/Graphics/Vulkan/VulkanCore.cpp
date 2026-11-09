// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "VulkanCore.h"
#include "VulkanCamera.h"
#include "VulkanContent.h"
#include "VulkanGPass.h"
#include "VulkanLight.h"
#include "VulkanLightCulling.h"
#include "VulkanHelpers.h"
#include "VulkanPostProcess.h"
#include "VulkanResources.h"
#include "VulkanShaders.h"
#include "Graphics/Shaders/SharedTypes.h"
#include "VulkanSurface.h"
#include "VulkanUpload.h"
#include <algorithm>
#include <cstdarg>
#include <cstdlib>

namespace primal::graphics::vulkan {

void
log_message(const char* format, ...) // NOLINT(modernize-avoid-variadic-functions): printf wrapper
{
    char buffer[1024];
    std::va_list args;
    va_start(args, format);
    std::va_list args_copy;
    va_copy(args_copy, args);
    const int length{ std::vsnprintf(buffer, sizeof(buffer), format, args) };
    va_end(args);

    const char* message{ buffer };
    std::unique_ptr<char[]> long_message;
    if (length >= (int)sizeof(buffer))
    {
        // Validation messages can be longer than the stack buffer.
        long_message = std::make_unique<char[]>((size_t)length + 1);
        std::vsnprintf(long_message.get(), (size_t)length + 1, format, args_copy);
        message = long_message.get();
    }
    va_end(args_copy);
    if (length < 0) return;

    std::fputs(message, stderr);
#if PRIMAL_PLATFORM_WINDOWS
    OutputDebugStringA(message);
#endif
}

} // namespace primal::graphics::vulkan

namespace primal::graphics::vulkan::core {
namespace {

// Graphics queue + per-frame command buffers, synchronized with a timeline semaphore
// (counterpart of d3d12_command; the timeline semaphore works like an ID3D12Fence).
class vulkan_command
{
public:
    vulkan_command() = default;
    DISABLE_COPY_AND_MOVE(vulkan_command);
    ~vulkan_command() { assert(!_queue && !_fence); }

    bool initialize(VkDevice device, u32 queue_family)
    {
        _device = device;
        _queue_family = queue_family;
        vkGetDeviceQueue(device, queue_family, 0, &_queue);
        NAME_VK_OBJECT(_queue, VK_OBJECT_TYPE_QUEUE, "GFX Queue");

        VkSemaphoreTypeCreateInfo timeline_info{ VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
        timeline_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        timeline_info.initialValue = 0;
        VkSemaphoreCreateInfo semaphore_info{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        semaphore_info.pNext = &timeline_info;
        if (!VkCall(vkCreateSemaphore(device, &semaphore_info, nullptr, &_fence))) return false;
        NAME_VK_OBJECT(_fence, VK_OBJECT_TYPE_SEMAPHORE, "GFX Frame Fence (timeline)");

        for (u32 i{ 0 }; i < frame_buffer_count; ++i)
        {
            command_frame& frame{ _cmd_frames[i] };

            VkCommandPoolCreateInfo pool_info{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
            pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            pool_info.queueFamilyIndex = queue_family;
            if (!VkCall(vkCreateCommandPool(device, &pool_info, nullptr, &frame.cmd_pool))) return false;
            NAME_VK_OBJECT_INDEXED(frame.cmd_pool, VK_OBJECT_TYPE_COMMAND_POOL, i, "GFX Command Pool");

            VkCommandBufferAllocateInfo alloc_info{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
            alloc_info.commandPool = frame.cmd_pool;
            alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            alloc_info.commandBufferCount = 1;
            if (!VkCall(vkAllocateCommandBuffers(device, &alloc_info, &frame.cmd_buffer))) return false;
            NAME_VK_OBJECT_INDEXED(frame.cmd_buffer, VK_OBJECT_TYPE_COMMAND_BUFFER, i, "GFX Command Buffer");

            VkSemaphoreCreateInfo binary_info{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
            if (!VkCall(vkCreateSemaphore(device, &binary_info, nullptr, &frame.image_acquired))) return false;
            NAME_VK_OBJECT_INDEXED(frame.image_acquired, VK_OBJECT_TYPE_SEMAPHORE, i, "Image Acquired Semaphore");
        }

        return true;
    }

    // Wait for the current frame to be signaled, then reset and begin its command buffer.
    VkCommandBuffer begin_frame()
    {
        command_frame& frame{ _cmd_frames[_frame_index] };
        frame.wait(_device, _fence);
        VkCall(vkResetCommandPool(_device, frame.cmd_pool, 0));

        VkCommandBufferBeginInfo begin_info{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VkCall(vkBeginCommandBuffer(frame.cmd_buffer, &begin_info));
        return frame.cmd_buffer;
    }

    // Submit the frame's command buffer (after the swapchain image was acquired), present,
    // and signal the timeline semaphore with the new fence value.
    void end_frame(vulkan_surface& surface)
    {
        command_frame& frame{ _cmd_frames[_frame_index] };
        VkCall(vkEndCommandBuffer(frame.cmd_buffer));

        const u64 fence_value{ ++_fence_value };

        VkSemaphoreSubmitInfo wait_info{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
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

        VkCommandBufferSubmitInfo cmd_info{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
        cmd_info.commandBuffer = frame.cmd_buffer;

        VkSubmitInfo2 submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
        submit.waitSemaphoreInfoCount = 1;
        submit.pWaitSemaphoreInfos = &wait_info;
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &cmd_info;
        submit.signalSemaphoreInfoCount = (u32)std::size(signal_infos);
        submit.pSignalSemaphoreInfos = &signal_infos[0];
        {
            std::scoped_lock lock{ _queue_mutex };
            VkCall(vkQueueSubmit2(_queue, 1, &submit, VK_NULL_HANDLE));

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
    void flush()
    {
        for (u32 i{ 0 }; i < frame_buffer_count; ++i)
        {
            _cmd_frames[i].wait(_device, _fence);
        }
        if (_queue)
        {
            std::scoped_lock lock{ _queue_mutex };
            VkCall(vkQueueWaitIdle(_queue));
        }
        _frame_index = 0;
    }

    void release()
    {
        if (!_device) return;
        flush();

        for (u32 i{ 0 }; i < frame_buffer_count; ++i)
        {
            _cmd_frames[i].release(_device);
        }

        if (_fence) vkDestroySemaphore(_device, _fence, nullptr);
        _fence = VK_NULL_HANDLE;
        _fence_value = 0;
        _queue = VK_NULL_HANDLE;
        _device = VK_NULL_HANDLE;
    }

    // For other modules (upload) that submit to the same queue, possibly from other threads.
    // NOTE: a VkQueue must be externally synchronized (submits, presents, wait idle).
    void submit(const VkSubmitInfo2& submit)
    {
        std::scoped_lock lock{ _queue_mutex };
        VkCall(vkQueueSubmit2(_queue, 1, &submit, VK_NULL_HANDLE));
    }

    [[nodiscard]] constexpr VkQueue queue() const { return _queue; }
    [[nodiscard]] constexpr u32 queue_family() const { return _queue_family; }
    [[nodiscard]] constexpr u32 frame_index() const { return _frame_index; }
    [[nodiscard]] constexpr VkSemaphore image_acquired_semaphore() const { return _cmd_frames[_frame_index].image_acquired; }

private:
    struct command_frame
    {
        VkCommandPool cmd_pool{ VK_NULL_HANDLE };
        VkCommandBuffer cmd_buffer{ VK_NULL_HANDLE };
        // Signaled by vkAcquireNextImageKHR. Reusable once this frame's fence value is reached,
        // because the frame's submit waited on it.
        VkSemaphore image_acquired{ VK_NULL_HANDLE };
        u64 fence_value{ 0 };

        void wait(VkDevice device, VkSemaphore fence) const
        {
            assert(device && fence);
            u64 completed{ 0 };
            VkCall(vkGetSemaphoreCounterValue(device, fence, &completed));
            // If the timeline value is still less than "fence_value", the GPU hasn't finished
            // executing this frame's command buffer yet.
            if (completed < fence_value)
            {
                VkSemaphoreWaitInfo wait_info{ VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO };
                wait_info.semaphoreCount = 1;
                wait_info.pSemaphores = &fence;
                wait_info.pValues = &fence_value;
                VkCall(vkWaitSemaphores(device, &wait_info, UINT64_MAX));
            }
        }

        void release(VkDevice device)
        {
            // Destroying the pool frees its command buffers.
            if (cmd_pool) vkDestroyCommandPool(device, cmd_pool, nullptr);
            if (image_acquired) vkDestroySemaphore(device, image_acquired, nullptr);
            cmd_pool = VK_NULL_HANDLE;
            cmd_buffer = VK_NULL_HANDLE;
            image_acquired = VK_NULL_HANDLE;
            fence_value = 0;
        }
    };

    VkDevice _device{ VK_NULL_HANDLE };
    VkQueue _queue{ VK_NULL_HANDLE };
    std::mutex _queue_mutex{};
    u32 _queue_family{ u32_invalid_id };
    VkSemaphore _fence{ VK_NULL_HANDLE };
    u64 _fence_value{ 0 };
    command_frame _cmd_frames[frame_buffer_count]{};
    u32 _frame_index{ 0 };
};

struct deferred_object
{
    VkObjectType type;
    u64 handle;
    VmaAllocation allocation;
};

using surface_collection = utl::free_list<vulkan_surface>;

constexpr const char* validation_layer_name{ "VK_LAYER_KHRONOS_validation" };

VkInstance vk_instance{ VK_NULL_HANDLE };
VkDebugUtilsMessengerEXT debug_messenger{ VK_NULL_HANDLE };
VkPhysicalDevice main_physical_device{ VK_NULL_HANDLE };
VkDevice main_device{ VK_NULL_HANDLE };
vulkan_command gfx_command;
surface_collection surfaces;
VmaAllocator vma_allocator{ VK_NULL_HANDLE };
vulkan_buffer zero_buffer_object{};
u32 storage_buffer_alignment{ 16 }; // max(16, minStorageBufferOffsetAlignment), set by create_device()
descriptor_heap srv_desc_heap;
constant_buffer constant_buffers[frame_buffer_count];
VkSampler static_samplers[vkx::static_sampler::count]{};
vkx::vulkan_barrier resource_barriers{};

// Same sizes as the D3D12 backend.
constexpr u32 srv_heap_capacity{ 4096 };
constexpr u32 constant_buffer_size{ 1024 * 1024 };

PFN_vkSetDebugUtilsObjectNameEXT set_debug_utils_object_name{ nullptr };

utl::vector<deferred_object> deferred_releases[frame_buffer_count]{};
u32 deferred_releases_flag[frame_buffer_count]{};
std::mutex deferred_releases_mutex{};

// Number of validation errors reported (validation is only on in debug builds). Printed at shutdown.
u32 validation_error_count{ 0 };

struct options
{
    bool enable_vsync{ true };   // enable/disable vsync
    bool enable_dxr{ false };    // enable/disable ray tracing (not supported yet)
    u32 msaa_samples{ 1 };      // MSAA samples (1, 2, 4, 8)
} options;

bool
failed_init()
{
    shutdown();
    return false;
}

VKAPI_ATTR VkBool32 VKAPI_CALL
debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
               const VkDebugUtilsMessengerCallbackDataEXT* data, void*)
{
    const char* label{ "info" };
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    {
        label = "ERROR";
        ++validation_error_count;
    }
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
        label = "warning";
    }
    log_message("::Vulkan %s: %s\n", label, data && data->pMessage ? data->pMessage : "");
    return VK_FALSE;
}

bool
has_layer(const utl::vector<VkLayerProperties>& layers, const char* name)
{
    for (const auto& layer : layers)
    {
        if (std::strcmp(layer.layerName, name) == 0) return true;
    }
    return false;
}

bool
has_extension(const utl::vector<VkExtensionProperties>& extensions, const char* name)
{
    for (const auto& extension : extensions)
    {
        if (std::strcmp(extension.extensionName, name) == 0) return true;
    }
    return false;
}

// Validation is on in debug builds if the layer is installed. PRIMAL_VULKAN_VALIDATION=0 turns it off.
bool
use_validation()
{
#ifdef _DEBUG
    constexpr const char* name{ "PRIMAL_VULKAN_VALIDATION" };
#ifdef _MSC_VER
    // std::getenv is deprecated by MSVC (C4996).
    char* env{ nullptr };
    size_t size{ 0 };
    if (_dupenv_s(&env, &size, name) != 0 || !env) return true;
    const bool enabled{ env[0] != '0' };
    std::free(env);
    return enabled;
#else
    const char* const env{ std::getenv(name) };
    return !env || env[0] != '0';
#endif
#else
    return false;
#endif
}

bool
create_instance()
{
#if PRIMAL_PLATFORM_WINDOWS
    // vulkan-1.dll is delay-loaded (Engine/Vulkan.props) so D3D12-only machines can still start.
    // Load it here first: calling into a missing delay-loaded DLL raises an SEH exception.
    if (!LoadLibraryW(L"vulkan-1.dll"))
    {
        log_message("Vulkan: vulkan-1.dll (the Vulkan loader) wasn't found.\n");
        return false;
    }
#endif

    u32 api_version{ 0 };
    VkCall(vkEnumerateInstanceVersion(&api_version));
    if (api_version < minimum_api_version)
    {
        log_message("Vulkan: the loader supports Vulkan %u.%u, 1.3 is required.\n",
                    VK_API_VERSION_MAJOR(api_version), VK_API_VERSION_MINOR(api_version));
        return false;
    }

    u32 count{ 0 };
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    utl::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());

    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    utl::vector<VkExtensionProperties> extensions(count);
    vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data());

    utl::vector<const char*> enabled_layers;
    utl::vector<const char*> enabled_extensions;
    enabled_extensions.emplace_back(VK_KHR_SURFACE_EXTENSION_NAME);

    // Window systems. The native handle of each window says which one it uses (see VulkanSurface.cpp).
    u32 window_system_count{ 0 };
#if PRIMAL_PLATFORM_LINUX
    constexpr const char* window_system_extensions[]{ "VK_KHR_xlib_surface", "VK_KHR_wayland_surface" };
#else
    constexpr const char* window_system_extensions[]{ "VK_KHR_win32_surface" };
#endif
    for (const char* name : window_system_extensions)
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

    const bool validation{ use_validation() && has_layer(layers, validation_layer_name) };
    const bool debug_utils{ has_extension(extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) };
    // VK_EXT_layer_settings is implemented by the validation layer, so it's listed with the layer's
    // extensions.
    bool layer_settings{ false };
    if (validation)
    {
        layer_settings = has_extension(extensions, VK_EXT_LAYER_SETTINGS_EXTENSION_NAME);
        if (!layer_settings)
        {
            u32 layer_extension_count{ 0 };
            vkEnumerateInstanceExtensionProperties(validation_layer_name, &layer_extension_count, nullptr);
            utl::vector<VkExtensionProperties> layer_extensions(layer_extension_count);
            vkEnumerateInstanceExtensionProperties(validation_layer_name, &layer_extension_count, layer_extensions.data());
            layer_settings = has_extension(layer_extensions, VK_EXT_LAYER_SETTINGS_EXTENSION_NAME);
        }
    }
    if (validation) enabled_layers.emplace_back(validation_layer_name);
#ifdef _DEBUG
    if (debug_utils) enabled_extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (use_validation() && !validation)
    {
        log_message("Warning: Vulkan validation layer is not available. Install vulkan-validation-layers.\n");
    }
#endif
    if (layer_settings) enabled_extensions.emplace_back(VK_EXT_LAYER_SETTINGS_EXTENSION_NAME);

    VkApplicationInfo app_info{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app_info.pApplicationName = "Primal";
    app_info.pEngineName = "Primal";
    app_info.engineVersion = VK_MAKE_API_VERSION(0, version.major, version.minor, version.revision);
    app_info.apiVersion = minimum_api_version;

    VkInstanceCreateInfo info{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    info.pApplicationInfo = &app_info;
    info.enabledLayerCount = (u32)enabled_layers.size();
    info.ppEnabledLayerNames = enabled_layers.data();
    info.enabledExtensionCount = (u32)enabled_extensions.size();
    info.ppEnabledExtensionNames = enabled_extensions.data();

    // Synchronization validation (hazards between commands/submits), the counterpart of the
    // D3D12 debug layer's resource state checks.
    const VkBool32 enable{ VK_TRUE };
    const VkLayerSettingEXT settings[]{
        { validation_layer_name, "validate_sync", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &enable },
    };
    VkLayerSettingsCreateInfoEXT settings_info{ VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT };
    settings_info.settingCount = (u32)std::size(settings);
    settings_info.pSettings = &settings[0];

    // Messages from vkCreateInstance/vkDestroyInstance go to this messenger.
    VkDebugUtilsMessengerCreateInfoEXT messenger_info{ VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
    messenger_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    messenger_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    messenger_info.pfnUserCallback = debug_callback;

    const void** next{ &info.pNext };
    if (validation && debug_utils)
    {
        *next = &messenger_info;
        next = &messenger_info.pNext;
    }
    if (layer_settings)
    {
        *next = &settings_info;
    }

    if (!VkCall(vkCreateInstance(&info, nullptr, &vk_instance))) return false;
    if (validation) log_message("::Vulkan validation layer enabled%s\n", layer_settings ? " (+ synchronization validation)" : "");

#ifdef _DEBUG
    if (debug_utils)
    {
        set_debug_utils_object_name = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetInstanceProcAddr(vk_instance, "vkSetDebugUtilsObjectNameEXT");
        if (validation)
        {
            const auto create_messenger{ (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(vk_instance, "vkCreateDebugUtilsMessengerEXT") };
            // messenger_info was part of the vkCreateInstance pNext chain (pNext may point at settings_info);
            // vkCreateDebugUtilsMessengerEXT requires pNext == nullptr.
            messenger_info.pNext = nullptr;
            if (create_messenger) VkCall(create_messenger(vk_instance, &messenger_info, nullptr, &debug_messenger));
        }
    }
#endif

    return true;
}

struct device_features
{
    VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT mutable_descriptor{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT };
    VkPhysicalDeviceVulkan13Features vulkan13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, &mutable_descriptor };
    VkPhysicalDeviceVulkan12Features vulkan12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &vulkan13 };
    VkPhysicalDeviceVulkan11Features vulkan11{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, &vulkan12 };
    VkPhysicalDeviceFeatures2 features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &vulkan11 };

    device_features() = default;
    DISABLE_COPY_AND_MOVE(device_features); // the pNext chain points into this struct
};

bool
get_required_features(const device_features& supported, device_features& enabled, bool log_missing)
{
    bool ok{ true };
    auto require = [&](VkBool32 is_supported, VkBool32& enable, const char* name) {
        enable = VK_TRUE;
        if (!is_supported)
        {
            if (log_missing) log_message("Vulkan: missing device feature %s\n", name);
            ok = false;
        }
    };
#define REQUIRE(level, name) require(supported.level.name, enabled.level.name, #name)
    // Vulkan 1.0
    REQUIRE(features.features, shaderInt16);                    // -enable-16bit-types
    REQUIRE(features.features, samplerAnisotropy);
    REQUIRE(features.features, textureCompressionBC);           // BC1-7 textures (always available in D3D12)
    REQUIRE(features.features, multiDrawIndirect);              // ExecuteIndirect equivalent
    REQUIRE(features.features, drawIndirectFirstInstance);
    // Vulkan 1.1
    REQUIRE(vulkan11, storageBuffer16BitAccess);                // -enable-16bit-types
    REQUIRE(vulkan11, uniformAndStorageBuffer16BitAccess);
    REQUIRE(vulkan11, shaderDrawParameters);
    // Vulkan 1.2
    REQUIRE(vulkan12, timelineSemaphore);                       // frame fences
    REQUIRE(vulkan12, scalarBlockLayout);                       // -fvk-use-dx-layout
    REQUIRE(vulkan12, shaderFloat16);                           // -enable-16bit-types
    REQUIRE(vulkan12, descriptorIndexing);                      // ResourceDescriptorHeap[]
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
    REQUIRE(vulkan13, dynamicRendering);                        // no render pass objects
    REQUIRE(vulkan13, synchronization2);
    REQUIRE(vulkan13, maintenance4);
    // VK_EXT_mutable_descriptor_type: typed views aliasing ResourceDescriptorHeap[]
    REQUIRE(mutable_descriptor, mutableDescriptorType);
#undef REQUIRE
    return ok;
}

constexpr const char* required_device_extensions[]{
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME,
    // Root CBVs/SRVs/UAVs (core in Vulkan 1.4, but 1.3 is the minimum).
    VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
};

// Returns the index of a queue family that supports graphics and compute, or u32_invalid_id.
// NOTE: presenting is checked per window (vulkan_surface::create()).
u32
find_graphics_queue_family(VkPhysicalDevice physical_device)
{
    u32 count{ 0 };
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, nullptr);
    utl::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, families.data());
    constexpr VkQueueFlags required{ VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT };
    for (u32 i{ 0 }; i < count; ++i)
    {
        if ((families[i].queueFlags & required) == required) return i;
    }
    return u32_invalid_id;
}

bool
is_device_suitable(VkPhysicalDevice physical_device, bool log_missing)
{
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical_device, &properties);
    if (properties.apiVersion < minimum_api_version)
    {
        if (log_missing) log_message("Vulkan: %s supports Vulkan %u.%u, 1.3 is required.\n", properties.deviceName,
                                     VK_API_VERSION_MAJOR(properties.apiVersion), VK_API_VERSION_MINOR(properties.apiVersion));
        return false;
    }

    u32 count{ 0 };
    vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &count, nullptr);
    utl::vector<VkExtensionProperties> extensions(count);
    vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &count, extensions.data());
    bool ok{ true };
    for (const char* name : required_device_extensions)
    {
        if (!has_extension(extensions, name))
        {
            if (log_missing) log_message("Vulkan: %s doesn't support %s\n", properties.deviceName, name);
            ok = false;
        }
    }
    if (!ok) return false;

    device_features supported{};
    vkGetPhysicalDeviceFeatures2(physical_device, &supported.features);
    device_features enabled{};
    if (!get_required_features(supported, enabled, log_missing)) return false;

    return find_graphics_queue_family(physical_device) != u32_invalid_id;
}

// Get the most performing GPU that supports everything we need (discrete > integrated > others).
// NOTE: like d3d12's determine_main_adapter(), this could later let the user choose.
VkPhysicalDevice
determine_main_physical_device()
{
    u32 count{ 0 };
    vkEnumeratePhysicalDevices(vk_instance, &count, nullptr);
    utl::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(vk_instance, &count, devices.data());

    auto rank = [](VkPhysicalDeviceType type) {
        switch (type)
        {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return 3;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return 2;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return 1;
        default: return 0; // CPU (lavapipe) and others
        }
    };

    VkPhysicalDevice best{ VK_NULL_HANDLE };
    s32 best_rank{ -1 };
    for (const VkPhysicalDevice device : devices)
    {
        if (!is_device_suitable(device, false)) continue;
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device, &properties);
        if (rank(properties.deviceType) > best_rank)
        {
            best = device;
            best_rank = rank(properties.deviceType);
        }
    }

    if (!best)
    {
        // Say why each device was rejected.
        for (const VkPhysicalDevice device : devices) is_device_suitable(device, true);
    }

    return best;
}

bool
create_device()
{
    main_physical_device = determine_main_physical_device();
    if (!main_physical_device)
    {
        log_message("Vulkan: no suitable GPU found.\n");
        return false;
    }

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(main_physical_device, &properties);
    log_message("::Vulkan device: %s (Vulkan %u.%u.%u)\n", properties.deviceName,
                VK_API_VERSION_MAJOR(properties.apiVersion), VK_API_VERSION_MINOR(properties.apiVersion),
                VK_API_VERSION_PATCH(properties.apiVersion));

    // Offsets of storage buffer ranges (root SRVs/UAVs) must be multiples of minStorageBufferOffsetAlignment (4 on
    // AMD/Mesa, 16 on NVIDIA, at most 256). D3D12 only needs 4.
    storage_buffer_alignment = std::max((u32)properties.limits.minStorageBufferOffsetAlignment, 16u);

    const u32 queue_family{ find_graphics_queue_family(main_physical_device) };
    const f32 priority{ 1.f };
    VkDeviceQueueCreateInfo queue_info{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    queue_info.queueFamilyIndex = queue_family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;

    device_features supported{};
    vkGetPhysicalDeviceFeatures2(main_physical_device, &supported.features);
    device_features enabled{};
    get_required_features(supported, enabled, true);

    VkDeviceCreateInfo info{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    info.pNext = &enabled.features;
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queue_info;
    info.enabledExtensionCount = (u32)std::size(required_device_extensions);
    info.ppEnabledExtensionNames = &required_device_extensions[0];

    if (!VkCall(vkCreateDevice(main_physical_device, &info, nullptr, &main_device))) return false;
    NAME_VK_OBJECT(main_device, VK_OBJECT_TYPE_DEVICE, "Main Vulkan Device");
    vkx::load_device_functions(main_device);

    return gfx_command.initialize(main_device, queue_family);
}

bool
create_allocator()
{
    VmaAllocatorCreateInfo info{};
    info.vulkanApiVersion = minimum_api_version;
    info.physicalDevice = main_physical_device;
    info.device = main_device;
    info.instance = vk_instance;
    return VkCall(vmaCreateAllocator(&info, &vma_allocator));
}

// A zeroed GPU buffer bound wherever a root SRV has no buffer yet (D3D12 binds address 0 there): light buffers before
// a light set has lights, element buffers of position-only meshes. 256 KB covers a 4K surface's light grid (120 x 68
// tiles of uint2).
bool
create_zero_buffer()
{
    constexpr u32 size{ 256 * 1024 };
    const std::unique_ptr<u8[]> zeros{ std::make_unique<u8[]>(size) }; // value-initialized
    vulkan_buffer_init_info info{};
    info.data = zeros.get();
    info.size = size;
    info.alignment = sizeof(math::v4);
    info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    zero_buffer_object = vulkan_buffer{ info, false };
    NAME_VK_OBJECT(zero_buffer_object.buffer(), VK_OBJECT_TYPE_BUFFER, "Zero Buffer");
    return zero_buffer_object.buffer() != VK_NULL_HANDLE;
}

bool
create_static_samplers()
{
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(main_physical_device, &properties);
    constexpr const char* names[]{ "Static Sampler - point", "Static Sampler - linear", "Static Sampler - anisotropic" };
    static_assert(std::size(names) == vkx::static_sampler::count);

    for (u32 i{ 0 }; i < vkx::static_sampler::count; ++i)
    {
        VkSamplerCreateInfo info{ vkx::sampler_info((vkx::static_sampler::type)i) };
        info.maxAnisotropy = std::min(info.maxAnisotropy, properties.limits.maxSamplerAnisotropy);
        if (!VkCall(vkCreateSampler(main_device, &info, nullptr, &static_samplers[i]))) return false;
        NAME_VK_OBJECT(static_samplers[i], VK_OBJECT_TYPE_SAMPLER, names[i]);
    }
    return true;
}

void
destroy_object(const deferred_object& object)
{
    const VkDevice device{ main_device };
    switch (object.type)
    {
    case VK_OBJECT_TYPE_SWAPCHAIN_KHR: vkDestroySwapchainKHR(device, (VkSwapchainKHR)object.handle, nullptr); break;
    case VK_OBJECT_TYPE_SURFACE_KHR: vkDestroySurfaceKHR(vk_instance, (VkSurfaceKHR)object.handle, nullptr); break;
    case VK_OBJECT_TYPE_IMAGE_VIEW: vkDestroyImageView(device, (VkImageView)object.handle, nullptr); break;
    case VK_OBJECT_TYPE_IMAGE:
        if (object.allocation) vmaDestroyImage(vma_allocator, (VkImage)object.handle, object.allocation);
        else vkDestroyImage(device, (VkImage)object.handle, nullptr);
        break;
    case VK_OBJECT_TYPE_BUFFER:
        if (object.allocation) vmaDestroyBuffer(vma_allocator, (VkBuffer)object.handle, object.allocation);
        else vkDestroyBuffer(device, (VkBuffer)object.handle, nullptr);
        break;
    case VK_OBJECT_TYPE_DESCRIPTOR_POOL: vkDestroyDescriptorPool(device, (VkDescriptorPool)object.handle, nullptr); break;
    case VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT:
        vkDestroyDescriptorSetLayout(device, (VkDescriptorSetLayout)object.handle, nullptr);
        break;
    case VK_OBJECT_TYPE_SAMPLER: vkDestroySampler(device, (VkSampler)object.handle, nullptr); break;
    case VK_OBJECT_TYPE_PIPELINE: vkDestroyPipeline(device, (VkPipeline)object.handle, nullptr); break;
    case VK_OBJECT_TYPE_PIPELINE_LAYOUT: vkDestroyPipelineLayout(device, (VkPipelineLayout)object.handle, nullptr); break;
    case VK_OBJECT_TYPE_SEMAPHORE: vkDestroySemaphore(device, (VkSemaphore)object.handle, nullptr); break;
    default:
        assert(false && "Vulkan: deferred release of an unsupported object type.");
        break;
    }
}

void
process_deferred_releases(u32 frame_idx, bool force_release = false)
{
    std::scoped_lock lock{ deferred_releases_mutex };

    // NOTE: The objects could still be in use in previous frames. So we wait
    //       another round before destroying them.
    //       force_release is used during shutdown, where we don't have to wait.
    if (!force_release && ++deferred_releases_flag[frame_idx] < frame_buffer_count) return;

    deferred_releases_flag[frame_idx] = 0;

    srv_desc_heap.process_deferred_free(frame_idx);

    utl::vector<deferred_object>& objects{ deferred_releases[frame_idx] };
    for (const auto& object : objects) destroy_object(object);
    objects.clear();
}

// Counterpart of d3d12's get_d3d12_frame_info(): fills GlobalShaderData in the frame's constant buffer.
vulkan_frame_info
get_vulkan_frame_info(const frame_info& info, constant_buffer& cbuffer, const vulkan_surface& surface, surface_id id,
                      u32 frame_idx)
{
    hlsl::GlobalShaderData data{};

    using namespace DirectX;
    camera::vulkan_camera* camera{ nullptr };
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
        // No camera (the SURFACES test): identity matrices; nothing is drawn, post-processing shows the gpass clear color.
        const XMMATRIX identity{ XMMatrixIdentity() };
        XMStoreFloat4x4A(&data.View, identity);
        XMStoreFloat4x4A(&data.Projection, identity);
        XMStoreFloat4x4A(&data.InvProjection, identity);
        XMStoreFloat4x4A(&data.ViewProjection, identity);
        XMStoreFloat4x4A(&data.InvViewProjection, identity);
        data.CameraDirection = { 0.f, 0.f, -1.f };
        // No lights (NumDirectionalLights 0) and no ambient light (what light::ambient_light() returns without one).
        data.AmbientLight = { -1.f, u32_invalid_id, u32_invalid_id, u32_invalid_id };
    }
    data.ViewWidth = (f32)surface.width();
    data.ViewHeight = (f32)surface.height();
    data.DeltaTime = info.average_frame_time;

    // NOTE: be careful not to read from this buffer. Reads are really really slow (write-combined memory).
    hlsl::GlobalShaderData* const shader_data{ cbuffer.allocate<hlsl::GlobalShaderData>() };
    // TODO: handle the case when cbuffer is full.
    assert(shader_data);
    memcpy(shader_data, &data, sizeof(hlsl::GlobalShaderData));

    vulkan_frame_info vulkan_info{};
    vulkan_info.info = &info;
    vulkan_info.camera = camera;
    vulkan_info.global_shader_data = cbuffer.range(shader_data);
    vulkan_info.surface_width = surface.width();
    vulkan_info.surface_height = surface.height();
    vulkan_info.surface_id = (id::id_type)id;
    vulkan_info.light_culling_id = surface.light_culling_id();
    vulkan_info.frame_index = frame_idx;
    return vulkan_info;
}

} // anonymous namespace

namespace detail {
void
deferred_release(VkObjectType type, u64 handle, VmaAllocation allocation)
{
    std::scoped_lock lock{ deferred_releases_mutex };
    const u32 frame_idx{ current_frame_index() };
    deferred_releases[frame_idx].push_back({ type, handle, allocation });
    deferred_releases_flag[frame_idx] = 1;
}
} // namespace detail

bool
initialize()
{
    if (main_device) shutdown();

    if (!create_instance()) return failed_init();
    if (!create_device()) return failed_init();
    if (!create_allocator()) return failed_init();
    if (!create_static_samplers()) return failed_init();
    if (!srv_desc_heap.initialize(srv_heap_capacity)) return failed_init();

    for (u32 i{ 0 }; i < frame_buffer_count; ++i)
    {
        new (&constant_buffers[i]) constant_buffer{ constant_buffer::get_default_init_info(constant_buffer_size) };
        if (!constant_buffers[i].buffer()) return failed_init();
        NAME_VK_OBJECT_INDEXED(constant_buffers[i].buffer(), VK_OBJECT_TYPE_BUFFER, i, "Global Constant Buffer");
    }

    // initialize modules
    // NOTE: upload goes first (D3D12: after fx): modules may upload data while they initialize (gpass does).
    if (!(upload::initialize() && create_zero_buffer() && shaders::initialize() && gpass::initialize() &&
          fx::initialize() && content::initialize() && delight::initialize()))
        return failed_init();
    return true;
}

void
shutdown()
{
    gfx_command.release();

    for (u32 i{ 0 }; i < frame_buffer_count; ++i)
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

    for (u32 i{ 0 }; i < frame_buffer_count; ++i)
    {
        constant_buffers[i].release();
    }

    // NOTE: modules may defer releases (and descriptor frees) while shutting down. Process those once
    //       more, then release the heap, which defers its own pool and layout.
    process_deferred_releases(0, true);
    srv_desc_heap.release();
    process_deferred_releases(0, true);

    assert(surfaces.empty() && "Vulkan: remove all surfaces before shutting down the renderer.");

    for (VkSampler& sampler : static_samplers)
    {
        if (sampler) vkDestroySampler(main_device, sampler, nullptr);
        sampler = VK_NULL_HANDLE;
    }

    if (vma_allocator) vmaDestroyAllocator(vma_allocator);
    vma_allocator = VK_NULL_HANDLE;

    if (main_device) vkDestroyDevice(main_device, nullptr);
    main_device = VK_NULL_HANDLE;
    main_physical_device = VK_NULL_HANDLE;

    if (debug_messenger)
    {
        const auto destroy_messenger{ (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(vk_instance, "vkDestroyDebugUtilsMessengerEXT") };
        if (destroy_messenger) destroy_messenger(vk_instance, debug_messenger, nullptr);
        debug_messenger = VK_NULL_HANDLE;
    }
    set_debug_utils_object_name = nullptr;

    if (vk_instance) vkDestroyInstance(vk_instance, nullptr);
    vk_instance = VK_NULL_HANDLE;

    if (validation_error_count) log_message("::Vulkan: %u validation error(s) were reported.\n", validation_error_count);
    validation_error_count = 0;
}

void
set_option(renderer_option::option option, const void* const parameter, [[maybe_unused]] u32 parameter_size)
{
    assert(option < renderer_option::count);
    assert(parameter && parameter_size);

    switch (option)
    {
    case renderer_option::vsync: {
        assert(parameter_size == sizeof(bool));
        // NOTE: surfaces fall back to FIFO (vsync) if the window system has no mailbox/immediate
        //       mode. They pick up the change on their next frame (vulkan_surface::needs_resize()).
        options.enable_vsync = *(const bool*)parameter;
    }
    break;
    case renderer_option::raytracing: {
        assert(parameter_size == sizeof(bool));
        options.enable_dxr = *(const bool*)parameter;
    }
    break;
    case renderer_option::msaa: {
        assert(parameter_size == sizeof(u32));
        const u32 msaa_samples{ *(const u32*)parameter };
        if (msaa_samples == 1 || msaa_samples == 2 || msaa_samples == 4 || msaa_samples == 8)
        {
            options.msaa_samples = msaa_samples;
        }
    }
    break;
    default:
        break;
    }
}

void
get_option(renderer_option::option option, void* const parameter, [[maybe_unused]] u32 parameter_size)
{
    assert(option < renderer_option::count);
    assert(parameter && parameter_size);
    switch (option)
    {
    case renderer_option::vsync:
        assert(parameter_size == sizeof(bool));
        *(bool*)parameter = options.enable_vsync;
        break;
    case renderer_option::raytracing:
        assert(parameter_size == sizeof(bool));
        *(bool*)parameter = options.enable_dxr;
        break;
    case renderer_option::msaa:
        assert(parameter_size == sizeof(u32));
        *(u32*)parameter = options.msaa_samples;
        break;
    default:
        break;
    }
}

VkInstance
instance()
{
    return vk_instance;
}

VkPhysicalDevice
physical_device()
{
    return main_physical_device;
}

VkDevice
device()
{
    return main_device;
}

VkQueue
graphics_queue()
{
    return gfx_command.queue();
}

void
submit_to_graphics_queue(const VkSubmitInfo2& submit)
{
    gfx_command.submit(submit);
}

u32
graphics_queue_family()
{
    return gfx_command.queue_family();
}

VmaAllocator
allocator()
{
    return vma_allocator;
}

descriptor_heap&
srv_heap()
{
    return srv_desc_heap;
}

constant_buffer&
cbuffer()
{
    return constant_buffers[current_frame_index()];
}

vkx::buffer_range
zero_buffer()
{
    return zero_buffer_object.range();
}

u32
storage_buffer_offset_alignment()
{
    return storage_buffer_alignment;
}

VkSampler
static_sampler(vkx::static_sampler::type type)
{
    assert(type < vkx::static_sampler::count);
    return static_samplers[type];
}

u32
current_frame_index()
{
    return gfx_command.frame_index();
}

void
set_deferred_releases_flag(u32 frame_idx)
{
    std::scoped_lock lock{ deferred_releases_mutex };
    deferred_releases_flag[frame_idx] = 1;
}

bool
vsync_enabled()
{
    return options.enable_vsync;
}

void
set_object_name([[maybe_unused]] u64 handle, [[maybe_unused]] VkObjectType type, [[maybe_unused]] const char* name)
{
    if (!set_debug_utils_object_name || !main_device || !handle) return;
    VkDebugUtilsObjectNameInfoEXT info{ VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
    info.objectType = type;
    info.objectHandle = handle;
    info.pObjectName = name;
    set_debug_utils_object_name(main_device, &info);
}

surface
create_surface(platform::window window)
{
    surface_id id{ surfaces.add(window) };
    if (!surfaces[id].create())
    {
        surfaces.remove(id);
        return {};
    }
    return surface{ id };
}

void
remove_surface(surface_id id)
{
    gfx_command.flush();
    surfaces.remove(id);
}

void
resize_surface(surface_id id, u32, u32)
{
    // NOTE: SDL sends a resize event when a window is first shown, at the size the swapchain was just created with.
    //       Skip the flush + recreate then (render_surface() checks again every frame anyway).
    if (!surfaces[id].needs_resize()) return;
    gfx_command.flush();
    surfaces[id].resize();
}

u32
surface_width(surface_id id)
{
    return surfaces[id].width();
}

u32
surface_height(surface_id id)
{
    return surfaces[id].height();
}

void
render_surface(surface_id id, frame_info info)
{
    vulkan_surface& surface{ surfaces[id] };

    // Window resized, swapchain out of date, or vsync toggled.
    if (surface.needs_resize())
    {
        gfx_command.flush();
        surface.resize();
    }

    // Wait for the GPU to finish with this frame's command buffer and reset it.
    const VkCommandBuffer cmd{ gfx_command.begin_frame() };
    const u32 frame_idx{ current_frame_index() };

    if (deferred_releases_flag[frame_idx])
    {
        process_deferred_releases(frame_idx);
    }

    constant_buffer& cb{ constant_buffers[frame_idx] };
    cb.clear();

    // Nothing to render to (minimized or out of date). The frame is skipped; its command
    // buffer is reset by the next begin_frame().
    if (!surface.acquire_next_image(gfx_command.image_acquired_semaphore())) return;

    const vulkan_frame_info vulkan_info{ get_vulkan_frame_info(info, cb, surface, id, frame_idx) };

    gpass::set_size({ vulkan_info.surface_width, vulkan_info.surface_height });
    vkx::vulkan_barrier& barriers{ resource_barriers };

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
    constexpr vkx::resource_state acquired{ VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_NONE,
                                            VK_IMAGE_LAYOUT_UNDEFINED };
    barriers.add(surface.image(), VK_IMAGE_ASPECT_COLOR_BIT, acquired, vkx::resource_states.render_target);
    barriers.apply(cmd);
    // Will write to the current back buffer, so back buffer is a render target
    fx::post_process(cmd, vulkan_info, surface.image_view(), surface.format());
    // after post process
    vkx::transition_image(cmd, surface.image(), VK_IMAGE_ASPECT_COLOR_BIT, vkx::resource_states.render_target,
                          vkx::resource_states.present);

    // Done recording commands. Now submit, present and signal the fence value for this frame.
    gfx_command.end_frame(surface);
}
} // namespace primal::graphics::vulkan::core
