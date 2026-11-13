// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "D3D12CommonHeaders.h"

#include <atomic>

namespace primal::graphics::d3d12
{

class DescriptorHeap;

struct DescriptorHandle
{
    D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
    u32 index{u32_invalid_id};

    [[nodiscard]] constexpr auto is_valid() const -> bool
    {
        return cpu.ptr != 0;
    }
    [[nodiscard]] constexpr auto is_shader_visible() const -> bool
    {
        return gpu.ptr != 0;
    }

#ifdef _DEBUG
  private:
    friend class DescriptorHeap;
    DescriptorHeap *_container{nullptr};
#endif
};

class DescriptorHeap
{
  public:
    explicit DescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE type)
        : _type{type}
    {
    }
    DISABLE_COPY_AND_MOVE(DescriptorHeap);
    ~DescriptorHeap()
    {
        assert(!_heap);
    }

    auto initialize(u32 capacity, bool is_shader_visible) -> bool;
    auto release() -> void;
    auto process_deferred_free(u32 frame_idx) -> void;

    [[nodiscard]] auto allocate() -> DescriptorHandle;
    auto free(DescriptorHandle &handle) -> void;

    [[nodiscard]] constexpr auto type() const -> D3D12_DESCRIPTOR_HEAP_TYPE
    {
        return _type;
    }
    [[nodiscard]] constexpr auto cpu_start() const -> D3D12_CPU_DESCRIPTOR_HANDLE
    {
        return _cpu_start;
    }
    [[nodiscard]] constexpr auto gpu_start() const -> D3D12_GPU_DESCRIPTOR_HANDLE
    {
        return _gpu_start;
    }
    [[nodiscard]] constexpr auto heap() const -> ID3D12DescriptorHeap *
    {
        return _heap;
    }
    [[nodiscard]] constexpr auto capacity() const -> u32
    {
        return _capacity;
    }
    [[nodiscard]] constexpr auto size() const -> u32
    {
        return _size;
    }
    [[nodiscard]] constexpr auto descriptor_size() const -> u32
    {
        return _descriptor_size;
    }
    [[nodiscard]] constexpr auto is_shader_visible() const -> bool
    {
        return _gpu_start.ptr != 0;
    }

  private:
    ID3D12DescriptorHeap *_heap{nullptr};
    D3D12_CPU_DESCRIPTOR_HANDLE _cpu_start{};
    D3D12_GPU_DESCRIPTOR_HANDLE _gpu_start{};
    std::unique_ptr<u32[]> _free_handles;
    std::unique_ptr<u8[]> _allocated_slots;
    utl::vector<u32> _deferred_free_indices[frame_buffer_count]{};
    std::mutex _mutex;
    u32 _capacity{0};
    u32 _size{0};
    u32 _descriptor_size{};
    const D3D12_DESCRIPTOR_HEAP_TYPE _type{};
};

struct D3D12BufferInitInfo
{
    ID3D12Heap1 *heap{nullptr};
    const void *data{nullptr};
    D3D12_RESOURCE_ALLOCATION_INFO1 allocation_info{};
    D3D12_RESOURCE_STATES initial_state{D3D12_RESOURCE_STATE_COMMON};
    D3D12_RESOURCE_FLAGS flags{D3D12_RESOURCE_FLAG_NONE};
    u32 size{0};
    u32 alignment{0};
};

class D3D12Buffer
{
  public:
    D3D12Buffer() = default;
    explicit D3D12Buffer(const D3D12BufferInitInfo &info, bool is_cpu_accessible);
    DISABLE_COPY(D3D12Buffer);
    constexpr D3D12Buffer(D3D12Buffer &&o) noexcept
        : _buffer{o._buffer}
        , _gpu_address{o._gpu_address}
        , _size{o._size}
    {
        o.reset();
    }

    constexpr auto operator=(D3D12Buffer &&o) noexcept -> D3D12Buffer &
    {
        assert(this != &o);
        if (this != &o)
        {
            release();
            move(o);
        }

        return *this;
    }

