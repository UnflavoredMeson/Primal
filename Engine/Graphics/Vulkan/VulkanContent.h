// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "VulkanCommonHeaders.h"
#include "VulkanHelpers.h"

// Meshes, textures, materials and render items (counterpart of D3D12Content.h).
namespace primal::graphics::vulkan::content {

bool initialize();
void shutdown();

namespace submesh {

// Counterpart of D3D12_INDEX_BUFFER_VIEW.
struct index_buffer_view
{
    VkBuffer buffer{ VK_NULL_HANDLE };
    u64 offset{ 0 };
    u32 size_in_bytes{ 0 };
    VkIndexType index_type{ VK_INDEX_TYPE_UINT16 };
};

struct views_cache
{
    vkx::buffer_range* const position_buffers; // root SRV t0 (D3D12: GPU virtual addresses)
    vkx::buffer_range* const element_buffers;  // root SRV t1 (buffer is null for position-only meshes)
    index_buffer_view* const index_buffer_views;
    VkPrimitiveTopology* const primitive_topologies;
    u32* const elements_types;
};

void add(const u8* const data, id::id_type* const ids, u32 count);
void remove(const id::id_type* const ids, u32 count);
void get_views(const id::id_type* const gpu_ids, u32 id_count, const views_cache& cache);
} // namespace submesh

namespace texture {
id::id_type add(const u8* const data);
void remove(id::id_type id);
void get_descriptor_indices(const id::id_type* const texture_ids, u32 id_count, u32* const indices);
} // namespace texture

namespace material {

// NOTE: no command signatures (D3D12 ExecuteIndirect);
struct materials_cache
{
    vkx::root_signature* const root_signatures;
    material_type::type* const material_types;
    u32** const descriptor_indices;
    u32* const texture_count;
    material_surface** const material_surfaces;
};

id::id_type add(material_init_info info);
void remove(id::id_type id);
void get_materials(const id::id_type* const material_ids, u32 material_count, const materials_cache& cache,
                   u32& descriptor_index_count);
} // namespace material

namespace render_item {

struct items_cache
{
    id::id_type* const entity_ids;
    id::id_type* const submesh_gpu_ids;
    id::id_type* const material_ids;
    VkPipeline* const gpass_pipelines;
    VkPipeline* const depth_pipelines;
};

id::id_type add(id::id_type entity_id, id::id_type geometry_content_id, u32 material_count,
                const id::id_type* const material_ids);
void remove(id::id_type id);
void get_vulkan_render_item_ids(const frame_info& info, utl::vector<id::id_type>& vulkan_render_item_ids);
void get_items(const id::id_type* const vulkan_render_item_ids, u32 id_count, const items_cache& cache);
} // namespace render_item

} // namespace primal::graphics::vulkan::content
