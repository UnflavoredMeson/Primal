// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "VulkanCommonHeaders.h"
#include "VulkanHelpers.h"

#include <atomic>

// Counterpart of D3D12Resources.h. Memory comes from the Vulkan Memory Allocator (core::allocator()).
namespace primal::graphics::vulkan
{

class DescriptorHeap;

//// DESCRIPTOR HEAP //////////////////////////////////////////////////////////////////////////////
// A slot in the resource heap. The index is what shaders use with ResourceDescriptorHeap[index].
struct DescriptorHandle
{
    u32 index{u32_invalid_id};

    [[nodiscard]] constexpr auto is_valid() const -> bool
    {
        return index != u32_invalid_id;
    }

#ifdef _DEBUG
  private:
    friend class DescriptorHeap;
    DescriptorHeap *_container{nullptr};
#endif
};

// The bindless resource heap (set 1, binding 0: one array of mutable descriptors, DXC's
// ResourceDescriptorHeap[]). Counterpart of the shader-visible CBV/SRV/UAV descriptor_heap in D3D12.
// RTV/DSV heaps have no Vulkan counterpart: render targets and depth buffers use image views.
// NOTE: the set is update-after-bind, so slots can be written while command buffers that use the heap
//       are pending, as long as those don't use the same slots (freed slots are reused frame_buffer_count
//       frames later, like in D3D12).
class DescriptorHeap
{
  public:
    DescriptorHeap() = default;
    DISABLE_COPY_AND_MOVE(DescriptorHeap);
    ~DescriptorHeap()
    {
        assert(!_pool);
    }

    auto initialize(u32 capacity) -> bool;
    auto release() -> void;
    auto process_deferred_free(u32 frame_idx) -> void;

    [[nodiscard]] auto allocate() -> DescriptorHandle;
    auto free(DescriptorHandle &handle) -> void;

    // Write a view into a slot. The descriptor type a slot holds can change (mutable descriptors).
    auto write_sampled_image(
        const DescriptorHandle &handle,
        VkImageView view,
        VkImageLayout layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL) const -> void;
    auto write_storage_image(const DescriptorHandle &handle, VkImageView view) const -> void; // layout GENERAL
    auto write_storage_buffer(const DescriptorHandle &handle, const vkx::BufferRange &range) const -> void;
    auto write_uniform_buffer(const DescriptorHandle &handle, const vkx::BufferRange &range) const -> void;

    [[nodiscard]] constexpr auto set() const -> VkDescriptorSet
    {
        return _set;
    }
    [[nodiscard]] constexpr auto set_layout() const -> VkDescriptorSetLayout
    {
        return _set_layout;
    }
    [[nodiscard]] constexpr auto capacity() const -> u32
    {
        return _capacity;
    }
    [[nodiscard]] constexpr auto size() const -> u32
    {
        return _size;
    }

  private:
    auto write(
        const DescriptorHandle &handle,
        VkDescriptorType type,
        const VkDescriptorImageInfo *image,
        const VkDescriptorBufferInfo *buffer) const -> void;

    VkDescriptorPool _pool{VK_NULL_HANDLE};
    VkDescriptorSetLayout _set_layout{VK_NULL_HANDLE};
    VkDescriptorSet _set{VK_NULL_HANDLE};
    std::unique_ptr<u32[]> _free_handles;
    std::unique_ptr<u8[]> _allocated_slots;
    utl::vector<u32> _deferred_free_indices[frame_buffer_count]{};
    mutable std::mutex _mutex; // also locked by the const write_*() functions
    u32 _capacity{0};
    u32 _size{0};
};

//// BUFFER ///////////////////////////////////////////////////////////////////////////////////////
struct VulkanBufferInitInfo
{
    const void *data{nullptr};
    VkBufferUsageFlags usage{0};
    u32 size{0};
    u32 alignment{0};
};

// A buffer with its own VMA allocation (counterpart of d3d12_buffer). CPU-accessible buffers are
// persistently mapped (host-visible, write-combined; D3D12's upload heap).
class VulkanBuffer
{
  public:
    VulkanBuffer() = default;
    explicit VulkanBuffer(const VulkanBufferInitInfo &info, bool is_cpu_accessible);
    DISABLE_COPY(VulkanBuffer);
    constexpr VulkanBuffer(VulkanBuffer &&o) noexcept
        : _buffer{o._buffer}
        , _allocation{o._allocation}
        , _cpu_address{o._cpu_address}
        , _size{o._size}
    {
        o.reset();
    }

