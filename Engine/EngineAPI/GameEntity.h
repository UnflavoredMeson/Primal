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
class GameScript;

namespace detail
{
template <class script_class>
auto create_script(game_entity::Entity entity) -> std::unique_ptr<EntityScript>;
template <class script_class>
auto create_game_script() -> std::unique_ptr<GameScript>;
}

// What every script has: its name and the log helpers. Derive from EntityScript or GameScript, not from this.
class ScriptBase
{
  protected:
    ScriptBase() = default;

    // The name the script was registered under (PRIMAL_SCRIPT, PRIMAL_GAME_SCRIPT), or "script" for a script made
    // some other way. The names are kept by the copy of the engine library that registered the script: in the
    // editor that is the game DLL's, so call this from the script's own code.
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

  private:
    template <class script_class>
    friend auto detail::create_script(game_entity::Entity entity) -> std::unique_ptr<EntityScript>;
    template <class script_class>
    friend auto detail::create_game_script() -> std::unique_ptr<GameScript>;

    auto log_message(log::Level level, const std::source_location &location, std::string_view text) const -> void;

    // Index of the type's name (detail::script_type_name()). A u32, not a pointer, so that EntityScript stays 16
    // bytes: scripts with 16-byte aligned members (math::vec) would otherwise be padded (MSVC C4324).
    u32 _script_type{u32_invalid_id};
};

// A script that belongs to an entity: PRIMAL_SCRIPT(Type).
class EntityScript : public game_entity::Entity, public ScriptBase
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

// A script that belongs to the game, not to an entity: game mode, managers, UI flow. PRIMAL_GAME_SCRIPT(Type).
// Every registered game script runs once per game, on the frame thread:
// - the constructor and begin_play(): at the sync point of the first frame (frame::run()), before the entity
//   scripts that begin play there;
// - update(dt): once per frame, before the entity scripts' update();
// - end_play() and the destructor: when the game ends (script::end_game_scripts(); a game calls it before its
//   entities are removed). A later frame starts them again, as a new game.
// The editor runs no frames, so it never starts them. Entities: create and remove them with
// game_entity::queue_create()/queue_remove() (directly in begin_play() too, which runs at the sync point).
class GameScript : public ScriptBase
{
  public:
    virtual ~GameScript() = default;
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
    GameScript() = default;
};

namespace detail
{
using script_ptr = std::unique_ptr<EntityScript>;
using script_creator = script_ptr (*)(game_entity::Entity entity);
using game_script_ptr = std::unique_ptr<GameScript>;
using game_script_creator = game_script_ptr (*)();

// The key of a script name: FNV-1a, 64 bits. The same on every compiler and standard library, unlike std::hash,
// so that an engine and a game library built with different toolchains find each other's scripts.
[[nodiscard]] constexpr auto script_name_hash(std::string_view name) -> u64
{
    u64 hash{0xcbf29ce484222325ull};
    for (const char c : name)
    {
        hash ^= static_cast<u8>(c);
        hash *= 0x100000001b3ull;
    }
    return hash;
}

// Registers a script type under a name, zero-terminated and alive for the whole program (PRIMAL_SCRIPT passes a
// string literal). Entity and game scripts share the names: one name, one script.
// Returns the type's index (script_type_name()), or u32_invalid_id when the name, or its hash, is taken already:
// then the first registration stays, and the error is logged (in every build).
auto register_script(const char *name, script_creator creator) -> u32;
// The same for a game script (PRIMAL_GAME_SCRIPT).
auto register_game_script(const char *name, game_script_creator creator) -> u32;
// The creator of the entity script registered under this name, or nullptr (also for a game script's name).
[[nodiscard]] auto find_script_creator(std::string_view name) -> script_creator;
// The name of a registered script type, by index (EntityScript::script_name()), or nullptr.
[[nodiscard]] auto script_type_name(u32 index) -> const char *;
// How many registrations failed because of a duplicate name (for tests).
[[nodiscard]] auto script_registration_errors() -> u32;

// The index of each registered script type's name.
template <class script_class>
inline u32 script_type_index{u32_invalid_id};

template <class script_class>
auto create_script(game_entity::Entity entity) -> script_ptr
{
    // Here and not in register_script(): this body is compiled once the class is complete, also when the
    // registration comes before the class (after a forward declaration).
    static_assert(std::is_base_of_v<EntityScript, script_class>, "A script derives from primal::script::EntityScript");
    assert(entity.is_valid());
    script_ptr script{std::make_unique<script_class>(entity)};
    script->_script_type = script_type_index<script_class>;
    return script;
}

template <class script_class>
auto create_game_script() -> game_script_ptr
{
    static_assert(std::is_base_of_v<GameScript, script_class>, "A game script derives from primal::script::GameScript");
    game_script_ptr script{std::make_unique<script_class>()};
    script->_script_type = script_type_index<script_class>;
    return script;
}

template <class script_class>
auto register_game_script(const char *name) -> u8
{
    const u32 index{register_game_script(name, &create_game_script<script_class>)};
    if (index != u32_invalid_id)
    {
        script_type_index<script_class> = index;
    }
    return 1;
}

template <class script_class>
auto register_script(const char *name) -> u8
{
    const u32 index{register_script(name, &create_script<script_class>)};
    if (index != u32_invalid_id)
    {
        script_type_index<script_class> = index;
    }
    return 1;
}
}
}

// Registers a script class under its name, as in
//
//     class FanScript : public primal::script::EntityScript { ... };
//     PRIMAL_SCRIPT(FanScript);
//
// Write it in the class's namespace, with the class name alone (not qualified), after the class or after a forward
// declaration of it. It can stand in a header: the registration is an inline variable, so a header included by
// several files still registers the script once. The name is what game.bin and the editor store: renaming the class
// renames the script, unless it was registered with PRIMAL_SCRIPT_NAMED(FanScript, "Fan"). Names must be unique in
// a game; a duplicate is logged as an error and not registered.
#define PRIMAL_SCRIPT_NAMED(TYPE, NAME)                                                                                \
    [[maybe_unused]] inline const ::u8 primal_script_registration_##TYPE                                               \
    {                                                                                                                  \
        ::primal::script::detail::register_script<TYPE>(NAME)                                                          \
    }
#define PRIMAL_SCRIPT(TYPE) PRIMAL_SCRIPT_NAMED(TYPE, #TYPE)
// The same for a game script (derived from primal::script::GameScript): every registered game script runs while the
// game runs (see GameScript).
#define PRIMAL_GAME_SCRIPT_NAMED(TYPE, NAME)                                                                           \
    [[maybe_unused]] inline const ::u8 primal_script_registration_##TYPE                                               \
    {                                                                                                                  \
        ::primal::script::detail::register_game_script<TYPE>(NAME)                                                     \
    }
#define PRIMAL_GAME_SCRIPT(TYPE) PRIMAL_GAME_SCRIPT_NAMED(TYPE, #TYPE)