    ~D3D12Buffer()
    {
        release();
    }

    auto release() -> void;
    [[nodiscard]] constexpr auto buffer() const -> ID3D12Resource *
    {
        return _buffer;
    }
    [[nodiscard]] constexpr auto gpu_address() const -> D3D12_GPU_VIRTUAL_ADDRESS
    {
        return _gpu_address;
    }
    [[nodiscard]] constexpr auto size() const -> u32
    {
        return _size;
    }

  private:
    constexpr auto move(D3D12Buffer &o) -> void
    {
        _buffer = o._buffer;
        _gpu_address = o._gpu_address;
        _size = o._size;
        o.reset();
    }

    constexpr auto reset() -> void
    {
        _buffer = nullptr;
        _gpu_address = 0;
        _size = 0;
    }

    ID3D12Resource *_buffer{nullptr};
    D3D12_GPU_VIRTUAL_ADDRESS _gpu_address{0};
    u32 _size{0};
};

class ConstantBuffer
{
  public:
    ConstantBuffer() = default;
    explicit ConstantBuffer(const D3D12BufferInitInfo &info);
    DISABLE_COPY_AND_MOVE(ConstantBuffer);
    ~ConstantBuffer()
    {
        release();
    }

    auto release() -> void
    {
        _buffer.release();
        _cpu_address = nullptr;
        _cpu_offset.store(0, std::memory_order_relaxed);
    }

    auto clear() -> void
    {
        _cpu_offset.store(0, std::memory_order_relaxed);
    }
    // Right after clear(), before the frame's first allocation: makes sure that many bytes fit, with a bigger buffer
    // (the next power of two) when they don't. The old buffer is released once no frame uses it.
    auto reserve(u64 bytes) -> void;
    // Lock-free: any thread may allocate. nullptr when the buffer is full (reserve() at the start of the frame).
    [[nodiscard]] auto allocate(u32 size) -> u8 *;

    template <typename T>
    [[nodiscard]] constexpr auto allocate() -> T *
    {
        return reinterpret_cast<T *>(allocate(sizeof(T)));
    }

    [[nodiscard]] constexpr auto buffer() const -> ID3D12Resource *
    {
        return _buffer.buffer();
    }
    [[nodiscard]] constexpr auto gpu_address() const -> D3D12_GPU_VIRTUAL_ADDRESS
    {
        return _buffer.gpu_address();
    }
    [[nodiscard]] constexpr auto size() const -> u32
    {
        return _buffer.size();
    }
    [[nodiscard]] constexpr auto cpu_address() const -> u8 *
    {
        return _cpu_address;
    }

    // Lock-free: only computes an address (called once per render item, also from jobs).
    template <typename T>
    [[nodiscard]] auto gpu_address(T *const allocation) const -> D3D12_GPU_VIRTUAL_ADDRESS
    {
        assert(_cpu_address && allocation);
        if (!_cpu_address || !allocation)
        {
            return {};
        }
        const u8 *const address{reinterpret_cast<const u8 *>(allocation)};
        assert(address <= _cpu_address + _cpu_offset.load(std::memory_order_relaxed));
        assert(address >= _cpu_address);
        const u64 offset{static_cast<u64>(address - _cpu_address)};
        return _buffer.gpu_address() + offset;
    }

    [[nodiscard]] constexpr static auto get_default_init_info(u32 size) -> D3D12BufferInitInfo
    {
        assert(size);
        D3D12BufferInitInfo info{};
        info.size = size;
        info.alignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
        return info;
    }

  private:
    auto map() -> void;

    D3D12Buffer _buffer;
    u8 *_cpu_address{nullptr};
    std::atomic<u32> _cpu_offset{0};
};

class UavClearableBuffer
{
  public:
    UavClearableBuffer() = default;
    explicit UavClearableBuffer(const D3D12BufferInitInfo &info);
    DISABLE_COPY(UavClearableBuffer);
    constexpr UavClearableBuffer(UavClearableBuffer &&o) noexcept
        : _buffer{std::move(o._buffer)}
        , _uav{o._uav}
        , _uav_shader_visible{o._uav_shader_visible}
    {
        o.reset();
    }

