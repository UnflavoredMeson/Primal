// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "D3D12GPass.h"
#include "D3D12Core.h"
#include "D3D12Content.h"
#include "D3D12Light.h"
#include "D3D12Camera.h"
#include "D3D12LightCulling.h"
#include "Graphics/Shaders/SharedTypes.h"
#include "Components/Entity.h"
#include "Components/Transform.h"

namespace primal::graphics::d3d12::gpass
{
namespace
{

constexpr math::u32v2 initial_dimensions{100, 100};

D3D12RenderTexture gpass_main_buffer{};
D3D12DepthBuffer gpass_depth_buffer{};
math::u32v2 dimensions{initial_dimensions};

#if _DEBUG
constexpr f32 clear_value[4]{0.5f, 0.5f, 0.5f, 1.f};
#else
constexpr f32 clear_value[4]{};
#endif

// NOTE (to myself): don't forget to #undef CONSTEXPR when you copy/paste this block of code!
#if USE_STL_VECTOR
#define CONSTEXPR
#else
#define CONSTEXPR constexpr
#endif

struct GpassCache
{
    utl::vector<id::id_type> d3d12_render_item_ids;
    utl::vector<u8> draw_indirect_pso_sort_flags;
    utl::vector<u32> gpass_grouped_indices;
    utl::vector<u32> depth_grouped_indices;
    u32 descriptor_index_count{0};
    u32 gpass_pso_count{0};
    u32 depth_pso_count{0};

    // NOTE: when adding new arrays, make sure to update resize() and struct_size.
    id::id_type *entity_ids{nullptr};
    id::id_type *submesh_gpu_ids{nullptr};
    id::id_type *material_ids{nullptr};
    ID3D12PipelineState **gpass_pipeline_states{nullptr};
    ID3D12PipelineState **depth_pipeline_states{nullptr};
    ID3D12RootSignature **root_signatures{nullptr};
    ID3D12CommandSignature **cmd_signatures{nullptr};
    MaterialType::Type *material_types{nullptr};
    u32 **descriptor_indices{nullptr};
    u32 *texture_counts{nullptr};
    MaterialSurface **material_surfaces{nullptr};
    D3D12_GPU_VIRTUAL_ADDRESS *position_buffers{nullptr};
    D3D12_GPU_VIRTUAL_ADDRESS *element_buffers{nullptr};
    D3D12_INDEX_BUFFER_VIEW *index_buffer_views{nullptr};
    D3D_PRIMITIVE_TOPOLOGY *primitive_topologies{nullptr};
    u32 *elements_types{nullptr};
    D3D12_GPU_VIRTUAL_ADDRESS *per_object_data{nullptr};
    D3D12_GPU_VIRTUAL_ADDRESS *material_data{nullptr};

    constexpr auto items_cache() const -> content::render_item::ItemsCache
    {
        return {entity_ids, submesh_gpu_ids, material_ids, gpass_pipeline_states, depth_pipeline_states};
    }

    constexpr auto views_cache() const -> content::submesh::ViewsCache
    {
        return {position_buffers, element_buffers, index_buffer_views, primitive_topologies, elements_types};
    }

    constexpr auto materials_cache() const -> content::material::MaterialsCache
    {
        return {root_signatures, cmd_signatures, material_types, descriptor_indices, texture_counts, material_surfaces};
    }

    CONSTEXPR auto size() const -> u32
    {
        return static_cast<u32>(d3d12_render_item_ids.size());
    }

    CONSTEXPR auto clear() -> void
    {
        d3d12_render_item_ids.clear();
        descriptor_index_count = 0;
    }

