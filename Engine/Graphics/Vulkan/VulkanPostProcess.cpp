// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "VulkanPostProcess.h"
#include "VulkanCore.h"
#include "VulkanGPass.h"
#include "VulkanHelpers.h"
#include "VulkanLightCulling.h"
#include "VulkanResources.h"
#include "VulkanShaders.h"

namespace primal::graphics::vulkan::fx
{
namespace
{

// D3D12's root constants (b1, ShaderConstants in PostProcess.hlsl). Written to the frame's constant
// buffer and bound as a root CBV (see vkx::root_parameter).
struct ShaderConstants
{
    u32 gpass_main_buffer_index;
    u32 gpass_depth_buffer_index;
};

vkx::RootSignature fx_root_sig{};

// D3D12 creates one PSO for its fixed back buffer format. Here the swapchain format depends on the
// window system, so there's one pipeline per format (normally just one).
struct FormatPipeline
{
    VkFormat format{VK_FORMAT_UNDEFINED};
    VkPipeline pipeline{VK_NULL_HANDLE};
};
constexpr u32 max_pipelines{4};
FormatPipeline fx_pipelines[max_pipelines]{};

auto create_fx_root_signature() -> bool
{
    assert(!fx_root_sig.is_valid());
    using param = vkx::RootParameter;
    constexpr VkShaderStageFlags pixel{VK_SHADER_STAGE_FRAGMENT_BIT};
    const param parameters[]{
        param::cbv(0, pixel), // GlobalData
        param::cbv(1, pixel), // ShaderParams (D3D12 root constants)
        // TODO: temporary for visualizing light culling (Frustums, LightGridOpaque). Remove later.
        param::srv(0, pixel),
        param::srv(1, pixel),
        param::sampler(vkx::StaticSampler::point, 0, pixel),
        param::sampler(vkx::StaticSampler::linear, 1, pixel),
    };
    fx_root_sig =
        vkx::create_root_signature(&parameters[0], (u32)std::size(parameters), "Post-process FX Root Signature");
    return fx_root_sig.is_valid();
}

auto get_fx_pipeline(VkFormat format) -> VkPipeline
{
    for (const FormatPipeline &entry : fx_pipelines)
    {
        if (entry.format == format)
            return entry.pipeline;
    }

    for (FormatPipeline &entry : fx_pipelines)
    {
        if (entry.format != VK_FORMAT_UNDEFINED)
            continue;

        vkx::GraphicsPipelineDesc desc{};
        desc.layout = fx_root_sig.layout;
        desc.vs = shaders::get_engine_shader(shaders::EngineShader::fullscreen_triangle_vs);
        desc.ps = shaders::get_engine_shader(shaders::EngineShader::post_process_ps);
        desc.rasterizer = vkx::rasterizer_state.no_cull;
        desc.render_target_formats[0] = format;
        desc.render_target_count = 1;

        entry.pipeline = vkx::create_graphics_pipeline(desc, "Post-process FX Pipeline");
        if (entry.pipeline)
            entry.format = format;
        return entry.pipeline;
    }

    assert(false && "Vulkan: too many back buffer formats for post-processing.");
    return VK_NULL_HANDLE;
}

}

auto initialize() -> bool
{
    return create_fx_root_signature();
}

auto shutdown() -> void
{
    for (FormatPipeline &entry : fx_pipelines)
    {
        core::deferred_release(entry.pipeline);
        entry.format = VK_FORMAT_UNDEFINED;
    }
    vkx::release(fx_root_sig);
}

auto post_process(
    VkCommandBuffer cmd,
    const VulkanFrameInfo &vulkan_info,
    VkImageView target_rtv,
    VkFormat target_format) -> void
{
    const VkPipeline pipeline{get_fx_pipeline(target_format)};
    if (!pipeline)
        return;

    ConstantBuffer &cbuffer{core::cbuffer()};
    ShaderConstants *const constants{cbuffer.allocate<ShaderConstants>()};
    assert(constants);
    if (!constants)
        return;
    constants->gpass_main_buffer_index = gpass::main_buffer().srv().index;
    constants->gpass_depth_buffer_index = gpass::depth_buffer().srv().index;

    vkx::RootDescriptors root{};
    root.cbv(0, vulkan_info.global_shader_data);
    root.cbv(1, cbuffer.range(constants));
    // TODO: temporary for visualizing light culling (Frustums, LightGridOpaque). Remove later (same as D3D12).
    root.srv(0, delight::frustums(vulkan_info.light_culling_id, vulkan_info.frame_index));
    root.srv(1, delight::light_grid_opaque(vulkan_info.light_culling_id, vulkan_info.frame_index));
    root.push(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, fx_root_sig);
    vkx::bind_resource_heap(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, fx_root_sig);
    ::vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    // NOTE: we don't need to clear the render target, because each pixel will
    //       be overwritten by pixels from gpass main buffer.
    //       We also don't need a depth buffer.
    VkRenderingAttachmentInfo color_attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color_attachment.imageView = target_rtv;
    color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea = {{0, 0}, {vulkan_info.surface_width, vulkan_info.surface_height}};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color_attachment;
    ::vkCmdBeginRendering(cmd, &rendering);
    vkx::set_viewport(cmd, vulkan_info.surface_width, vulkan_info.surface_height);
    ::vkCmdDraw(cmd, 3, 1, 0, 0);
    ::vkCmdEndRendering(cmd);
}

}
