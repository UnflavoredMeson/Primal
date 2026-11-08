// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "VulkanCommonHeaders.h"
#include "VulkanHelpers.h"

// Counterpart of D3D12Resources.h. Memory comes from the Vulkan Memory Allocator (core::allocator()).
namespace primal::graphics::vulkan {

class descriptor_heap;

//// DESCRIPTOR HEAP //////////////////////////////////////////////////////////////////////////////
// A slot in the resource heap. The index is what shaders use with ResourceDescriptorHeap[index].
struct descriptor_handle
{
    u32 index{ u32_invalid_id };

    [[nodiscard]] constexpr bool is_valid() const { return index != u32_invalid_id; }

#ifdef _DEBUG
private:
    friend class descriptor_heap;
    descriptor_heap* _container{ nullptr };
#endif
};

// The bindless resource heap (set 1, binding 0: one array of mutable descriptors, DXC's
// ResourceDescriptorHeap[]). Counterpart of the shader-visible CBV/SRV/UAV descriptor_heap in D3D12.
// RTV/DSV heaps have no Vulkan counterpart: render targets and depth buffers use image views.
// NOTE: the set is update-after-bind, so slots can be written while command buffers that use the heap
//       are pending, as long as those don't use the same slots (freed slots are reused frame_buffer_count
//       frames later, like in D3D12).
class descriptor_heap
{
public:
    descriptor_heap() = default;
    DISABLE_COPY_AND_MOVE(descriptor_heap);
    ~descriptor_heap() { assert(!_pool); }

    bool initialize(u32 capacity);
    void release();
    void process_deferred_free(u32 frame_idx);

    [[nodiscard]] descriptor_handle allocate();
    void free(descriptor_handle& handle);

    // Write a view into a slot. The descriptor type a slot holds can change (mutable descriptors).
    void write_sampled_image(const descriptor_handle& handle, VkImageView view,
                             VkImageLayout layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL) const;
    void write_storage_image(const descriptor_handle& handle, VkImageView view) const; // layout GENERAL
    void write_storage_buffer(const descriptor_handle& handle, const vkx::buffer_range& range) const;
    void write_uniform_buffer(const descriptor_handle& handle, const vkx::buffer_range& range) const;

    [[nodiscard]] constexpr VkDescriptorSet set() const { return _set; }
    [[nodiscard]] constexpr VkDescriptorSetLayout set_layout() const { return _set_layout; }
    [[nodiscard]] constexpr u32 capacity() const { return _capacity; }
    [[nodiscard]] constexpr u32 size() const { return _size; }

private:
    void write(const descriptor_handle& handle, VkDescriptorType type, const VkDescriptorImageInfo* image,
               const VkDescriptorBufferInfo* buffer) const;

    VkDescriptorPool _pool{ VK_NULL_HANDLE };
    VkDescriptorSetLayout _set_layout{ VK_NULL_HANDLE };
    VkDescriptorSet _set{ VK_NULL_HANDLE };
    std::unique_ptr<u32[]> _free_handles{};
    std::unique_ptr<u8[]> _allocated_slots{};
    utl::vector<u32> _deferred_free_indices[frame_buffer_count]{};
    std::mutex _mutex{};
    u32 _capacity{ 0 };
    u32 _size{ 0 };
};

//// BUFFER ///////////////////////////////////////////////////////////////////////////////////////
struct vulkan_buffer_init_info
{
    const void* data{ nullptr };
    VkBufferUsageFlags usage{ 0 };
    u32 size{ 0 };
    u32 alignment{ 0 };
};

// A buffer with its own VMA allocation (counterpart of d3d12_buffer). CPU-accessible buffers are
// persistently mapped (host-visible, write-combined; D3D12's upload heap).
class vulkan_buffer
{
public:
    vulkan_buffer() = default;
    explicit vulkan_buffer(const vulkan_buffer_init_info& info, bool is_cpu_accessible);
    DISABLE_COPY(vulkan_buffer);
    constexpr vulkan_buffer(vulkan_buffer&& o) noexcept
        : _buffer{ o._buffer }, _allocation{ o._allocation }, _cpu_address{ o._cpu_address }, _size{ o._size }
    {
        o.reset();
    }