    CONSTEXPR auto resize() -> void
    {
        const u64 items_count{d3d12_render_item_ids.size()};
        const u64 new_buffer_size{items_count * struct_size};
        const u64 old_buffer_size{_buffer.size()};

        if (new_buffer_size != old_buffer_size)
        {
            draw_indirect_pso_sort_flags.resize(items_count);
            gpass_grouped_indices.resize(items_count);
            depth_grouped_indices.resize(items_count);

            _buffer.resize(new_buffer_size);

            entity_ids = reinterpret_cast<id::id_type *>(_buffer.data());
            submesh_gpu_ids = (&entity_ids[items_count]);
            material_ids = (&submesh_gpu_ids[items_count]);
            gpass_pipeline_states = reinterpret_cast<ID3D12PipelineState **>(&material_ids[items_count]);
            depth_pipeline_states = (&gpass_pipeline_states[items_count]);
            root_signatures = reinterpret_cast<ID3D12RootSignature **>(&depth_pipeline_states[items_count]);
            cmd_signatures = reinterpret_cast<ID3D12CommandSignature **>(&root_signatures[items_count]);
            material_types = reinterpret_cast<MaterialType::Type *>(&cmd_signatures[items_count]);
            descriptor_indices = reinterpret_cast<u32 **>(&material_types[items_count]);
            texture_counts = reinterpret_cast<u32 *>(&descriptor_indices[items_count]);
            material_surfaces = reinterpret_cast<MaterialSurface **>(&texture_counts[items_count]);
            position_buffers = reinterpret_cast<D3D12_GPU_VIRTUAL_ADDRESS *>(&material_surfaces[items_count]);
            element_buffers = (&position_buffers[items_count]);
            index_buffer_views = reinterpret_cast<D3D12_INDEX_BUFFER_VIEW *>(&element_buffers[items_count]);
            primitive_topologies = reinterpret_cast<D3D_PRIMITIVE_TOPOLOGY *>(&index_buffer_views[items_count]);
            elements_types = reinterpret_cast<u32 *>(&primitive_topologies[items_count]);
            per_object_data = reinterpret_cast<D3D12_GPU_VIRTUAL_ADDRESS *>(&elements_types[items_count]);
            material_data = (&per_object_data[items_count]);
        }
    }

  private:
    constexpr static u32 struct_size{
        sizeof(id::id_type) +               // entity_ids
        sizeof(id::id_type) +               // submesh_gpu_ids
        sizeof(id::id_type) +               // material_ids
        sizeof(ID3D12PipelineState *) +     // gpass_pipeline_states
        sizeof(ID3D12PipelineState *) +     // depth_pipeline_states
        sizeof(ID3D12RootSignature *) +     // root_signatures
        sizeof(ID3D12CommandSignature *) +  // cmd_signatures
        sizeof(MaterialType::Type) +        // material_types
        sizeof(u32 *) +                     // descriptor_indices
        sizeof(u32) +                       // texture_counts
        sizeof(MaterialSurface *) +         // material_surface
        sizeof(D3D12_GPU_VIRTUAL_ADDRESS) + // position_buffers
        sizeof(D3D12_GPU_VIRTUAL_ADDRESS) + // element_buffers
        sizeof(D3D12_INDEX_BUFFER_VIEW) +   // index_buffer_views
        sizeof(D3D_PRIMITIVE_TOPOLOGY) +    // primitive_topologies
        sizeof(u32) +                       // elements_types
        sizeof(D3D12_GPU_VIRTUAL_ADDRESS) + // per_object_data
        sizeof(D3D12_GPU_VIRTUAL_ADDRESS)   // material_data
    };

    utl::vector<u8> _buffer;
} frame_cache;

// Good boy!
#undef CONSTEXPR

class CommandBuffer
{
  public:
    auto resize(u32 items_count) -> void
    {
        assert(items_count);
        _commands.resize(items_count);
        _buffer_size = items_count * sizeof(DrawIndexedIndirectCommand);

        // Create a buffer twice the size of the items count for both gpass and depth pass commands.
        // The first half will be used for gpass commands and the second half for depth pass commands.
        const u32 total_size{_buffer_size * 2};

        if (_cmd_buffer.size() < math::align_size_up<D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT>(total_size))
        {
            _cmd_buffer = D3D12Buffer{ConstantBuffer::get_default_init_info(total_size), true};
            NAME_D3D12_OBJECT_INDEXED(_cmd_buffer.buffer(), core::current_frame_index(), L"Indirect Command Buffer");

            const D3D12_RANGE read_range{0, 0}; // We won't be reading from this buffer on the CPU.
            DXCall(_cmd_buffer.buffer()->Map(0, &read_range, reinterpret_cast<void **>(&_cpu_address)));
            assert(_cpu_address);
        }
    }

    auto release() -> void
    {
        _cmd_buffer.release();
        _cpu_address = nullptr;
    }

