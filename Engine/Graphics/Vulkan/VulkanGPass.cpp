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

import primal.jobs;

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
    // fill_per_object_data(): one entry per entity (not per render item).
    utl::vector<id::id_type> object_entity_ids;
    utl::vector<u32> item_objects; // per render item: its entity's index in object_entity_ids
    // record_pass(): the secondary command buffers of the pass being recorded, in draw order.
    utl::vector<VkCommandBuffer> secondaries;

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
};

// One cache per surface (indexed by the surface id), so that surfaces could later be prepared and recorded at the
// same time (issue #3). Grown by prepare_render_frame() on the rendering thread.
std::vector<GpassCache> frame_caches;

auto surface_cache(SurfaceId surface_id) -> GpassCache &
{
    const u32 index{id::index(surface_id)};
    if (index >= frame_caches.size())
    {
        frame_caches.resize(index + 1);
    }
    return frame_caches[index];
}

auto surface_cache(const VulkanFrameInfo &vulkan_info) -> GpassCache &
{
    return surface_cache(SurfaceId{vulkan_info.surface_id});
}

// Material data per item: material_surface + texture descriptor indices (StructuredBuffer<uint> MaterialData). Each
// item is bound as a root SRV, so it starts at a multiple of the storage buffer offset alignment (D3D12 packs the
// items; root SRVs there only need 4-byte alignment).
auto material_data_size(const GpassCache &cache) -> u32
{
    const u32 material_data_alignment{core::storage_buffer_offset_alignment()};
    u32 size{0};
    for (u32 i{0}; i < cache.size(); ++i)
    {
        size += static_cast<u32>(math::align_size_up(
            sizeof(MaterialSurface) + cache.texture_counts[i] * sizeof(u32), material_data_alignment));
    }
    return size;
}

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

// Per-object data (world matrices) once per entity: the render items of an entity are next to each other. The
// entities are listed first; their data is computed in parallel (job system) straight into the constant buffer.
auto fill_per_object_data(const VulkanFrameInfo &vulkan_info) -> void
{
    GpassCache &cache{surface_cache(vulkan_info)};
    const u32 render_items_count{cache.size()};

    cache.object_entity_ids.clear();
    cache.item_objects.resize(render_items_count);
    for (u32 i{0}; i < render_items_count; ++i)
    {
        if (cache.object_entity_ids.empty() || cache.object_entity_ids.back() != cache.entity_ids[i])
        {
            cache.object_entity_ids.push_back(cache.entity_ids[i]);
        }
        cache.item_objects[i] = static_cast<u32>(cache.object_entity_ids.size() - 1);
    }

    const u32 object_count{static_cast<u32>(cache.object_entity_ids.size())};
    constexpr u32 stride{static_cast<u32>(math::align_size_up<ConstantBuffer::alignment>(sizeof(hlsl::PerObjectData)))};
    ConstantBuffer &cbuffer{core::cbuffer()};
    u8 *const objects{cbuffer.allocate(object_count * stride)};
    assert(objects);

    // About 22 ns per object: 1,500 objects take 34 us on one thread, and waking
    // the workers costs more than that (37-48 us with batches of 128 to 1024). Up to 1024 objects run on the calling
    // thread.
    constexpr u32 batch_size{1024};
    jobs::parallel_for(
        object_count,
        batch_size,
        [&cache, &vulkan_info, objects](u32 begin, u32 end)
        {
            const math::mat vp{vulkan_info.camera->view_projection()};
            for (u32 i{begin}; i < end; ++i)
            {
                hlsl::PerObjectData data{};
                transform::get_transform_matrices(
                    game_entity::EntityId{cache.object_entity_ids[i]}, data.World, data.InvWorld);
                const math::mat world{math::load(data.World)};
                math::store(data.WorldViewProjection, math::mul(world, vp));
                std::memcpy(objects + static_cast<size_t>(i) * stride, &data, sizeof(hlsl::PerObjectData));
            }
        });

    for (u32 i{0}; i < render_items_count; ++i)
    {
        u8 *const object{objects + static_cast<size_t>(cache.item_objects[i]) * stride};
        cache.per_object_data[i] = cbuffer.range(object, sizeof(hlsl::PerObjectData));
    }
}

