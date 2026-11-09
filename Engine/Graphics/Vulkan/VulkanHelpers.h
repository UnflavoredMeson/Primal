// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "VulkanCommonHeaders.h"

// Counterpart of d3d12::d3dx (D3D12Helpers.h): sampler states, barriers, and "root signatures"
// (pipeline layouts) with their root descriptors.
namespace primal::graphics::vulkan::vkx
{

//// SHADER BINDINGS //////////////////////////////////////////////////////////////////////////////
// HLSL register(xN, space0) -> descriptor set 0, binding N + shift. Must match the DXC arguments in
// EngineDLL/ShaderCompilation.cpp (-fvk-{b,t,u,s}-shift).
constexpr u32 cbv_binding_shift{0};
constexpr u32 srv_binding_shift{100};
constexpr u32 uav_binding_shift{200};
constexpr u32 sampler_binding_shift{300};

// Descriptor sets (DXC -fvk-bind-resource-heap 0 1). Set 0 holds the root parameters and is a push
// descriptor set; set 1 is the bindless resource heap (ResourceDescriptorHeap[]).
// NOTE: SamplerDescriptorHeap[] (-fvk-bind-sampler-heap 0 2) isn't used by any shader, so there's no set 2.
constexpr u32 root_descriptor_set{0};
constexpr u32 resource_heap_set{1};

// Push descriptors are core in Vulkan 1.4; on 1.3 devices they come from VK_KHR_push_descriptor.
// Every set-0 binding counts against maxPushDescriptors (spec minimum: 32).
constexpr u32 max_root_parameters{32};

//// SAMPLERS /////////////////////////////////////////////////////////////////////////////////////
// The D3D12 static samplers (d3dx::sampler_state). They're immutable samplers in Vulkan, created
// once by core (core::static_sampler()).
struct StaticSampler
{
    enum Type : u32
    {
        point,
        linear,
        anisotropic,

        count
    };
};

[[nodiscard]] constexpr auto sampler_info(StaticSampler::Type type) -> VkSamplerCreateInfo
{
    const bool point{type == StaticSampler::point};
    VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.magFilter = point ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    info.minFilter = info.magFilter;
    info.mipmapMode = point ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.mipLodBias = 0.f;
    info.anisotropyEnable = type == StaticSampler::anisotropic ? VK_TRUE : VK_FALSE;
    info.maxAnisotropy = type == StaticSampler::anisotropic ? 16.f : 1.f;
    info.compareEnable = VK_FALSE;
    info.compareOp = VK_COMPARE_OP_NEVER;
    info.minLod = 0.f;
    info.maxLod = VK_LOD_CLAMP_NONE; // D3D12_FLOAT32_MAX
    info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    info.unnormalizedCoordinates = VK_FALSE;
    return info;
}

//// BUFFER RANGE /////////////////////////////////////////////////////////////////////////////////
// What a D3D12_GPU_VIRTUAL_ADDRESS is used for in the D3D12 backend: a buffer (or a part of one)
// bound as a root CBV/SRV/UAV. Vulkan descriptors take the buffer, an offset and a size.
struct BufferRange
{
    VkBuffer buffer{VK_NULL_HANDLE};
    u64 offset{0};
    u64 size{VK_WHOLE_SIZE};

