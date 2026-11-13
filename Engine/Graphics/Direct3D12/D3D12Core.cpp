// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#define INITGUID

#include "D3D12Core.h"

#include "D3D12Camera.h"
#include "D3D12Content.h"
#include "D3D12GPass.h"
#include "D3D12Light.h"
#include "D3D12LightCulling.h"
#include "D3D12PostProcess.h"
#include "D3D12Shaders.h"
#include "D3D12Surface.h"
#include "D3D12Upload.h"
#include "Graphics/Shaders/SharedTypes.h"

#include <cstdlib>
#include <memory>
#include <span>

import primal.jobs;

using namespace Microsoft::WRL;

namespace primal::graphics::d3d12::core
{

namespace
{

class D3D12Command
{
  public:
    D3D12Command() = default;
    DISABLE_COPY_AND_MOVE(D3D12Command);
    explicit D3D12Command(id3d12_device *const device, D3D12_COMMAND_LIST_TYPE type)
    {
        HRESULT hr{S_OK};
        D3D12_COMMAND_QUEUE_DESC desc{};
        desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        desc.NodeMask = 0;
        desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        desc.Type = type;

        DXCall(hr = device->CreateCommandQueue(&desc, IID_PPV_ARGS(&_cmd_queue)));
        if (FAILED(hr))
        {
            goto _error;
        }
        NAME_D3D12_OBJECT(
            _cmd_queue,
            type == D3D12_COMMAND_LIST_TYPE_DIRECT    ? L"GFX Command Queue"
            : type == D3D12_COMMAND_LIST_TYPE_COMPUTE ? L"Compute Command Queue"
                                                      : L"Command Queue");

        for (u32 i{0}; i < frame_buffer_count; ++i)
        {
            CommandFrame &frame{_cmd_frames[i]};
            DXCall(hr = device->CreateCommandAllocator(type, IID_PPV_ARGS(&frame.cmd_allocator)));
            if (FAILED(hr))
            {
                goto _error;
            }
            NAME_D3D12_OBJECT_INDEXED(
                frame.cmd_allocator,
                i,
                type == D3D12_COMMAND_LIST_TYPE_DIRECT    ? L"GFX Command Allocator"
                : type == D3D12_COMMAND_LIST_TYPE_COMPUTE ? L"Compute Command Allocator"
                                                          : L"Command Allocator");
        }

        DXCall(
            hr = device->CreateCommandList(0, type, _cmd_frames[0].cmd_allocator, nullptr, IID_PPV_ARGS(&_cmd_list)));
        if (FAILED(hr))
        {
            goto _error;
        }
        DXCall(_cmd_list->Close());
        NAME_D3D12_OBJECT(
            _cmd_list,
            type == D3D12_COMMAND_LIST_TYPE_DIRECT    ? L"GFX Command List"
            : type == D3D12_COMMAND_LIST_TYPE_COMPUTE ? L"Compute Command List"
                                                      : L"Command List");
        _lists.push_back(_cmd_list);
        _device = device;
        _type = type;

        DXCall(hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_fence)));
        if (FAILED(hr))
        {
            goto _error;
        }
        NAME_D3D12_OBJECT(_fence, L"D3D12 Fence");

        return;

    _error:
        release();
    }

    ~D3D12Command()
    {
        assert(!_cmd_queue && _lists.empty() && !_fence);
    }

    // Wait for the current frame to be signaled and reset the command list/allocator.
    auto begin_frame() -> void
    {
        const CommandFrame &frame{_cmd_frames[_frame_index]};
        frame.wait(_fence);
        DXCall(frame.cmd_allocator->Reset());
        _queued.clear();
        _list_index = 0;
        _cmd_list = _lists[0];
        DXCall(_cmd_list->Reset(frame.cmd_allocator, nullptr));
    }

    // Closes the command list being recorded, queues it and then 'lists' (closed command lists recorded elsewhere),
    // and continues the frame in another of this frame's command lists. A new list starts with no state set.
    auto queue_and_continue(std::span<ID3D12CommandList *const> lists) -> void
    {
        DXCall(_cmd_list->Close());
        _queued.push_back(_cmd_list);
        for (ID3D12CommandList *const list : lists)
        {
            _queued.push_back(list);
        }

        ID3D12CommandAllocator *const allocator{_cmd_frames[_frame_index].cmd_allocator};
        if (++_list_index == _lists.size())
        {
            id3d12_graphics_command_list *list{nullptr};
            DXCall(_device->CreateCommandList(0, _type, allocator, nullptr, IID_PPV_ARGS(&list)));
            NAME_D3D12_OBJECT_INDEXED(list, _list_index, L"GFX Command List");
            _lists.push_back(list);
        }
        else
        {
            DXCall(_lists[_list_index]->Reset(allocator, nullptr));
        }
        _cmd_list = _lists[_list_index];
    }

