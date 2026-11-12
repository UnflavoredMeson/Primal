// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "Common.h"
#include "CommonHeaders.h"
#include "Components/Entity.h"
#include "Components/Geometry.h"
#include "Components/Script.h"
#include "Components/Transform.h"
#include "Id.h"
#include "Utilities/Threading.h"

using namespace primal;

auto to_quat(math::v3 angles, bool is_degrees) -> math::v4
{
    if (is_degrees)
    {
        angles = math::to_radians(angles);
    }

    return math::quat_from_euler(angles);
}

namespace
{

struct TransformComponent
{
    f32 position[3];
    f32 rotation[3];
    f32 scale[3];

    auto to_init_info() const -> transform::InitInfo
    {
        transform::InitInfo info{};
        std::memcpy(&info.position[0], &position[0], sizeof(position));
        std::memcpy(&info.scale[0], &scale[0], sizeof(scale));
        const math::v3 rot{&rotation[0]};
        math::v4 rot_quat{::to_quat(rot, true)};
        std::memcpy(&info.rotation[0], &rot_quat.x, sizeof(info.rotation));
        return info;
    }
};

struct ScriptComponent
{
    script::detail::script_creator script_creator;

    auto to_init_info() const -> script::InitInfo
    {
        script::InitInfo info{};
        info.script_creator = script_creator;
        return info;
    }
};

struct GeometryComponent
{
    id::id_type geometry_content_id;
    u32 material_count;
    id::id_type *material_ids;

    auto to_init_info() const -> geometry::InitInfo
    {
        geometry::InitInfo info{};
        info.geometry_content_id = geometry_content_id;
        info.material_count = material_count;
        info.material_ids = material_ids;
        return info;
    }
};

struct GameEntityDescriptor
{
    TransformComponent transform;
    ScriptComponent script;
    GeometryComponent geometry;
};

utl::vector<transform::ComponentCache> transform_cache{};

auto get_euler_angles_from_local_frame(const math::m3x3 &m) -> math::v3
{
    float pitch{0.f}, yaw{0.f}, roll{0.f};
    // Assuming front is Z, up is Y and right is X
    if (m._32 < 1.f)
    {
        if (m._32 > -1.f)
        {
            pitch = std::asinf(-m._32);
            yaw = std::atan2f(m._31, m._33);
            roll = std::atan2f(m._12, m._22);
        }
        else
        {
            // m._32 == -1
            pitch = math::half_pi;
            yaw = -std::atan2f(-m._13, m._11);
            roll = 0.f;
        }
    }
    else
    {
        // m._32 == +1
        pitch = -math::half_pi;
        yaw = std::atan2f(-m._13, m._11);
        roll = 0.f;
    }

    return {pitch * math::to_deg, yaw * math::to_deg, roll * math::to_deg};
}

auto get_local_pos(math::v3 pos, u32 id) -> math::v3
{
    const transform::Component xform{transform::TransformId{id}};
    return math::to_v3(xform.calculate_local_position(pos));
}

auto get_world_quat(math::v3 w, u32 id) -> math::v4
{
    w = math::to_radians(w);
    const transform::Component xform{transform::TransformId{id}};
    return math::to_v4(xform.calculate_world_rotation(w));
}

auto get_local_quat(math::v3 w, u32 id) -> math::v4
{
    w = math::to_radians(w);
    const transform::Component xform{transform::TransformId{id}};
    return math::to_v4(xform.calculate_local_rotation(w));
}

auto get_absolute_quat(math::v3 w, u32 id) -> math::v4
{
    w = math::to_radians(w);
    const transform::Component xform{transform::TransformId{id}};
    return math::to_v4(xform.calculate_absolute_rotation(w));
}
}

utl::TicketMutex mutex{};

EDITOR_INTERFACE auto CreateGameEntity(GameEntityDescriptor *e) -> id::id_type
{
    const std::scoped_lock lock{mutex};
    assert(e);
    const GameEntityDescriptor &desc{*e};
    transform::InitInfo transform_info{desc.transform.to_init_info()};
    script::InitInfo script_info{desc.script.to_init_info()};
    geometry::InitInfo geometry_info{desc.geometry.to_init_info()};
    const game_entity::EntityInfo entity_info{
        &transform_info,
        &script_info,
        id::is_valid(desc.geometry.geometry_content_id) ? &geometry_info : nullptr,
    };
    return game_entity::create(entity_info).get_id();
}

EDITOR_INTERFACE auto RemoveGameEntity(id::id_type id) -> void
{
    const std::scoped_lock lock{mutex};
    assert(id::is_valid(id));
    game_entity::remove(game_entity::EntityId{id});
}

EDITOR_INTERFACE auto UpdateComponent(id::id_type entity_id, GameEntityDescriptor *e, ComponentType::Type type) -> b32
{
    const std::scoped_lock lock{mutex};
    assert(id::is_valid(entity_id) && e && type != ComponentType::transform);
    const GameEntityDescriptor &desc{*e};
    script::InitInfo script_info{desc.script.to_init_info()};
    geometry::InitInfo geometry_info{desc.geometry.to_init_info()};
    const game_entity::EntityInfo entity_info{
        nullptr,
        &script_info,
        id::is_valid(desc.geometry.geometry_content_id) ? &geometry_info : nullptr,
    };
    return game_entity::update_component(game_entity::EntityId{entity_id}, entity_info, type) ? 1 : 0;
}