    [[nodiscard]] constexpr auto is_valid() const -> bool
    {
        return buffer != VK_NULL_HANDLE;
    }
};

//// BARRIERS /////////////////////////////////////////////////////////////////////////////////////
// The Vulkan counterpart of a D3D12 resource state: where (stage) and how (access) a resource is
// used, plus the image layout. Transitions go from one state to another (vulkan_barrier).
struct ResourceState
{
    VkPipelineStageFlags2 stage{VK_PIPELINE_STAGE_2_NONE};
    VkAccessFlags2 access{VK_ACCESS_2_NONE};
    VkImageLayout layout{VK_IMAGE_LAYOUT_UNDEFINED}; // ignored for buffers
};

// NOTE: shader reads use VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL (Vulkan 1.3), which is valid for color and
//       depth images alike, so descriptors in the resource heap don't depend on the image's aspect.
constexpr struct
{
    // Previous contents aren't needed (the D3D12 equivalent is a discard or a full overwrite).
    const ResourceState undefined{VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED};
    const ResourceState render_target{
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    const ResourceState depth_write{
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL};
    // Depth test without writes, and sampling in the same pass (D3D12_RESOURCE_STATE_DEPTH_READ | *_SHADER_RESOURCE).
    // READ_ONLY_OPTIMAL (not DEPTH_READ_ONLY_OPTIMAL): it's valid for a read-only depth attachment too, and it's the
    // layout the depth buffer's SRV descriptor is written with.
    const ResourceState depth_read{
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL};
    const ResourceState pixel_shader_resource{
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_SHADER_READ_BIT,
        VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL};
    const ResourceState non_pixel_shader_resource{
        VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_READ_BIT,
        VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL};
    const ResourceState all_shader_resource{
        VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_READ_BIT,
        VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL};
    const ResourceState unordered_access{
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_IMAGE_LAYOUT_GENERAL};
    const ResourceState indirect_argument{
        VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
        VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED};
    const ResourceState copy_source{
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
        VK_ACCESS_2_TRANSFER_READ_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
    const ResourceState copy_dest{
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
        VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL};
    // Swapchain images. The present waits on the frame's render finished semaphore, which the submit
    // signals after all commands, so nothing needs to be made available to a later stage.
    const ResourceState present{VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
} resource_states;

// Collects image/buffer barriers and records them with one vkCmdPipelineBarrier2
// (counterpart of d3dx::d3d12_resource_barrier).
class VulkanBarrier
{
  public:
    constexpr static u32 max_barriers{64};

    // Transitions an image (or some of its mips) from one state to another.
    auto add(
        VkImage image,
        VkImageAspectFlags aspect,
        const ResourceState &before,
        const ResourceState &after,
        u32 base_mip = 0,
        u32 mip_count = VK_REMAINING_MIP_LEVELS) -> void;

    // Makes writes to a buffer visible to a later use (D3D12 UAV barrier / state transition).
    auto add(
        VkBuffer buffer,
        const ResourceState &before,
        const ResourceState &after,
        u64 offset = 0,
        u64 size = VK_WHOLE_SIZE) -> void;

    auto apply(VkCommandBuffer cmd) -> void;

  private:
    VkImageMemoryBarrier2 _image_barriers[max_barriers]{};
    VkBufferMemoryBarrier2 _buffer_barriers[max_barriers]{};
    u32 _image_count{0};
    u32 _buffer_count{0};
};

auto transition_image(
    VkCommandBuffer cmd,
    VkImage image,
    VkImageAspectFlags aspect,
    const ResourceState &before,
    const ResourceState &after,
    u32 base_mip = 0,
    u32 mip_count = VK_REMAINING_MIP_LEVELS) -> void;

//// ROOT SIGNATURES //////////////////////////////////////////////////////////////////////////////
// A D3D12 root parameter as a set-0 binding. Root CBVs/SRVs/UAVs become push descriptors (set per
// draw/dispatch with root_descriptors, like SetGraphicsRoot*View), and static samplers become
// immutable samplers.
// NOTE: root constants don't exist here. The shaders declare them as ConstantBuffer<T> (b1), which
//       DXC compiles to a uniform buffer, so they're written to the frame's constant buffer and bound
//       as a root CBV. (Push constants would need [[vk::push_constant]] in the shaders.)
struct RootParameter
{
    VkDescriptorType type{VK_DESCRIPTOR_TYPE_MAX_ENUM};
    u32 binding{u32_invalid_id};
    VkShaderStageFlags stages{VK_SHADER_STAGE_ALL};
    StaticSampler::Type sampler_type{StaticSampler::count}; // VK_DESCRIPTOR_TYPE_SAMPLER only

    // ConstantBuffer<T> : register(bN)
    [[nodiscard]] constexpr static auto cbv(u32 shader_register, VkShaderStageFlags visibility = VK_SHADER_STAGE_ALL)
        -> RootParameter
    {
        return {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, cbv_binding_shift + shader_register, visibility};
    }

    // StructuredBuffer<T> : register(tN). (Texture SRVs come from the resource heap, not root parameters.)
    [[nodiscard]] constexpr static auto srv(u32 shader_register, VkShaderStageFlags visibility = VK_SHADER_STAGE_ALL)
        -> RootParameter
    {
        return {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, srv_binding_shift + shader_register, visibility};
    }

    // RWStructuredBuffer<T> : register(uN)
    [[nodiscard]] constexpr static auto uav(u32 shader_register, VkShaderStageFlags visibility = VK_SHADER_STAGE_ALL)
        -> RootParameter
    {
        return {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, uav_binding_shift + shader_register, visibility};
    }

    // SamplerState : register(sN), a D3D12 static sampler.
    [[nodiscard]] constexpr static auto sampler(
        StaticSampler::Type sampler_kind,
        u32 shader_register,
        VkShaderStageFlags visibility = VK_SHADER_STAGE_ALL) -> RootParameter
    {
        return {VK_DESCRIPTOR_TYPE_SAMPLER, sampler_binding_shift + shader_register, visibility, sampler_kind};
    }
};

// Counterpart of ID3D12RootSignature: a pipeline layout with the root parameters in set 0 (push
// descriptors) and the resource heap in set 1.
struct RootSignature
{
    VkPipelineLayout layout{VK_NULL_HANDLE};
    VkDescriptorSetLayout root_set_layout{VK_NULL_HANDLE};

    [[nodiscard]] constexpr auto is_valid() const -> bool
    {
        return layout != VK_NULL_HANDLE;
    }
};

[[nodiscard]] auto create_root_signature(const RootParameter *parameters, u32 count, const char *name) -> RootSignature;
// Deferred release of the pipeline layout and its set-0 layout.
auto release(RootSignature &signature) -> void;

// Records the root CBVs/SRVs/UAVs for the next draws/dispatches with one vkCmdPushDescriptorSetKHR
// (counterpart of SetGraphicsRoot*View / SetComputeRoot*View). Bindings that aren't set keep the
// values pushed before for the same pipeline layout.
class RootDescriptors
{
  public:
    auto cbv(u32 shader_register, const BufferRange &range) -> void
    {
        add(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, cbv_binding_shift + shader_register, range);
    }
    auto srv(u32 shader_register, const BufferRange &range) -> void
    {
        add(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, srv_binding_shift + shader_register, range);
    }
    auto uav(u32 shader_register, const BufferRange &range) -> void
    {
        add(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, uav_binding_shift + shader_register, range);
    }

    auto push(VkCommandBuffer cmd, VkPipelineBindPoint bind_point, const RootSignature &signature) -> void;

  private:
    auto add(VkDescriptorType type, u32 binding, const BufferRange &range) -> void;

    VkWriteDescriptorSet _writes[max_root_parameters]{};
    VkDescriptorBufferInfo _buffers[max_root_parameters]{};
    u32 _count{0};
};

// Binds the resource heap (set 1) for a pipeline layout (counterpart of SetDescriptorHeaps). Needed
// once per command buffer and bind point, as long as the next layouts are compatible with set 1.
auto bind_resource_heap(VkCommandBuffer cmd, VkPipelineBindPoint bind_point, const RootSignature &signature) -> void;

// Loads the device-level functions the helpers use (vkCmdPushDescriptorSetKHR). Called by core.
auto load_device_functions(VkDevice device) -> void;

//// PIPELINES //////////////////////////////////////////////////////////////////////////////////
// Entry point of every SPIR-V module: ShaderCompilation.cpp compiles with -fspv-entrypoint-name=main,
// because compiled shaders (content::compiled_shader) don't store the HLSL function name.
constexpr const char *spirv_entry_point{"main"};

// SPIR-V code of one shader stage (counterpart of D3D12_SHADER_BYTECODE, plus the entry point name).
struct ShaderBytecode
{
    const u32 *code{nullptr};
    u64 size{0}; // in bytes
    const char *entry_point{spirv_entry_point};

    [[nodiscard]] constexpr auto is_valid() const -> bool
    {
        return code && size;
    }
};

// Rasterizer, depth and blend presets with the same names and values as d3dx::rasterizer_state,
// depth_state and blend_state.
// NOTE: front faces are counter-clockwise like in the D3D12 presets (FrontCounterClockwise = 1).
//       That holds because every pass uses a negative viewport height (vkx::set_viewport()), which
//       makes clip space y point up and the framebuffer winding match D3D12's.
[[nodiscard]] constexpr auto rasterizer_desc(VkPolygonMode fill_mode, VkCullModeFlags cull_mode)
    -> VkPipelineRasterizationStateCreateInfo
{
    VkPipelineRasterizationStateCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    info.depthClampEnable = VK_FALSE; // DepthClipEnable = 1
    info.rasterizerDiscardEnable = VK_FALSE;
    info.polygonMode = fill_mode;
    info.cullMode = cull_mode;
    info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    info.depthBiasEnable = VK_FALSE;
    info.lineWidth = 1.f;
    return info;
}

constexpr struct
{
    const VkPipelineRasterizationStateCreateInfo no_cull{rasterizer_desc(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE)};
    const VkPipelineRasterizationStateCreateInfo backface_cull{
        rasterizer_desc(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT)};
    const VkPipelineRasterizationStateCreateInfo frontface_cull{
        rasterizer_desc(VK_POLYGON_MODE_FILL, VK_CULL_MODE_FRONT_BIT)};
    // Needs the fillModeNonSolid device feature (not required yet).
    const VkPipelineRasterizationStateCreateInfo wireframe{rasterizer_desc(VK_POLYGON_MODE_LINE, VK_CULL_MODE_NONE)};
} rasterizer_state;

[[nodiscard]] constexpr auto depth_desc(bool enable, bool write, VkCompareOp compare)
    -> VkPipelineDepthStencilStateCreateInfo
{
    VkPipelineDepthStencilStateCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    info.depthTestEnable = enable ? VK_TRUE : VK_FALSE;
    info.depthWriteEnable = write ? VK_TRUE : VK_FALSE;
    info.depthCompareOp = compare;
    info.depthBoundsTestEnable = VK_FALSE;
    info.stencilTestEnable = VK_FALSE;
    info.minDepthBounds = 0.f;
    info.maxDepthBounds = 1.f;
    return info;
}

constexpr struct
{
    const VkPipelineDepthStencilStateCreateInfo disabled{depth_desc(false, false, VK_COMPARE_OP_LESS_OR_EQUAL)};
    const VkPipelineDepthStencilStateCreateInfo enabled{depth_desc(true, true, VK_COMPARE_OP_LESS_OR_EQUAL)};
    const VkPipelineDepthStencilStateCreateInfo enabled_readonly{depth_desc(true, false, VK_COMPARE_OP_LESS_OR_EQUAL)};
    const VkPipelineDepthStencilStateCreateInfo reversed{depth_desc(true, true, VK_COMPARE_OP_GREATER_OR_EQUAL)};
    const VkPipelineDepthStencilStateCreateInfo reversed_readonly{
        depth_desc(true, false, VK_COMPARE_OP_GREATER_OR_EQUAL)};
} depth_state;

[[nodiscard]] constexpr auto blend_desc(bool enable, VkBlendFactor src, VkBlendFactor dst)
    -> VkPipelineColorBlendAttachmentState
{
    VkPipelineColorBlendAttachmentState state{};
    state.blendEnable = enable ? VK_TRUE : VK_FALSE;
    state.srcColorBlendFactor = src;
    state.dstColorBlendFactor = dst;
    state.colorBlendOp = VK_BLEND_OP_ADD;
    state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    state.alphaBlendOp = VK_BLEND_OP_ADD;
    state.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    return state;
}

// One state for all color attachments (D3D12's IndependentBlendEnable = 0).
constexpr struct
{
    const VkPipelineColorBlendAttachmentState disabled{
        blend_desc(false, VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA)};
    const VkPipelineColorBlendAttachmentState alpha_blend{
        blend_desc(true, VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA)};
    const VkPipelineColorBlendAttachmentState additive{blend_desc(true, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE)};
    const VkPipelineColorBlendAttachmentState premultiplied{
        blend_desc(true, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA)};
} blend_state;

// Counterpart of the D3D12 pipeline state stream for graphics PSOs. Viewport and scissor are dynamic
// state (RSSetViewports / RSSetScissorRects); render target formats are given up front (dynamic rendering).
struct GraphicsPipelineDesc
{
    constexpr static u32 max_render_targets{8};

    VkPipelineLayout layout{VK_NULL_HANDLE};
    ShaderBytecode vs{};
    ShaderBytecode ps{};
    VkPrimitiveTopology topology{VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkPipelineRasterizationStateCreateInfo rasterizer{rasterizer_state.no_cull};
    VkPipelineDepthStencilStateCreateInfo depth_stencil{depth_state.disabled};
    VkPipelineColorBlendAttachmentState blend{blend_state.disabled};
    VkFormat render_target_formats[max_render_targets]{};
    u32 render_target_count{0};
    VkFormat depth_format{VK_FORMAT_UNDEFINED};
    VkSampleCountFlagBits samples{VK_SAMPLE_COUNT_1_BIT};
};

[[nodiscard]] auto create_graphics_pipeline(const GraphicsPipelineDesc &desc, const char *name) -> VkPipeline;
[[nodiscard]] auto create_compute_pipeline(VkPipelineLayout layout, const ShaderBytecode &cs, const char *name)
    -> VkPipeline;

// D3D12-style viewport: (0, 0) is the top-left corner and clip space y points up. Done with a
// negative viewport height (core since Vulkan 1.1), so the HLSL shaders and the D3D12 projection
// matrices work unchanged. Also sets the scissor to the same rectangle.
auto set_viewport(VkCommandBuffer cmd, u32 width, u32 height) -> void;

//// MISC ///////////////////////////////////////////////////////////////////////////////////////
// A texture format: the Vulkan format for a DXGI_FORMAT value (texture assets store DXGI formats) and its
// texel block size, needed to describe buffer -> image copies.
struct FormatInfo
{
    VkFormat format{VK_FORMAT_UNDEFINED};
    u32 block_width{1}; // texels per block (4 for BC formats)
    u32 block_height{1};
    u32 block_size{0}; // bytes per block (per texel for uncompressed formats)

    [[nodiscard]] constexpr auto is_valid() const -> bool
    {
        return format != VK_FORMAT_UNDEFINED;
    }
};

// VK_FORMAT_UNDEFINED for DXGI formats that aren't in the table yet.
[[nodiscard]] auto dxgi_format_info(u32 dxgi_format) -> FormatInfo;

////////////////////////////////////////////////////////////////////////////////////
[[nodiscard]] constexpr auto aspect_flags(VkFormat format) -> VkImageAspectFlags
{
    switch (format)
    {
        case VK_FORMAT_D16_UNORM:
        case VK_FORMAT_X8_D24_UNORM_PACK32:
        case VK_FORMAT_D32_SFLOAT: return VK_IMAGE_ASPECT_DEPTH_BIT;
        case VK_FORMAT_D16_UNORM_S8_UINT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT: return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        case VK_FORMAT_S8_UINT: return VK_IMAGE_ASPECT_STENCIL_BIT;
        default: return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}

}