    // Signal the fence with the new fence value.
    auto end_frame(const D3D12Surface &surface) -> void
    {
        DXCall(_cmd_list->Close());
        _queued.push_back(_cmd_list);
        _cmd_queue->ExecuteCommandLists(static_cast<u32>(_queued.size()), _queued.data());

        // Presenting swap chain buffers happens in lockstep with frame buffers.
        surface.present();

        const u64 fence_value{++_fence_value};
        CommandFrame &frame{_cmd_frames[_frame_index]};
        frame.fence_value = fence_value;
        DXCall(_cmd_queue->Signal(_fence, fence_value));

        _frame_index = (_frame_index + 1) % frame_buffer_count;
    }

    auto flush() -> void
    {
        for (u32 i{0}; i < frame_buffer_count; ++i)
        {
            _cmd_frames[i].wait(_fence);
        }
        _frame_index = 0;
    }

    auto release() -> void
    {
        flush();
        core::release(_fence);
        _fence_value = 0;

        core::release(_cmd_queue);
        for (id3d12_graphics_command_list *&list : _lists)
        {
            core::release(list);
        }
        _lists.clear();
        _queued.clear();
        _cmd_list = nullptr;

        for (u32 i{0}; i < frame_buffer_count; ++i)
        {
            _cmd_frames[i].release();
        }
    }

    [[nodiscard]] constexpr auto command_queue() const -> ID3D12CommandQueue *
    {
        return _cmd_queue;
    }
    [[nodiscard]] constexpr auto command_list() const -> id3d12_graphics_command_list *
    {
        return _cmd_list;
    }
    [[nodiscard]] constexpr auto frame_index() const -> u32
    {
        return _frame_index;
    }

  private:
    struct CommandFrame
    {
        ID3D12CommandAllocator *cmd_allocator{nullptr};
        u64 fence_value{0};

        auto wait(ID3D12Fence1 *fence) const -> void
        {
            assert(fence);
            // If the current fence value is still less than "fence_value"
            // then we know the GPU has not finished executing the command lists
            // since it has not reached the "_cmd_queue->Signal()" command
            if (fence->GetCompletedValue() < fence_value)
            {
                // We have the fence create an event which is signaled once the fence's current value equals
                // "fence_value" Wait until the fence has triggered the event that its current value has reached
                // "fence_value" indicating that command queue has finished executing.
                DXCall(fence->SetEventOnCompletion(fence_value, nullptr));
            }
        }

        auto release() -> void
        {
            core::release(cmd_allocator);
            fence_value = 0;
        }
    };

    ID3D12CommandQueue *_cmd_queue{nullptr};
    // The frame's commands may span several lists (queue_and_continue()): _lists owns them (reused every frame; a
    // submitted list may be reset at once, its memory belongs to the allocator), _cmd_list is the one recording.
    id3d12_graphics_command_list *_cmd_list{nullptr};
    utl::vector<id3d12_graphics_command_list *> _lists;
    u32 _list_index{0};
    utl::vector<ID3D12CommandList *> _queued;
    id3d12_device *_device{nullptr};
    D3D12_COMMAND_LIST_TYPE _type{D3D12_COMMAND_LIST_TYPE_DIRECT};
    ID3D12Fence1 *_fence{nullptr};
    u64 _fence_value{0};
    CommandFrame _cmd_frames[frame_buffer_count]{};
    u32 _frame_index{0};
};

constexpr UINT d3d12_sdk_version{D3D12_SDK_VERSION};
constexpr const char *d3d12_sdk_path{".\\D3D12\\"};
constexpr D3D_FEATURE_LEVEL minimum_feature_level{D3D_FEATURE_LEVEL_11_0};

using surface_collection = utl::free_list<D3D12Surface>;

ID3D12SDKConfiguration1 *d3d12_sdk_config{nullptr};
ID3D12DeviceFactory *d3d12_device_factory{nullptr};
id3d12_device *main_device{nullptr};
IDXGIFactory7 *dxgi_factory{nullptr};
D3D12Command gfx_command;
surface_collection surfaces;
d3dx::D3D12ResourceBarrier resource_barriers{};
ConstantBuffer constant_buffers[frame_buffer_count];