    constexpr auto operator=(UavClearableBuffer &&o) noexcept -> UavClearableBuffer &
    {
        assert(this != &o);
        if (this != &o)
        {
            release();
            move(o);
        }

        return *this;
    }

    ~UavClearableBuffer()
    {
        release();
    }

    auto release() -> void;

    auto clear_uav(id3d12_graphics_command_list *const cmd_list, const u32 *const values) const -> void
    {
        assert(buffer());
        assert(_uav.is_valid() && _uav_shader_visible.is_valid() && _uav_shader_visible.is_shader_visible());
        cmd_list->ClearUnorderedAccessViewUint(_uav_shader_visible.gpu, _uav.cpu, buffer(), values, 0, nullptr);
    }

    auto clear_uav(id3d12_graphics_command_list *const cmd_list, const f32 *const values) const -> void
    {
        assert(buffer());
        assert(_uav.is_valid() && _uav_shader_visible.is_valid() && _uav_shader_visible.is_shader_visible());
        cmd_list->ClearUnorderedAccessViewFloat(_uav_shader_visible.gpu, _uav.cpu, buffer(), values, 0, nullptr);
    }

    [[nodiscard]] constexpr auto buffer() const -> ID3D12Resource *
    {
        return _buffer.buffer();
    }
    [[nodiscard]] constexpr auto gpu_address() const -> D3D12_GPU_VIRTUAL_ADDRESS
    {
        return _buffer.gpu_address();
    }
    [[nodiscard]] constexpr auto size() const -> u32
    {
        return _buffer.size();
    }
    [[nodiscard]] constexpr auto uav() const -> DescriptorHandle
    {
        return _uav;
    }
    [[nodiscard]] constexpr auto uav_shader_visible() const -> DescriptorHandle
    {
        return _uav_shader_visible;
    }

    [[nodiscard]] constexpr static auto get_default_init_info(u32 size) -> D3D12BufferInitInfo
    {
        assert(size);
        D3D12BufferInitInfo info{};
        info.size = size;
        info.alignment = sizeof(math::v4);
        info.flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        return info;
    }

  private:
    constexpr auto move(UavClearableBuffer &o) -> void
    {
        _buffer = std::move(o._buffer);
        _uav = o._uav;
        _uav_shader_visible = o._uav_shader_visible;
        o.reset();
    }

    constexpr auto reset() -> void
    {
        _uav = {};
        _uav_shader_visible = {};
    }

    D3D12Buffer _buffer;
    DescriptorHandle _uav{};
    DescriptorHandle _uav_shader_visible{};
};

struct D3D12TextureInitInfo
{
    ID3D12Heap1 *heap{nullptr};
    ID3D12Resource *resource{nullptr};
    D3D12_SHADER_RESOURCE_VIEW_DESC *srv_desc{nullptr};
    D3D12_RESOURCE_DESC *desc{nullptr};
    D3D12_RESOURCE_ALLOCATION_INFO1 allocation_info{};
    D3D12_RESOURCE_STATES initial_state{};
    D3D12_CLEAR_VALUE clear_value{};
};

class D3D12Texture
{
  public:
    constexpr static u32 max_mips{14}; // support up to 16k resolutions.
    D3D12Texture() = default;
    explicit D3D12Texture(D3D12TextureInitInfo info);
    DISABLE_COPY(D3D12Texture);
    constexpr D3D12Texture(D3D12Texture &&o) noexcept
        : _resource{o._resource}
        , _srv{o._srv}
    {
        o.reset();
    }

    constexpr auto operator=(D3D12Texture &&o) noexcept -> D3D12Texture &
    {
        assert(this != &o);
        if (this != &o)
        {
            release();
            move(o);
        }
        return *this;
    }

    ~D3D12Texture()
    {
        release();
    }

