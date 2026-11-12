// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "Components/Entity.h"
#include "Components/Script.h"
#include "Components/Transform.h"
#include "Core/Log.h"

#include <doctest.h>

#include <algorithm>
#include <vector>

import primal.frame;
import primal.jobs;

using namespace primal;

// NOTE: entities and scripts are global (see EntityTests.cpp): every test removes what it creates. Entity creation
//       goes through the queue here, so a test runs one more frame to remove what it made.

TEST_SUITE_BEGIN("frame");

namespace
{

auto transform_info(math::v3 position = {}) -> transform::InitInfo
{
    return {
        .position = {position.x, position.y, position.z},
        .rotation = {0.f, 0.f, 0.f, 1.f},
        .scale = {1.f, 1.f, 1.f},
    };
}

auto run_frame() -> void
{
    frame::run(1.f / 60.f, [] {});
}

// Records the phase its update() runs in, and moves its entity.
class PhaseScript : public script::EntityScript
{
  public:
    static inline frame::Phase seen_phase{frame::Phase::between_frames};

    explicit PhaseScript(game_entity::Entity entity)
        : script::EntityScript{entity}
    {
    }

    auto update(f32) -> void override
    {
        seen_phase = frame::phase();
        set_position({5.f, 6.f, 7.f});
    }
};
REGISTER_SCRIPT(PhaseScript);

// Removes its own entity in its first update().
class SelfRemovingScript : public script::EntityScript
{
  public:
    static inline u32 alive{0};

    explicit SelfRemovingScript(game_entity::Entity entity)
        : script::EntityScript{entity}
    {
        ++alive;
    }
    DISABLE_COPY_AND_MOVE(SelfRemovingScript);
    ~SelfRemovingScript() override
    {
        --alive;
    }

    auto update(f32) -> void override
    {
        game_entity::queue_remove(get_id());
    }
};
REGISTER_SCRIPT(SelfRemovingScript);

// Queues one new entity in its first update().
class SpawningScript : public script::EntityScript
{
  public:
    static inline std::vector<game_entity::Entity> spawned;

    explicit SpawningScript(game_entity::Entity entity)
        : script::EntityScript{entity}
    {
    }

    auto update(f32) -> void override
    {
        if (_has_spawned)
        {
            return;
        }
        _has_spawned = true;
        transform::InitInfo info{transform_info({1.f, 2.f, 3.f})};
        game_entity::queue_create(
            {.transform = &info}, [](game_entity::Entity entity, void *) { spawned.push_back(entity); });
    }

  private:
    bool _has_spawned{false};
};
REGISTER_SCRIPT(SpawningScript);

auto collect(game_entity::Entity entity, void *user_data) -> void
{
    static_cast<std::vector<game_entity::Entity> *>(user_data)->push_back(entity);
}

}

TEST_CASE("frame::run() goes through the phases in order and counts the frame")
{
    transform::InitInfo info{transform_info()};
    script::InitInfo script_info{&script::detail::create_script<PhaseScript>};
    const game_entity::Entity entity{game_entity::create({.transform = &info, .script = &script_info})};
    REQUIRE(entity.is_valid());

    CHECK(frame::phase() == frame::Phase::between_frames);
    CHECK(frame::structural_changes_allowed());
    const u64 index{frame::index()};

    frame::Phase render_phase{frame::Phase::between_frames};
    bool changes_allowed_in_render{true};
    math::m4x4 world{};
    frame::run(
        0.1f,
        [&]
        {
            render_phase = frame::phase();
            changes_allowed_in_render = frame::structural_changes_allowed();
            // The script's transform change of this frame is applied, with its matrix, before rendering.
            math::m4x4 inverse_world{};
            transform::get_transform_matrices(entity.get_id(), world, inverse_world);
        });

    CHECK(PhaseScript::seen_phase == frame::Phase::game_update);
    CHECK(render_phase == frame::Phase::render);
    CHECK_FALSE(changes_allowed_in_render);
    CHECK(math::is_equal(world._41, 5.f));
    CHECK(math::is_equal(world._43, 7.f));
    CHECK(frame::phase() == frame::Phase::between_frames);
    CHECK(frame::index() == index + 1);

    game_entity::remove(entity.get_id());
}