DescriptorHeap rtv_desc_heap{D3D12_DESCRIPTOR_HEAP_TYPE_RTV};
DescriptorHeap dsv_desc_heap{D3D12_DESCRIPTOR_HEAP_TYPE_DSV};
DescriptorHeap srv_desc_heap{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV};
DescriptorHeap uav_desc_heap{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV};

utl::vector<IUnknown *> deferred_releases[frame_buffer_count]{};
u32 deferred_releases_flag[frame_buffer_count]{};
std::mutex deferred_releases_mutex{};

// VSync can only be disabled if DXGI supports tearing.
b32 tearing_is_supported{0};

// The per-frame constant buffer's usual size. A frame with many render items gets a bigger one (reserve()).
constexpr u32 constant_buffer_size{1024 * 1024};
// Room reserved for what the frame allocates besides the gpass's render items (global shader data, ...).
constexpr u64 other_constant_bytes{256 * 1024};

// One per thread that records command lists in jobs: an allocator per frame, and the lists recorded with it (reused
// after the allocator is reset). Slot jobs::worker_index(), or the last slot for a thread outside the job system.
struct ThreadCommandLists
{
    ID3D12CommandAllocator *allocators[frame_buffer_count]{};
    utl::vector<id3d12_graphics_command_list *> lists[frame_buffer_count];
    u32 used[frame_buffer_count]{};
};
// unique_ptr: the vector grows on the rendering thread while no job records; a job keeps a reference meanwhile.
utl::vector<std::unique_ptr<ThreadCommandLists>> thread_command_lists;

// Set from the environment in initialize().
bool parallel_recording{true}; // PRIMAL_D3D12_PARALLEL_RECORDING (default 1)
bool draw_indirect{false};     // PRIMAL_D3D12_DRAW_INDIRECT (default 0)

// The viewport and scissor of the surface being rendered, for command lists that start in the middle of a frame.
D3D12_VIEWPORT current_viewport{};
D3D12_RECT current_scissor_rect{};

auto env_flag(const char *name, bool fallback) -> bool
{
    // std::getenv is deprecated by MSVC (C4996).
    char *env{nullptr};
    size_t size{0};
    if (::_dupenv_s(&env, &size, name) != 0 || !env)
    {
        return fallback;
    }
    const bool value{env[0] != '0'};
    std::free(env);
    return value;
}

// What every command list of a surface's frame starts with: the shader-visible heap, viewport and scissor.
auto set_frame_state(id3d12_graphics_command_list *cmd_list) -> void
{
    ID3D12DescriptorHeap *const heaps[]{srv_desc_heap.heap()};
    cmd_list->SetDescriptorHeaps(1, &heaps[0]);
    cmd_list->RSSetViewports(1, &current_viewport);
    cmd_list->RSSetScissorRects(1, &current_scissor_rect);
}

auto reset_thread_command_lists(u32 frame_idx) -> void
{
    for (const std::unique_ptr<ThreadCommandLists> &lists : thread_command_lists)
    {
        DXCall(lists->allocators[frame_idx]->Reset());
        lists->used[frame_idx] = 0;
    }
}

auto release_thread_command_lists() -> void
{
    for (const std::unique_ptr<ThreadCommandLists> &lists : thread_command_lists)
    {
        for (u32 i{0}; i < frame_buffer_count; ++i)
        {
            for (id3d12_graphics_command_list *&list : lists->lists[i])
            {
                release(list);
            }
            release(lists->allocators[i]);
        }
    }
    thread_command_lists.clear();
}

#ifdef _DEBUG
// The debug layer's messages go to the log (Category::d3d12) through this callback, instead of only to a debugger.
ID3D12InfoQueue1 *debug_info_queue{nullptr};
DWORD debug_message_callback_cookie{0};

// NOLINTNEXTLINE(modernize-use-trailing-return-type): a D3D12 callback with a calling convention.
void CALLBACK debug_layer_message(
    D3D12_MESSAGE_CATEGORY /*category*/,
    D3D12_MESSAGE_SEVERITY severity,
    D3D12_MESSAGE_ID id,
    LPCSTR description,
    void * /*context*/)
{
    const std::string_view text{description ? description : ""};
    const auto id_number{std::to_underlying(id)};
    switch (severity)
    {
        case D3D12_MESSAGE_SEVERITY_CORRUPTION:
        case D3D12_MESSAGE_SEVERITY_ERROR:
            // error() returns when the line is written: SetBreakOnSeverity() breaks right after this.
            log::error(log::Category::d3d12, "Debug layer: {} (message {})", text, id_number);
            break;
        case D3D12_MESSAGE_SEVERITY_WARNING:
            log::warning(log::Category::d3d12, "Debug layer: {} (message {})", text, id_number);
            break;
        case D3D12_MESSAGE_SEVERITY_INFO:
        case D3D12_MESSAGE_SEVERITY_MESSAGE:
        default: log::debug(log::Category::d3d12, "Debug layer: {} (message {})", text, id_number); break;
    }
}

