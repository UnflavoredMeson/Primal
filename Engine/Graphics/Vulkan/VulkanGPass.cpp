// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "VulkanGPass.h"

#include "Components/Entity.h"
#include "Components/Transform.h"
#include "Graphics/Shaders/SharedTypes.h"
#include "VulkanCamera.h"
#include "VulkanContent.h"
#include "VulkanCore.h"
#include "VulkanLight.h"
#include "VulkanLightCulling.h"
#include "VulkanResources.h"

namespace primal::graphics::vulkan::gpass
{
namespace
{

constexpr math::u32v2 initial_dimensions{100, 100};

VulkanRenderTexture gpass_main_buffer{};
VulkanDepthBuffer gpass_depth_buffer{};
math::u32v2 dimensions{initial_dimensions};

#if _DEBUG
constexpr VkClearColorValue clear_value{{0.5f, 0.5f, 0.5f, 1.f}};
#else
constexpr VkClearColorValue clear_value{};
#endif

// Registers of the opaque material root parameters (d3d12::gpass::opaque_root_parameter, see TestShader.hlsl):
// b0 GlobalData, b1 PerObjectBuffer, t0 VertexPositions, t1 Elements, t2 MaterialData, t3 DirectionalLights,
// t4 CullableLights, t5 LightGrid, t6 LightIndexList. The root signatures are created by content::material.
struct OpaqueRegister
{
    enum : u32
    {
        global_shader_data = 0, // b0
        per_object_data = 1,    // b1
        position_buffer = 0,    // t0
        element_buffer = 1,     // t1
        material_data = 2,      // t2
        directional_lights = 3, // t3
        cullable_lights = 4,    // t4
        light_grid = 5,         // t5
        light_index_list = 6,   // t6
    };
};

// Per-frame render item data (counterpart of d3d12's gpass_cache; separate arrays instead of one packed buffer).
struct GpassCache
{
    utl::vector<id::id_type> vulkan_render_item_ids;
    u32 descriptor_index_count{0};

    utl::vector<id::id_type> entity_ids;
    utl::vector<id::id_type> submesh_gpu_ids;
    utl::vector<id::id_type> material_ids;
    utl::vector<VkPipeline> gpass_pipelines;
    utl::vector<VkPipeline> depth_pipelines;
    utl::vector<vkx::RootSignature> root_signatures;
    utl::vector<MaterialType::Type> material_types;
    utl::vector<u32 *> descriptor_indices;
    utl::vector<u32> texture_counts;
    utl::vector<MaterialSurface *> material_surfaces;
    utl::vector<vkx::BufferRange> position_buffers;
    utl::vector<vkx::BufferRange> element_buffers;
    utl::vector<content::submesh::IndexBufferView> index_buffer_views;
    utl::vector<VkPrimitiveTopology> primitive_topologies;
    utl::vector<u32> elements_types;
    utl::vector<vkx::BufferRange> per_object_data;
    utl::vector<vkx::BufferRange> material_data;

    [[nodiscard]] auto items_cache() -> content::render_item::ItemsCache
    {
        return {
            entity_ids.data(),
            submesh_gpu_ids.data(),
            material_ids.data(),
            gpass_pipelines.data(),
            depth_pipelines.data()};
    }

    [[nodiscard]] auto views_cache() -> content::submesh::ViewsCache
    {
        return {
            position_buffers.data(),
            element_buffers.data(),
            index_buffer_views.data(),
            primitive_topologies.data(),
            elements_types.data()};
    }

    [[nodiscard]] auto materials_cache() -> content::material::MaterialsCache
    {
        return {
            root_signatures.data(),
            material_types.data(),
            descriptor_indices.data(),
            texture_counts.data(),
            material_surfaces.data()};
    }

    [[nodiscard]] auto size() const -> u32
    {
        return static_cast<u32>(vulkan_render_item_ids.size());
    }

    auto clear() -> void
    {
        vulkan_render_item_ids.clear();
        descriptor_index_count = 0;
    }

