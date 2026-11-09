// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "VulkanCommonHeaders.h"
#include "VulkanHelpers.h"

// Meshes, textures, materials and render items (counterpart of D3D12Content.h).
namespace primal::graphics::vulkan::content
{

auto initialize() -> bool;
auto shutdown() -> void;

namespace submesh
{

// Counterpart of D3D12_INDEX_BUFFER_VIEW.
struct IndexBufferView
{
    VkBuffer buffer{VK_NULL_HANDLE};
    u64 offset{0};
    u32 size_in_bytes{0};
    VkIndexType index_type{VK_INDEX_TYPE_UINT16};
};

struct ViewsCache
{
    vkx::BufferRange *const position_buffers; // root SRV t0 (D3D12: GPU virtual addresses)
    vkx::BufferRange *const element_buffers;  // root SRV t1 (buffer is null for position-only meshes)
    IndexBufferView *const index_buffer_views;
    VkPrimitiveTopology *const primitive_topologies;
    u32 *const elements_types;
};

auto add(const u8 *const data, id::id_type *const ids, u32 count) -> void;
auto remove(const id::id_type *const ids, u32 count) -> void;
auto get_views(const id::id_type *const gpu_ids, u32 id_count, const ViewsCache &cache) -> void;
}

namespace texture
{
auto add(const u8 *const data) -> id::id_type;
auto remove(id::id_type id) -> void;
auto get_descriptor_indices(const id::id_type *const texture_ids, u32 id_count, u32 *const indices) -> void;
}

namespace material
{

// NOTE: no command signatures (D3D12 ExecuteIndirect);
struct MaterialsCache
{
    vkx::RootSignature *const root_signatures;
    MaterialType::Type *const material_types;
    u32 **const descriptor_indices;
    u32 *const texture_count;
    MaterialSurface **const material_surfaces;
};

auto add(MaterialInitInfo info) -> id::id_type;
auto remove(id::id_type id) -> void;
auto get_materials(
    const id::id_type *const material_ids,
    u32 material_count,
    const MaterialsCache &cache,
    u32 &descriptor_index_count) -> void;
}

namespace render_item
{

struct ItemsCache
{
    id::id_type *const entity_ids;
    id::id_type *const submesh_gpu_ids;
    id::id_type *const material_ids;
    VkPipeline *const gpass_pipelines;
    VkPipeline *const depth_pipelines;
};

auto add(
    id::id_type entity_id,
    id::id_type geometry_content_id,
    u32 material_count,
    const id::id_type *const material_ids) -> id::id_type;
auto remove(id::id_type id) -> void;
auto get_vulkan_render_item_ids(const FrameInfo &info, utl::vector<id::id_type> &vulkan_render_item_ids) -> void;
auto get_items(const id::id_type *const vulkan_render_item_ids, u32 id_count, const ItemsCache &cache) -> void;
}

}