EDITOR_INTERFACE auto GetComponentId(id::id_type entity_id, ComponentType::Type type) -> id::id_type
{
    const std::scoped_lock lock{mutex};
    assert(id::is_valid(entity_id));
    const game_entity::Entity entity{game_entity::EntityId{entity_id}};

    switch (type)
    {
        case ComponentType::transform: return entity.transform().get_id();
        case ComponentType::script: return entity.script().get_id();
        case ComponentType::geometry: return entity.geometry().get_id();
        default: return id::invalid_id;
    }
}

EDITOR_INTERFACE auto GetPosition(id::id_type const *ids, f32 *x, f32 *y, f32 *z, u32 count) -> void
{
    assert(ids && count);
    const std::scoped_lock lock{mutex};

    for (u32 i{0}; i < count; ++i)
    {
        assert(
            transform::TransformId{ids[i]} == game_entity::Entity{game_entity::EntityId{ids[i]}}.transform().get_id());
        // NOTE: transform id is the same as entity id
        const transform::TransformId id{ids[i]};
        const transform::Component t{id};
        const math::v3 pos{t.position()};
        x[i] = pos.x;
        y[i] = pos.y;
        z[i] = pos.z;
    }
}

EDITOR_INTERFACE auto GetRotation(id::id_type const *ids, f32 *x, f32 *y, f32 *z, u32 count) -> void
{
    assert(ids && count);
    const std::scoped_lock lock{mutex};
    for (u32 i{0}; i < count; ++i)
    {
        assert(
            transform::TransformId{ids[i]} == game_entity::Entity{game_entity::EntityId{ids[i]}}.transform().get_id());
        // NOTE: transform id is the same as entity id
        const transform::TransformId id{ids[i]};
        const transform::Component t{id};
        const math::v3 euler{get_euler_angles_from_local_frame(t.local_frame())};
        x[i] = euler.x;
        y[i] = euler.y;
        z[i] = euler.z;
    }
}

EDITOR_INTERFACE auto GetScale(id::id_type const *ids, f32 *x, f32 *y, f32 *z, u32 count) -> void
{
    assert(ids && count);
    const std::scoped_lock lock{mutex};

    for (u32 i{0}; i < count; ++i)
    {
        assert(
            transform::TransformId{ids[i]} == game_entity::Entity{game_entity::EntityId{ids[i]}}.transform().get_id());
        // NOTE: transform id is the same as entity id
        const transform::TransformId id{ids[i]};
        const transform::Component t{id};
        const math::v3 scale{t.scale()};
        x[i] = scale.x;
        y[i] = scale.y;
        z[i] = scale.z;
    }
}

EDITOR_INTERFACE auto SetPosition(
    id::id_type const *ids,
    f32 const *x,
    f32 const *y,
    f32 const *z,
    u32 count,
    b32 is_local) -> void
{
    assert(ids && count);
    const std::scoped_lock lock{mutex};

    transform_cache.resize(count);

    for (u32 i{0}; i < count; ++i)
    {
        const math::v3 v{x[i], y[i], z[i]};
        transform_cache[i].position = !is_local ? v : get_local_pos(v, ids[i]);
        transform_cache[i].flags = transform::ComponentFlags::position;
        assert(
            transform::TransformId{ids[i]} == game_entity::Entity{game_entity::EntityId{ids[i]}}.transform().get_id());
        // NOTE: transform id is the same as entity id
        transform_cache[i].id = transform::TransformId{ids[i]};
    }

    transform::update(transform_cache.data(), count);
}

EDITOR_INTERFACE auto SetRotation(
    id::id_type const *ids,
    f32 const *x,
    f32 const *y,
    f32 const *z,
    u32 count,
    u32 frame) -> void
{
    assert(ids && count);
    const std::scoped_lock lock{mutex};

    transform_cache.resize(count);

    using frame_fn_ptr = math::v4 (*)(math::v3, u32);
    frame_fn_ptr frame_fn{&get_absolute_quat};

    if (frame == transform::Space::local)
    {
        frame_fn = &get_local_quat;
    }
    else if (frame == transform::Space::world)
    {
        frame_fn = &get_world_quat;
    }

    for (u32 i{0}; i < count; ++i)
    {
        const math::v3 v{x[i], y[i], z[i]};
        transform_cache[i].rotation = frame_fn(v, ids[i]);
        transform_cache[i].flags = transform::ComponentFlags::rotation;
        assert(
            transform::TransformId{ids[i]} == game_entity::Entity{game_entity::EntityId{ids[i]}}.transform().get_id());
        // NOTE: transform id is the same as entity id
        transform_cache[i].id = transform::TransformId{ids[i]};
    }

    transform::update(transform_cache.data(), count);
}

EDITOR_INTERFACE auto SetScale(id::id_type const *ids, f32 const *x, f32 const *y, f32 const *z, u32 count, b32) -> void
{
    assert(ids && count);
    const std::scoped_lock lock{mutex};

    transform_cache.resize(count);

    for (u32 i{0}; i < count; ++i)
    {
        transform_cache[i].scale = {x[i], y[i], z[i]};
        transform_cache[i].flags = transform::ComponentFlags::scale;
        assert(
            transform::TransformId{ids[i]} == game_entity::Entity{game_entity::EntityId{ids[i]}}.transform().get_id());
        // NOTE: transform id is the same as entity id
        transform_cache[i].id = transform::TransformId{ids[i]};
    }

    transform::update(transform_cache.data(), count);
}