    auto upload_gpass_commands() const -> void
    {
        upload_commands(true);
    }
    auto upload_depth_commands() const -> void
    {
        upload_commands(false);
    }
    [[nodiscard]] constexpr auto buffer() const -> ID3D12Resource *
    {
        return _cmd_buffer.buffer();
    }
    [[nodiscard]] constexpr auto size() const -> u32
    {
        return _buffer_size;
    }
    [[nodiscard]] constexpr auto commands() -> utl::vector<DrawIndexedIndirectCommand> &
    {
        return _commands;
    }

  private:
    auto upload_commands(bool is_gpass) const -> void
    {
        if (_buffer_size)
        {
            std::memcpy(is_gpass ? _cpu_address : _cpu_address + _buffer_size, _commands.data(), _buffer_size);
        }
    }

    D3D12Buffer _cmd_buffer;
    u8 *_cpu_address{nullptr};
    u32 _buffer_size{0};
    utl::vector<DrawIndexedIndirectCommand> _commands;
} command_buffers[frame_buffer_count];

auto create_buffers(math::u32v2 size) -> bool
{
    assert(size.x && size.y);
    gpass_main_buffer.release();
    gpass_depth_buffer.release();

    D3D12_RESOURCE_DESC desc{};
    desc.Alignment = 0; // NOTE: 0 is the same as 64KB (or 4MB for MSAA)
    desc.DepthOrArraySize = 1;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    desc.Format = main_buffer_format;
    desc.Height = size.y;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.MipLevels = 0; // make space for all mip levels
    desc.SampleDesc = {1, 0};
    desc.Width = size.x;

    // Create the main buffer
    {
        D3D12TextureInitInfo info{};
        info.desc = &desc;
        info.initial_state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        info.clear_value.Format = desc.Format;
        std::memcpy(&info.clear_value.Color, &clear_value[0], sizeof(clear_value));
        gpass_main_buffer = D3D12RenderTexture{info};
    }

    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    desc.Format = depth_buffer_format;
    desc.MipLevels = 1;

    // Create the depth buffer
    {
        D3D12TextureInitInfo info{};
        info.desc = &desc;
        info.initial_state = D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        info.clear_value.Format = desc.Format;
        info.clear_value.DepthStencil.Depth = 0.f;
        info.clear_value.DepthStencil.Stencil = 0;

        gpass_depth_buffer = D3D12DepthBuffer{info};
    }

    NAME_D3D12_OBJECT(gpass_main_buffer.resource(), L"GPass Main Buffer");
    NAME_D3D12_OBJECT(gpass_depth_buffer.resource(), L"GPass Depth Buffer");

    return gpass_main_buffer.resource() && gpass_depth_buffer.resource();
}

auto fill_per_object_data(const D3D12FrameInfo &d3d12_info) -> void
{
    const GpassCache &cache{frame_cache};
    const u32 render_items_count{cache.size()};
    id::id_type current_entity_id{id::invalid_id};
    hlsl::PerObjectData *current_data_pointer{nullptr};

    ConstantBuffer &cbuffer{core::cbuffer()};

    using namespace DirectX;
    for (u32 i{0}; i < render_items_count; ++i)
    {
        if (current_entity_id != cache.entity_ids[i])
        {
            current_entity_id = cache.entity_ids[i];
            hlsl::PerObjectData data{};
            transform::get_transform_matrices(game_entity::EntityId{current_entity_id}, data.World, data.InvWorld);
            const XMMATRIX world{XMLoadFloat4x4(&data.World)};
            const XMMATRIX wvp{XMMatrixMultiply(world, d3d12_info.camera->view_projection())};
            XMStoreFloat4x4(&data.WorldViewProjection, wvp);

            current_data_pointer = cbuffer.allocate<hlsl::PerObjectData>();
            std::memcpy(current_data_pointer, &data, sizeof(hlsl::PerObjectData));
        }

        assert(current_data_pointer);
        cache.per_object_data[i] = cbuffer.gpu_address(current_data_pointer);
    }
}

auto set_root_parameters(id3d12_graphics_command_list *const cmd_list, u32 cache_index) -> void
{
    const GpassCache &cache{frame_cache};
    assert(cache_index < cache.size());

    const MaterialType::Type mtl_type{cache.material_types[cache_index]};
    switch (mtl_type)
    {
        case MaterialType::opaque:
        {
            using params = OpaqueRootParameter;
            cmd_list->SetGraphicsRootShaderResourceView(params::position_buffer, cache.position_buffers[cache_index]);
            cmd_list->SetGraphicsRootShaderResourceView(params::element_buffer, cache.element_buffers[cache_index]);
            cmd_list->SetGraphicsRootConstantBufferView(params::per_object_data, cache.per_object_data[cache_index]);
            cmd_list->SetGraphicsRootShaderResourceView(params::material_data, cache.material_data[cache_index]);
        }
        break;
    }
}

auto prepare_render_frame(const D3D12FrameInfo &d3d12_info) -> void
{
    assert(d3d12_info.info && d3d12_info.camera);

    GpassCache &cache{frame_cache};
    cache.clear();
    if (!d3d12_info.info->render_item_ids || !d3d12_info.info->render_item_count)
    {
        return;
    }

    using namespace content;
    render_item::get_d3d12_render_item_ids(*d3d12_info.info, cache.d3d12_render_item_ids);
    cache.resize();
    const u32 items_count{cache.size()};
    const render_item::ItemsCache items_cache{cache.items_cache()};
    render_item::get_items(cache.d3d12_render_item_ids.data(), items_count, items_cache);

    const submesh::ViewsCache views_cache{cache.views_cache()};
    submesh::get_views(items_cache.submesh_gpu_ids, items_count, views_cache);

    const material::MaterialsCache materials_cache{cache.materials_cache()};
    material::get_materials(items_cache.material_ids, items_count, materials_cache, cache.descriptor_index_count);

    ConstantBuffer &cbuffer{core::cbuffer()};
    const u32 size{items_count * sizeof(MaterialSurface) + cache.descriptor_index_count * sizeof(u32)};
    u32 *const material_data{reinterpret_cast<u32 *>(cbuffer.allocate(size))};
    u32 mtl_data_offset{0};

    for (u32 i{0}; i < items_count; ++i)
    {
        cache.material_data[i] = cbuffer.gpu_address(material_data + mtl_data_offset);

        std::memcpy(&material_data[mtl_data_offset], cache.material_surfaces[i], sizeof(MaterialSurface));
        mtl_data_offset += sizeof(MaterialSurface) / sizeof(u32);
        const u32 texture_count{cache.texture_counts[i]};

        if (texture_count)
        {
            std::memcpy(&material_data[mtl_data_offset], cache.descriptor_indices[i], texture_count * sizeof(u32));
            mtl_data_offset += texture_count;
        }
    }

    assert(mtl_data_offset == size / sizeof(u32));

    fill_per_object_data(d3d12_info);
}

auto group_by_pso() -> void
{
    GpassCache &cache{frame_cache};
    const u32 items_count{cache.size()};

    if (!items_count)
    {
        return;
    }

    assert(cache.draw_indirect_pso_sort_flags.size() == items_count);
    assert(cache.gpass_grouped_indices.size() == items_count);
    assert(cache.depth_grouped_indices.size() == items_count);

    std::memset(cache.draw_indirect_pso_sort_flags.data(), 0, items_count);
    std::memset(cache.gpass_grouped_indices.data(), u32_invalid_id, items_count * sizeof(u32));
    std::memset(cache.depth_grouped_indices.data(), u32_invalid_id, items_count * sizeof(u32));

    for (u32 pass{0}; pass < 2; ++pass)
    {
        u32 item_index{0};
        u32 pso_count{0};
        const u8 pass_mask{pass == 0 ? static_cast<u8>(0x0f) : static_cast<u8>(0xf0)};
        const u8 flag_set{pass == 0 ? static_cast<u8>(0x01) : static_cast<u8>(0x10)};
        const u8 flag_new_pso{pass == 0 ? static_cast<u8>(0x02) : static_cast<u8>(0x20)};
        utl::vector<u32> &grouped_indices{pass == 0 ? cache.gpass_grouped_indices : cache.depth_grouped_indices};
        ID3D12PipelineState **&pipeline_states{pass == 0 ? cache.gpass_pipeline_states : cache.depth_pipeline_states};

        for (;;)
        {
            u32 index{0};
            while (index < items_count && (cache.draw_indirect_pso_sort_flags[index] & pass_mask))
            {
                ++index;
            }

            if (index >= items_count)
            {
                break;
            }

            ID3D12PipelineState const *current_pso{pipeline_states[index]};

            // mark the first item that's using the current pso
            cache.draw_indirect_pso_sort_flags[index] |= flag_new_pso;
            ++pso_count;

            for (u32 i{index}; i < items_count; ++i)
            {
                if (!(cache.draw_indirect_pso_sort_flags[i] & flag_set) && current_pso == pipeline_states[i])
                {
                    cache.draw_indirect_pso_sort_flags[i] |= flag_set;
                    grouped_indices[item_index] = i;
                    ++item_index;
                }
            }
        }

        assert(pso_count && item_index == items_count);
        assert(std::find(grouped_indices.begin(), grouped_indices.end(), u32_invalid_id) == grouped_indices.end());

        if (pass == 0)
        {
            cache.gpass_pso_count = pso_count;
        }
        else
        {
            cache.depth_pso_count = pso_count;
        }
    }
}

auto record_depth_command_buffer(
    const D3D12FrameInfo &d3d12_info,
    u32 *const new_pso_indices,
    [[maybe_unused]] u32 pso_index_count) -> void
{
    assert(frame_cache.depth_pso_count == pso_index_count);
    const u32 frame_idx{d3d12_info.frame_index};
    const GpassCache &cache{frame_cache};
    const u32 items_count{cache.size()};
    CommandBuffer &cmd_buffer{command_buffers[frame_idx]};

    u32 pso_index{0};

    for (u32 i{0}; i < items_count; ++i)
    {
        assert((cache.draw_indirect_pso_sort_flags[i] & 0xf0) && cache.depth_grouped_indices[i] != u32_invalid_id);
        const u32 cache_index{cache.depth_grouped_indices[i]};

        if (cache.draw_indirect_pso_sort_flags[cache_index] & 0x20)
        {
            new_pso_indices[pso_index++] = i;
        }

        DrawIndexedIndirectCommand &cmd{cmd_buffer.commands()[i]};

        switch (cache.material_types[cache_index])
        {
            case MaterialType::opaque:
            {
                using idx = OpaqueRootParameter;
                cmd.opaque.parameters[idx::global_shader_data] = d3d12_info.global_shader_data;
                cmd.opaque.parameters[idx::per_object_data] = cache.per_object_data[cache_index];
                cmd.opaque.parameters[idx::position_buffer] = cache.position_buffers[cache_index];
                cmd.opaque.parameters[idx::element_buffer] = cache.element_buffers[cache_index];
                cmd.opaque.parameters[idx::material_data] = cache.material_data[cache_index];

                const D3D12_INDEX_BUFFER_VIEW &ibv{cache.index_buffer_views[cache_index]};
                cmd.index_buffer_view = ibv;

                const u32 index_count{ibv.SizeInBytes >> (ibv.Format == DXGI_FORMAT_R16_UINT ? 1 : 2)};
                cmd.draw_indexed_args.IndexCountPerInstance = index_count;
                cmd.draw_indexed_args.InstanceCount = 1;
                cmd.draw_indexed_args.StartIndexLocation = 0;
                cmd.draw_indexed_args.BaseVertexLocation = 0;
                cmd.draw_indexed_args.StartInstanceLocation = 0;
            }
            break;
        }
    }

    cmd_buffer.upload_depth_commands();
}

auto record_gpass_command_buffer(
    const D3D12FrameInfo &d3d12_info,
    u32 *const new_pso_indices,
    [[maybe_unused]] u32 pso_index_count) -> void
{
    assert(frame_cache.gpass_pso_count == pso_index_count);
    const u32 frame_idx{d3d12_info.frame_index};
    const GpassCache &cache{frame_cache};
    const u32 items_count{cache.size()};
    CommandBuffer &cmd_buffer{command_buffers[frame_idx]};

    const id::id_type light_culling_id{d3d12_info.light_culling_id};
    const D3D12_GPU_VIRTUAL_ADDRESS non_cullable_lights{light::non_cullable_light_buffer(frame_idx)};
    const D3D12_GPU_VIRTUAL_ADDRESS cullable_lights{light::cullable_light_buffer(frame_idx)};
    const D3D12_GPU_VIRTUAL_ADDRESS light_grid{delight::light_grid_opaque(light_culling_id, frame_idx)};
    const D3D12_GPU_VIRTUAL_ADDRESS light_index_list{delight::light_index_list_opaque(light_culling_id, frame_idx)};
    u32 pso_index{0};

    for (u32 i{0}; i < items_count; ++i)
    {
        assert((cache.draw_indirect_pso_sort_flags[i] & 0x0f) && cache.gpass_grouped_indices[i] != u32_invalid_id);
        const u32 cache_index{cache.gpass_grouped_indices[i]};

        if (cache.draw_indirect_pso_sort_flags[cache_index] & 0x02)
        {
            new_pso_indices[pso_index++] = i;
        }

        DrawIndexedIndirectCommand &cmd{cmd_buffer.commands()[i]};

        switch (cache.material_types[cache_index])
        {
            case MaterialType::opaque:
            {
                using idx = OpaqueRootParameter;
                cmd.opaque.parameters[idx::global_shader_data] = d3d12_info.global_shader_data;
                cmd.opaque.parameters[idx::per_object_data] = cache.per_object_data[cache_index];
                cmd.opaque.parameters[idx::position_buffer] = cache.position_buffers[cache_index];
                cmd.opaque.parameters[idx::element_buffer] = cache.element_buffers[cache_index];
                cmd.opaque.parameters[idx::material_data] = cache.material_data[cache_index];
                cmd.opaque.parameters[idx::directional_lights] = non_cullable_lights;
                cmd.opaque.parameters[idx::cullable_lights] = cullable_lights;
                cmd.opaque.parameters[idx::light_grid] = light_grid;
                cmd.opaque.parameters[idx::light_index_list] = light_index_list;

                const D3D12_INDEX_BUFFER_VIEW &ibv{cache.index_buffer_views[cache_index]};
                cmd.index_buffer_view = ibv;

                const u32 index_count{ibv.SizeInBytes >> (ibv.Format == DXGI_FORMAT_R16_UINT ? 1 : 2)};
                cmd.draw_indexed_args.IndexCountPerInstance = index_count;
                cmd.draw_indexed_args.InstanceCount = 1;
                cmd.draw_indexed_args.StartIndexLocation = 0;
                cmd.draw_indexed_args.BaseVertexLocation = 0;
                cmd.draw_indexed_args.StartInstanceLocation = 0;
            }
            break;
        }
    }

    cmd_buffer.upload_gpass_commands();
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

    for (u32 i{0}; i < frame_buffer_count; ++i)
    {
        command_buffers[i].release();
    }
}

auto main_buffer() -> const D3D12RenderTexture &
{
    return gpass_main_buffer;
}

auto depth_buffer() -> const D3D12DepthBuffer &
{
    return gpass_depth_buffer;
}

auto set_size(math::u32v2 size) -> void
{
    math::u32v2 &d{dimensions};
    if (size.x > d.x || size.y > d.y)
    {
        d = {std::max(size.x, d.x), std::max(size.y, d.y)};
        create_buffers(d);
    }
}

auto depth_prepass(id3d12_graphics_command_list *cmd_list, const D3D12FrameInfo &d3d12_info) -> void
{
    prepare_render_frame(d3d12_info);

    const GpassCache &cache{frame_cache};
    const u32 items_count{cache.size()};

    ID3D12RootSignature *current_root_signature{nullptr};
    ID3D12PipelineState *current_pipeline_state{nullptr};

    for (u32 i{0}; i < items_count; ++i)
    {
        if (current_root_signature != cache.root_signatures[i])
        {
            current_root_signature = cache.root_signatures[i];
            cmd_list->SetGraphicsRootSignature(current_root_signature);
            cmd_list->SetGraphicsRootConstantBufferView(
                OpaqueRootParameter::global_shader_data, d3d12_info.global_shader_data);
        }

        if (current_pipeline_state != cache.depth_pipeline_states[i])
        {
            current_pipeline_state = cache.depth_pipeline_states[i];
            cmd_list->SetPipelineState(current_pipeline_state);
        }

        set_root_parameters(cmd_list, i);

        const D3D12_INDEX_BUFFER_VIEW &ibv{cache.index_buffer_views[i]};
        const u32 index_count{ibv.SizeInBytes >> (ibv.Format == DXGI_FORMAT_R16_UINT ? 1 : 2)};

        cmd_list->IASetIndexBuffer(&ibv);
        cmd_list->IASetPrimitiveTopology(cache.primitive_topologies[i]);
        cmd_list->DrawIndexedInstanced(index_count, 1, 0, 0, 0);
    }
}

auto render(id3d12_graphics_command_list *cmd_list, const D3D12FrameInfo &d3d12_info) -> void
{
    const GpassCache &cache{frame_cache};
    const u32 items_count{cache.size()};
    const u32 frame_index{d3d12_info.frame_index};
    const id::id_type light_culling_id{d3d12_info.light_culling_id};

    ID3D12RootSignature *current_root_signature{nullptr};
    ID3D12PipelineState *current_pipeline_state{nullptr};

    for (u32 i{0}; i < items_count; ++i)
    {
        if (current_root_signature != cache.root_signatures[i])
        {
            using idx = OpaqueRootParameter;
            current_root_signature = cache.root_signatures[i];
            cmd_list->SetGraphicsRootSignature(current_root_signature);
            cmd_list->SetGraphicsRootConstantBufferView(idx::global_shader_data, d3d12_info.global_shader_data);
            cmd_list->SetGraphicsRootShaderResourceView(
                idx::directional_lights, light::non_cullable_light_buffer(frame_index));
            cmd_list->SetGraphicsRootShaderResourceView(
                idx::cullable_lights, light::cullable_light_buffer(frame_index));
            cmd_list->SetGraphicsRootShaderResourceView(
                idx::light_grid, delight::light_grid_opaque(light_culling_id, frame_index));
            cmd_list->SetGraphicsRootShaderResourceView(
                idx::light_index_list, delight::light_index_list_opaque(light_culling_id, frame_index));
        }

        if (current_pipeline_state != cache.gpass_pipeline_states[i])
        {
            current_pipeline_state = cache.gpass_pipeline_states[i];
            cmd_list->SetPipelineState(current_pipeline_state);
        }

        set_root_parameters(cmd_list, i);

        const D3D12_INDEX_BUFFER_VIEW &ibv{cache.index_buffer_views[i]};
        const u32 index_count{ibv.SizeInBytes >> (ibv.Format == DXGI_FORMAT_R16_UINT ? 1 : 2)};

        cmd_list->IASetIndexBuffer(&ibv);
        cmd_list->IASetPrimitiveTopology(cache.primitive_topologies[i]);
        cmd_list->DrawIndexedInstanced(index_count, 1, 0, 0, 0);
    }
}

auto depth_prepass_indirect(id3d12_graphics_command_list *cmd_list, const D3D12FrameInfo &d3d12_info) -> void
{
    prepare_render_frame(d3d12_info);

    const GpassCache &cache{frame_cache};
    const u32 items_count{cache.size()};

    if (!items_count)
    {
        return;
    }

    const u32 frame_idx{d3d12_info.frame_index};
    CommandBuffer &cmd_buffer{command_buffers[frame_idx]};
    cmd_buffer.resize(items_count);
    group_by_pso();

    const u32 pso_count{cache.depth_pso_count};
    u32 *const new_pso_indices{static_cast<u32 *>(alloca(pso_count * sizeof(u32)))};
    record_depth_command_buffer(d3d12_info, new_pso_indices, pso_count);

    // NOLINTBEGIN(clang-analyzer-core.CallAndMessage,clang-analyzer-core.UndefinedBinaryOperatorResult):
    // new_pso_indices is filled by record_*_command_buffer()
    for (u32 i{0}; i < pso_count; ++i)
    {
        const u32 cache_index{cache.depth_grouped_indices[new_pso_indices[i]]};
        ID3D12RootSignature *current_root_signature{cache.root_signatures[cache_index]};
        ID3D12PipelineState *current_pso{cache.depth_pipeline_states[cache_index]};
        const D3D_PRIMITIVE_TOPOLOGY topology{cache.primitive_topologies[cache_index]};

        const u32 cmd_count{
            i + 1 < pso_count ? new_pso_indices[i + 1] - new_pso_indices[i] : items_count - new_pso_indices[i]};

        cmd_list->SetGraphicsRootSignature(current_root_signature);
        cmd_list->SetPipelineState(current_pso);
        cmd_list->IASetPrimitiveTopology(topology);

        cmd_list->ExecuteIndirect(
            cache.cmd_signatures[cache_index],
            cmd_count,
            cmd_buffer.buffer(),
            cmd_buffer.size() + new_pso_indices[i] * sizeof(DrawIndexedIndirectCommand),
            nullptr,
            0);
    }
    // NOLINTEND(clang-analyzer-core.CallAndMessage,clang-analyzer-core.UndefinedBinaryOperatorResult)
}

auto render_indirect(id3d12_graphics_command_list *cmd_list, const D3D12FrameInfo &d3d12_info) -> void
{
    const GpassCache &cache{frame_cache};
    const u32 items_count{cache.size()};

    if (!items_count)
    {
        return;
    }

    const u32 frame_idx{d3d12_info.frame_index};
    const CommandBuffer &cmd_buffer{command_buffers[frame_idx]};
    const u32 pso_count{cache.gpass_pso_count};
    u32 *const new_pso_indices{static_cast<u32 *>(alloca(pso_count * sizeof(u32)))};
    record_gpass_command_buffer(d3d12_info, new_pso_indices, pso_count);

    // NOLINTBEGIN(clang-analyzer-core.CallAndMessage,clang-analyzer-core.UndefinedBinaryOperatorResult):
    // new_pso_indices is filled by record_*_command_buffer()
    for (u32 i{0}; i < pso_count; ++i)
    {
        const u32 cache_index{cache.gpass_grouped_indices[new_pso_indices[i]]};
        ID3D12RootSignature *current_root_signature{cache.root_signatures[cache_index]};
        ID3D12PipelineState *current_pso{cache.gpass_pipeline_states[cache_index]};
        const D3D_PRIMITIVE_TOPOLOGY topology{cache.primitive_topologies[cache_index]};

        const u32 cmd_count{
            i + 1 < pso_count ? new_pso_indices[i + 1] - new_pso_indices[i] : items_count - new_pso_indices[i]};

        cmd_list->SetGraphicsRootSignature(current_root_signature);
        cmd_list->SetPipelineState(current_pso);
        cmd_list->IASetPrimitiveTopology(topology);

        cmd_list->ExecuteIndirect(
            cache.cmd_signatures[cache_index],
            cmd_count,
            cmd_buffer.buffer(),
            new_pso_indices[i] * sizeof(DrawIndexedIndirectCommand),
            nullptr,
            0);
    }
    // NOLINTEND(clang-analyzer-core.CallAndMessage,clang-analyzer-core.UndefinedBinaryOperatorResult)
}

auto add_transitions_for_depth_prepass(d3dx::D3D12ResourceBarrier &barriers) -> void
{
    barriers.add(
        gpass_main_buffer.resource(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY);
    barriers.add(
        gpass_depth_buffer.resource(),
        D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_DEPTH_WRITE);
}

auto add_transitions_for_gpass(d3dx::D3D12ResourceBarrier &barriers) -> void
{
    barriers.add(
        gpass_main_buffer.resource(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_BARRIER_FLAG_END_ONLY);
    barriers.add(
        gpass_depth_buffer.resource(),
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
}

auto add_transitions_for_post_process(d3dx::D3D12ResourceBarrier &barriers) -> void
{
    barriers.add(
        gpass_main_buffer.resource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

auto set_render_targets_for_depth_prepass(id3d12_graphics_command_list *cmd_list) -> void
{
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv{gpass_depth_buffer.dsv()};
    cmd_list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 0.f, 0, 0, nullptr);
    cmd_list->OMSetRenderTargets(0, nullptr, 0, &dsv);
}

auto set_render_targets_for_gpass(id3d12_graphics_command_list *cmd_list) -> void
{
    const D3D12_CPU_DESCRIPTOR_HANDLE rtv{gpass_main_buffer.rtv(0)};
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv{gpass_depth_buffer.dsv()};

    cmd_list->ClearRenderTargetView(rtv, clear_value, 0, nullptr);
    cmd_list->OMSetRenderTargets(1, &rtv, 0, &dsv);
}

}
