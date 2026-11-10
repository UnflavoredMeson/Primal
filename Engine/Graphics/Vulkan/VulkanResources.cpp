// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "VulkanResources.h"
#include "VulkanCore.h"
#include "VulkanUpload.h"

namespace primal::graphics::vulkan
{
//// DESCRIPTOR HEAP //////////////////////////////////////////////////////////////////////////////
namespace
{

// The descriptor types a resource heap slot can hold (everything DXC can make of ResourceDescriptorHeap[]
// in the current shaders: textures, RW textures, ConstantBuffer<T>, (RW)StructuredBuffer<T>).
constexpr VkDescriptorType heap_descriptor_types[]{
    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
    VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
};

}

auto DescriptorHeap::initialize(u32 capacity) -> bool
{
    const std::scoped_lock lock{_mutex};
    assert(capacity);
    assert(!_pool && !_size);

    const VkDevice device{core::device()};
    assert(device);

    const VkMutableDescriptorTypeListEXT type_list{
        static_cast<u32>(std::size(heap_descriptor_types)), &heap_descriptor_types[0]};
    VkMutableDescriptorTypeCreateInfoEXT mutable_info{VK_STRUCTURE_TYPE_MUTABLE_DESCRIPTOR_TYPE_CREATE_INFO_EXT};
    mutable_info.mutableDescriptorTypeListCount = 1;
    mutable_info.pMutableDescriptorTypeLists = &type_list;

    // Set layout: binding 0 = ResourceDescriptorHeap[capacity].
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_MUTABLE_EXT;
    binding.descriptorCount = capacity;
    binding.stageFlags = VK_SHADER_STAGE_ALL;

    // Like a D3D12 descriptor heap: slots may be unwritten (partially bound) and can be written while
    // command buffers using the set are recorded or pending (update after bind / unused while pending).
    const VkDescriptorBindingFlags binding_flags{
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
        VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT};
    VkDescriptorSetLayoutBindingFlagsCreateInfo flags_info{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
    flags_info.pNext = &mutable_info;
    flags_info.bindingCount = 1;
    flags_info.pBindingFlags = &binding_flags;

    VkDescriptorSetLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout_info.pNext = &flags_info;
    layout_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layout_info.bindingCount = 1;
    layout_info.pBindings = &binding;

    VkDescriptorSetLayoutSupport support{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_SUPPORT};
    ::vkGetDescriptorSetLayoutSupport(device, &layout_info, &support);
    if (!support.supported)
    {
        log_message("Vulkan: a resource heap with %u mutable descriptors isn't supported.\n", capacity);
        return false;
    }

    if (!VkCall(::vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &_set_layout)))
    {
        return false;
    }
    NAME_VK_OBJECT(_set_layout, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "Resource Heap Layout");

    const VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_MUTABLE_EXT, capacity};
    VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.pNext = &mutable_info; // the pool's mutable descriptors can hold the same types
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    pool_info.maxSets = 1;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    if (!VkCall(::vkCreateDescriptorPool(device, &pool_info, nullptr, &_pool)))
    {
        return false;
    }
    NAME_VK_OBJECT(_pool, VK_OBJECT_TYPE_DESCRIPTOR_POOL, "Resource Heap Pool");

    VkDescriptorSetAllocateInfo set_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    set_info.descriptorPool = _pool;
    set_info.descriptorSetCount = 1;
    set_info.pSetLayouts = &_set_layout;
    if (!VkCall(::vkAllocateDescriptorSets(device, &set_info, &_set)))
    {
        return false;
    }
    NAME_VK_OBJECT(_set, VK_OBJECT_TYPE_DESCRIPTOR_SET, "Resource Heap");

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

    return true;
}

auto DescriptorHeap::release() -> void
{
    assert(!_size);
    // The set is freed with its pool.
    core::deferred_release(_pool);
    core::deferred_release(_set_layout);
    _set = VK_NULL_HANDLE;
    _capacity = 0;
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
    assert(_set);
    assert(_size < _capacity);

    const u32 index{_free_handles[_size]};

    assert(!_allocated_slots[index]);
    _allocated_slots[index] = 1;
    ++_size;

    DescriptorHandle handle;
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
    const std::scoped_lock lock{_mutex};
    assert(_set && _size);
    assert(handle._container == this);
    assert(handle.index < _capacity);

    const u32 frame_idx{core::current_frame_index()};
    _deferred_free_indices[frame_idx].push_back(handle.index);
    core::set_deferred_releases_flag(frame_idx);
    handle = {};
}