    constexpr auto operator=(VulkanBuffer &&o) noexcept -> VulkanBuffer &
    {
        assert(this != &o);
        if (this != &o)
        {
            release();
            move(o);
        }

        return *this;
    }

    ~VulkanBuffer()
    {
        release();
    }

    auto release() -> void;
    [[nodiscard]] constexpr auto buffer() const -> VkBuffer
    {
        return _buffer;
    }
    [[nodiscard]] constexpr auto size() const -> u32
    {
        return _size;
    }
    // Mapped memory of a CPU-accessible buffer, nullptr otherwise.
    [[nodiscard]] constexpr auto cpu_address() const -> u8 *
    {
        return _cpu_address;
    }
    [[nodiscard]] constexpr auto range(u64 offset = 0, u64 size = VK_WHOLE_SIZE) const -> vkx::BufferRange
    {
        return {_buffer, offset, size};
    }

  private:
    constexpr auto move(VulkanBuffer &o) -> void
    {
        _buffer = o._buffer;
        _allocation = o._allocation;
        _cpu_address = o._cpu_address;
        _size = o._size;
        o.reset();
    }

    constexpr auto reset() -> void
    {
        _buffer = VK_NULL_HANDLE;
        _allocation = VK_NULL_HANDLE;
        _cpu_address = nullptr;
        _size = 0;
    }

    VkBuffer _buffer{VK_NULL_HANDLE};
    VmaAllocation _allocation{VK_NULL_HANDLE};
    u8 *_cpu_address{nullptr};
    u32 _size{0};
};

//// CONSTANT BUFFER //////////////////////////////////////////////////////////////////////////////
// Linear per-frame allocator for shader constants (counterpart of d3d12::constant_buffer). Core
// keeps one per frame and clears it at the start of the frame.
class ConstantBuffer
{
  public:
    // Valid for uniform and storage buffer offsets on every device (the spec's maximum for
    // minUniformBufferOffsetAlignment / minStorageBufferOffsetAlignment), same as D3D12.
    constexpr static u32 alignment{256};

    ConstantBuffer() = default;
    explicit ConstantBuffer(const VulkanBufferInitInfo &info);
    DISABLE_COPY_AND_MOVE(ConstantBuffer);
    ~ConstantBuffer()
    {
        release();
    }

    auto release() -> void
    {
        _buffer.release();
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
    [[nodiscard]] auto allocate() -> T *
    {
        return reinterpret_cast<T *>(allocate(sizeof(T)));
    }

    [[nodiscard]] constexpr auto buffer() const -> VkBuffer
    {
        return _buffer.buffer();
    }
    [[nodiscard]] constexpr auto size() const -> u32
    {
        return _buffer.size();
    }
    [[nodiscard]] constexpr auto cpu_address() const -> u8 *
    {
        return _buffer.cpu_address();
    }

    // The part of the buffer an allocation is in, to bind it as a root CBV/SRV
    // (counterpart of d3d12::constant_buffer::gpu_address()). Lock-free: only computes the range.
    template <typename T>
    [[nodiscard]] auto range(T *const allocation, u64 size = sizeof(T)) const -> vkx::BufferRange
    {
        const u8 *const base{cpu_address()};
        assert(base && allocation);
        if (!base || !allocation)
        {
            return {};
        }
        const u8 *const address{reinterpret_cast<const u8 *>(allocation)};
        assert(address >= base && address + size <= base + _cpu_offset.load(std::memory_order_relaxed));
        return {buffer(), static_cast<u64>(address - base), size};
    }

    [[nodiscard]] constexpr static auto get_default_init_info(u32 size) -> VulkanBufferInitInfo
    {
        assert(size);
        VulkanBufferInitInfo info{};
        info.size = size;
        info.alignment = alignment;
        // Root CBVs, root SRVs (structured buffers written by the CPU, e.g. lights) and indirect arguments.
        info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                     VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        return info;
    }

  private:
    VulkanBuffer _buffer;
    std::atomic<u32> _cpu_offset{0};
};

//// UAV CLEARABLE BUFFER /////////////////////////////////////////////////////////////////////////
// A GPU-only storage buffer that can be cleared (counterpart of d3d12::uav_clearable_buffer).
// D3D12 needs a UAV descriptor in a CPU and a GPU heap to clear it; Vulkan fills it directly.
class UavClearableBuffer
{
  public:
    UavClearableBuffer() = default;
    explicit UavClearableBuffer(const VulkanBufferInitInfo &info);
    DISABLE_COPY(UavClearableBuffer);
    UavClearableBuffer(UavClearableBuffer &&o) noexcept = default;
    auto operator=(UavClearableBuffer &&o) noexcept -> UavClearableBuffer & = default;
    ~UavClearableBuffer()
    {
        release();
    }