// Needs ID3D12InfoQueue1 (Windows 11, or the Agility SDK's debug layer); without it the messages only reach a
// debugger, as before.
auto start_debug_layer_logging(ID3D12InfoQueue *info_queue) -> void
{
    if (FAILED(info_queue->QueryInterface(IID_PPV_ARGS(&debug_info_queue))))
    {
        log::debug(
            log::Category::d3d12, "ID3D12InfoQueue1 isn't available: debug layer messages go to the debugger only.");
        return;
    }

    if (FAILED(debug_info_queue->RegisterMessageCallback(
            &debug_layer_message, D3D12_MESSAGE_CALLBACK_FLAG_NONE, nullptr, &debug_message_callback_cookie)))
    {
        log::warning(log::Category::d3d12, "Couldn't register the debug layer message callback.");
        release(debug_info_queue);
        return;
    }

    // The debug layer keeps writing to the debugger too: SetMuteDebugOutput(TRUE) would also stop the callback
    // so in a debugger each message shows twice (the layer's line and the log's).
    //
    // One line through the callback, so that a Debug run shows that the route works. Severity MESSAGE: the layer's
    // default storage filter denies INFO, and the callback only gets what the filter lets through.
    debug_info_queue->AddApplicationMessage(D3D12_MESSAGE_SEVERITY_MESSAGE, "messages go to the log");
}

// Before ReportLiveDeviceObjects(): that report goes to the debugger only, as before.
auto stop_debug_layer_logging() -> void
{
    if (debug_info_queue)
    {
        debug_info_queue->UnregisterMessageCallback(debug_message_callback_cookie);
        debug_message_callback_cookie = 0;
        release(debug_info_queue);
    }
}
#endif

struct Options
{
    bool enable_vsync{true}; // enable/disable vsync
    bool enable_dxr{false};  // enable/disable DXR (ray tracing)
    u32 msaa_samples{1};     // MSAA samples (1, 2, 4, 8)
} options;

auto failed_init() -> bool
{
    shutdown();
    return false;
}

// Get the first most performing adapter that supports the minimum feature level.
// NOTE: this function can be expanded in functionality with, for example, checking if any
//       output devices (i.e. screens) are attached, enumerate the supported resolutions, provide
//       a means for the user to choose which adapter to use in a multi-adapter setting, etc.
auto determine_main_adapter() -> IDXGIAdapter4 *
{
    IDXGIAdapter4 *adapter{nullptr};

    // get adapters in descending order of performance
    for (u32 i{0};
         dxgi_factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) !=
         DXGI_ERROR_NOT_FOUND;
         ++i)
    {
        // pick the first adapter that supports the minimum feature level.
        if (SUCCEEDED(
                d3d12_device_factory->CreateDevice(adapter, minimum_feature_level, __uuidof(ID3D12Device), nullptr)))
        {
            return adapter;
        }
        release(adapter);
    }

    return nullptr;
}

auto get_max_feature_level(IDXGIAdapter4 *adapter) -> D3D_FEATURE_LEVEL
{
    constexpr D3D_FEATURE_LEVEL feature_levels[4]{
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_12_1,
    };

    D3D12_FEATURE_DATA_FEATURE_LEVELS feature_level_info{};
    feature_level_info.NumFeatureLevels = _countof(feature_levels);
    feature_level_info.pFeatureLevelsRequested = feature_levels;

    ComPtr<ID3D12Device> device;
    DXCall(d3d12_device_factory->CreateDevice(adapter, minimum_feature_level, IID_PPV_ARGS(&device)));
    DXCall(device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &feature_level_info, sizeof(feature_level_info)));
    return feature_level_info.MaxSupportedFeatureLevel;
}