    constexpr vulkan_buffer& operator=(vulkan_buffer&& o) noexcept
    {
        assert(this != &o);
        if (this != &o)
        {
            release();
            move(o);
        }

        return *this;
    }

    ~vulkan_buffer() { release(); }

    void release();
    [[nodiscard]] constexpr VkBuffer buffer() const { return _buffer; }
    [[nodiscard]] constexpr u32 size() const { return _size; }
    // Mapped memory of a CPU-accessible buffer, nullptr otherwise.
    [[nodiscard]] constexpr u8* cpu_address() const { return _cpu_address; }
    [[nodiscard]] constexpr vkx::buffer_range range(u64 offset = 0, u64 size = VK_WHOLE_SIZE) const
    {
        return { _buffer, offset, size };
    }

private:
    constexpr void move(vulkan_buffer& o)
    {
        _buffer = o._buffer;
        _allocation = o._allocation;
        _cpu_address = o._cpu_address;
        _size = o._size;
        o.reset();
    }

    constexpr void reset()
    {
        _buffer = VK_NULL_HANDLE;
        _allocation = VK_NULL_HANDLE;
        _cpu_address = nullptr;
        _size = 0;
    }

    VkBuffer _buffer{ VK_NULL_HANDLE };
    VmaAllocation _allocation{ VK_NULL_HANDLE };
    u8* _cpu_address{ nullptr };
    u32 _size{ 0 };
};

//// CONSTANT BUFFER //////////////////////////////////////////////////////////////////////////////
// Linear per-frame allocator for shader constants (counterpart of d3d12::constant_buffer). Core
// keeps one per frame and clears it at the start of the frame.
class constant_buffer
{
public:
    // Valid for uniform and storage buffer offsets on every device (the spec's maximum for
    // minUniformBufferOffsetAlignment / minStorageBufferOffsetAlignment), same as D3D12.
    constexpr static u32 alignment{ 256 };

    constant_buffer() = default;
    explicit constant_buffer(const vulkan_buffer_init_info& info);
    DISABLE_COPY_AND_MOVE(constant_buffer);
    ~constant_buffer() { release(); }

    void release()
    {
        _buffer.release();
        _cpu_offset = 0;
    }

    constexpr void clear() { _cpu_offset = 0; }
    [[nodiscard]] u8* allocate(u32 size);

    template<typename T>
    [[nodiscard]] T* allocate()
    {
        return (T*)allocate(sizeof(T));
    }

    [[nodiscard]] constexpr VkBuffer buffer() const { return _buffer.buffer(); }
    [[nodiscard]] constexpr u32 size() const { return _buffer.size(); }
    [[nodiscard]] constexpr u8* cpu_address() const { return _buffer.cpu_address(); }

    // The part of the buffer an allocation is in, to bind it as a root CBV/SRV
    // (counterpart of d3d12::constant_buffer::gpu_address()).
    template<typename T>
    [[nodiscard]] vkx::buffer_range range(T* const allocation, u64 size = sizeof(T))
    {
        std::scoped_lock lock{ _mutex };
        const u8* const base{ cpu_address() };
        assert(base);
        if (!base) return {};
        const u8* const address{ (const u8*)allocation };
        assert(address >= base && address + size <= base + _cpu_offset);
        return { buffer(), (u64)(address - base), size };
    }

    [[nodiscard]] constexpr static vulkan_buffer_init_info get_default_init_info(u32 size)
    {
        assert(size);
        vulkan_buffer_init_info info{};
        info.size = size;
        info.alignment = alignment;
        // Root CBVs, root SRVs (structured buffers written by the CPU, e.g. lights) and indirect arguments.
        info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        return info;
    }

private:
    vulkan_buffer _buffer{};
    u32 _cpu_offset{ 0 };
    std::mutex _mutex{};
};

//// UAV CLEARABLE BUFFER /////////////////////////////////////////////////////////////////////////
// A GPU-only storage buffer that can be cleared (counterpart of d3d12::uav_clearable_buffer).
// D3D12 needs a UAV descriptor in a CPU and a GPU heap to clear it; Vulkan fills it directly.
class uav_clearable_buffer
{
public:
    uav_clearable_buffer() = default;
    explicit uav_clearable_buffer(const vulkan_buffer_init_info& info);
    DISABLE_COPY(uav_clearable_buffer);
    uav_clearable_buffer(uav_clearable_buffer&& o) noexcept = default;
    uav_clearable_buffer& operator=(uav_clearable_buffer&& o) noexcept = default;
    ~uav_clearable_buffer() { release(); }

