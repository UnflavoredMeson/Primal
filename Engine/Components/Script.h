// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "ComponentsCommon.h"

namespace primal::script
{

struct InitInfo
{
    detail::script_creator script_creator;
};

auto create(InitInfo info, game_entity::Entity entity) -> Component;
auto remove(Component c) -> void;
// Phase 2 of a frame (the sync point, after the queued entity commands): begin_play() of every script created
// since the last call. Scripts created by a begin_play() begin play in the same call.
auto begin_play_new_scripts() -> void;
// Phase 3 of a frame (frame::run()): every script's update(dt). Transform changes go into per-worker caches.
auto update_scripts(f32 dt) -> void;
// Phase 4: applies the cached transform changes and computes the changed world matrices.
auto apply_transform_changes() -> void;
// begin_play_new_scripts(), update_scripts() and apply_transform_changes(), for code that doesn't run frames
// (tests).
auto update(f32 dt) -> void;

}
