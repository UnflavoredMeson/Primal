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

// Immediate: only between frames or at the sync point (frame::structural_changes_allowed(); asserted in Debug).
// The frame thread's own code (game setup, the editor, tools) uses these.
auto create(EntityInfo info) -> Entity;
auto remove(EntityId id) -> void;
auto update_component(EntityId id, EntityInfo info, ComponentType::Type type) -> bool;
auto is_alive(EntityId id) -> bool;

// Called on the frame thread when a queued entity has been created. user_data is what was passed to queue_create().
using created_callback = void (*)(Entity entity, void *user_data);

// Deferred: from any thread at any time (scripts, jobs). The entity is created or removed at the next sync point
// (phase 2 of frame::run()), in the order the commands were queued. queue_create() copies the init infos, also the
// geometry's material ids. An entity that is already gone when its removal runs is skipped.
auto queue_create(EntityInfo info, created_callback on_created = nullptr, void *user_data = nullptr) -> void;
auto queue_remove(EntityId id) -> void;

// The sync point: runs the queued commands. A command that queues more (a callback) runs at the next sync point.
auto process_queued_commands() -> void;
}
}