PRIMAL_NOINLINE auto process_deferred_releases(u32 frame_idx, bool force_release = false) -> void
{
    const std::scoped_lock lock{deferred_releases_mutex};

    // NOTE: The resources could still be in use in previous frames. So we wait
    //       another round before releasing the resources.
    //      force_release is used during shutdown, where we don't have to wait.
    if (!force_release && ++deferred_releases_flag[frame_idx] < frame_buffer_count)
    {
        return;
    }

    deferred_releases_flag[frame_idx] = 0;

    rtv_desc_heap.process_deferred_free(frame_idx);
    dsv_desc_heap.process_deferred_free(frame_idx);
    srv_desc_heap.process_deferred_free(frame_idx);
    uav_desc_heap.process_deferred_free(frame_idx);

    utl::vector<IUnknown *> &resources{deferred_releases[frame_idx]};
    if (!resources.empty())
    {
        for (auto &resource : resources)
        {
            release(resource);
        }
        resources.clear();
    }
}

auto get_d3d12_frame_info(const FrameInfo &info, ConstantBuffer &cbuffer, const D3D12Surface &surface, u32 frame_idx)
    -> D3D12FrameInfo
{
    camera::D3D12Camera &camera{camera::get(info.camera_id)};
    camera.update();
    hlsl::GlobalShaderData data{};

    math::store(data.View, camera.view());
    math::store(data.Projection, camera.projection());
    math::store(data.InvProjection, camera.inverse_projection());
    math::store(data.ViewProjection, camera.view_projection());
    math::store(data.InvViewProjection, camera.inverse_view_projection());
    math::store(data.CameraPosition, camera.position());
    math::store(data.CameraDirection, camera.direction());
    data.ViewWidth = surface.viewport().Width;
    data.ViewHeight = surface.viewport().Height;
    data.NumDirectionalLights = light::non_cullable_light_count(info.light_set_key);
    data.DeltaTime = info.average_frame_time;
    data.AmbientLight = light::ambient_light(info.light_set_key);

    // NOTE: be careful not to read from this buffer. Reads are really really slow.
    hlsl::GlobalShaderData *const shader_data{cbuffer.allocate<hlsl::GlobalShaderData>()};
    // TODO: handle the case when cbuffer is full.
    std::memcpy(shader_data, &data, sizeof(hlsl::GlobalShaderData));

    D3D12FrameInfo d3d12_info{
        &info,
        &camera,
        cbuffer.gpu_address(shader_data),
        surface.width(),
        surface.height(),
        surface.light_culling_id(),
        frame_idx,
    };

    return d3d12_info;
}

}

namespace detail
{
auto deferred_release(IUnknown *resource) -> void
{
    const std::scoped_lock lock{deferred_releases_mutex};
    const u32 frame_idx{current_frame_index()};
    deferred_releases[frame_idx].push_back(resource);
    deferred_releases_flag[frame_idx] = 1;
}
}

