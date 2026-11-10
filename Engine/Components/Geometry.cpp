// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "Geometry.h"

#include "Entity.h"
#include "Graphics/Renderer.h"

namespace primal::geometry
{
namespace
{

utl::vector<id::id_type> render_item_ids;
utl::vector<game_entity::EntityId> owning_entity_ids;
utl::vector<GeometryId> owner_ids;
utl::vector<id::id_type> id_mapping;

utl::vector<id::generation_type> generations;
utl::deque<GeometryId> free_ids;

#if _DEBUG
auto exists(GeometryId id) -> bool
{
    assert(id::is_valid(id));
    const id::id_type index{id::index(id)};
    assert(
        index < generations.size() && (!id::is_valid(id_mapping[index]) || id_mapping[index] < render_item_ids.size()));
    assert(generations[index] == id::generation(id));
    return (generations[index] == id::generation(id)) && id::is_valid(id_mapping[index]) &&
           id::is_valid(render_item_ids[id_mapping[index]]);
}
#endif
}

auto create(InitInfo info, game_entity::Entity entity) -> Component
{
    assert(entity.is_valid());
    assert(id::is_valid(info.geometry_content_id) && info.material_count && info.material_ids);

    GeometryId id{};
    if (free_ids.size() > id::min_deleted_elements)
    {
        id = free_ids.front();
        assert(!exists(id));
        free_ids.pop_front();
        id = GeometryId{id::new_generation(id)};
        ++generations[id::index(id)];
    }
    else
    {
        id = GeometryId{static_cast<id::id_type>(id_mapping.size())};
        id_mapping.emplace_back();
        generations.push_back(0);
    }

    assert(id::is_valid(id));
    const id::id_type index{static_cast<id::id_type>(render_item_ids.size())};
    render_item_ids.emplace_back(
        graphics::add_render_item(entity.get_id(), info.geometry_content_id, info.material_count, info.material_ids));
    owner_ids.emplace_back(id::index(id));
    owning_entity_ids.emplace_back(entity.get_id());
    id_mapping[id::index(id)] = index;
    return Component{id};
}

auto remove(Component c) -> void
{
    assert(c.is_valid() && exists(c.get_id()));
    const GeometryId id{c.get_id()};
    const id::id_type index{id_mapping[id::index(id)]};
    const GeometryId last_id{owner_ids.back()};
    graphics::remove_render_item(render_item_ids[index]);
    utl::erase_unordered(render_item_ids, index);
    utl::erase_unordered(owning_entity_ids, index);
    utl::erase_unordered(owner_ids, index);
    id_mapping[id::index(last_id)] = index;
    id_mapping[id::index(id)] = id::invalid_id;

    if (generations[id::index(id)] < id::max_generation)
    {
        free_ids.push_back(id);
    }
}

auto get_render_item_ids(id::id_type *const item_ids, u32 count) -> void
{
    assert(item_ids && render_item_ids.size() >= count);
    std::memcpy(item_ids, render_item_ids.data(), count * sizeof(id::id_type));
}

auto get_render_item_ids(const id::id_type *const geometry_ids, id::id_type *const item_ids, u32 count) -> void
{
    assert(geometry_ids && item_ids && count);
    assert(render_item_ids.size() >= count);
    for (u32 i{0}; i < count; ++i)
    {
        const GeometryId id{geometry_ids[i]};
        assert(id::is_valid(id) && exists(id));
        const id::id_type index{id_mapping[id::index(id)]};
        assert(index < render_item_ids.size() && id::is_valid(render_item_ids[index]));
        item_ids[i] = render_item_ids[index];
    }
}

auto get_entity_ids(const id::id_type *const geometry_ids, game_entity::EntityId *const entity_ids, u32 count) -> void
{
    assert(geometry_ids && entity_ids && count);
    for (u32 i{0}; i < count; ++i)
    {
        const GeometryId id{geometry_ids[i]};
        assert(id::is_valid(id) && exists(id));
        const id::id_type index{id_mapping[id::index(id)]};
        assert(index < owning_entity_ids.size() && id::is_valid(owning_entity_ids[index]));
        entity_ids[i] = owning_entity_ids[index];
    }
}

}