    auto resize() -> void
    {
        const u64 count{vulkan_render_item_ids.size()};
        entity_ids.resize(count);
        submesh_gpu_ids.resize(count);
        material_ids.resize(count);
        gpass_pipelines.resize(count);
        depth_pipelines.resize(count);
        root_signatures.resize(count);
        material_types.resize(count);
        descriptor_indices.resize(count);
        texture_counts.resize(count);
        material_surfaces.resize(count);
        position_buffers.resize(count);
        element_buffers.resize(count);
        index_buffer_views.resize(count);
        primitive_topologies.resize(count);
        elements_types.resize(count);
        per_object_data.resize(count);
        material_data.resize(count);
    }
} frame_cache;

auto create_buffers(math::u32v2 size) -> bool
{
    assert(size.x && size.y);
    gpass_main_buffer.release();
    gpass_depth_buffer.release();

    VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent = {size.x, size.y, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VulkanTextureInitInfo info{};
    info.image_info = &image_info;

    // Create the main buffer
    {
        image_info.format = main_buffer_format;
        gpass_main_buffer = VulkanRenderTexture{info};
    }

    // Create the depth buffer
    {
        image_info.format = depth_buffer_format;
        gpass_depth_buffer = VulkanDepthBuffer{info};
    }

    NAME_VK_OBJECT(gpass_main_buffer.image(), VK_OBJECT_TYPE_IMAGE, "GPass Main Buffer");
    NAME_VK_OBJECT(gpass_depth_buffer.image(), VK_OBJECT_TYPE_IMAGE, "GPass Depth Buffer");

    return gpass_main_buffer.image() && gpass_depth_buffer.image();
}

auto fill_per_object_data(const VulkanFrameInfo &vulkan_info) -> void
{
    GpassCache &cache{frame_cache};
    const u32 render_items_count{cache.size()};
    id::id_type current_entity_id{id::invalid_id};
    hlsl::PerObjectData *current_data_pointer{nullptr};

    ConstantBuffer &cbuffer{core::cbuffer()};

    for (u32 i{0}; i < render_items_count; ++i)
    {
        if (current_entity_id != cache.entity_ids[i])
        {
            current_entity_id = cache.entity_ids[i];
            hlsl::PerObjectData data{};
            transform::get_transform_matrices(game_entity::EntityId{current_entity_id}, data.World, data.InvWorld);
            const math::mat world{math::load(data.World)};
            const math::mat wvp{math::mul(world, vulkan_info.camera->view_projection())};
            math::store(data.WorldViewProjection, wvp);

            current_data_pointer = cbuffer.allocate<hlsl::PerObjectData>();
            std::memcpy(current_data_pointer, &data, sizeof(hlsl::PerObjectData));
        }

        assert(current_data_pointer);
        cache.per_object_data[i] = cbuffer.range(current_data_pointer);
    }
}

auto prepare_render_frame(const VulkanFrameInfo &vulkan_info) -> void
{
    assert(vulkan_info.info);

    GpassCache &cache{frame_cache};
    cache.clear();
    // No camera (e.g. the SURFACES test) or nothing to draw.
    if (!vulkan_info.camera || !vulkan_info.info->render_item_ids || !vulkan_info.info->render_item_count)
    {
        return;
    }

    using namespace content;
    render_item::get_vulkan_render_item_ids(*vulkan_info.info, cache.vulkan_render_item_ids);
    cache.resize();
    const u32 items_count{cache.size()};
    const render_item::ItemsCache items_cache{cache.items_cache()};
    render_item::get_items(cache.vulkan_render_item_ids.data(), items_count, items_cache);

    const submesh::ViewsCache views_cache{cache.views_cache()};
    submesh::get_views(items_cache.submesh_gpu_ids, items_count, views_cache);

    const material::MaterialsCache materials_cache{cache.materials_cache()};
    material::get_materials(items_cache.material_ids, items_count, materials_cache, cache.descriptor_index_count);

    // Material data: material_surface + texture descriptor indices per item (StructuredBuffer<uint> MaterialData).
    // Each item is bound as a root SRV, so it starts at a multiple of the storage buffer offset alignment
    // (D3D12 packs the items; root SRVs there only need 4-byte alignment).
    const u32 material_data_alignment{core::storage_buffer_offset_alignment()};
    u32 size{0};
    for (u32 i{0}; i < items_count; ++i)
    {
        size += static_cast<u32>(math::align_size_up(
            sizeof(MaterialSurface) + cache.texture_counts[i] * sizeof(u32), material_data_alignment));
    }

    ConstantBuffer &cbuffer{core::cbuffer()};
    u8 *const material_data{cbuffer.allocate(size)};
    assert(material_data);
    u32 mtl_data_offset{0};

    for (u32 i{0}; i < items_count; ++i)
    {
        const u32 texture_count{cache.texture_counts[i]};
        const u32 item_size{static_cast<u32>(sizeof(MaterialSurface) + texture_count * sizeof(u32))};
        u8 *const item_data{material_data + mtl_data_offset};
        cache.material_data[i] = cbuffer.range(item_data, item_size);

        std::memcpy(item_data, cache.material_surfaces[i], sizeof(MaterialSurface));
        if (texture_count)
        {
            std::memcpy(item_data + sizeof(MaterialSurface), cache.descriptor_indices[i], texture_count * sizeof(u32));
        }
        mtl_data_offset += static_cast<u32>(math::align_size_up(item_size, material_data_alignment));
    }

    assert(mtl_data_offset == size);

    fill_per_object_data(vulkan_info);
}

// Root CBVs/SRVs that change per render item (D3D12: set_root_parameters()).
auto add_per_item_root_parameters(vkx::RootDescriptors &root, u32 cache_index) -> void
{
    const GpassCache &cache{frame_cache};
    assert(cache_index < cache.size());

    switch (cache.material_types[cache_index])
    {
        case MaterialType::opaque:
        {
            using reg = OpaqueRegister;
            const vkx::BufferRange &elements{cache.element_buffers[cache_index]};
            root.srv(reg::position_buffer, cache.position_buffers[cache_index]);
            // Position-only meshes have no element buffer (their vertex shaders don't read t1). D3D12 binds address 0.
            root.srv(reg::element_buffer, elements.is_valid() ? elements : core::zero_buffer());
            root.cbv(reg::per_object_data, cache.per_object_data[cache_index]);
            root.srv(reg::material_data, cache.material_data[cache_index]);
        }
        break;
        default: break;
    }
}

auto draw_item(VkCommandBuffer cmd, u32 cache_index) -> void
{
    const content::submesh::IndexBufferView &ibv{frame_cache.index_buffer_views[cache_index]};
    const u32 index_count{ibv.size_in_bytes >> (ibv.index_type == VK_INDEX_TYPE_UINT16 ? 1 : 2)};
    ::vkCmdBindIndexBuffer(cmd, ibv.buffer, ibv.offset, ibv.index_type);
    ::vkCmdDrawIndexed(cmd, index_count, 1, 0, 0, 0);
}

}

auto initialize() -> bool
{
    return create_buffers(initial_dimensions);
}

auto shutdown() -> void
{
    gpass_main_buffer.release();
    gpass_depth_buffer.release();
    dimensions = initial_dimensions;
}

auto main_buffer() -> const VulkanRenderTexture &
{
    return gpass_main_buffer;
}

auto depth_buffer() -> const VulkanDepthBuffer &
{
    return gpass_depth_buffer;
}

auto set_size(math::u32v2 size) -> void
{
    math::u32v2 &d{dimensions};
    if (size.x > d.x || size.y > d.y)
    {
        d = {std::max(size.x, d.x), std::max(size.y, d.y)};
        create_buffers(d); // the old buffers are released frame_buffer_count frames later
    }
}

// Direct draws, like d3d12::gpass::depth_prepass() / render() (DRAW_INDIRECT 0): per render item, the root
// parameters are pushed (vkCmdPushDescriptorSetKHR) and the item is drawn with vkCmdDrawIndexed. The D3D12
// ExecuteIndirect path changes root descriptors per draw, which Vulkan can only do with
// VK_EXT_device_generated_commands; not ported.
auto depth_prepass(VkCommandBuffer cmd, const VulkanFrameInfo &vulkan_info) -> void
{
    prepare_render_frame(vulkan_info);

    VkRenderingAttachmentInfo depth_attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth_attachment.imageView = gpass_depth_buffer.dsv();
    depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth_attachment.clearValue.depthStencil = {0.f, 0}; // reversed depth, same as ClearDepthStencilView(0.f)

    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea = {{0, 0}, {vulkan_info.surface_width, vulkan_info.surface_height}};
    rendering.layerCount = 1;
    rendering.pDepthAttachment = &depth_attachment;
    ::vkCmdBeginRendering(cmd, &rendering);

    const GpassCache &cache{frame_cache};
    const u32 items_count{cache.size()};

    VkPipelineLayout current_layout{VK_NULL_HANDLE};
    VkPipeline current_pipeline{VK_NULL_HANDLE};

    for (u32 i{0}; i < items_count; ++i)
    {
        vkx::RootDescriptors root{};
        const vkx::RootSignature &signature{cache.root_signatures[i]};
        if (current_layout != signature.layout)
        {
            current_layout = signature.layout;
            root.cbv(OpaqueRegister::global_shader_data, vulkan_info.global_shader_data);
            vkx::bind_resource_heap(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, signature);
        }

        if (current_pipeline != cache.depth_pipelines[i])
        {
            current_pipeline = cache.depth_pipelines[i];
            ::vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, current_pipeline);
        }

        add_per_item_root_parameters(root, i);
        root.push(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, signature);
        draw_item(cmd, i);
    }