auto DescriptorHeap::write(
    const DescriptorHandle &handle,
    VkDescriptorType type,
    const VkDescriptorImageInfo *image,
    const VkDescriptorBufferInfo *buffer) const -> void
{
    assert(_set && handle.is_valid() && handle.index < _capacity);
    assert(handle._container == this);
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = _set;
    write.dstBinding = 0;
    write.dstArrayElement = handle.index;
    write.descriptorCount = 1;
    write.descriptorType = type; // a concrete type: the slot becomes a descriptor of that type
    write.pImageInfo = image;
    write.pBufferInfo = buffer;
    // Host access to the set must be externally synchronized, and textures are created on several
    // threads (D3D12's CreateShaderResourceView is free-threaded).
    const std::scoped_lock lock{_mutex};
    ::vkUpdateDescriptorSets(core::device(), 1, &write, 0, nullptr);
}

auto DescriptorHeap::write_sampled_image(const DescriptorHandle &handle, VkImageView view, VkImageLayout layout) const
    -> void
{
    assert(view);
    const VkDescriptorImageInfo image{VK_NULL_HANDLE, view, layout};
    write(handle, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &image, nullptr);
}

auto DescriptorHeap::write_storage_image(const DescriptorHandle &handle, VkImageView view) const -> void
{
    assert(view);
    const VkDescriptorImageInfo image{VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_GENERAL};
    write(handle, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &image, nullptr);
}

auto DescriptorHeap::write_storage_buffer(const DescriptorHandle &handle, const vkx::BufferRange &range) const -> void
{
    assert(range.is_valid());
    const VkDescriptorBufferInfo buffer{range.buffer, range.offset, range.size};
    write(handle, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &buffer);
}

auto DescriptorHeap::write_uniform_buffer(const DescriptorHandle &handle, const vkx::BufferRange &range) const -> void
{
    assert(range.is_valid());
    const VkDescriptorBufferInfo buffer{range.buffer, range.offset, range.size};
    write(handle, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &buffer);
}