// The allocating half of the frame's preparation; collect_render_items() has filled the cache.
auto prepare_render_frame(const VulkanFrameInfo &vulkan_info) -> void
{
    assert(vulkan_info.info);

    GpassCache &cache{surface_cache(vulkan_info)};
    const u32 items_count{cache.size()};
    // No camera (e.g. the SURFACES test) or nothing to draw: collect_render_items() left the cache empty.
    if (!items_count || !vulkan_info.camera)
    {
        return;
    }

    const u32 material_data_alignment{core::storage_buffer_offset_alignment()};
    const u32 size{material_data_size(cache)};

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
auto add_per_item_root_parameters(vkx::RootDescriptors &root, const GpassCache &cache, u32 cache_index) -> void
{
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

auto draw_item(VkCommandBuffer cmd, const GpassCache &cache, u32 cache_index) -> void
{
    const content::submesh::IndexBufferView &ibv{cache.index_buffer_views[cache_index]};
    const u32 index_count{ibv.size_in_bytes >> (ibv.index_type == VK_INDEX_TYPE_UINT16 ? 1 : 2)};
    ::vkCmdBindIndexBuffer(cmd, ibv.buffer, ibv.offset, ibv.index_type);
    ::vkCmdDrawIndexed(cmd, index_count, 1, 0, 0, 0);
}

// Records the depth prepass draws of items [begin, end). The first item binds the resource heap and the global
// root parameters, so a range can start a command buffer of its own.
auto record_depth_items(
    VkCommandBuffer cmd,
    const VulkanFrameInfo &vulkan_info,
    const GpassCache &cache,
    u32 begin,
    u32 end) -> void
{
    VkPipelineLayout current_layout{VK_NULL_HANDLE};
    VkPipeline current_pipeline{VK_NULL_HANDLE};

    for (u32 i{begin}; i < end; ++i)
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

        add_per_item_root_parameters(root, cache, i);
        root.push(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, signature);
        draw_item(cmd, cache, i);
    }
}

// Records the gpass draws of items [begin, end), like record_depth_items().
auto record_gpass_items(
    VkCommandBuffer cmd,
    const VulkanFrameInfo &vulkan_info,
    const GpassCache &cache,
    u32 begin,
    u32 end) -> void
{
    VkPipelineLayout current_layout{VK_NULL_HANDLE};
    VkPipeline current_pipeline{VK_NULL_HANDLE};
    const u32 frame_index{vulkan_info.frame_index};
    const id::id_type light_culling_id{vulkan_info.light_culling_id};

    for (u32 i{begin}; i < end; ++i)
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

        add_per_item_root_parameters(root, cache, i);
        root.push(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, signature);
        draw_item(cmd, cache, i);
    }
}

using record_function = auto (*)(VkCommandBuffer, const VulkanFrameInfo &, const GpassCache &, u32, u32) -> void;