auto initialize() -> bool
{
    // determine what is the maximum feature level that is supported
    // create a ID3D12Device (this a virtual adapter).

    if (main_device)
    {
        shutdown();
    }

    HRESULT hr{S_OK};

    DXCall(hr = ::D3D12GetInterface(CLSID_D3D12SDKConfiguration, IID_PPV_ARGS(&d3d12_sdk_config)));
    if (FAILED(hr))
    {
        return failed_init();
    }

    DXCall(
        hr = d3d12_sdk_config->CreateDeviceFactory(
            d3d12_sdk_version, d3d12_sdk_path, IID_PPV_ARGS(&d3d12_device_factory)));
    if (FAILED(hr))
    {
        return failed_init();
    }

    u32 dxgi_factory_flags{0}; // NOLINT(misc-const-correctness): set in Debug builds
#ifdef _DEBUG
    // Enable debugging layer. Requires "Graphics Tools" optional feature
    {
        ComPtr<ID3D12Debug6> debug_interface;
        if (SUCCEEDED(
                d3d12_device_factory->GetConfigurationInterface(CLSID_D3D12Debug, IID_PPV_ARGS(&debug_interface))))
        {
            debug_interface->EnableDebugLayer();
#if 0
#pragma message("WARNING: GPU-based validation is enabled. This will considerably slow down the renderer!")
            debug_interface->SetEnableGPUBasedValidation(1);
#endif
        }
        else
        {
            log::warning(
                log::Category::d3d12,
                "The debug interface is not available. Verify that the Graphics Tools optional feature is installed "
                "on this system.");
        }

        dxgi_factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
    }
#endif

    DXCall(hr = ::CreateDXGIFactory2(dxgi_factory_flags, IID_PPV_ARGS(&dxgi_factory)));
    if (FAILED(hr))
    {
        return failed_init();
    }

    tearing_is_supported = 0;
    parallel_recording = env_flag("PRIMAL_D3D12_PARALLEL_RECORDING", true);
    draw_indirect = env_flag("PRIMAL_D3D12_DRAW_INDIRECT", false);
    log::info(
        log::Category::d3d12,
        "Draws: {}",
        draw_indirect        ? "ExecuteIndirect (PRIMAL_D3D12_DRAW_INDIRECT=1)"
        : parallel_recording ? "direct, recorded in parallel above 512 items"
                             : "direct, recorded serially (PRIMAL_D3D12_PARALLEL_RECORDING=0)");

#if 1
    DXCall(dxgi_factory->CheckFeatureSupport(
        DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing_is_supported, sizeof(tearing_is_supported)));
#else
#pragma message("TEARING SUPPORT HAS BEEN DISABLED IN D3D12CORE::INITIALIZE()!")
#endif

    // determine which adapter (i.e. graphics card) to use, if any
    ComPtr<IDXGIAdapter4> main_adapter;
    main_adapter.Attach(determine_main_adapter());
    if (!main_adapter)
    {
        return failed_init();
    }

    const D3D_FEATURE_LEVEL max_feature_level{get_max_feature_level(main_adapter.Get())};
    assert(max_feature_level >= minimum_feature_level);
    if (max_feature_level < minimum_feature_level)
    {
        return failed_init();
    }

    DXCall(hr = d3d12_device_factory->CreateDevice(main_adapter.Get(), max_feature_level, IID_PPV_ARGS(&main_device)));
    if (FAILED(hr))
    {
        return failed_init();
    }

#ifdef _DEBUG
    {
        ComPtr<ID3D12InfoQueue> info_queue;
        DXCall(main_device->QueryInterface(IID_PPV_ARGS(&info_queue)));

        D3D12_MESSAGE_ID disabled_messages[]{
            // TODO: this will happen when using execute indirect. We might want to switch to a method
            //       that doesn't require the index buffer view being set in the command buffer.
            //       Maybe use mesh shaders?
            D3D12_MESSAGE_ID_GPU_BASED_VALIDATION_RESOURCE_STATE_IMPRECISE,
        };

        D3D12_INFO_QUEUE_FILTER filter{};
        filter.DenyList.NumIDs = _countof(disabled_messages);
        filter.DenyList.pIDList = disabled_messages;
        info_queue->AddStorageFilterEntries(&filter);

        info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
        info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);
        info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);

        start_debug_layer_logging(info_queue.Get());
    }
#endif

    bool result{true};
    result &= rtv_desc_heap.initialize(512, false);
    result &= dsv_desc_heap.initialize(512, false);
    result &= srv_desc_heap.initialize(4096, true);
    result &= uav_desc_heap.initialize(512, false);
    if (!result)
    {
        return failed_init();
    }

    for (u32 i{0}; i < frame_buffer_count; ++i)
    {
        new (&constant_buffers[i]) ConstantBuffer{ConstantBuffer::get_default_init_info(constant_buffer_size)};
        NAME_D3D12_OBJECT_INDEXED(constant_buffers[i].buffer(), i, L"Global Constant Buffer");
        if (!constant_buffers[i].buffer())
        {
            return failed_init();
        }
    }

    new (&gfx_command) D3D12Command(main_device, D3D12_COMMAND_LIST_TYPE_DIRECT);
    if (!gfx_command.command_queue())
    {
        return failed_init();
    }

    // initialize modules
    if (!(shaders::initialize() && gpass::initialize() && fx::initialize() && upload::initialize() &&
          content::initialize() && delight::initialize()))
    {
        return failed_init();
    }

    NAME_D3D12_OBJECT(main_device, L"Main D3D12 Device");
    NAME_D3D12_OBJECT(rtv_desc_heap.heap(), L"RTV Descriptor Heap");
    NAME_D3D12_OBJECT(dsv_desc_heap.heap(), L"DSV Descriptor Heap");
    NAME_D3D12_OBJECT(srv_desc_heap.heap(), L"SRV Descriptor Heap");
    NAME_D3D12_OBJECT(uav_desc_heap.heap(), L"UAV Descriptor Heap");

    return true;
}

