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
auto update(f32 dt) -> void;

}
