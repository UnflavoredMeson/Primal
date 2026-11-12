// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "../Common/CommonHeaders.h"
#include "GeometryComponent.h"
#include "ScriptComponent.h"
#include "TransformComponent.h"

namespace primal::game_entity
{

DEFINE_TYPED_ID(EntityId);

class Entity
{
  public:
    constexpr explicit Entity(EntityId id)
        : _id{id}
    {
    }
    constexpr Entity()
        : _id{id::invalid_id}
    {
    }
    [[nodiscard]] constexpr auto get_id() const -> EntityId
    {
        return _id;
    }
    [[nodiscard]] constexpr auto is_valid() const -> bool
    {
        return id::is_valid(_id);
    }

    [[nodiscard]] auto transform() const -> transform::Component;
    [[nodiscard]] auto script() const -> script::Component;
    [[nodiscard]] auto geometry() const -> geometry::Component;

    [[nodiscard]] auto rotation() const -> math::v4
    {
        return transform().rotation();
    }
    [[nodiscard]] auto position() const -> math::v3
    {
        return transform().position();
    }
    [[nodiscard]] auto scale() const -> math::v3
    {
        return transform().scale();
    }
    [[nodiscard]] auto right() const -> math::v3
    {
        return transform().right();
    }
    [[nodiscard]] auto up() const -> math::v3
    {
        return transform().up();
    }
    [[nodiscard]] auto front() const -> math::v3
    {
        return transform().front();
    }
    [[nodiscard]] auto local_frame() const -> math::m3x3
    {
        return transform().local_frame();
    }

  private:
    EntityId _id;
};
}

namespace primal::script
{
class EntityScript : public game_entity::Entity
{
  public:
    virtual ~EntityScript() = default;
    // Lifetime, in order (all on the frame thread, except update() later, when scripts run in parallel):
    // - the constructor: when the entity is created; other entities created with it may not exist yet;
    // - begin_play(): at the next sync point of a frame (frame::run()), before the first update(); every entity
    //   created before that sync point exists;
    // - update(dt): once per frame;
    // - end_play(): when the entity or the script is removed (also when the game unloads), only if begin_play()
    //   was called; the entity still exists. Entities to remove from here: game_entity::queue_remove();
    // - the destructor.
    virtual auto begin_play() -> void
    {
    }
    virtual auto update(f32) -> void
    {
    }
    virtual auto end_play() -> void
    {
    }

  protected:
    constexpr explicit EntityScript(game_entity::Entity entity)
        : game_entity::Entity{entity.get_id()}
    {
    }

    auto set_rotation(math::v4 rotation_quaternion) const -> void
    {
        set_rotation(this, rotation_quaternion);
    }
    auto set_position(math::v3 position) const -> void
    {
        set_position(this, position);
    }
    auto set_scale(math::v3 scale) const -> void
    {
        set_scale(this, scale);
    }

    static auto set_rotation(const game_entity::Entity *const entity, math::v4 rotation_quaternion) -> void;
    static auto set_position(const game_entity::Entity *const entity, math::v3 position) -> void;
    static auto set_scale(const game_entity::Entity *const entity, math::v3 scale) -> void;
};

namespace detail
{
using script_ptr = std::unique_ptr<EntityScript>;
using script_creator = script_ptr (*)(game_entity::Entity entity);
using string_hash = std::hash<std::string>;

auto register_script(size_t, script_creator) -> u8;
#ifdef USE_WITH_EDITOR
extern "C" __declspec(dllexport)
#endif
auto get_script_creator(size_t tag) -> script_creator;

template <class script_class>
auto create_script(game_entity::Entity entity) -> script_ptr
{
    assert(entity.is_valid());
    return std::make_unique<script_class>(entity);
}

#ifdef USE_WITH_EDITOR
auto add_script_name(const char *name) -> u8;

#define REGISTER_SCRIPT(TYPE)                                                                                          \
    namespace                                                                                                          \
    {                                                                                                                  \
    const u8 _reg_##TYPE{primal::script::detail::register_script(                                                      \
        primal::script::detail::string_hash()(#TYPE),                                                                  \
        &primal::script::detail::create_script<TYPE>)};                                                                \
    const u8 _name_##TYPE{primal::script::detail::add_script_name(#TYPE)};                                             \
    }

#else
#define REGISTER_SCRIPT(TYPE)                                                                                          \
    namespace                                                                                                          \
    {                                                                                                                  \
    const u8 _reg_##TYPE{primal::script::detail::register_script(                                                      \
        primal::script::detail::string_hash()(#TYPE),                                                                  \
        &primal::script::detail::create_script<TYPE>)};                                                                \
    }

#endif
}
}
