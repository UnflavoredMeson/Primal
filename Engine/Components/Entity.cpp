// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "Entity.h"
#include "Transform.h"
#include "Script.h"
#include "Geometry.h"

namespace primal::game_entity
{
namespace
{

utl::vector<transform::Component> transforms;
utl::vector<script::Component> scripts;
utl::vector<geometry::Component> geometries;

utl::vector<id::generation_type> generations;
utl::deque<EntityId> free_ids;

}

auto create(EntityInfo info) -> Entity
{
    assert(info.transform); // All game entities must have a transform component
    if (!info.transform)
    {
        return {};
    }

    EntityId id{};

    if (free_ids.size() > id::min_deleted_elements)
    {
        id = free_ids.front();
        assert(!is_alive(id));
        free_ids.pop_front();
        id = EntityId{id::new_generation(id)};
        ++generations[id::index(id)];
    }
    else
    {
        id = EntityId{(id::id_type)generations.size()};
        generations.push_back(0);

        // Resize components
        // NOTE: we don't call resize(), so the number of memory allocations stays low
        transforms.emplace_back();
        scripts.emplace_back();
        geometries.emplace_back();
    }

    const Entity new_entity{id};
    const id::id_type index{id::index(id)};

    // Create transform component
    assert(!transforms[index].is_valid());
    transforms[index] = transform::create(*info.transform, new_entity);
    assert(transforms[index].get_id() == id);
    if (!transforms[index].is_valid())
    {
        return {};
    }

    // Create script component
    if (info.script && info.script->script_creator)
    {
        assert(!scripts[index].is_valid());
        scripts[index] = script::create(*info.script, new_entity);
        assert(scripts[index].is_valid());
    }

    // Create geometry component
    if (info.geometry)
    {
        assert(!geometries[index].is_valid());
        geometries[index] = geometry::create(*info.geometry, new_entity);
        assert(geometries[index].is_valid());
    }

    return new_entity;
}

auto remove(EntityId id) -> void
{
    const id::id_type index{id::index(id)};
    assert(is_alive(id));

    if (geometries[index].is_valid())
    {
        geometry::remove(geometries[index]);
        geometries[index] = {};
    }

    if (scripts[index].is_valid())
    {
        script::remove(scripts[index]);
        scripts[index] = {};
    }

    transform::remove(transforms[index]);
    transforms[index] = {};

    if (generations[index] < id::max_generation)
    {
        free_ids.push_back(id);
    }
}

auto update_component(EntityId id, EntityInfo info, ComponentType::Type type) -> bool
{
    assert(is_alive(id) && type != ComponentType::transform);
    if (type == ComponentType::transform)
    {
        return false;
    }
    const Entity entity{id};
    const id::id_type index{id::index(id)};

    if (type == ComponentType::script)
    {
        if (scripts[index].is_valid())
        {
            script::remove(scripts[index]);
            scripts[index] = {};
        }

        if (info.script && info.script->script_creator)
        {
            const script::Component new_script{script::create(*info.script, entity)};
            assert(new_script.is_valid());

            if (new_script.is_valid())
            {
                scripts[index] = new_script;
                return true;
            }
        }
    }
    else if (type == ComponentType::geometry)
    {
        if (geometries[index].is_valid())
        {
            geometry::remove(geometries[index]);
            geometries[index] = {};
        }

        if (info.geometry)
        {
            const geometry::Component new_geometry{geometry::create(*info.geometry, entity)};
            assert(new_geometry.is_valid());

            if (new_geometry.is_valid())
            {
                geometries[index] = new_geometry;
                return true;
            }
        }
    }

    return false;
}

auto is_alive(EntityId id) -> bool
{
    assert(id::is_valid(id));
    const id::id_type index{id::index(id)};
    assert(index < generations.size());
    return generations[index] == id::generation(id) && transforms[index].is_valid();
}

auto Entity::transform() const -> transform::Component
{
    assert(is_alive(_id));
    return transforms[id::index(_id)];
}

auto Entity::script() const -> script::Component
{
    assert(is_alive(_id));
    return scripts[id::index(_id)];
}

auto Entity::geometry() const -> geometry::Component
{
    assert(is_alive(_id));
    return geometries[id::index(_id)];
}

}