TEST_CASE("a queued entity is created at the next sync point, with copies of its init infos")
{
    std::vector<game_entity::Entity> created;
    {
        // The init info goes out of scope before the entity is made: the queue keeps a copy.
        transform::InitInfo info{transform_info({4.f, 5.f, 6.f})};
        game_entity::queue_create({.transform = &info}, &collect, &created);
    }
    CHECK(created.empty());

    run_frame();
    REQUIRE(created.size() == 1);
    const game_entity::Entity entity{created[0]};
    REQUIRE(entity.is_valid());
    CHECK(game_entity::is_alive(entity.get_id()));
    CHECK(math::is_equal(entity.position().y, 5.f));

    game_entity::queue_remove(entity.get_id());
    CHECK(game_entity::is_alive(entity.get_id()));
    run_frame();
    CHECK_FALSE(game_entity::is_alive(entity.get_id()));
}

TEST_CASE("a script can remove its own entity")
{
    transform::InitInfo info{transform_info()};
    script::InitInfo script_info{&script::detail::create_script<SelfRemovingScript>};
    const game_entity::Entity entity{game_entity::create({.transform = &info, .script = &script_info})};
    REQUIRE(SelfRemovingScript::alive == 1);

    // Frame 1: the script queues the removal; frame 2: its sync point removes the entity and the script.
    run_frame();
    CHECK(game_entity::is_alive(entity.get_id()));
    run_frame();
    CHECK_FALSE(game_entity::is_alive(entity.get_id()));
    CHECK(SelfRemovingScript::alive == 0);
}

TEST_CASE("an entity a script creates exists from the next frame")
{
    transform::InitInfo info{transform_info()};
    script::InitInfo script_info{&script::detail::create_script<SpawningScript>};
    const game_entity::Entity spawner{game_entity::create({.transform = &info, .script = &script_info})};
    SpawningScript::spawned.clear();

    run_frame();
    CHECK(SpawningScript::spawned.empty());
    run_frame();
    REQUIRE(SpawningScript::spawned.size() == 1);
    CHECK(math::is_equal(SpawningScript::spawned[0].position().z, 3.f));

    game_entity::queue_remove(SpawningScript::spawned[0].get_id());
    game_entity::queue_remove(spawner.get_id());
    run_frame();
    CHECK_FALSE(game_entity::is_alive(SpawningScript::spawned[0].get_id()));
    CHECK_FALSE(game_entity::is_alive(spawner.get_id()));
}

TEST_CASE("a removal of an entity that is already gone is skipped")
{
    transform::InitInfo info{transform_info()};
    const game_entity::Entity entity{game_entity::create({.transform = &info})};
    game_entity::queue_remove(entity.get_id());
    game_entity::queue_remove(entity.get_id());
    run_frame();
    CHECK_FALSE(game_entity::is_alive(entity.get_id()));
}

TEST_CASE("jobs on several threads can queue entity commands")
{
    const log::Level level{log::level()};
    log::set_level(log::Level::warning);
    REQUIRE(jobs::initialize(4));

    // The callbacks run at the sync point, on the frame thread, so they can share one vector.
    constexpr u32 count{1000};
    std::vector<game_entity::Entity> created;
    jobs::parallel_for(
        count,
        50,
        [&created](u32 begin, u32 end)
        {
            for (u32 i{begin}; i < end; ++i)
            {
                transform::InitInfo info{transform_info({static_cast<f32>(i), 0.f, 0.f})};
                game_entity::queue_create({.transform = &info}, &collect, &created);
            }
        });
    CHECK(created.empty());

    run_frame();
    REQUIRE(created.size() == count);
    std::vector<u32> seen(count, 0);
    u32 alive{0};
    for (const game_entity::Entity entity : created)
    {
        alive += game_entity::is_alive(entity.get_id()) ? 1 : 0;
        const auto x{static_cast<u32>(entity.position().x)};
        if (x < count)
        {
            ++seen[x];
        }
    }
    CHECK(alive == count);
    CHECK(std::ranges::all_of(seen, [](u32 n) { return n == 1; }));

    jobs::parallel_for(
        count,
        50,
        [&created](u32 begin, u32 end)
        {
            for (u32 i{begin}; i < end; ++i)
            {
                game_entity::queue_remove(created[i].get_id());
            }
        });
    run_frame();
    alive = 0;
    for (const game_entity::Entity entity : created)
    {
        alive += game_entity::is_alive(entity.get_id()) ? 1 : 0;
    }
    CHECK(alive == 0);

    jobs::shutdown();
    log::set_level(level);
}

TEST_SUITE_END();