    ::vkCmdEndRendering(cmd);
}

auto render(VkCommandBuffer cmd, const VulkanFrameInfo &vulkan_info) -> void
{
    VkRenderingAttachmentInfo color_attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color_attachment.imageView = gpass_main_buffer.rtv(0);
    color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.clearValue.color = clear_value;

    // Depth test against the prepass, no writes (gpass pipelines use depth_state.reversed_readonly).
    VkRenderingAttachmentInfo depth_attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth_attachment.imageView = gpass_depth_buffer.dsv();
    depth_attachment.imageLayout = vkx::resource_states.depth_read.layout;
    depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_NONE;

    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea = {{0, 0}, {vulkan_info.surface_width, vulkan_info.surface_height}};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color_attachment;
    rendering.pDepthAttachment = &depth_attachment;
    ::vkCmdBeginRendering(cmd, &rendering);

    const GpassCache &cache{frame_cache};
    const u32 items_count{cache.size()};

    VkPipelineLayout current_layout{VK_NULL_HANDLE};
    VkPipeline current_pipeline{VK_NULL_HANDLE};
    const u32 frame_index{vulkan_info.frame_index};
    const id::id_type light_culling_id{vulkan_info.light_culling_id};

    for (u32 i{0}; i < items_count; ++i)
    {
        vkx::RootDescriptors root{};
        const vkx::RootSignature &signature{cache.root_signatures[i]};
        if (current_layout != signature.layout)
        {
            using reg = OpaqueRegister;
            current_layout = signature.layout;
            root.cbv(reg::global_shader_data, vulkan_info.global_shader_data);
            root.srv(reg::directional_lights, light::non_cullable_light_buffer(frame_index));
            root.srv(reg::cullable_lights, light::cullable_light_buffer(frame_index));
            root.srv(reg::light_grid, delight::light_grid_opaque(light_culling_id, frame_index));
            root.srv(reg::light_index_list, delight::light_index_list_opaque(light_culling_id, frame_index));
            vkx::bind_resource_heap(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, signature);
        }

        if (current_pipeline != cache.gpass_pipelines[i])
        {
            current_pipeline = cache.gpass_pipelines[i];
            ::vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, current_pipeline);
        }

        add_per_item_root_parameters(root, i);
        root.push(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, signature);
        draw_item(cmd, i);
    }

