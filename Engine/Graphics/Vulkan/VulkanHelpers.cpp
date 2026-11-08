// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "VulkanHelpers.h"
#include "VulkanCore.h"
#include "VulkanResources.h"

namespace primal::graphics::vulkan::vkx {
namespace {

PFN_vkCmdPushDescriptorSetKHR cmd_push_descriptor_set{ nullptr };

} // anonymous namespace

//// BARRIERS /////////////////////////////////////////////////////////////////////////////////////
void
vulkan_barrier::add(VkImage image, VkImageAspectFlags aspect, const resource_state& before, const resource_state& after,
                    u32 base_mip, u32 mip_count)
{
    assert(image);
    assert(_image_count < max_barriers);
    if (_image_count >= max_barriers) return;

    VkImageMemoryBarrier2& barrier{ _image_barriers[_image_count++] };
    barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
    barrier.srcStageMask = before.stage;
    barrier.srcAccessMask = before.access;
    barrier.dstStageMask = after.stage;
    barrier.dstAccessMask = after.access;
    barrier.oldLayout = before.layout;
    barrier.newLayout = after.layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = { aspect, base_mip, mip_count, 0, VK_REMAINING_ARRAY_LAYERS };
}

void
vulkan_barrier::add(VkBuffer buffer, const resource_state& before, const resource_state& after, u64 offset, u64 size)
{
    assert(buffer);
    assert(_buffer_count < max_barriers);
    if (_buffer_count >= max_barriers) return;

    VkBufferMemoryBarrier2& barrier{ _buffer_barriers[_buffer_count++] };
    barrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
    barrier.srcStageMask = before.stage;
    barrier.srcAccessMask = before.access;
    barrier.dstStageMask = after.stage;
    barrier.dstAccessMask = after.access;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = buffer;
    barrier.offset = offset;
    barrier.size = size;
}

void
vulkan_barrier::apply(VkCommandBuffer cmd)
{
    assert(_image_count || _buffer_count);
    VkDependencyInfo dependency{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    dependency.imageMemoryBarrierCount = _image_count;
    dependency.pImageMemoryBarriers = &_image_barriers[0];
    dependency.bufferMemoryBarrierCount = _buffer_count;
    dependency.pBufferMemoryBarriers = &_buffer_barriers[0];
    vkCmdPipelineBarrier2(cmd, &dependency);
    _image_count = 0;
    _buffer_count = 0;
}

void
transition_image(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, const resource_state& before,
                 const resource_state& after, u32 base_mip, u32 mip_count)
{
    vulkan_barrier barrier{};
    barrier.add(image, aspect, before, after, base_mip, mip_count);
    barrier.apply(cmd);
}

//// ROOT SIGNATURES //////////////////////////////////////////////////////////////////////////////
root_signature
create_root_signature(const root_parameter* parameters, u32 count, [[maybe_unused]] const char* name)
{
    assert(count <= max_root_parameters);
    assert(!count || parameters);
    const VkDevice device{ core::device() };
    assert(device);

    VkDescriptorSetLayoutBinding bindings[max_root_parameters]{};
    VkSampler samplers[max_root_parameters]{}; // pImmutableSamplers must stay valid until the layout is created
    for (u32 i{ 0 }; i < count; ++i)
    {
        const root_parameter& parameter{ parameters[i] };
        assert(parameter.type != VK_DESCRIPTOR_TYPE_MAX_ENUM && parameter.binding != u32_invalid_id);
        VkDescriptorSetLayoutBinding& binding{ bindings[i] };
        binding.binding = parameter.binding;
        binding.descriptorType = parameter.type;
        binding.descriptorCount = 1;
        binding.stageFlags = parameter.stages;
        if (parameter.type == VK_DESCRIPTOR_TYPE_SAMPLER)
        {
            samplers[i] = core::static_sampler(parameter.sampler_type);
            assert(samplers[i]);
            binding.pImmutableSamplers = &samplers[i];
        }
    }

    root_signature signature{};

    VkDescriptorSetLayoutCreateInfo set_info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    set_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    set_info.bindingCount = count;
    set_info.pBindings = &bindings[0];
    if (!VkCall(vkCreateDescriptorSetLayout(device, &set_info, nullptr, &signature.root_set_layout))) return {};
    NAME_VK_OBJECT(signature.root_set_layout, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, name);

    const VkDescriptorSetLayout set_layouts[]{ signature.root_set_layout, core::srv_heap().set_layout() };
    static_assert(root_descriptor_set == 0 && resource_heap_set == 1);
    assert(set_layouts[1]);

    VkPipelineLayoutCreateInfo layout_info{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    layout_info.setLayoutCount = (u32)std::size(set_layouts);
    layout_info.pSetLayouts = &set_layouts[0];
    if (!VkCall(vkCreatePipelineLayout(device, &layout_info, nullptr, &signature.layout)))
    {
        core::deferred_release(signature.root_set_layout);
        return {};
    }
    NAME_VK_OBJECT(signature.layout, VK_OBJECT_TYPE_PIPELINE_LAYOUT, name);

    return signature;
}

void
release(root_signature& signature)
{
    core::deferred_release(signature.layout);
    core::deferred_release(signature.root_set_layout);
}

void
root_descriptors::add(VkDescriptorType type, u32 binding, const buffer_range& range)
{
    assert(range.is_valid());
    assert(_count < max_root_parameters);
    if (_count >= max_root_parameters) return;

    VkDescriptorBufferInfo& buffer{ _buffers[_count] };
    buffer.buffer = range.buffer;
    buffer.offset = range.offset;
    buffer.range = range.size;

    VkWriteDescriptorSet& write{ _writes[_count] };
    write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    write.dstBinding = binding;
    write.descriptorCount = 1;
    write.descriptorType = type;
    write.pBufferInfo = &buffer;
    ++_count;
}

void
root_descriptors::push(VkCommandBuffer cmd, VkPipelineBindPoint bind_point, const root_signature& signature)
{
    assert(cmd_push_descriptor_set && signature.is_valid());
    if (_count) cmd_push_descriptor_set(cmd, bind_point, signature.layout, root_descriptor_set, _count, &_writes[0]);
    _count = 0;
}

void
bind_resource_heap(VkCommandBuffer cmd, VkPipelineBindPoint bind_point, const root_signature& signature)
{
    assert(signature.is_valid());
    const VkDescriptorSet set{ core::srv_heap().set() };
    assert(set);
    vkCmdBindDescriptorSets(cmd, bind_point, signature.layout, resource_heap_set, 1, &set, 0, nullptr);
}

void
load_device_functions(VkDevice device)
{
    cmd_push_descriptor_set = device ? (PFN_vkCmdPushDescriptorSetKHR)vkGetDeviceProcAddr(device, "vkCmdPushDescriptorSetKHR") : nullptr;
    assert(!device || cmd_push_descriptor_set);
}

} // namespace primal::graphics::vulkan::vkx
