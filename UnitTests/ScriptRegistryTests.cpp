// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "ScriptRegistryTests.h"

#include "Components/Script.h"
#include "Core/Log.h"

#include <doctest.h>

#include <string>

using namespace primal;

// NOTE: the registry is global and keeps what the tests add: each test registers names of its own.

TEST_SUITE_BEGIN("script registry");

namespace
{

class RenamedScript : public script::EntityScript
{
  public:
    constexpr explicit RenamedScript(game_entity::Entity entity)
        : script::EntityScript{entity}
    {
    }
};
PRIMAL_SCRIPT_NAMED(RenamedScript, "Renamed");

}

TEST_CASE("script names hash with FNV-1a, the same on every compiler")
{
    // The FNV-1a 64-bit test vectors.
    static_assert(script::detail::script_name_hash("") == 0xcbf29ce484222325ull);
    static_assert(script::detail::script_name_hash("a") == 0xaf63dc4c8601ec8cull);
    static_assert(script::detail::script_name_hash("foobar") == 0x85944171f73967e8ull);
    CHECK(script::detail::script_name_hash("FanScript") != script::detail::script_name_hash("FanScript2"));
}

TEST_CASE("every script of the test program registered without an error")
{
    CHECK(script::detail::script_registration_errors() == script_registry_test::errors_made_by_tests);
}

TEST_CASE("a script is found by its name")
{
    CHECK(
        script::detail::find_script_creator("HeaderScript") ==
        &script::detail::create_script<script_registry_test::HeaderScript>);
    CHECK(script::detail::find_script_creator("NoSuchScript") == nullptr);
    CHECK(script::detail::find_script_creator("") == nullptr);
    // A prefix or a longer name is another name.
    CHECK(script::detail::find_script_creator("HeaderScrip") == nullptr);
    CHECK(script::detail::find_script_creator("HeaderScriptX") == nullptr);
}

TEST_CASE("the registered name is the script's name")
{
    const u32 index{script::detail::script_type_index<script_registry_test::HeaderScript>};
    REQUIRE(index != u32_invalid_id);
    CHECK(std::string{script::detail::script_type_name(index)} == "HeaderScript");
    CHECK(script::detail::script_type_name(u32_invalid_id) == nullptr);
}

TEST_CASE("PRIMAL_SCRIPT_NAMED registers a script under the given name")
{
    CHECK(script::detail::find_script_creator("Renamed") == &script::detail::create_script<RenamedScript>);
    CHECK(script::detail::find_script_creator("RenamedScript") == nullptr);
    const u32 index{script::detail::script_type_index<RenamedScript>};
    REQUIRE(index != u32_invalid_id);
    CHECK(std::string{script::detail::script_type_name(index)} == "Renamed");
}

TEST_CASE("a script registered in a header that two files include is registered once")
{
    // One variable for both files, initialized once: a second registration would have been an error (checked by
    // "every script of the test program registered without an error").
    CHECK(
        script_registry_test::registration_seen_by_other_file() ==
        &script_registry_test::primal_script_registration_HeaderScript);
    CHECK(script::detail::find_script_creator("HeaderScript") != nullptr);
}

TEST_CASE("a duplicate name keeps the first script and counts an error")
{
    const u32 errors_before{script::detail::script_registration_errors()};
    const script::detail::script_creator first{&script::detail::create_script<script_registry_test::HeaderScript>};
    const script::detail::script_creator second{&script::detail::create_script<RenamedScript>};

    const u32 index{script::detail::register_script("DuplicateTestScript", first)};
    CHECK(index != u32_invalid_id);
    // The error message is expected here: keep it out of the test output.
    log::enable_console(false);
    const u32 duplicate{script::detail::register_script("DuplicateTestScript", second)};
    log::flush();
    log::enable_console(true);
    CHECK(duplicate == u32_invalid_id);
    ++script_registry_test::errors_made_by_tests;

    CHECK(script::detail::find_script_creator("DuplicateTestScript") == first);
    CHECK(script::detail::script_registration_errors() == errors_before + 1);
}

TEST_SUITE_END();