    void release() { _buffer.release(); }

    // Same as ClearUnorderedAccessViewUint/Float on an R32 view: every 32-bit element gets values[0].
    // The caller adds the barrier between the clear (copy_dest) and the next use, like a UAV barrier.
    void clear_uav(VkCommandBuffer cmd, const u32* const values) const
    {
        assert(buffer() && values);
        vkCmdFillBuffer(cmd, buffer(), 0, VK_WHOLE_SIZE, values[0]);
    }

    void clear_uav(VkCommandBuffer cmd, const f32* const values) const
    {
        assert(values);
        u32 bits{ 0 };
        std::memcpy(&bits, values, sizeof(bits));
        clear_uav(cmd, &bits);
    }

    [[nodiscard]] constexpr VkBuffer buffer() const { return _buffer.buffer(); }
    [[nodiscard]] constexpr u32 size() const { return _buffer.size(); }
    [[nodiscard]] constexpr vkx::buffer_range range() const { return _buffer.range(); }

    [[nodiscard]] constexpr static vulkan_buffer_init_info get_default_init_info(u32 size)
    {
        assert(size);
        vulkan_buffer_init_info info{};
        info.size = size;
        info.alignment = sizeof(math::v4);
        info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        return info;
    }

private:
    vulkan_buffer _buffer{};
};

//// TEXTURE //////////////////////////////////////////////////////////////////////////////////////
struct vulkan_texture_init_info
{
    // Required. sType, imageType, format, extent, mipLevels, arrayLayers, samples and usage must be set.
    // VK_IMAGE_USAGE_SAMPLED_BIT is added (every texture gets an SRV).
    const VkImageCreateInfo* image_info{ nullptr };
    // Optional SRV view (image is filled in). Default: all mips and layers, 2D / 2D array / cube
    // (VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT with 6 layers) / cube array, aspect from the format.
    const VkImageViewCreateInfo* srv_view_info{ nullptr };
    // The layout the image is in whenever shaders sample it (written into the SRV descriptor).
    VkImageLayout srv_layout{ VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL };
};

// An image + VMA allocation + SRV in the resource heap (counterpart of d3d12_texture).
// The image starts in VK_IMAGE_LAYOUT_UNDEFINED; transition it before use (vkx::vulkan_barrier).
class vulkan_texture
{
public:
    constexpr static u32 max_mips{ 14 }; // support up to 16k resolutions.
    vulkan_texture() = default;
    explicit vulkan_texture(const vulkan_texture_init_info& info);
    DISABLE_COPY(vulkan_texture);
    constexpr vulkan_texture(vulkan_texture&& o) noexcept
        : _image{ o._image }, _allocation{ o._allocation }, _view{ o._view }, _srv{ o._srv }, _format{ o._format }, _aspect{ o._aspect }, _mip_count{ o._mip_count }
    {
        o.reset();
    }

    constexpr vulkan_texture& operator=(vulkan_texture&& o) noexcept
    {
        assert(this != &o);
        if (this != &o)
        {
            release();
            move(o);
        }
        return *this;
    }

    ~vulkan_texture() { release(); }

    void release();
    [[nodiscard]] constexpr VkImage image() const { return _image; }
    // The SRV view (all mips).
    [[nodiscard]] constexpr VkImageView view() const { return _view; }
    [[nodiscard]] constexpr descriptor_handle srv() const { return _srv; }
    [[nodiscard]] constexpr VkFormat format() const { return _format; }
    [[nodiscard]] constexpr VkImageAspectFlags aspect() const { return _aspect; }
    [[nodiscard]] constexpr u32 mip_count() const { return _mip_count; }

private:
    constexpr void move(vulkan_texture& o)
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

