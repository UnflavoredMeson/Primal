// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "ComponentsCommon.h"

namespace primal
{

struct ComponentType
{
    enum Type : u32
    {
        transform,
        script,
        geometry,

        count
    };
};

#define INIT_INFO(component)                                                                                           \
    namespace component                                                                                                \
    {                                                                                                                  \
    struct InitInfo;                                                                                                   \
    }

INIT_INFO(transform);
INIT_INFO(script);
INIT_INFO(geometry);

#undef INIT_INFO

namespace game_entity
{
struct EntityInfo
{
    transform::InitInfo *transform{nullptr};
    script::InitInfo *script{nullptr};
    geometry::InitInfo *geometry{nullptr};
};

auto create(EntityInfo info) -> Entity;
auto remove(EntityId id) -> void;
auto update_component(EntityId id, EntityInfo info, ComponentType::Type type) -> bool;
auto is_alive(EntityId id) -> bool;
}
}
