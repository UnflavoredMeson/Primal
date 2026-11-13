// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "../Common/CommonHeaders.h"
#include "../Core/Log.h"
#include "GeometryComponent.h"
#include "ScriptComponent.h"
#include "TransformComponent.h"

#include <format>
#include <memory>
#include <source_location>
#include <string_view>
#include <type_traits>
#include <utility>

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
class EntityScript;

namespace detail
{
template <class script_class>
auto create_script(game_entity::Entity entity) -> std::unique_ptr<EntityScript>;
}

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
    // The class name given to REGISTER_SCRIPT, or "script" for a script made some other way. The names are kept by
    // the copy of the engine library that registered the script: in the editor that is the game DLL's, so call this
    // from the script's own code.
    [[nodiscard]] auto script_name() const -> std::string_view;

    // Logging for scripts: the message gets log::Category::script and the script's name, as in
    // "[    1.2345] [info   ] [script] PlayerScript: picked up 3 coins (PlayerScript.cpp:20)".
    //     log_info("picked up {} coins", count);
    // log::info(log::Category::..., ...) works in scripts too, for messages without the name.
    template <typename... Args>
    auto log_debug(log::detail::Format<std::type_identity_t<Args>...> format, Args &&...args) const -> void
    {
        if (log::Level::debug >= log::compiled_level)
        {
            log_message(log::Level::debug, format.location, std::format(format.format, std::forward<Args>(args)...));
        }
    }
    template <typename... Args>
    auto log_info(log::detail::Format<std::type_identity_t<Args>...> format, Args &&...args) const -> void
    {
        if (log::Level::info >= log::compiled_level)
        {
            log_message(log::Level::info, format.location, std::format(format.format, std::forward<Args>(args)...));
        }
    }
    template <typename... Args>
    auto log_warning(log::detail::Format<std::type_identity_t<Args>...> format, Args &&...args) const -> void
    {
        log_message(log::Level::warning, format.location, std::format(format.format, std::forward<Args>(args)...));
    }
    template <typename... Args>
    auto log_error(log::detail::Format<std::type_identity_t<Args>...> format, Args &&...args) const -> void
    {
        log_message(log::Level::error, format.location, std::format(format.format, std::forward<Args>(args)...));
    }

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

  private:
    template <class script_class>
    friend auto detail::create_script(game_entity::Entity entity) -> std::unique_ptr<EntityScript>;

    auto log_message(log::Level level, const std::source_location &location, std::string_view text) const -> void;

    // Index of the type's name (detail::add_script_type_name()). A u32, not a pointer, so that the class stays 16
    // bytes: scripts with 16-byte aligned members (math::vec) would otherwise be padded (MSVC C4324).
    u32 _script_type{u32_invalid_id};
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

// The class names of the registered script types, by index (EntityScript::script_name()).
auto add_script_type_name(const char *name) -> u32;
[[nodiscard]] auto script_type_name(u32 index) -> const char *;

// The index of each registered script type's name, set by REGISTER_SCRIPT.
template <class script_class>
inline u32 script_type_index{u32_invalid_id};

template <class script_class>
auto set_script_type_name(const char *name) -> u8
{
    script_type_index<script_class> = add_script_type_name(name);
    return 1;
}

template <class script_class>
auto create_script(game_entity::Entity entity) -> script_ptr
{
    assert(entity.is_valid());
    script_ptr script{std::make_unique<script_class>(entity)};
    script->_script_type = script_type_index<script_class>;
    return script;
}

#ifdef USE_WITH_EDITOR
auto add_script_name(const char *name) -> u8;

#define REGISTER_SCRIPT(TYPE)                                                                                          \
    namespace                                                                                                          \
    {                                                                                                                  \
    const u8 _reg_##TYPE{primal::script::detail::register_script(                                                      \
        primal::script::detail::string_hash()(#TYPE),                                                                  \
        &primal::script::detail::create_script<TYPE>)};                                                                \
    const u8 _type_name_##TYPE{primal::script::detail::set_script_type_name<TYPE>(#TYPE)};                             \
    const u8 _name_##TYPE{primal::script::detail::add_script_name(#TYPE)};                                             \
    }

#else
#define REGISTER_SCRIPT(TYPE)                                                                                          \
    namespace                                                                                                          \
    {                                                                                                                  \
    const u8 _reg_##TYPE{primal::script::detail::register_script(                                                      \
        primal::script::detail::string_hash()(#TYPE),                                                                  \
        &primal::script::detail::create_script<TYPE>)};                                                                \
    const u8 _type_name_##TYPE{primal::script::detail::set_script_type_name<TYPE>(#TYPE)};                             \
    }

#endif
}
}
