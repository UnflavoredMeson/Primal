// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "Script.h"
#include "Entity.h"
#include "Transform.h"

#define USE_TRANSFORM_CACHE_MAP 0

namespace primal::script
{
namespace
{

utl::vector<detail::script_ptr> entity_scripts;
utl::vector<id::id_type> id_mapping;

utl::vector<id::generation_type> generations;
utl::deque<ScriptId> free_ids;

utl::vector<transform::ComponentCache> transform_cache;
#if USE_TRANSFORM_CACHE_MAP
std::unordered_map<id::id_type, u32> cache_map;
#endif

using script_registry = std::unordered_map<size_t, detail::script_creator>;
auto registry() -> script_registry &
{
    // NOTE: we put this static variable in a function because of
    //       the initialization order of static data. This way, we can
    //       be certain that the data is initialized before accessing it.
    static script_registry reg;
    return reg;
}

#ifdef USE_WITH_EDITOR
auto script_names() -> utl::vector<std::string> &
{
    // NOTE: we put this static variable in a function because of
    //       the initialization order of static data. This way, we can
    //       be certain that the data is initialized before accessing it.
    static utl::vector<std::string> names;
    return names;
}
#endif

#if _DEBUG
auto exists(ScriptId id) -> bool
{
    assert(id::is_valid(id));
    const id::id_type index{id::index(id)};
    assert(
        index < generations.size() && !(id::is_valid(id_mapping[index]) && id_mapping[index] >= entity_scripts.size()));
    assert(generations[index] == id::generation(id));
    return (id::is_valid(id_mapping[index]) && generations[index] == id::generation(id)) &&
           entity_scripts[id_mapping[index]] && entity_scripts[id_mapping[index]]->is_valid();
}
#endif

#if USE_TRANSFORM_CACHE_MAP
auto get_cache_ptr(const game_entity::Entity *const entity) -> transform::ComponentCache *const
{
    assert(game_entity::is_alive((*entity).get_id()));
    const transform::TransformId id{(*entity).transform().get_id()};

    u32 index{u32_invalid_id};
    auto pair = cache_map.try_emplace(id, id::invalid_id);

    // cache_map didn't have an entry for this id, new entry inserted
    if (pair.second)
    {
        index = (u32)transform_cache.size();
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
auto get_cache_ptr(const game_entity::Entity *const entity) -> transform::ComponentCache *const
{
    assert(game_entity::is_alive((*entity).get_id()));
    const transform::TransformId id{(*entity).transform().get_id()};

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

auto register_script(size_t tag, script_creator func) -> u8
{
    bool result{registry().insert(script_registry::value_type{tag, func}).second};
    assert(result);
    return result;
}

auto get_script_creator(size_t tag) -> script_creator
{
    auto script = primal::script::registry().find(tag);
    assert(script != primal::script::registry().end() && script->first == tag);
    return script->second;
}

#ifdef USE_WITH_EDITOR
auto add_script_name(const char *name) -> u8
{
    script_names().emplace_back(name);
    return true;
}
#endif // USE_WITH_EDITOR

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
        id = ScriptId{(id::id_type)id_mapping.size()};
        id_mapping.emplace_back();
        generations.push_back(0);
    }

    assert(id::is_valid(id));
    const id::id_type index{(id::id_type)entity_scripts.size()};
    entity_scripts.emplace_back(info.script_creator(entity));
    assert(entity_scripts.back()->get_id() == entity.get_id());
    id_mapping[id::index(id)] = index;
    return Component{id};
}

auto remove(Component c) -> void
{
    assert(c.is_valid() && exists(c.get_id()));
    const ScriptId id{c.get_id()};
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

auto update(f32 dt) -> void
{
    for (const auto &ptr : entity_scripts)
    {
        ptr->update(dt);
    }

    if (transform_cache.size())
    {
        transform::update(transform_cache.data(), (u32)transform_cache.size());
        transform_cache.clear();

#if USE_TRANSFORM_CACHE_MAP
        cache_map.clear();
#endif
    }
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

}

#ifdef USE_WITH_EDITOR
#include <atlsafe.h>

extern "C" __declspec(dllexport) auto get_script_names() -> LPSAFEARRAY
{
    const u32 size{(u32)primal::script::script_names().size()};
    if (!size)
        return nullptr;
    CComSafeArray<BSTR> names(size);
    for (u32 i{0}; i < size; ++i)
    {
        names.SetAt(i, ::A2BSTR_EX(primal::script::script_names()[i].c_str()), false);
    }
    return names.Detach();
}
#endif // USE_WITH_EDITOR
