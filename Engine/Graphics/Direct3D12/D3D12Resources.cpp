// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "D3D12Resources.h"

#include "D3D12Core.h"
#include "D3D12Helpers.h"

namespace primal::graphics::d3d12
{
//// DESCRIPTOR HEAP //////////////////////////////////////////////////////////////////////////////
auto DescriptorHeap::initialize(u32 capacity, bool is_shader_visible) -> bool
{
    const std::scoped_lock lock{_mutex};
    assert(capacity && capacity < D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_2);
    assert(_type != D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER || capacity <= D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE);

    if (_type == D3D12_DESCRIPTOR_HEAP_TYPE_DSV || _type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV)
    {
        is_shader_visible = false;
    }

    release();

    id3d12_device *const device{core::device()};
    assert(device);

    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Flags = is_shader_visible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    desc.NumDescriptors = capacity;
    desc.Type = _type;
    desc.NodeMask = 0;

    HRESULT hr{S_OK};
    DXCall(hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&_heap)));
    if (FAILED(hr))
    {
        return false;
    }

    _free_handles = std::make_unique<u32[]>(capacity);
    _allocated_slots = std::make_unique<u8[]>(capacity);
    _capacity = capacity;
    _size = 0;

    for (u32 i{0}; i < capacity; ++i)
    {
        _free_handles[i] = i;
    }
    std::memset(_allocated_slots.get(), 0, capacity);
    DEBUG_OP(for (u32 i{0}; i < frame_buffer_count; ++i) { assert(_deferred_free_indices[i].empty()); });

    _descriptor_size = device->GetDescriptorHandleIncrementSize(_type);
    _cpu_start = _heap->GetCPUDescriptorHandleForHeapStart();
    _gpu_start = is_shader_visible ? _heap->GetGPUDescriptorHandleForHeapStart() : D3D12_GPU_DESCRIPTOR_HANDLE{0};

    return true;
}

auto DescriptorHeap::release() -> void
{
    assert(!_size);
    core::deferred_release(_heap);
}

auto DescriptorHeap::process_deferred_free(u32 frame_idx) -> void
{
    const std::scoped_lock lock{_mutex};
    assert(frame_idx < frame_buffer_count);

    utl::vector<u32> &indices{_deferred_free_indices[frame_idx]};
    if (!indices.empty())
    {
        for (const auto index : indices)
        {
            if (!_allocated_slots[index])
            {
                assert(false && "Descriptor slot is already free. Double free detected.");
                continue;
            }
            --_size;
            _free_handles[_size] = index;
            _allocated_slots[index] = 0;
        }
        indices.clear();
    }
}

auto DescriptorHeap::allocate() -> DescriptorHandle
{
    const std::scoped_lock lock{_mutex};
    assert(_heap);
    assert(_size < _capacity);

    const u32 index{_free_handles[_size]};

    assert(!_allocated_slots[index]);
    _allocated_slots[index] = 1;

    const u32 offset{index * _descriptor_size};
    ++_size;

    DescriptorHandle handle;
    handle.cpu.ptr = _cpu_start.ptr + offset;
    if (is_shader_visible())
    {
        handle.gpu.ptr = _gpu_start.ptr + offset;
    }

    handle.index = index;
    DEBUG_OP(handle._container = this);
    return handle;
}