    ::vkCmdEndRendering(cmd);
}

auto add_transitions_for_depth_prepass(vkx::VulkanBarrier &barriers) -> void
{
    // Both buffers are completely overwritten every frame (cleared), so each frame starts from
    // VK_IMAGE_LAYOUT_UNDEFINED, which also covers the first frame after they're (re)created. The barrier
    // still has to wait for the previous frame's readers (post-process, light culling): a write after a read
    // only needs an execution dependency, hence no access mask.
    constexpr vkx::ResourceState depth_buffer_discard{
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_NONE,
        VK_IMAGE_LAYOUT_UNDEFINED};
    barriers.add(
        gpass_depth_buffer.image(), VK_IMAGE_ASPECT_DEPTH_BIT, depth_buffer_discard, vkx::resource_states.depth_write);
}

auto add_transitions_for_gpass(vkx::VulkanBarrier &barriers) -> void
{
    constexpr vkx::ResourceState main_buffer_discard{
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED};
    barriers.add(
        gpass_main_buffer.image(), VK_IMAGE_ASPECT_COLOR_BIT, main_buffer_discard, vkx::resource_states.render_target);
    barriers.add(
        gpass_depth_buffer.image(),
        VK_IMAGE_ASPECT_DEPTH_BIT,
        vkx::resource_states.depth_write,
        vkx::resource_states.depth_read);
}

auto add_transitions_for_post_process(vkx::VulkanBarrier &barriers) -> void
{
    barriers.add(
        gpass_main_buffer.image(),
        VK_IMAGE_ASPECT_COLOR_BIT,
        vkx::resource_states.render_target,
        vkx::resource_states.pixel_shader_resource);
    // The depth buffer stays in depth_read, which post-processing can sample (READ_ONLY_OPTIMAL).
}

}
