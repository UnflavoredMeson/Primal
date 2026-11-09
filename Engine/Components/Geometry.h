// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "ComponentsCommon.h"

namespace primal::geometry
{

struct InitInfo
{
    id::id_type geometry_content_id;
    u32 material_count;
    id::id_type *material_ids;
};

auto create(InitInfo info, game_entity::Entity entity) -> Component;
auto remove(Component c) -> void;
auto get_render_item_ids(id::id_type *const item_ids, u32 count) -> void;
auto get_render_item_ids(const id::id_type *const geometry_ids, id::id_type *const item_ids, u32 count) -> void;
auto get_entity_ids(const id::id_type *const geometry_ids, game_entity::EntityId *const entity_ids, u32 count) -> void;
}
