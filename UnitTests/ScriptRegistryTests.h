// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "EngineAPI/GameEntity.h"

// A script registered in a header that two files include (ScriptRegistryTests.cpp and ScriptRegistryOtherFile.cpp):
// PRIMAL_SCRIPT's inline variable must register it once. In a named namespace: in an unnamed one, each file would get
// a class of its own, and the second registration would be a duplicate name.
namespace script_registry_test
{

class HeaderScript : public primal::script::EntityScript
{
  public:
    constexpr explicit HeaderScript(primal::game_entity::Entity entity)
        : primal::script::EntityScript{entity}
    {
    }
};
PRIMAL_SCRIPT(HeaderScript);

// Registration errors that tests cause on purpose (duplicate names), in every test file: the start-up check
// ("every script of the test program registered without an error") subtracts them.
inline u32 errors_made_by_tests{0};

// The address of HeaderScript's registration variable as the other file sees it.
[[nodiscard]] auto registration_seen_by_other_file() -> const u8 *;

}
