// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "ComponentsCommon.h"

namespace primal::transform
{

struct InitInfo
{
    f32 position[3]{};
    f32 rotation[4]{};
    f32 scale[3]{1.f, 1.f, 1.f};
};

struct ComponentFlags
{
    enum Flags : u32
    {
        rotation = 0x01,
        position = 0x02,
        scale = 0x04,

        all = rotation | position | scale
    };
};

struct ComponentCache
{
    math::v4 rotation;
    math::v3 position;
    math::v3 scale;
    TransformId id;
    u32 flags;
};

auto create(InitInfo info, game_entity::Entity entity) -> Component;
auto remove(Component c) -> void;

auto get_transform_matrices(const game_entity::EntityId id, math::m4x4 &world, math::m4x4 &inverse_world) -> void;
auto get_updated_components_flags(const game_entity::EntityId *const ids, u32 count, u8 *const flags) -> void;
auto update(const ComponentCache *const cache, u32 count) -> void;
}