    auto release() -> void
    {
        _buffer.release();
    }

    // Same as ClearUnorderedAccessViewUint/Float on an R32 view: every 32-bit element gets values[0].
    // The caller adds the barrier between the clear (copy_dest) and the next use, like a UAV barrier.
    auto clear_uav(VkCommandBuffer cmd, const u32 *const values) const -> void
    {
        assert(buffer() && values);
        ::vkCmdFillBuffer(cmd, buffer(), 0, VK_WHOLE_SIZE, values[0]);
    }

    auto clear_uav(VkCommandBuffer cmd, const f32 *const values) const -> void
    {
        assert(values);
        u32 bits{0};
        std::memcpy(&bits, values, sizeof(bits));
        clear_uav(cmd, &bits);
    }

    [[nodiscard]] constexpr auto buffer() const -> VkBuffer
    {
        return _buffer.buffer();
    }
    [[nodiscard]] constexpr auto size() const -> u32
    {
        return _buffer.size();
    }
    [[nodiscard]] constexpr auto range() const -> vkx::BufferRange
    {
        return _buffer.range();
    }

    [[nodiscard]] constexpr static auto get_default_init_info(u32 size) -> VulkanBufferInitInfo
    {
        assert(size);
        VulkanBufferInitInfo info{};
        info.size = size;
        info.alignment = sizeof(math::v4);
        info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        return info;
    }

  private:
    VulkanBuffer _buffer;
};

//// TEXTURE //////////////////////////////////////////////////////////////////////////////////////
struct VulkanTextureInitInfo
{
    // Required. sType, imageType, format, extent, mipLevels, arrayLayers, samples and usage must be set.
    // VK_IMAGE_USAGE_SAMPLED_BIT is added (every texture gets an SRV).
    const VkImageCreateInfo *image_info{nullptr};
    // Optional SRV view (image is filled in). Default: all mips and layers, 2D / 2D array / cube
    // (VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT with 6 layers) / cube array, aspect from the format.
    const VkImageViewCreateInfo *srv_view_info{nullptr};
    // The layout the image is in whenever shaders sample it (written into the SRV descriptor).
    VkImageLayout srv_layout{VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL};
};

// An image + VMA allocation + SRV in the resource heap (counterpart of d3d12_texture).
// The image starts in VK_IMAGE_LAYOUT_UNDEFINED; transition it before use (vkx::vulkan_barrier).
class VulkanTexture
{
  public:
    constexpr static u32 max_mips{14}; // support up to 16k resolutions.
    VulkanTexture() = default;
    explicit VulkanTexture(const VulkanTextureInitInfo &info);
    DISABLE_COPY(VulkanTexture);
    constexpr VulkanTexture(VulkanTexture &&o) noexcept
        : _image{o._image}
        , _allocation{o._allocation}
        , _view{o._view}
        , _srv{o._srv}
        , _format{o._format}
        , _aspect{o._aspect}
        , _mip_count{o._mip_count}
    {
        o.reset();
    }

    constexpr auto operator=(VulkanTexture &&o) noexcept -> VulkanTexture &
    {
        assert(this != &o);
        if (this != &o)
        {
            release();
            move(o);
        }
        return *this;
    }

    ~VulkanTexture()
    {
        release();
    }

    auto release() -> void;
    [[nodiscard]] constexpr auto image() const -> VkImage
    {
        return _image;
    }
    // The SRV view (all mips).
    [[nodiscard]] constexpr auto view() const -> VkImageView
    {
        return _view;
    }
    [[nodiscard]] constexpr auto srv() const -> DescriptorHandle
    {
        return _srv;
    }
    [[nodiscard]] constexpr auto format() const -> VkFormat
    {
        return _format;
    }
    [[nodiscard]] constexpr auto aspect() const -> VkImageAspectFlags
    {
        return _aspect;
    }
    [[nodiscard]] constexpr auto mip_count() const -> u32
    {
        return _mip_count;
    }

  private:
    constexpr auto move(VulkanTexture &o) -> void
    {
        _image = o._image;
        _allocation = o._allocation;
        _view = o._view;
        _srv = o._srv;
        _format = o._format;
        _aspect = o._aspect;
        _mip_count = o._mip_count;
        o.reset();
    }

