// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "D3D12CommonHeaders.h"

namespace primal::graphics::d3d12::content
{

auto initialize() -> bool;
auto shutdown() -> void;

namespace submesh
{

struct ViewsCache
{
    D3D12_GPU_VIRTUAL_ADDRESS *const position_buffers;
    D3D12_GPU_VIRTUAL_ADDRESS *const element_buffers;
    D3D12_INDEX_BUFFER_VIEW *const index_buffer_views;
    D3D_PRIMITIVE_TOPOLOGY *const primitive_topologies;
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

struct MaterialsCache
{
    ID3D12RootSignature **const root_signatures;
    ID3D12CommandSignature **const cmd_signatures;
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
    ID3D12PipelineState **const gpass_psos;
    ID3D12PipelineState **const depth_psos;
};

auto add(
    id::id_type entity_id,
    id::id_type geometry_content_id,
    u32 material_count,
    const id::id_type *const material_ids) -> id::id_type;
auto remove(id::id_type id) -> void;
auto get_d3d12_render_item_ids(const FrameInfo &info, utl::vector<id::id_type> &d3d12_render_item_ids) -> void;
auto get_items(const id::id_type *const d3d12_render_item_ids, u32 id_count, const ItemsCache &cache) -> void;
}
}
