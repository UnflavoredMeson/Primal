// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "Script.h"

#include "../Core/Log.h"
#include "Entity.h"
#include "Transform.h"

#include <string_view>
#include <unordered_map>

#define USE_TRANSFORM_CACHE_MAP 0

import primal.jobs;

namespace primal::script
{
// ScriptBase adds a u32 next to Entity's u32 id: one 16-byte block with the virtual table pointer (see ScriptBase).
static_assert(sizeof(EntityScript) == 16);

namespace
{

utl::vector<detail::script_ptr> entity_scripts;
utl::vector<id::id_type> id_mapping;

utl::vector<id::generation_type> generations;
utl::deque<ScriptId> free_ids;
// Per script id slot: 1 once begin_play() was called (end_play() is called only then).
utl::vector<u8> has_begun_play;
// Scripts created since the last begin_play_new_scripts().
utl::vector<ScriptId> new_scripts;

// The game scripts while the game runs (begin_play_new_scripts() starts them, end_game_scripts() ends them).
utl::vector<detail::game_script_ptr> game_scripts;
bool game_scripts_running{false};

// One cache per job system thread (jobs::worker_index()), so that scripts can later run in parallel without
// locking. They run one after another for now.
utl::vector<utl::vector<transform::ComponentCache>> transform_caches;
#if USE_TRANSFORM_CACHE_MAP
std::unordered_map<id::id_type, u32> cache_map;
#endif

// The registered script types: index = type index (detail::script_type_name()), plus a map from the name's hash.
// In a function because registrations run during static initialization, in no particular order between files:
// this way the registry exists before the first one.
struct ScriptRegistry
{
    struct Type
    {
        const char *name;                         // zero-terminated, alive for the whole program (a string literal)
        detail::script_creator creator;           // an entity script, or
        detail::game_script_creator game_creator; // a game script
    };
    utl::vector<Type> types;
    std::unordered_map<u64, u32> by_hash;
    u32 errors{0};
};

auto registry() -> ScriptRegistry &
{
    static ScriptRegistry instance;
    return instance;
}

// The script of this id hasn't been removed (Release too, unlike exists()).
auto is_alive(ScriptId id) -> bool
{
    const id::id_type index{id::index(id)};
    return index < generations.size() && generations[index] == id::generation(id) && id::is_valid(id_mapping[index]);
}

auto script_of(ScriptId id) -> EntityScript &
{
    return *entity_scripts[id_mapping[id::index(id)]];
}

#if _DEBUG
auto exists(ScriptId id) -> bool
{
    assert(id::is_valid(id));
    const id::id_type index{id::index(id)};
    assert(
        index < generations.size() && (!id::is_valid(id_mapping[index]) || id_mapping[index] < entity_scripts.size()));
    assert(generations[index] == id::generation(id));
    return (id::is_valid(id_mapping[index]) && generations[index] == id::generation(id)) &&
           entity_scripts[id_mapping[index]] && entity_scripts[id_mapping[index]]->is_valid();
}
#endif

// The cache of the calling thread. Only threads of the job system have one.
auto this_thread_cache() -> utl::vector<transform::ComponentCache> &
{
    const u32 worker{jobs::worker_index()};
    assert(worker != u32_invalid_id && "Scripts may only change transforms on a thread of the job system.");
    if (worker >= transform_caches.size())
    {
        // NOTE: only the frame thread gets here (scripts run on it): script::update() sizes the caches for every
        //       thread before scripts could run on others.
        transform_caches.resize(worker + 1);
    }
    return transform_caches[worker];
}

#if USE_TRANSFORM_CACHE_MAP
auto get_cache_ptr(const game_entity::Entity *const entity) -> transform::ComponentCache *
{
    assert(game_entity::is_alive((*entity).get_id()));
    const transform::TransformId id{(*entity).transform().get_id()};
    utl::vector<transform::ComponentCache> &transform_cache{this_thread_cache()};

    u32 index{u32_invalid_id};
    auto pair = cache_map.try_emplace(id, id::invalid_id);

    // cache_map didn't have an entry for this id, new entry inserted
    if (pair.second)
    {
        index = static_cast<u32>(transform_cache.size());
        transform_cache.emplace_back();
        transform_cache.back().id = id;
        cache_map[id] = index;
    }
    else
    {
        index = cache_map[id];
    }

    assert(index < transform_cache.size());
    return &transform_cache[index];
}
#else
auto get_cache_ptr(const game_entity::Entity *const entity) -> transform::ComponentCache *
{
    assert(game_entity::is_alive((*entity).get_id()));
    const transform::TransformId id{(*entity).transform().get_id()};
    utl::vector<transform::ComponentCache> &transform_cache{this_thread_cache()};

    for (auto &cache : transform_cache)
    {
        if (cache.id == id)
        {
            return &cache;
        }
    }

    transform_cache.emplace_back();
    transform_cache.back().id = id;

    return &transform_cache.back();
}
#endif
}

namespace detail
{

namespace
{

auto add_script_type(const char *name, script_creator creator, game_script_creator game_creator) -> u32
{
    assert(name && *name && (creator != nullptr) != (game_creator != nullptr));
    ScriptRegistry &reg{registry()};
    const u32 index{static_cast<u32>(reg.types.size())};
    const auto [entry, inserted]{reg.by_hash.try_emplace(script_name_hash(name), index)};
    if (!inserted)
    {
        // NOTE: this runs during static initialization, before log::initialize(): the log then writes from this
        //       thread, so the message isn't lost (in the editor it goes to the game DLL's stderr).
        ++reg.errors;
        const std::string_view first{reg.types[entry->second].name};
        if (first == std::string_view{name})
        {
            log::error(
                log::Category::script,
                "Two scripts are registered as \"{}\"; the second one is ignored. Give one of them another name: "
                "PRIMAL_SCRIPT_NAMED(Type, \"Name\")",
                name);
        }
        else
        {
            log::error(
                log::Category::script,
                R"(The script names "{}" and "{}" have the same hash; the second one is ignored. Rename one of them)",
                name,
                first);
        }
        return u32_invalid_id;
    }
    reg.types.push_back({.name = name, .creator = creator, .game_creator = game_creator});
    return index;
}

}

auto register_script(const char *name, script_creator creator) -> u32
{
    return add_script_type(name, creator, nullptr);
}

auto register_game_script(const char *name, game_script_creator creator) -> u32
{
    return add_script_type(name, nullptr, creator);
}

auto find_script_creator(std::string_view name) -> script_creator
{
    // NOTE: nullptr for a name that isn't registered (a game.bin or an editor project that names a script the
    //       game code no longer has). The callers check for it.
    const ScriptRegistry &reg{registry()};
    const auto entry{reg.by_hash.find(script_name_hash(name))};
    if (entry == reg.by_hash.end() || std::string_view{reg.types[entry->second].name} != name)
    {
        return nullptr;
    }
    return reg.types[entry->second].creator;
}

auto script_type_name(u32 index) -> const char *
{
    return index < registry().types.size() ? registry().types[index].name : nullptr;
}

auto script_registration_errors() -> u32
{
    return registry().errors;
}

}

auto create(InitInfo info, game_entity::Entity entity) -> Component
{
    assert(entity.is_valid());
    assert(info.script_creator);

    ScriptId id{};
    if (free_ids.size() > id::min_deleted_elements)
    {
        id = free_ids.front();
        assert(!exists(id));
        free_ids.pop_front();
        id = ScriptId{id::new_generation(id)};
        ++generations[id::index(id)];
    }
    else
    {
        id = ScriptId{static_cast<id::id_type>(id_mapping.size())};
        id_mapping.emplace_back();
        generations.push_back(0);
        has_begun_play.push_back(0);
    }
    has_begun_play[id::index(id)] = 0;

    assert(id::is_valid(id));
    const id::id_type index{static_cast<id::id_type>(entity_scripts.size())};
    entity_scripts.emplace_back(info.script_creator(entity));
    assert(entity_scripts.back()->get_id() == entity.get_id());
    id_mapping[id::index(id)] = index;
    new_scripts.push_back(id);
    return Component{id};
}

auto remove(Component c) -> void
{
    assert(c.is_valid() && exists(c.get_id()));
    const ScriptId id{c.get_id()};
    if (has_begun_play[id::index(id)])
    {
        has_begun_play[id::index(id)] = 0;
        // NOTE: before the index is read: end_play() may create or remove other scripts, which moves them.
        script_of(id).end_play();
    }
    const id::id_type index{id_mapping[id::index(id)]};
    const ScriptId last_id{entity_scripts.back()->script().get_id()};
    utl::erase_unordered(entity_scripts, index);
    id_mapping[id::index(last_id)] = index;
    id_mapping[id::index(id)] = id::invalid_id;

    if (generations[id::index(id)] < id::max_generation)
    {
        free_ids.push_back(id);
    }
}

auto start_game_scripts() -> void
{
    assert(game_scripts.empty());
    game_scripts_running = true;
    // Every one is created before the first begin_play(), so that a game script can rely on the others existing.
    for (const ScriptRegistry::Type &type : registry().types)
    {
        if (type.game_creator)
        {
            game_scripts.emplace_back(type.game_creator());
        }
    }
    for (const auto &game_script : game_scripts)
    {
        game_script->begin_play();
    }
}

auto end_game_scripts() -> void
{
    // In reverse order of their start, then destroyed in the same order.
    for (u32 i{static_cast<u32>(game_scripts.size())}; i > 0; --i)
    {
        game_scripts[i - 1]->end_play();
    }
    for (u32 i{static_cast<u32>(game_scripts.size())}; i > 0; --i)
    {
        game_scripts[i - 1].reset();
    }
    game_scripts.clear();
    game_scripts_running = false;
}

auto game_script_count() -> u32
{
    return static_cast<u32>(game_scripts.size());
}

auto begin_play_new_scripts() -> void
{
    if (!game_scripts_running)
    {
        start_game_scripts();
    }

    // By index: a begin_play() may create scripts, which are added to the list and begin play here too.
    for (u32 i{0}; i < new_scripts.size(); ++i)
    {
        const ScriptId id{new_scripts[i]};
        // Removed again before it could begin play: skipped (and no end_play()).
        if (is_alive(id) && !has_begun_play[id::index(id)])
        {
            has_begun_play[id::index(id)] = 1;
            script_of(id).begin_play();
        }
    }
    new_scripts.clear();
}

auto update_scripts(f32 dt) -> void
{
    if (transform_caches.size() < jobs::worker_count())
    {
        transform_caches.resize(jobs::worker_count());
    }

    // NOTE: range-for is safe: during the update phase entities (and their scripts) can only be queued, so neither
    //       list changes here.
    for (const auto &game_script : game_scripts)
    {
        game_script->update(dt);
    }
    for (const auto &ptr : entity_scripts)
    {
        ptr->update(dt);
    }
}

auto apply_transform_changes() -> void
{
    for (utl::vector<transform::ComponentCache> &transform_cache : transform_caches)
    {
        if (!transform_cache.empty())
        {
            transform::update(transform_cache.data(), static_cast<u32>(transform_cache.size()));
            transform_cache.clear();
        }
    }
#if USE_TRANSFORM_CACHE_MAP
    cache_map.clear();
#endif
    transform::update_matrices();
}

auto update(f32 dt) -> void
{
    begin_play_new_scripts();
    update_scripts(dt);
    apply_transform_changes();
}

auto EntityScript::set_rotation(const game_entity::Entity *const entity, math::v4 rotation_quaternion) -> void
{
    transform::ComponentCache &cache{*get_cache_ptr(entity)};
    cache.flags |= transform::ComponentFlags::rotation;
    cache.rotation = rotation_quaternion;
}

auto EntityScript::set_position(const game_entity::Entity *const entity, math::v3 position) -> void
{
    transform::ComponentCache &cache{*get_cache_ptr(entity)};
    cache.flags |= transform::ComponentFlags::position;
    cache.position = position;
}

auto EntityScript::set_scale(const game_entity::Entity *const entity, math::v3 scale) -> void
{
    transform::ComponentCache &cache{*get_cache_ptr(entity)};
    cache.flags |= transform::ComponentFlags::scale;
    cache.scale = scale;
}

auto ScriptBase::script_name() const -> std::string_view
{
    const char *const name{detail::script_type_name(_script_type)};
    return name ? name : "script";
}

auto ScriptBase::log_message(log::Level level, const std::source_location &location, std::string_view text) const
    -> void
{
    const std::string_view name{script_name()};
    log::detail::write(level, log::Category::script, location, "{}: {}", std::make_format_args(name, text));
}

}

#ifdef USE_WITH_EDITOR
#include <atlsafe.h>

// The names of the game's scripts, for the editor's script list.
EDITOR_INTERFACE auto get_script_names() -> LPSAFEARRAY
{
    const auto &types{primal::script::registry().types};
    const u32 size{static_cast<u32>(types.size())};
    if (!size)
    {
        return nullptr;
    }
    CComSafeArray<BSTR> names(size);
    for (u32 i{0}; i < size; ++i)
    {
        names.SetAt(i, ::A2BSTR_EX(types[i].name), false);
    }
    return names.Detach();
}

// The creator of a game's entity script, by name (UTF-8). EngineDLL calls this in the game DLL it loads.
EDITOR_INTERFACE auto primal_get_script_creator(const char *name) -> primal::script::detail::script_creator
{
    return name ? primal::script::detail::find_script_creator(name) : nullptr;
}

// The editor (EngineDLL) calls this in the game DLL it loads, so that the game's log messages reach the editor's log
// (see log::detail::set_forward()).
EDITOR_INTERFACE auto set_log_forward(primal::log::detail::forward_function function) -> void
{
    primal::log::detail::set_forward(function);
}
#endif
