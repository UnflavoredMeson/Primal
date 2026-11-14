// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "Components/Entity.h"
#include "Components/Script.h"
#include "Components/Transform.h"
#include "Core/Log.h"
#include "ScriptRegistryTests.h"

#include <doctest.h>

#include <string>
#include <vector>

import primal.frame;

using namespace primal;

// NOTE: game scripts are global like the rest of the script system, and every registered one starts at the first
//       frame of any test. So each test here ends them first (script::end_game_scripts()), and the event list is
//       cleared after that.

TEST_SUITE_BEGIN("game script");

namespace
{

// What the scripts below did, in order.
std::vector<std::string> events;

auto run_frame() -> void
{
    frame::run(1.f / 60.f, [] {});
}

class TestGameScript : public script::GameScript
{
  public:
    static inline u32 alive{0};
    static inline frame::Phase update_phase{frame::Phase::between_frames};
    static inline std::string logged_name;
    // When set, begin_play() creates an entity (at the sync point) and update() queues one.
    static inline bool create_entities{false};
    static inline game_entity::EntityId created_directly{id::invalid_id};
    static inline std::vector<game_entity::Entity> queued;

    TestGameScript()
    {
        ++alive;
        events.emplace_back("game: constructor");
    }
    ~TestGameScript() override
    {
        --alive;
    }
    TestGameScript(const TestGameScript &) = delete;
    auto operator=(const TestGameScript &) -> TestGameScript & = delete;
    TestGameScript(TestGameScript &&) = delete;
    auto operator=(TestGameScript &&) -> TestGameScript & = delete;

    auto begin_play() -> void override
    {
        events.emplace_back("game: begin_play");
        logged_name = std::string{script_name()};
        if (create_entities)
        {
            // begin_play() runs at the sync point, where entities may be created directly.
            transform::InitInfo info{.rotation = {0.f, 0.f, 0.f, 1.f}, .scale = {1.f, 1.f, 1.f}};
            created_directly = game_entity::create({.transform = &info}).get_id();
        }
    }
    auto update(f32) -> void override
    {
        events.emplace_back("game: update");
        update_phase = frame::phase();
        if (create_entities && queued.empty())
        {
            transform::InitInfo info{.rotation = {0.f, 0.f, 0.f, 1.f}, .scale = {1.f, 1.f, 1.f}};
            game_entity::queue_create(
                {.transform = &info}, [](game_entity::Entity entity, void *) { queued.push_back(entity); }, nullptr);
        }
    }
    auto end_play() -> void override
    {
        events.emplace_back("game: end_play");
        alive_at_end_play = alive;
    }

    // alive when end_play() ran: the script still exists then.
    static inline u32 alive_at_end_play{0};
};
PRIMAL_GAME_SCRIPT(TestGameScript);

class OrderEntityScript : public script::EntityScript
{
  public:
    explicit OrderEntityScript(game_entity::Entity entity)
        : script::EntityScript{entity}
    {
    }

    auto begin_play() -> void override
    {
        events.emplace_back("entity: begin_play");
    }
    auto update(f32) -> void override
    {
        events.emplace_back("entity: update");
    }
    auto end_play() -> void override
    {
        events.emplace_back("entity: end_play");
    }
};
PRIMAL_SCRIPT(OrderEntityScript);

// Ends the game scripts a previous test may have started, and forgets what they did.
auto fresh_start() -> void
{
    script::end_game_scripts();
    events.clear();
    TestGameScript::create_entities = false;
    TestGameScript::queued.clear();
}

}

TEST_CASE("game scripts start at the first frame, before the entity scripts, and end with end_game_scripts()")
{
    fresh_start();
    REQUIRE(TestGameScript::alive == 0);
    REQUIRE(script::game_script_count() == 0);

    transform::InitInfo info{.rotation = {0.f, 0.f, 0.f, 1.f}, .scale = {1.f, 1.f, 1.f}};
    script::InitInfo script_info{&script::detail::create_script<OrderEntityScript>};
    const game_entity::Entity entity{game_entity::create({.transform = &info, .script = &script_info})};
    REQUIRE(entity.is_valid());
    CHECK(TestGameScript::alive == 0); // nothing starts before a frame

    run_frame();
    CHECK(TestGameScript::alive == 1);
    CHECK(script::game_script_count() >= 1);
    CHECK(
        events == std::vector<std::string>{
                      "game: constructor", "game: begin_play", "entity: begin_play", "game: update", "entity: update"});

    events.clear();
    run_frame();
    CHECK(events == std::vector<std::string>{"game: update", "entity: update"});
    CHECK(TestGameScript::update_phase == frame::Phase::game_update);

    events.clear();
    script::end_game_scripts();
    CHECK(events == std::vector<std::string>{"game: end_play"});
    CHECK(TestGameScript::alive_at_end_play == 1); // end_play() first,
    CHECK(TestGameScript::alive == 0);             // then destroyed
    CHECK(script::game_script_count() == 0);

    // A later frame starts a new game: a new instance.
    events.clear();
    run_frame();
    CHECK(TestGameScript::alive == 1);
    CHECK(
        events == std::vector<std::string>{"game: constructor", "game: begin_play", "game: update", "entity: update"});

    game_entity::remove(entity.get_id());
    script::end_game_scripts();
}

TEST_CASE("a game script creates entities in begin_play() and queues them in update()")
{
    fresh_start();
    TestGameScript::create_entities = true;

    run_frame();
    REQUIRE(id::is_valid(TestGameScript::created_directly));
    CHECK(game_entity::is_alive(TestGameScript::created_directly));
    CHECK(TestGameScript::queued.empty()); // queued in update(), created at the next sync point

    run_frame();
    REQUIRE(TestGameScript::queued.size() == 1);
    CHECK(game_entity::is_alive(TestGameScript::queued[0].get_id()));

    game_entity::remove(TestGameScript::created_directly);
    game_entity::remove(TestGameScript::queued[0].get_id());
    TestGameScript::create_entities = false;
    script::end_game_scripts();
}

TEST_CASE("a game script has its registered name and isn't an entity script")
{
    fresh_start();
    run_frame();
    CHECK(TestGameScript::logged_name == "TestGameScript");
    CHECK(script::detail::find_script_creator("TestGameScript") == nullptr);
    script::end_game_scripts();
}

TEST_CASE("game scripts and entity scripts share their names")
{
    const u32 errors_before{script::detail::script_registration_errors()};
    log::enable_console(false); // the error message is expected
    const u32 index{
        script::detail::register_game_script("OrderEntityScript", &script::detail::create_game_script<TestGameScript>)};
    log::flush();
    log::enable_console(true);
    ++script_registry_test::errors_made_by_tests;
    CHECK(index == u32_invalid_id);
    CHECK(script::detail::script_registration_errors() == errors_before + 1);
    CHECK(
        script::detail::find_script_creator("OrderEntityScript") == &script::detail::create_script<OrderEntityScript>);
}

TEST_SUITE_END();
