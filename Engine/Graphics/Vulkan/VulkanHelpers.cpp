// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "VulkanHelpers.h"

#include "VulkanCore.h"
#include "VulkanResources.h"

namespace primal::graphics::vulkan::vkx
{
namespace
{

PFN_vkCmdPushDescriptorSetKHR cmd_push_descriptor_set{nullptr};

}

//// BARRIERS /////////////////////////////////////////////////////////////////////////////////////
auto VulkanBarrier::add(
    VkImage image,
    VkImageAspectFlags aspect,
    const ResourceState &before,
    const ResourceState &after,
    u32 base_mip,
    u32 mip_count) -> void
{
    assert(image);
    assert(_image_count < max_barriers);
    if (_image_count >= max_barriers)
    {
        return;
    }

    VkImageMemoryBarrier2 &barrier{_image_barriers[_image_count++]};
    barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = before.stage;
    barrier.srcAccessMask = before.access;
    barrier.dstStageMask = after.stage;
    barrier.dstAccessMask = after.access;
    barrier.oldLayout = before.layout;
    barrier.newLayout = after.layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {aspect, base_mip, mip_count, 0, VK_REMAINING_ARRAY_LAYERS};
}

auto VulkanBarrier::add(VkBuffer buffer, const ResourceState &before, const ResourceState &after, u64 offset, u64 size)
    -> void
{
    assert(buffer);
    assert(_buffer_count < max_barriers);
    if (_buffer_count >= max_barriers)
    {
        return;
    }

    VkBufferMemoryBarrier2 &barrier{_buffer_barriers[_buffer_count++]};
    barrier = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
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

auto VulkanBarrier::apply(VkCommandBuffer cmd) -> void
{
    assert(_image_count || _buffer_count);
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = _image_count;
    dependency.pImageMemoryBarriers = &_image_barriers[0];
    dependency.bufferMemoryBarrierCount = _buffer_count;
    dependency.pBufferMemoryBarriers = &_buffer_barriers[0];
    ::vkCmdPipelineBarrier2(cmd, &dependency);
    _image_count = 0;
    _buffer_count = 0;
}

auto transition_image(
    VkCommandBuffer cmd,
    VkImage image,
    VkImageAspectFlags aspect,
    const ResourceState &before,
    const ResourceState &after,
    u32 base_mip,
    u32 mip_count) -> void
{
    VulkanBarrier barrier{};
    barrier.add(image, aspect, before, after, base_mip, mip_count);
    barrier.apply(cmd);
}

//// ROOT SIGNATURES //////////////////////////////////////////////////////////////////////////////
auto create_root_signature(const RootParameter *parameters, u32 count, [[maybe_unused]] const char *name)
    -> RootSignature
{
    assert(count <= max_root_parameters);
    assert(!count || parameters);
    const VkDevice device{core::device()};
    assert(device);

    VkDescriptorSetLayoutBinding bindings[max_root_parameters]{};
    VkSampler samplers[max_root_parameters]{}; // pImmutableSamplers must stay valid until the layout is created
    for (u32 i{0}; i < count; ++i)
    {
        const RootParameter &parameter{parameters[i]};
        assert(parameter.type != VK_DESCRIPTOR_TYPE_MAX_ENUM && parameter.binding != u32_invalid_id);
        VkDescriptorSetLayoutBinding &binding{bindings[i]};
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

    RootSignature signature{};

    VkDescriptorSetLayoutCreateInfo set_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    set_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    set_info.bindingCount = count;
    set_info.pBindings = &bindings[0];
    if (!VkCall(::vkCreateDescriptorSetLayout(device, &set_info, nullptr, &signature.root_set_layout)))
    {
        return {};
    }
    NAME_VK_OBJECT(signature.root_set_layout, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, name);

    const VkDescriptorSetLayout set_layouts[]{signature.root_set_layout, core::srv_heap().set_layout()};
    static_assert(root_descriptor_set == 0 && resource_heap_set == 1);
    assert(set_layouts[1]);

    VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout_info.setLayoutCount = static_cast<u32>(std::size(set_layouts));
    layout_info.pSetLayouts = &set_layouts[0];
    if (!VkCall(::vkCreatePipelineLayout(device, &layout_info, nullptr, &signature.layout)))
    {
        core::deferred_release(signature.root_set_layout);
        return {};
    }
    NAME_VK_OBJECT(signature.layout, VK_OBJECT_TYPE_PIPELINE_LAYOUT, name);

    return signature;
}

auto release(RootSignature &signature) -> void
{
    core::deferred_release(signature.layout);
    core::deferred_release(signature.root_set_layout);
}

auto RootDescriptors::add(VkDescriptorType type, u32 binding, const BufferRange &range) -> void
{
    assert(range.is_valid());
    assert(_count < max_root_parameters);
    if (_count >= max_root_parameters)
    {
        return;
    }

    VkDescriptorBufferInfo &buffer{_buffers[_count]};
    buffer.buffer = range.buffer;
    buffer.offset = range.offset;
    buffer.range = range.size;

    VkWriteDescriptorSet &write{_writes[_count]};
    write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstBinding = binding;
    write.descriptorCount = 1;
    write.descriptorType = type;
    write.pBufferInfo = &buffer;
    ++_count;
}

auto RootDescriptors::push(VkCommandBuffer cmd, VkPipelineBindPoint bind_point, const RootSignature &signature) -> void
{
    assert(cmd_push_descriptor_set && signature.is_valid());
    if (_count)
    {
        cmd_push_descriptor_set(cmd, bind_point, signature.layout, root_descriptor_set, _count, &_writes[0]);
    }
    _count = 0;
}

auto bind_resource_heap(VkCommandBuffer cmd, VkPipelineBindPoint bind_point, const RootSignature &signature) -> void
{
    assert(signature.is_valid());
    const VkDescriptorSet set{core::srv_heap().set()};
    assert(set);
    ::vkCmdBindDescriptorSets(cmd, bind_point, signature.layout, resource_heap_set, 1, &set, 0, nullptr);
}

//// PIPELINES //////////////////////////////////////////////////////////////////////////////////
namespace
{

// Shader modules are only needed while a pipeline is created.
auto create_shader_module(VkDevice device, const ShaderBytecode &shader) -> VkShaderModule
{
    assert(shader.is_valid() && (shader.size & 3) == 0);
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = shader.size;
    info.pCode = shader.code;
    VkShaderModule module{VK_NULL_HANDLE};
    VkCall(::vkCreateShaderModule(device, &info, nullptr, &module));
    return module;
}

}

auto create_graphics_pipeline(const GraphicsPipelineDesc &desc, [[maybe_unused]] const char *name) -> VkPipeline
{
    const VkDevice device{core::device()};
    assert(device && desc.layout && desc.vs.is_valid());
    assert(desc.render_target_count <= GraphicsPipelineDesc::max_render_targets);

    VkShaderModule modules[2]{};
    VkPipelineShaderStageCreateInfo stages[2]{};
    u32 stage_count{0};
    const auto add_stage = [&](const ShaderBytecode &shader, VkShaderStageFlagBits stage)
    {
        if (!shader.is_valid())
        {
            return;
        }
        modules[stage_count] = create_shader_module(device, shader);
        VkPipelineShaderStageCreateInfo &info{stages[stage_count]};
        info = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        info.stage = stage;
        info.module = modules[stage_count];
        info.pName = shader.entry_point;
        ++stage_count;
    };
    add_stage(desc.vs, VK_SHADER_STAGE_VERTEX_BIT);
    add_stage(desc.ps, VK_SHADER_STAGE_FRAGMENT_BIT);

    // Vertex data comes from structured buffers (vertex pulling), like the D3D12 shaders.
    const VkPipelineVertexInputStateCreateInfo vertex_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};

    VkPipelineInputAssemblyStateCreateInfo input_assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    input_assembly.topology = desc.topology;

    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = desc.samples;

    VkPipelineColorBlendAttachmentState blend_attachments[GraphicsPipelineDesc::max_render_targets]{};
    for (u32 i{0}; i < desc.render_target_count; ++i)
    {
        blend_attachments[i] = desc.blend;
    }
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = desc.render_target_count;
    blend.pAttachments = &blend_attachments[0];

    constexpr VkDynamicState dynamic_states[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = static_cast<u32>(std::size(dynamic_states));
    dynamic.pDynamicStates = &dynamic_states[0];

    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = desc.render_target_count;
    rendering.pColorAttachmentFormats = &desc.render_target_formats[0];
    rendering.depthAttachmentFormat = desc.depth_format;

    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.pNext = &rendering;
    info.stageCount = stage_count;
    info.pStages = &stages[0];
    info.pVertexInputState = &vertex_input;
    info.pInputAssemblyState = &input_assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &desc.rasterizer;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &desc.depth_stencil;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = desc.layout;

    VkPipeline pipeline{VK_NULL_HANDLE};
    // TODO: pipeline cache.
    VkCall(::vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline));
    for (u32 i{0}; i < stage_count; ++i)
    {
        ::vkDestroyShaderModule(device, modules[i], nullptr);
    }
    NAME_VK_OBJECT(pipeline, VK_OBJECT_TYPE_PIPELINE, name);
    return pipeline;
}

auto create_compute_pipeline(VkPipelineLayout layout, const ShaderBytecode &cs, [[maybe_unused]] const char *name)
    -> VkPipeline
{
    const VkDevice device{core::device()};
    assert(device && layout && cs.is_valid());

    VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    info.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    info.stage.module = create_shader_module(device, cs);
    info.stage.pName = cs.entry_point;
    info.layout = layout;

    VkPipeline pipeline{VK_NULL_HANDLE};
    VkCall(::vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline));
    ::vkDestroyShaderModule(device, info.stage.module, nullptr);
    NAME_VK_OBJECT(pipeline, VK_OBJECT_TYPE_PIPELINE, name);
    return pipeline;
}

auto set_viewport(VkCommandBuffer cmd, u32 width, u32 height) -> void
{
    const VkViewport viewport{
        0.f, static_cast<f32>(height), static_cast<f32>(width), -static_cast<f32>(height), 0.f, 1.f};
    const VkRect2D scissor{{0, 0}, {width, height}};
    ::vkCmdSetViewport(cmd, 0, 1, &viewport);
    ::vkCmdSetScissor(cmd, 0, 1, &scissor);
}

//// FORMATS ////////////////////////////////////////////////////////////////////////////////////
auto dxgi_format_info(u32 dxgi_format) -> FormatInfo
{
    // DXGI_FORMAT values (dxgiformat.h). Only formats that textures use, or are likely to use.
    switch (dxgi_format)
    {
        case 2: return {VK_FORMAT_R32G32B32A32_SFLOAT, 1, 1, 16};      // R32G32B32A32_FLOAT (IBL cube maps)
        case 6: return {VK_FORMAT_R32G32B32_SFLOAT, 1, 1, 12};         // R32G32B32_FLOAT
        case 10: return {VK_FORMAT_R16G16B16A16_SFLOAT, 1, 1, 8};      // R16G16B16A16_FLOAT
        case 11: return {VK_FORMAT_R16G16B16A16_UNORM, 1, 1, 8};       // R16G16B16A16_UNORM
        case 16: return {VK_FORMAT_R32G32_SFLOAT, 1, 1, 8};            // R32G32_FLOAT
        case 24: return {VK_FORMAT_A2B10G10R10_UNORM_PACK32, 1, 1, 4}; // R10G10B10A2_UNORM (R in the low bits)
        case 26: return {VK_FORMAT_B10G11R11_UFLOAT_PACK32, 1, 1, 4};  // R11G11B10_FLOAT (R in the low bits)
        case 28: return {VK_FORMAT_R8G8B8A8_UNORM, 1, 1, 4};           // R8G8B8A8_UNORM
        case 29: return {VK_FORMAT_R8G8B8A8_SRGB, 1, 1, 4};            // R8G8B8A8_UNORM_SRGB
        case 34: return {VK_FORMAT_R16G16_SFLOAT, 1, 1, 4};            // R16G16_FLOAT (BRDF LUT)
        case 35: return {VK_FORMAT_R16G16_UNORM, 1, 1, 4};             // R16G16_UNORM
        case 41: return {VK_FORMAT_R32_SFLOAT, 1, 1, 4};               // R32_FLOAT
        case 49: return {VK_FORMAT_R8G8_UNORM, 1, 1, 2};               // R8G8_UNORM
        case 54: return {VK_FORMAT_R16_SFLOAT, 1, 1, 2};               // R16_FLOAT
        case 56: return {VK_FORMAT_R16_UNORM, 1, 1, 2};                // R16_UNORM
        case 61: return {VK_FORMAT_R8_UNORM, 1, 1, 1};                 // R8_UNORM
        case 71: return {VK_FORMAT_BC1_RGBA_UNORM_BLOCK, 4, 4, 8};     // BC1_UNORM
        case 72: return {VK_FORMAT_BC1_RGBA_SRGB_BLOCK, 4, 4, 8};      // BC1_UNORM_SRGB
        case 74: return {VK_FORMAT_BC2_UNORM_BLOCK, 4, 4, 16};         // BC2_UNORM
        case 75: return {VK_FORMAT_BC2_SRGB_BLOCK, 4, 4, 16};          // BC2_UNORM_SRGB
        case 77: return {VK_FORMAT_BC3_UNORM_BLOCK, 4, 4, 16};         // BC3_UNORM
        case 78: return {VK_FORMAT_BC3_SRGB_BLOCK, 4, 4, 16};          // BC3_UNORM_SRGB
        case 80: return {VK_FORMAT_BC4_UNORM_BLOCK, 4, 4, 8};          // BC4_UNORM
        case 81: return {VK_FORMAT_BC4_SNORM_BLOCK, 4, 4, 8};          // BC4_SNORM
        case 83: return {VK_FORMAT_BC5_UNORM_BLOCK, 4, 4, 16};         // BC5_UNORM
        case 84: return {VK_FORMAT_BC5_SNORM_BLOCK, 4, 4, 16};         // BC5_SNORM
        case 87: return {VK_FORMAT_B8G8R8A8_UNORM, 1, 1, 4};           // B8G8R8A8_UNORM
        case 91: return {VK_FORMAT_B8G8R8A8_SRGB, 1, 1, 4};            // B8G8R8A8_UNORM_SRGB
        case 95: return {VK_FORMAT_BC6H_UFLOAT_BLOCK, 4, 4, 16};       // BC6H_UF16
        case 96: return {VK_FORMAT_BC6H_SFLOAT_BLOCK, 4, 4, 16};       // BC6H_SF16
        case 98: return {VK_FORMAT_BC7_UNORM_BLOCK, 4, 4, 16};         // BC7_UNORM
        case 99: return {VK_FORMAT_BC7_SRGB_BLOCK, 4, 4, 16};          // BC7_UNORM_SRGB
        default: return {};
    }
}

auto load_device_functions(VkDevice device) -> void
{
    cmd_push_descriptor_set = device ? reinterpret_cast<PFN_vkCmdPushDescriptorSetKHR>(
                                           ::vkGetDeviceProcAddr(device, "vkCmdPushDescriptorSetKHR"))
                                     : nullptr;
    assert(!device || cmd_push_descriptor_set);
}

}