    constexpr void reset()
    {
        _image = VK_NULL_HANDLE;
        _allocation = VK_NULL_HANDLE;
        _view = VK_NULL_HANDLE;
        _srv = {};
        _format = VK_FORMAT_UNDEFINED;
        _aspect = 0;
        _mip_count = 0;
    }

    VkImage _image{ VK_NULL_HANDLE };
    VmaAllocation _allocation{ VK_NULL_HANDLE };
    VkImageView _view{ VK_NULL_HANDLE };
    descriptor_handle _srv{};
    VkFormat _format{ VK_FORMAT_UNDEFINED };
    VkImageAspectFlags _aspect{ 0 };
    u32 _mip_count{ 0 };
};

//// RENDER TEXTURE ///////////////////////////////////////////////////////////////////////////////
// A texture that can be rendered to, one color attachment view per mip (counterpart of
// d3d12_render_texture's RTVs).
class vulkan_render_texture
{
public:
    vulkan_render_texture() = default;
    explicit vulkan_render_texture(const vulkan_texture_init_info& info);
    DISABLE_COPY(vulkan_render_texture);
    constexpr vulkan_render_texture(vulkan_render_texture&& o) noexcept
        : _texture{ std::move(o._texture) }
    {
        for (u32 i{ 0 }; i < vulkan_texture::max_mips; ++i) _rtv[i] = o._rtv[i];
        o.reset();
    }

    constexpr vulkan_render_texture& operator=(vulkan_render_texture&& o) noexcept
    {
        assert(this != &o);
        if (this != &o)
        {
            release();
            move(o);
        }
        return *this;
    }

    ~vulkan_render_texture() { release(); }

    void release();
    [[nodiscard]] constexpr u32 mip_count() const { return _texture.mip_count(); }
    [[nodiscard]] constexpr VkImageView rtv(u32 mip_index) const
    {
        assert(mip_index < mip_count());
        return _rtv[mip_index];
    }
    [[nodiscard]] constexpr descriptor_handle srv() const { return _texture.srv(); }
    [[nodiscard]] constexpr VkImage image() const { return _texture.image(); }
    [[nodiscard]] constexpr VkFormat format() const { return _texture.format(); }

private:
    constexpr void move(vulkan_render_texture& o)
    {
        _texture = std::move(o._texture);
        for (u32 i{ 0 }; i < vulkan_texture::max_mips; ++i) _rtv[i] = o._rtv[i];
        o.reset();
    }

    constexpr void reset()
    {
        for (u32 i{ 0 }; i < vulkan_texture::max_mips; ++i) _rtv[i] = VK_NULL_HANDLE;
    }

    vulkan_texture _texture{};
    VkImageView _rtv[vulkan_texture::max_mips]{};
};

//// DEPTH BUFFER /////////////////////////////////////////////////////////////////////////////////
// A depth texture (counterpart of d3d12_depth_buffer). D3D12 needs a typeless resource with separate
// DSV and SRV formats; in Vulkan one depth-aspect view serves as both.
class vulkan_depth_buffer
{
public:
    vulkan_depth_buffer() = default;
    // image_info->format must be a depth format (no stencil). Usage gets DEPTH_STENCIL_ATTACHMENT added.
    explicit vulkan_depth_buffer(const vulkan_texture_init_info& info);
    DISABLE_COPY(vulkan_depth_buffer);
    vulkan_depth_buffer(vulkan_depth_buffer&& o) noexcept = default;
    vulkan_depth_buffer& operator=(vulkan_depth_buffer&& o) noexcept = default;
    ~vulkan_depth_buffer() { release(); }

    void release() { _texture.release(); }
    [[nodiscard]] constexpr VkImageView dsv() const { return _texture.view(); }
    [[nodiscard]] constexpr descriptor_handle srv() const { return _texture.srv(); }
    [[nodiscard]] constexpr VkImage image() const { return _texture.image(); }
    [[nodiscard]] constexpr VkFormat format() const { return _texture.format(); }

private:
    vulkan_texture _texture{};
};

} // namespace primal::graphics::vulkan