auto DescriptorHeap::free(DescriptorHandle &handle) -> void
{
    if (!handle.is_valid())
    {
        return;
    }
    const u32 frame_idx{core::current_frame_index()};
    {
        const std::scoped_lock lock{_mutex};
        assert(_heap && _size);
        assert(handle._container == this);
        assert(handle.cpu.ptr >= _cpu_start.ptr);
        assert((handle.cpu.ptr - _cpu_start.ptr) % _descriptor_size == 0);
        assert(handle.index < _capacity);
        const u32 index{static_cast<u32>(handle.cpu.ptr - _cpu_start.ptr) / _descriptor_size};
        assert(handle.index == index);

        _deferred_free_indices[frame_idx].push_back(index);
    }
    // Not under _mutex: process_deferred_releases() holds the deferred-release mutex while it takes _mutex, so taking
    // them the other way round here could deadlock.
    core::set_deferred_releases_flag(frame_idx);
    handle = {};
}
//// D3D12 BUFFER /////////////////////////////////////////////////////////////////////////////////
D3D12Buffer::D3D12Buffer(const D3D12BufferInitInfo &info, bool is_cpu_accessible)
{
    assert(!_buffer && info.size && info.alignment);
    _size = static_cast<u32>(math::align_size_up(info.size, info.alignment));
    _buffer = d3dx::create_buffer(
        info.data, _size, is_cpu_accessible, info.initial_state, info.flags, info.heap, info.allocation_info.Offset);
    _gpu_address = _buffer->GetGPUVirtualAddress();
    NAME_D3D12_OBJECT_INDEXED(_buffer, _size, L"D3D12 Buffer - size");
}

auto D3D12Buffer::release() -> void
{
    core::deferred_release(_buffer);
    _gpu_address = 0;
    _size = 0;
}
//// CONSTANT BUFFER //////////////////////////////////////////////////////////////////////////////
ConstantBuffer::ConstantBuffer(const D3D12BufferInitInfo &info)
    : _buffer{info, true}
{
    map();
}

auto ConstantBuffer::map() -> void
{
    NAME_D3D12_OBJECT_INDEXED(buffer(), size(), L"Constant Buffer - size");

    const D3D12_RANGE range{};
    DXCall(buffer()->Map(0, &range, reinterpret_cast<void **>(&_cpu_address)));
    assert(_cpu_address);
}

auto ConstantBuffer::reserve(u64 bytes) -> void
{
    assert(_cpu_offset.load(std::memory_order_relaxed) == 0 && "reserve() before the frame's first allocation");
    if (bytes <= _buffer.size())
    {
        return;
    }

    u64 new_size{_buffer.size() ? _buffer.size() : D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT};
    while (new_size < bytes)
    {
        new_size *= 2;
    }
    assert(new_size <= u32_invalid_id);
    log::debug(log::Category::d3d12, "Constant buffer grown from {} to {} bytes", _buffer.size(), new_size);

    // The old buffer goes through deferred release: a frame still in flight may use it.
    _buffer = D3D12Buffer{get_default_init_info(static_cast<u32>(new_size)), true};
    _cpu_address = nullptr;
    map();
}