auto shutdown() -> void
{
    gfx_command.release();
    release_thread_command_lists();

    // NOTE: we don't call process_deferred_releases at the end because
    //       some resources (such as swap chains) can't be released before
    //       their depending resources are released.
    for (u32 i{0}; i < frame_buffer_count; ++i)
    {
        process_deferred_releases(i, true);
    }

    // shutdown modules
    delight::shutdown();
    content::shutdown();
    upload::shutdown();
    fx::shutdown();
    gpass::shutdown();
    shaders::shutdown();

    release(dxgi_factory);

    for (u32 i{0}; i < frame_buffer_count; ++i)
    {
        constant_buffers[i].release();
    }

    // NOTE: some modules free their descriptors when they shutdown.
    //       We process those by calling process_deferred_free once more.
    rtv_desc_heap.process_deferred_free(0);
    dsv_desc_heap.process_deferred_free(0);
    srv_desc_heap.process_deferred_free(0);
    uav_desc_heap.process_deferred_free(0);

    rtv_desc_heap.release();
    dsv_desc_heap.release();
    srv_desc_heap.release();
    uav_desc_heap.release();

    // NOTE: some types only use deferred release for their resources during
    //       shutdown/reset/clear. To finally release these resources we call
    //       process_deferred_releases once more.
    process_deferred_releases(0, true);

#ifdef _DEBUG
    if (main_device)
    {
        {
            ComPtr<ID3D12InfoQueue> info_queue;
            DXCall(main_device->QueryInterface(IID_PPV_ARGS(&info_queue)));
            info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, false);
            info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, false);
            info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, false);
        }
        stop_debug_layer_logging();

        ComPtr<ID3D12DebugDevice2> debug_device;
        DXCall(main_device->QueryInterface(IID_PPV_ARGS(&debug_device)));
        release(main_device);
        DXCall(
            debug_device->ReportLiveDeviceObjects(D3D12_RLDO_SUMMARY | D3D12_RLDO_DETAIL | D3D12_RLDO_IGNORE_INTERNAL));
    }
#endif

    release(main_device);
    release(d3d12_device_factory);
    release(d3d12_sdk_config);
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
            const bool enable{*static_cast<const bool *>(parameter)};
            options.enable_vsync = enable || !tearing_is_supported;
        }
        break;
        case RendererOption::raytracing:
        {
            assert(parameter_size == sizeof(bool));
            const bool enable{*static_cast<const bool *>(parameter)};
            options.enable_dxr = enable;
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
        {
            assert(parameter_size == sizeof(bool));
            *static_cast<bool *>(parameter) = options.enable_vsync;
        }
        break;
        case RendererOption::raytracing:
        {
            assert(parameter_size == sizeof(bool));
            *static_cast<bool *>(parameter) = options.enable_dxr;
        }
        break;
        case RendererOption::msaa:
        {
            assert(parameter_size == sizeof(u32));
            *static_cast<u32 *>(parameter) = options.msaa_samples;
        }
        break;
        default: break;
    }
}

auto device() -> id3d12_device *
{
    return main_device;
}

auto rtv_heap() -> DescriptorHeap &
{
    return rtv_desc_heap;
}

auto dsv_heap() -> DescriptorHeap &
{
    return dsv_desc_heap;
}

auto srv_heap() -> DescriptorHeap &
{
    return srv_desc_heap;
}

auto uav_heap() -> DescriptorHeap &
{
    return uav_desc_heap;
}