    auto release() -> void;
    [[nodiscard]] constexpr auto resource() const -> ID3D12Resource *
    {
        return _resource;
    }
    [[nodiscard]] constexpr auto srv() const -> DescriptorHandle
    {
        return _srv;
    }

  private:
    constexpr auto move(D3D12Texture &o) -> void
    {
        _resource = o._resource;
        _srv = o._srv;
        o.reset();
    }

    constexpr auto reset() -> void
    {
        _resource = nullptr;
        _srv = {};
    }

    ID3D12Resource *_resource{nullptr};
    DescriptorHandle _srv;
};

class D3D12RenderTexture
{
  public:
    D3D12RenderTexture() = default;
    explicit D3D12RenderTexture(D3D12TextureInitInfo info);
    DISABLE_COPY(D3D12RenderTexture);
    constexpr D3D12RenderTexture(D3D12RenderTexture &&o) noexcept
        : _texture{std::move(o._texture)}
        , _mip_count{o._mip_count}
    {
        for (u32 i{0}; i < _mip_count; ++i)
        {
            _rtv[i] = o._rtv[i];
        }
        o.reset();
    }

    constexpr auto operator=(D3D12RenderTexture &&o) noexcept -> D3D12RenderTexture &
    {
        assert(this != &o);
        if (this != &o)
        {
            release();
            move(o);
        }
        return *this;
    }

    ~D3D12RenderTexture()
    {
        release();
    }

    auto release() -> void;
    [[nodiscard]] constexpr auto mip_count() const -> u32
    {
        return _mip_count;
    }
    [[nodiscard]] constexpr auto rtv(u32 mip_index) const -> D3D12_CPU_DESCRIPTOR_HANDLE
    {
        assert(mip_index < _mip_count);
        return _rtv[mip_index].cpu;
    }
    [[nodiscard]] constexpr auto srv() const -> DescriptorHandle
    {
        return _texture.srv();
    }
    [[nodiscard]] constexpr auto resource() const -> ID3D12Resource *
    {
        return _texture.resource();
    }

  private:
    constexpr auto move(D3D12RenderTexture &o) -> void
    {
        _texture = std::move(o._texture);
        _mip_count = o._mip_count;
        for (u32 i{0}; i < _mip_count; ++i)
        {
            _rtv[i] = o._rtv[i];
        }
        o.reset();
    }

    constexpr auto reset() -> void
    {
        for (u32 i{0}; i < _mip_count; ++i)
        {
            _rtv[i] = {};
        }
        _mip_count = 0;
    }

    D3D12Texture _texture;
    DescriptorHandle _rtv[D3D12Texture::max_mips]{};
    u32 _mip_count{0};
};

class D3D12DepthBuffer
{
  public:
    D3D12DepthBuffer() = default;
    explicit D3D12DepthBuffer(D3D12TextureInitInfo info);
    DISABLE_COPY(D3D12DepthBuffer);
    constexpr D3D12DepthBuffer(D3D12DepthBuffer &&o) noexcept
        : _texture{std::move(o._texture)}
        , _dsv{o._dsv}
    {
        o._dsv = {};
    }

    constexpr auto operator=(D3D12DepthBuffer &&o) noexcept -> D3D12DepthBuffer &
    {
        assert(this != &o);
        if (this != &o)
        {
            _texture = std::move(o._texture);
            _dsv = o._dsv;
            o._dsv = {};
        }
        return *this;
    }

    ~D3D12DepthBuffer()
    {
        release();
    }

    auto release() -> void;
    [[nodiscard]] constexpr auto dsv() const -> D3D12_CPU_DESCRIPTOR_HANDLE
    {
        return _dsv.cpu;
    }
    [[nodiscard]] constexpr auto srv() const -> DescriptorHandle
    {
        return _texture.srv();
    }
    [[nodiscard]] constexpr auto resource() const -> ID3D12Resource *
    {
        return _texture.resource();
    }

  private:
    D3D12Texture _texture;
    DescriptorHandle _dsv{};
};
}