//// VULKAN BUFFER ////////////////////////////////////////////////////////////////////////////////
VulkanBuffer::VulkanBuffer(const VulkanBufferInitInfo &info, bool is_cpu_accessible)
{
    assert(!_buffer && info.size && info.alignment && info.usage);
    _size = static_cast<u32>(math::align_size_up(info.size, info.alignment));

    VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.size = _size;
    buffer_info.usage = info.usage;
    // GPU-only buffers with initial data are filled by a copy from an upload buffer.
    if (info.data && !is_cpu_accessible)
    {
        buffer_info.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
    if (is_cpu_accessible)
    {
        // Upload heap: host-visible, write-combined, mapped for the buffer's lifetime.
        alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }

    VmaAllocationInfo allocation{};
    if (!VkCall(
            ::vmaCreateBufferWithAlignment(
                core::allocator(), &buffer_info, &alloc_info, info.alignment, &_buffer, &_allocation, &allocation)))
    {
        reset();
        return;
    }
    _cpu_address = static_cast<u8 *>(allocation.pMappedData);
    assert(!is_cpu_accessible || _cpu_address);
    NAME_VK_OBJECT_INDEXED(_buffer, VK_OBJECT_TYPE_BUFFER, _size, "Vulkan Buffer - size");

    if (info.data)
    {
        // Same rule as d3dx::create_buffer(): data the CPU will change later goes into a CPU-accessible
        // buffer; data uploaded once into a GPU-only buffer goes through an upload context.
        if (is_cpu_accessible)
        {
            std::memcpy(_cpu_address, info.data, info.size);
        }
        else
        {
            upload::VulkanUploadContext context{_size};
            std::memcpy(context.cpu_address(), info.data, info.size);
            const VkBufferCopy region{0, 0, info.size};
            ::vkCmdCopyBuffer(context.command_buffer(), context.upload_buffer(), _buffer, 1, &region);
            context.end_upload();
        }
    }
}

auto VulkanBuffer::release() -> void
{
    core::deferred_release(_buffer, _allocation);
    _cpu_address = nullptr;
    _size = 0;
}

//// CONSTANT BUFFER //////////////////////////////////////////////////////////////////////////////
ConstantBuffer::ConstantBuffer(const VulkanBufferInitInfo &info)
    : _buffer{info, true}
{
    assert(info.alignment % alignment == 0);
    NAME_VK_OBJECT_INDEXED(buffer(), VK_OBJECT_TYPE_BUFFER, size(), "Constant Buffer - size");
}

auto ConstantBuffer::allocate(u32 size) -> u8 *
{
    const std::scoped_lock lock{_mutex};
    const u32 aligned_size{static_cast<u32>(math::align_size_up<alignment>(size))};
    assert(_cpu_offset + aligned_size <= _buffer.size());
    if (_cpu_offset + aligned_size <= _buffer.size())
    {
        u8 *const address{_buffer.cpu_address() + _cpu_offset};
        _cpu_offset += aligned_size;
        return address;
    }

    return nullptr;
}

//// UAV CLEARABLE BUFFER /////////////////////////////////////////////////////////////////////////
UavClearableBuffer::UavClearableBuffer(const VulkanBufferInitInfo &info)
    : _buffer{info, false}
{
    assert(info.size && info.alignment);
    assert((info.usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) && (info.usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT));
    NAME_VK_OBJECT_INDEXED(buffer(), VK_OBJECT_TYPE_BUFFER, info.size, "UAV Clearable Buffer - size");
}

//// VULKAN TEXTURE ///////////////////////////////////////////////////////////////////////////////
namespace
{

auto default_view_type(const VkImageCreateInfo &info) -> VkImageViewType
{
    switch (info.imageType)
    {
        case VK_IMAGE_TYPE_1D: return info.arrayLayers > 1 ? VK_IMAGE_VIEW_TYPE_1D_ARRAY : VK_IMAGE_VIEW_TYPE_1D;
        case VK_IMAGE_TYPE_3D: return VK_IMAGE_VIEW_TYPE_3D;
        default: break;
    }

    if ((info.flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) && info.arrayLayers % 6 == 0)
    {
        return info.arrayLayers > 6 ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY : VK_IMAGE_VIEW_TYPE_CUBE;
    }
    return info.arrayLayers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
}

}

VulkanTexture::VulkanTexture(const VulkanTextureInitInfo &info)
{
    assert(info.image_info);
    const VkDevice device{core::device()};
    assert(device);

    VkImageCreateInfo image_info{*info.image_info};
    assert(image_info.sType == VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    assert(image_info.mipLevels && image_info.mipLevels <= max_mips);
    image_info.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    // Render targets and depth buffers get their own memory block (D3D12 committed resource).
    if (image_info.usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))
    {
        alloc_info.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    }

    if (!VkCall(::vmaCreateImage(core::allocator(), &image_info, &alloc_info, &_image, &_allocation, nullptr)))
    {
        reset();
        return;
    }

    _format = image_info.format;
    _aspect = vkx::aspect_flags(_format);
    _mip_count = image_info.mipLevels;

    VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    if (info.srv_view_info)
    {
        view_info = *info.srv_view_info;
    }
    else
    {
        view_info.viewType = default_view_type(image_info);
        view_info.format = _format;
        // Depth/stencil formats: shaders sample the depth aspect.
        view_info.subresourceRange = {
            _aspect & VK_IMAGE_ASPECT_STENCIL_BIT ? static_cast<VkImageAspectFlags>(VK_IMAGE_ASPECT_DEPTH_BIT)
                                                  : _aspect,
            0,
            VK_REMAINING_MIP_LEVELS,
            0,
            VK_REMAINING_ARRAY_LAYERS};
    }
    view_info.image = _image;
    if (!VkCall(::vkCreateImageView(device, &view_info, nullptr, &_view)))
    {
        release();
        return;
    }

    _srv = core::srv_heap().allocate();
    core::srv_heap().write_sampled_image(_srv, _view, info.srv_layout);
}

auto VulkanTexture::release() -> void
{
    core::srv_heap().free(_srv);
    core::deferred_release(_view);
    core::deferred_release(_image, _allocation);
    _format = VK_FORMAT_UNDEFINED;
    _aspect = 0;
    _mip_count = 0;
}

//// RENDER TEXTURE ///////////////////////////////////////////////////////////////////////////////
VulkanRenderTexture::VulkanRenderTexture(const VulkanTextureInitInfo &info)
{
    assert(info.image_info);
    VkImageCreateInfo image_info{*info.image_info};
    image_info.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    VulkanTextureInitInfo texture_info{info};
    texture_info.image_info = &image_info;
    _texture = VulkanTexture{texture_info};
    if (!image())
    {
        return;
    }

    const VkDevice device{core::device()};
    VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_info.image = image();
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = format();
    view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    for (u32 i{0}; i < mip_count(); ++i)
    {
        view_info.subresourceRange.baseMipLevel = i;
        VkCall(::vkCreateImageView(device, &view_info, nullptr, &_rtv[i]));
    }
}

auto VulkanRenderTexture::release() -> void
{
    for (u32 i{0}; i < VulkanTexture::max_mips; ++i)
    {
        core::deferred_release(_rtv[i]);
    }
    _texture.release();
}

//// DEPTH BUFFER /////////////////////////////////////////////////////////////////////////////////
VulkanDepthBuffer::VulkanDepthBuffer(const VulkanTextureInitInfo &info)
{
    assert(info.image_info && !info.srv_view_info);
    assert(vkx::aspect_flags(info.image_info->format) == VK_IMAGE_ASPECT_DEPTH_BIT);
    assert(info.image_info->mipLevels == 1);
    VkImageCreateInfo image_info{*info.image_info};
    image_info.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    VulkanTextureInitInfo texture_info{info};
    texture_info.image_info = &image_info;
    _texture = VulkanTexture{texture_info};
}

}