// Records one rendering pass over every item: into the primary command buffer, or, with many items, into secondary
// command buffers that jobs record in batches and the primary executes in order (issue #1 step 7, a prototype).
auto record_pass(
    VkCommandBuffer cmd,
    VkRenderingInfo rendering,
    VkFormat color_format,
    const VulkanFrameInfo &vulkan_info,
    GpassCache &cache,
    record_function record) -> void
{
    const u32 items_count{cache.size()};
    // 512 items per secondary command buffer (clang Release, RADV, 2026-11-12): 6,400 items took 3.2-3.4 ms a frame
    // recorded serially, 2.23-2.27 ms with batches of 512; 64 per batch was hardly faster than serial (3.2 ms), 256
    // and 1024 a little slower than 512. Up to 512 items are recorded serially.
    constexpr u32 batch_size{512};
    if (!core::parallel_recording_enabled() || items_count <= batch_size)
    {
        ::vkCmdBeginRendering(cmd, &rendering);
        record(cmd, vulkan_info, cache, 0, items_count);
        ::vkCmdEndRendering(cmd);
        return;
    }

    const u32 batch_count{(items_count + batch_size - 1) / batch_size};
    cache.secondaries.resize(batch_count);
    core::prepare_thread_command_pools();

    // A secondary command buffer recorded for a render pass instance of vkCmdBeginRendering() states its formats.
    VkCommandBufferInheritanceRenderingInfo inheritance_rendering{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDERING_INFO};
    inheritance_rendering.colorAttachmentCount = color_format == VK_FORMAT_UNDEFINED ? 0 : 1;
    inheritance_rendering.pColorAttachmentFormats = &color_format;
    inheritance_rendering.depthAttachmentFormat = depth_buffer_format;
    inheritance_rendering.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkCommandBufferInheritanceInfo inheritance{VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO};
    inheritance.pNext = &inheritance_rendering;

    jobs::parallel_for(
        batch_count,
        1,
        [&](u32 first, u32 last)
        {
            for (u32 batch{first}; batch < last; ++batch)
            {
                // Viewport and scissor are dynamic state, which a secondary command buffer doesn't inherit.
                const VkCommandBuffer secondary{core::begin_secondary(inheritance)};
                vkx::set_viewport(secondary, vulkan_info.surface_width, vulkan_info.surface_height);
                const u32 begin{batch * batch_size};
                record(secondary, vulkan_info, cache, begin, std::min(begin + batch_size, items_count));
                VkCall(::vkEndCommandBuffer(secondary));
                cache.secondaries[batch] = secondary;
            }
        });

    rendering.flags = VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT;
    ::vkCmdBeginRendering(cmd, &rendering);
    ::vkCmdExecuteCommands(cmd, batch_count, cache.secondaries.data());
    ::vkCmdEndRendering(cmd);
}

}

auto initialize() -> bool
{
    return create_buffers(initial_dimensions);
}

auto collect_render_items(const FrameInfo &info, SurfaceId surface_id) -> u64
{
    GpassCache &cache{surface_cache(surface_id)};
    cache.clear();
    // No camera (e.g. the SURFACES test) or nothing to draw.
    if (!id::is_valid(info.camera_id) || !info.render_item_ids || !info.render_item_count)
    {
        return 0;
    }

    using namespace content;
    render_item::get_vulkan_render_item_ids(info, cache.vulkan_render_item_ids);
    cache.resize();
    const u32 items_count{cache.size()};
    const render_item::ItemsCache items_cache{cache.items_cache()};
    render_item::get_items(cache.vulkan_render_item_ids.data(), items_count, items_cache);

    const submesh::ViewsCache views_cache{cache.views_cache()};
    submesh::get_views(items_cache.submesh_gpu_ids, items_count, views_cache);

    const material::MaterialsCache materials_cache{cache.materials_cache()};
    material::get_materials(items_cache.material_ids, items_count, materials_cache, cache.descriptor_index_count);

    // At most: per-object data counts every item as its own object.
    constexpr u64 per_object_stride{math::align_size_up<ConstantBuffer::alignment>(sizeof(hlsl::PerObjectData))};
    return math::align_size_up<ConstantBuffer::alignment>(u64{material_data_size(cache)}) +
           items_count * per_object_stride;
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

    record_pass(cmd, rendering, VK_FORMAT_UNDEFINED, vulkan_info, surface_cache(vulkan_info), &record_depth_items);
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

    record_pass(cmd, rendering, main_buffer_format, vulkan_info, surface_cache(vulkan_info), &record_gpass_items);
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
