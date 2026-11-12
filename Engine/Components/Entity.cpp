// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "Entity.h"

#include "Geometry.h"
#include "Script.h"
#include "Transform.h"

#include <mutex>
#include <vector>

import primal.frame;

namespace primal::game_entity
{
namespace
{

utl::vector<transform::Component> transforms;
utl::vector<script::Component> scripts;
utl::vector<geometry::Component> geometries;

utl::vector<id::generation_type> generations;
utl::deque<EntityId> free_ids;

// A queued create or remove, with copies of everything the caller pointed to.
struct Command
{
    EntityId remove_id{id::invalid_id}; // valid for a removal
    transform::InitInfo transform{};
    script::InitInfo script{};
    geometry::InitInfo geometry{};
    bool has_geometry{false};
    std::vector<id::id_type> material_ids;
    created_callback on_created{nullptr};
    void *user_data{nullptr};
};

std::mutex command_mutex;
std::vector<Command> queued_commands;
// The batch process_queued_commands() runs; kept to reuse its memory.
std::vector<Command> running_commands;

auto run_command(Command &command) -> void
{
    if (id::is_valid(command.remove_id))
    {
        if (is_alive(command.remove_id))
        {
            remove(command.remove_id);
        }
        return;
    }

    if (command.has_geometry)
    {
        command.geometry.material_ids = command.material_ids.empty() ? nullptr : command.material_ids.data();
    }
    const EntityInfo info{
        .transform = &command.transform,
        .script = command.script.script_creator ? &command.script : nullptr,
        .geometry = command.has_geometry ? &command.geometry : nullptr,
    };
    const Entity entity{create(info)};
    if (command.on_created)
    {
        command.on_created(entity, command.user_data);
    }
}

}

auto create(EntityInfo info) -> Entity
{
    assert(frame::structural_changes_allowed() && "Inside a frame, use game_entity::queue_create().");
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
        id = EntityId{static_cast<id::id_type>(generations.size())};
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
    assert(frame::structural_changes_allowed() && "Inside a frame, use game_entity::queue_remove().");
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
    assert(frame::structural_changes_allowed() && "Components can't be replaced inside a frame.");
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

auto queue_create(EntityInfo info, created_callback on_created, void *user_data) -> void
{
    assert(info.transform); // All game entities must have a transform component
    if (!info.transform)
    {
        return;
    }

    Command command{
        .transform = *info.transform,
        .script = info.script ? *info.script : script::InitInfo{},
        .on_created = on_created,
        .user_data = user_data,
    };
    if (info.geometry)
    {
        command.geometry = *info.geometry;
        command.has_geometry = true;
        if (info.geometry->material_ids && info.geometry->material_count)
        {
            command.material_ids.assign(
                info.geometry->material_ids, info.geometry->material_ids + info.geometry->material_count);
        }
    }

    const std::scoped_lock lock{command_mutex};
    queued_commands.push_back(std::move(command));
}

auto queue_remove(EntityId id) -> void
{
    assert(id::is_valid(id));
    const std::scoped_lock lock{command_mutex};
    queued_commands.push_back({.remove_id = id});
}

auto process_queued_commands() -> void
{
    assert(frame::structural_changes_allowed());
    assert(running_commands.empty() && "process_queued_commands() was called from a command.");
    {
        const std::scoped_lock lock{command_mutex};
        running_commands.swap(queued_commands);
    }
    for (Command &command : running_commands)
    {
        run_command(command);
    }
    running_commands.clear();
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