    constexpr auto reset() -> void
    {
        _image = VK_NULL_HANDLE;
        _allocation = VK_NULL_HANDLE;
        _view = VK_NULL_HANDLE;
        _srv = {};
        _format = VK_FORMAT_UNDEFINED;
        _aspect = 0;
        _mip_count = 0;
    }

    VkImage _image{VK_NULL_HANDLE};
    VmaAllocation _allocation{VK_NULL_HANDLE};
    VkImageView _view{VK_NULL_HANDLE};
    DescriptorHandle _srv{};
    VkFormat _format{VK_FORMAT_UNDEFINED};
    VkImageAspectFlags _aspect{0};
    u32 _mip_count{0};
};

//// RENDER TEXTURE ///////////////////////////////////////////////////////////////////////////////
// A texture that can be rendered to, one color attachment view per mip (counterpart of
// d3d12_render_texture's RTVs).
class VulkanRenderTexture
{
  public:
    VulkanRenderTexture() = default;
    explicit VulkanRenderTexture(const VulkanTextureInitInfo &info);
    DISABLE_COPY(VulkanRenderTexture);
    constexpr VulkanRenderTexture(VulkanRenderTexture &&o) noexcept
        : _texture{std::move(o._texture)}
    {
        for (u32 i{0}; i < VulkanTexture::max_mips; ++i)
        {
            _rtv[i] = o._rtv[i];
        }
        o.reset();
    }

    constexpr auto operator=(VulkanRenderTexture &&o) noexcept -> VulkanRenderTexture &
    {
        assert(this != &o);
        if (this != &o)
        {
            release();
            move(o);
        }
        return *this;
    }

    ~VulkanRenderTexture()
    {
        release();
    }

    auto release() -> void;
    [[nodiscard]] constexpr auto mip_count() const -> u32
    {
        return _texture.mip_count();
    }
    [[nodiscard]] constexpr auto rtv(u32 mip_index) const -> VkImageView
    {
        assert(mip_index < mip_count());
        return _rtv[mip_index];
    }
    [[nodiscard]] constexpr auto srv() const -> DescriptorHandle
    {
        return _texture.srv();
    }
    [[nodiscard]] constexpr auto image() const -> VkImage
    {
        return _texture.image();
    }
    [[nodiscard]] constexpr auto format() const -> VkFormat
    {
        return _texture.format();
    }

  private:
    constexpr auto move(VulkanRenderTexture &o) -> void
    {
        _texture = std::move(o._texture);
        for (u32 i{0}; i < VulkanTexture::max_mips; ++i)
        {
            _rtv[i] = o._rtv[i];
        }
        o.reset();
    }

    constexpr auto reset() -> void
    {
        for (u32 i{0}; i < VulkanTexture::max_mips; ++i)
        {
            _rtv[i] = VK_NULL_HANDLE;
        }
    }

    VulkanTexture _texture;
    VkImageView _rtv[VulkanTexture::max_mips]{};
};

//// DEPTH BUFFER /////////////////////////////////////////////////////////////////////////////////
// A depth texture (counterpart of d3d12_depth_buffer). D3D12 needs a typeless resource with separate
// DSV and SRV formats; in Vulkan one depth-aspect view serves as both.
class VulkanDepthBuffer
{
  public:
    VulkanDepthBuffer() = default;
    // image_info->format must be a depth format (no stencil). Usage gets DEPTH_STENCIL_ATTACHMENT added.
    explicit VulkanDepthBuffer(const VulkanTextureInitInfo &info);
    DISABLE_COPY(VulkanDepthBuffer);
    VulkanDepthBuffer(VulkanDepthBuffer &&o) noexcept = default;
    auto operator=(VulkanDepthBuffer &&o) noexcept -> VulkanDepthBuffer & = default;
    ~VulkanDepthBuffer()
    {
        release();
    }

    auto release() -> void
    {
        _texture.release();
    }
    [[nodiscard]] constexpr auto dsv() const -> VkImageView
    {
        return _texture.view();
    }
    [[nodiscard]] constexpr auto srv() const -> DescriptorHandle
    {
        return _texture.srv();
    }
    [[nodiscard]] constexpr auto image() const -> VkImage
    {
        return _texture.image();
    }
    [[nodiscard]] constexpr auto format() const -> VkFormat
    {
        return _texture.format();
    }

  private:
    VulkanTexture _texture;
};

}