auto ConstantBuffer::allocate(u32 size) -> u8 *
{
    const u32 aligned_size{static_cast<u32>(d3dx::align_size_for_constant_buffer(size))};
    u32 offset{_cpu_offset.load(std::memory_order_relaxed)};
    do
    {
        if (offset + aligned_size > _buffer.size())
        {
            assert(false && "The frame's constant buffer is full: reserve() more at the start of the frame.");
            return nullptr;
        }
    } while (!_cpu_offset.compare_exchange_weak(offset, offset + aligned_size, std::memory_order_relaxed));

    return _cpu_address + offset;
}
//// UAV CLEARABLE BUFFER /////////////////////////////////////////////////////////////////////////
UavClearableBuffer::UavClearableBuffer(const D3D12BufferInitInfo &info)
    : _buffer{info, false}
{
    assert(info.size && info.alignment);
    NAME_D3D12_OBJECT_INDEXED(buffer(), info.size, L"UAV Clearable Buffer - size");

    assert(info.flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    _uav = core::uav_heap().allocate();
    _uav_shader_visible = core::srv_heap().allocate();
    D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
    desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    desc.Format = DXGI_FORMAT_R32_UINT;
    desc.Buffer.CounterOffsetInBytes = 0;
    desc.Buffer.FirstElement = 0;
    desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    desc.Buffer.NumElements = _buffer.size() / sizeof(u32);

    core::device()->CreateUnorderedAccessView(buffer(), nullptr, &desc, _uav.cpu);
    core::device()->CopyDescriptorsSimple(1, _uav_shader_visible.cpu, _uav.cpu, core::srv_heap().type());
}

auto UavClearableBuffer::release() -> void
{
    core::srv_heap().free(_uav_shader_visible);
    core::uav_heap().free(_uav);
    _buffer.release();
}
//// D3D12 TEXTURE ////////////////////////////////////////////////////////////////////////////////
D3D12Texture::D3D12Texture(D3D12TextureInitInfo info)
{
    auto *const device{core::device()};
    assert(device);

    D3D12_CLEAR_VALUE const *const clear_value{
        info.desc && (info.desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET ||
                      info.desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
            ? &info.clear_value
            : nullptr};

    if (info.resource)
    {
        assert(!info.heap);
        _resource = info.resource;
    }
    else if (info.heap)
    {
        assert(info.desc);
        DXCall(device->CreatePlacedResource(
            info.heap,
            info.allocation_info.Offset,
            info.desc,
            info.initial_state,
            clear_value,
            IID_PPV_ARGS(&_resource)));
    }
    else
    {
        assert(info.desc);
        DXCall(device->CreateCommittedResource(
            &d3dx::heap_properties.default_heap,
            D3D12_HEAP_FLAG_NONE,
            info.desc,
            info.initial_state,
            clear_value,
            IID_PPV_ARGS(&_resource)));
    }

    assert(_resource);
    _srv = core::srv_heap().allocate();
    device->CreateShaderResourceView(_resource, info.srv_desc, _srv.cpu);
}

auto D3D12Texture::release() -> void
{
    core::srv_heap().free(_srv);
    core::deferred_release(_resource);
}
//// RENDER TEXTURE ///////////////////////////////////////////////////////////////////////////////
D3D12RenderTexture::D3D12RenderTexture(D3D12TextureInitInfo info)
    : _texture{info}
{
    assert(info.desc);
    _mip_count = resource()->GetDesc().MipLevels;
    assert(_mip_count && _mip_count <= D3D12Texture::max_mips);

    DescriptorHeap &rtv_heap{core::rtv_heap()};
    D3D12_RENDER_TARGET_VIEW_DESC desc{};
    desc.Format = info.desc->Format;
    desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    desc.Texture2D.MipSlice = 0;

    auto *const device{core::device()};
    assert(device);

    for (u32 i{0}; i < _mip_count; ++i)
    {
        _rtv[i] = rtv_heap.allocate();
        device->CreateRenderTargetView(resource(), &desc, _rtv[i].cpu);
        ++desc.Texture2D.MipSlice;
    }
}

auto D3D12RenderTexture::release() -> void
{
    for (u32 i{0}; i < _mip_count; ++i)
    {
        core::rtv_heap().free(_rtv[i]);
    }
    _texture.release();
    _mip_count = 0;
}
//// DEPTH BUFFER /////////////////////////////////////////////////////////////////////////////////
D3D12DepthBuffer::D3D12DepthBuffer(D3D12TextureInitInfo info)
{
    assert(info.desc);
    const DXGI_FORMAT dsv_format{info.desc->Format};

    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
    if (info.desc->Format == DXGI_FORMAT_D32_FLOAT)
    {
        info.desc->Format = DXGI_FORMAT_R32_TYPELESS;
        srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
    }

    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels = 1;
    srv_desc.Texture2D.MostDetailedMip = 0;
    srv_desc.Texture2D.PlaneSlice = 0;
    srv_desc.Texture2D.ResourceMinLODClamp = 0.f;

    assert(!info.srv_desc && !info.resource);
    info.srv_desc = &srv_desc;
    _texture = D3D12Texture(info);

    D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc{};
    dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsv_desc.Flags = D3D12_DSV_FLAG_NONE;
    dsv_desc.Format = dsv_format;
    dsv_desc.Texture2D.MipSlice = 0;

    _dsv = core::dsv_heap().allocate();

    auto *const device{core::device()};
    assert(device);
    device->CreateDepthStencilView(resource(), &dsv_desc, _dsv.cpu);
}

auto D3D12DepthBuffer::release() -> void
{
    core::dsv_heap().free(_dsv);
    _texture.release();
}
}