auto cbuffer() -> ConstantBuffer &
{
    return constant_buffers[current_frame_index()];
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

auto allow_tearing() -> bool
{
    return tearing_is_supported;
}

auto vsync_enabled() -> bool
{
    return options.enable_vsync;
}

auto create_surface(platform::Window window) -> Surface
{
    const SurfaceId id{surfaces.add(window)};
    surfaces[id].create_swap_chain(dxgi_factory, gfx_command.command_queue());
    return Surface{id};
}

auto remove_surface(SurfaceId id) -> void
{
    gfx_command.flush();
    surfaces.remove(id);
}

auto resize_surface(SurfaceId id, u32, u32) -> void
{
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
    // Wait for the GPU to finish with the command allocator and
    // reset the allocator once the GPU is done with it.
    // This frees the memory that was used to store commands.
    gfx_command.begin_frame();

    const u32 frame_idx{current_frame_index()};
    reset_thread_command_lists(frame_idx);

    // Reset (clear) the global constant buffer for the current frame.
    ConstantBuffer &cbuffer{constant_buffers[frame_idx]};
    cbuffer.clear();

    if (deferred_releases_flag[frame_idx])
    {
        process_deferred_releases(frame_idx);
    }

    // The render items decide how much of the constant buffer the frame needs: room for them and for the rest of
    // the frame (global data), before anything is allocated.
    cbuffer.reserve(other_constant_bytes + gpass::collect_render_items(info));

    const D3D12Surface &surface{surfaces[id]};
    ID3D12Resource *const current_back_buffer{surface.back_buffer()};
    current_viewport = surface.viewport();
    current_scissor_rect = surface.scissor_rect();

    const D3D12FrameInfo d3d12_info{get_d3d12_frame_info(info, cbuffer, surface, frame_idx)};

    gpass::set_size({d3d12_info.surface_width, d3d12_info.surface_height});
    d3dx::D3D12ResourceBarrier &barriers{resource_barriers};

    // Record commands. NOTE: the gpass passes may continue the frame in another command list (parallel
    // recording): take gfx_command.command_list() again after them.
    id3d12_graphics_command_list *cmd_list{gfx_command.command_list()};
    set_frame_state(cmd_list);

    // Depth prepass
    barriers.add(
        current_back_buffer,
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY);
    gpass::add_transitions_for_depth_prepass(barriers);
    barriers.apply(cmd_list);
    gpass::set_render_targets_for_depth_prepass(cmd_list);

    if (draw_indirect)
    {
        gpass::depth_prepass_indirect(cmd_list, d3d12_info);
    }
    else
    {
        gpass::depth_prepass(cmd_list, d3d12_info);
    }
    cmd_list = gfx_command.command_list();

    // Geometry and lighting pass
    gpass::add_transitions_for_gpass(barriers);
    light::update_light_buffers(d3d12_info);
    delight::cull_lights(cmd_list, d3d12_info, barriers);
    barriers.apply(cmd_list);
    gpass::set_render_targets_for_gpass(cmd_list);

    if (draw_indirect)
    {
        gpass::render_indirect(cmd_list, d3d12_info);
    }
    else
    {
        gpass::render(cmd_list, d3d12_info);
    }
    cmd_list = gfx_command.command_list();

    // Post-process
    gpass::add_transitions_for_post_process(barriers);
    barriers.add(
        current_back_buffer,
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_BARRIER_FLAG_END_ONLY);
    barriers.apply(cmd_list);
    // Will write to the current back buffer, so back buffer is a render target
    fx::post_process(cmd_list, d3d12_info, surface.rtv());
    // after post process
    d3dx::transition_resource(
        cmd_list, current_back_buffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

    // Done recording commands. Now execute commands,
    // signal and increment the fence value for next frame.
    gfx_command.end_frame(surface);
}

auto prepare_thread_command_lists() -> void
{
    const u32 slots{jobs::worker_count() + 1};
    while (thread_command_lists.size() < slots)
    {
        auto lists{std::make_unique<ThreadCommandLists>()};
        for (u32 i{0}; i < frame_buffer_count; ++i)
        {
            DXCall(main_device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&lists->allocators[i])));
            NAME_D3D12_OBJECT_INDEXED(
                lists->allocators[i],
                thread_command_lists.size() * frame_buffer_count + i,
                L"Thread Command Allocator");
        }
        thread_command_lists.emplace_back(std::move(lists));
    }
}

auto begin_thread_command_list() -> id3d12_graphics_command_list *
{
    const u32 worker{jobs::worker_index()};
    const u32 slot{worker == u32_invalid_id ? jobs::worker_count() : worker};
    assert(slot < thread_command_lists.size() && "Call prepare_thread_command_lists() before recording in jobs.");
    ThreadCommandLists &lists{*thread_command_lists[slot]};
    const u32 frame_idx{current_frame_index()};
    ID3D12CommandAllocator *const allocator{lists.allocators[frame_idx]};

    utl::vector<id3d12_graphics_command_list *> &frame_lists{lists.lists[frame_idx]};
    const u32 index{lists.used[frame_idx]++};
    if (index == frame_lists.size())
    {
        // A new list is created recording.
        id3d12_graphics_command_list *list{nullptr};
        DXCall(
            main_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&list)));
        NAME_D3D12_OBJECT_INDEXED(list, slot, L"Thread Command List");
        frame_lists.push_back(list);
    }
    else
    {
        DXCall(frame_lists[index]->Reset(allocator, nullptr));
    }

    id3d12_graphics_command_list *const cmd_list{frame_lists[index]};
    set_frame_state(cmd_list);
    return cmd_list;
}

auto execute_thread_command_lists(std::span<ID3D12CommandList *const> lists) -> void
{
    gfx_command.queue_and_continue(lists);
    set_frame_state(gfx_command.command_list());
}

auto command_list() -> id3d12_graphics_command_list *
{
    return gfx_command.command_list();
}

auto parallel_recording_enabled() -> bool
{
    return parallel_recording;
}
}
