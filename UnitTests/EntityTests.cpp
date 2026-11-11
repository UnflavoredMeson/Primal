// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "Components/Entity.h"
#include "Components/Script.h"
#include "Components/Transform.h"

#include <doctest.h>

#include <string>

using namespace primal;

// NOTE: the entity, transform and script systems are global and keep their state between test cases, so no test
//       relies on a particular id or on the systems being empty. Every test removes the entities it creates.

namespace
{

constexpr f32 identity_rotation[4]{0.f, 0.f, 0.f, 1.f};

auto create_entity(math::v3 position = {}, math::v3 scale = {1.f, 1.f, 1.f}) -> game_entity::Entity
{
    transform::InitInfo transform_info{
        .position = {position.x, position.y, position.z},
        .rotation = {identity_rotation[0], identity_rotation[1], identity_rotation[2], identity_rotation[3]},
        .scale = {scale.x, scale.y, scale.z},
    };
    return game_entity::create({.transform = &transform_info});
}

auto is_equal(const math::v3 &a, const math::v3 &b) -> bool
{
    return math::is_equal(a.x, b.x) && math::is_equal(a.y, b.y) && math::is_equal(a.z, b.z);
}

// Moves its entity every update and counts the calls.
class MovingScript : public script::EntityScript
{
  public:
    static inline u32 update_count{0};
    static inline u32 alive{0};

    explicit MovingScript(game_entity::Entity entity)
        : script::EntityScript{entity}
    {
        ++alive;
    }
    DISABLE_COPY_AND_MOVE(MovingScript);
    ~MovingScript() override
    {
        --alive;
    }

    auto update(f32 dt) -> void override
    {
        ++update_count;
        const math::v3 p{position()};
        set_position({p.x + dt, p.y, p.z});
    }
};

REGISTER_SCRIPT(MovingScript);

auto create_scripted_entity() -> game_entity::Entity
{
    transform::InitInfo transform_info{.rotation = {0.f, 0.f, 0.f, 1.f}};
    script::InitInfo script_info{&script::detail::create_script<MovingScript>};
    return game_entity::create({.transform = &transform_info, .script = &script_info});
}

}

TEST_SUITE_BEGIN("game_entity");

TEST_CASE("a default entity is invalid")
{
    constexpr game_entity::Entity entity{};

    CHECK_FALSE(entity.is_valid());
    CHECK_FALSE(id::is_valid(entity.get_id()));
}

TEST_CASE("an entity needs a transform")
{
#ifdef NDEBUG
    // Debug asserts instead.
    CHECK_FALSE(game_entity::create({}).is_valid());
#endif
}

TEST_CASE("a created entity is alive until it is removed")
{
    const game_entity::Entity entity{create_entity()};

    REQUIRE(entity.is_valid());
    CHECK(game_entity::is_alive(entity.get_id()));
    CHECK(entity.transform().is_valid());
    CHECK_FALSE(entity.script().is_valid());
    CHECK_FALSE(entity.geometry().is_valid());

    game_entity::remove(entity.get_id());
    CHECK_FALSE(game_entity::is_alive(entity.get_id()));
}

TEST_CASE("entities that are alive have different ids")
{
    constexpr u32 count{100};
    utl::vector<game_entity::Entity> entities;
    for (u32 i{0}; i < count; ++i)
    {
        entities.push_back(create_entity());
    }

    for (u32 i{0}; i < count; ++i)
    {
        CHECK(game_entity::is_alive(entities[i].get_id()));
        for (u32 j{i + 1}; j < count; ++j)
        {
            CHECK(id::id_type{entities[i].get_id()} != id::id_type{entities[j].get_id()});
        }
    }

    for (const auto &entity : entities)
    {
        game_entity::remove(entity.get_id());
    }
}

TEST_CASE("removing an entity leaves the others alone")
{
    const game_entity::Entity first{create_entity({1.f, 0.f, 0.f})};
    const game_entity::Entity second{create_entity({2.f, 0.f, 0.f})};
    const game_entity::Entity third{create_entity({3.f, 0.f, 0.f})};

    game_entity::remove(second.get_id());

    CHECK(game_entity::is_alive(first.get_id()));
    CHECK_FALSE(game_entity::is_alive(second.get_id()));
    CHECK(game_entity::is_alive(third.get_id()));
    CHECK(is_equal(first.position(), {1.f, 0.f, 0.f}));
    CHECK(is_equal(third.position(), {3.f, 0.f, 0.f}));

    game_entity::remove(first.get_id());
    game_entity::remove(third.get_id());
}

TEST_CASE("a slot is used again with a new generation, and the old id stays dead")
{
    // Slots are only reused once more than min_deleted_elements ids are waiting, oldest first.
    constexpr u32 count{id::min_deleted_elements + 500};
    utl::vector<game_entity::Entity> old_entities;
    for (u32 i{0}; i < count; ++i)
    {
        old_entities.push_back(create_entity());
    }
    for (const auto &entity : old_entities)
    {
        game_entity::remove(entity.get_id());
    }

    utl::vector<game_entity::Entity> new_entities;
    for (u32 i{0}; i < 400; ++i)
    {
        const game_entity::Entity entity{create_entity()};
        REQUIRE(entity.is_valid());
        CHECK(game_entity::is_alive(entity.get_id()));
        CHECK(id::generation(entity.get_id()) > 0U);
        new_entities.push_back(entity);
    }

    // Every old id is dead, including those whose slot now holds a new entity.
    u32 reused_slots{0};
    for (const auto &old_entity : old_entities)
    {
        CHECK_FALSE(game_entity::is_alive(old_entity.get_id()));
        for (const auto &new_entity : new_entities)
        {
            if (id::index(old_entity.get_id()) == id::index(new_entity.get_id()))
            {
                CHECK(id::generation(new_entity.get_id()) > id::generation(old_entity.get_id()));
                ++reused_slots;
            }
        }
    }
    CHECK(reused_slots > 0U);

    for (const auto &entity : new_entities)
    {
        game_entity::remove(entity.get_id());
    }
}

TEST_CASE("generations never pass the maximum, however often slots are reused")
{
    // Enough rounds for every slot in the queue to run out of generations. A slot that reaches the last
    // generation is retired and new slots take over.
    constexpr u32 rounds{(id::min_deleted_elements + 1) * (id::max_generation + 2U)};
    u32 over_the_maximum{0};
    u32 dead_on_arrival{0};

    for (u32 i{0}; i < rounds; ++i)
    {
        const game_entity::Entity entity{create_entity()};
        if (id::generation(entity.get_id()) > id::max_generation)
        {
            ++over_the_maximum;
        }
        if (!entity.is_valid() || !game_entity::is_alive(entity.get_id()))
        {
            ++dead_on_arrival;
        }
        game_entity::remove(entity.get_id());
    }

    CHECK(over_the_maximum == 0U);
    CHECK(dead_on_arrival == 0U);
}

TEST_SUITE_END();

TEST_SUITE_BEGIN("transform");

TEST_CASE("an entity starts with the transform it was created with")
{
    transform::InitInfo info{
        .position = {1.f, 2.f, 3.f},
        .rotation = {0.f, 0.f, 0.f, 1.f},
        .scale = {4.f, 5.f, 6.f},
    };
    const game_entity::Entity entity{game_entity::create({.transform = &info})};
    REQUIRE(entity.is_valid());

    CHECK(is_equal(entity.position(), {1.f, 2.f, 3.f}));
    CHECK(is_equal(entity.scale(), {4.f, 5.f, 6.f}));
    const math::v4 rotation{entity.rotation()};
    CHECK(rotation.x == 0.f);
    CHECK(rotation.y == 0.f);
    CHECK(rotation.z == 0.f);
    CHECK(rotation.w == 1.f);

    game_entity::remove(entity.get_id());
}

TEST_CASE("the default scale is one")
{
    constexpr transform::InitInfo info{};

    CHECK(info.scale[0] == 1.f);
    CHECK(info.scale[1] == 1.f);
    CHECK(info.scale[2] == 1.f);
}

TEST_CASE("the local frame follows the rotation")
{
    SUBCASE("no rotation: right is +x, up is +y, front is +z")
    {
        const game_entity::Entity entity{create_entity()};

        CHECK(is_equal(entity.right(), {1.f, 0.f, 0.f}));
        CHECK(is_equal(entity.up(), {0.f, 1.f, 0.f}));
        CHECK(is_equal(entity.front(), {0.f, 0.f, 1.f}));

        const math::m3x3 frame{entity.local_frame()};
        CHECK(math::is_equal(frame._11, 1.f));
        CHECK(math::is_equal(frame._22, 1.f));
        CHECK(math::is_equal(frame._33, 1.f));
        CHECK(math::is_equal(frame._12, 0.f));

        game_entity::remove(entity.get_id());
    }

    SUBCASE("a quarter turn around +y: right is -z, up is +y, front is +x")
    {
        const f32 half_angle{math::half_pi * 0.5f};
        transform::InitInfo info{
            .rotation = {0.f, DirectX::XMScalarSin(half_angle), 0.f, DirectX::XMScalarCos(half_angle)}};
        const game_entity::Entity entity{game_entity::create({.transform = &info})};
        REQUIRE(entity.is_valid());

        CHECK(is_equal(entity.right(), {0.f, 0.f, -1.f}));
        CHECK(is_equal(entity.up(), {0.f, 1.f, 0.f}));
        CHECK(is_equal(entity.front(), {1.f, 0.f, 0.f}));

        game_entity::remove(entity.get_id());
    }
}

TEST_CASE("the world matrix holds scale and position; the inverse leaves the position out")
{
    const game_entity::Entity entity{create_entity({1.f, 2.f, 3.f}, {2.f, 4.f, 8.f})};
    REQUIRE(entity.is_valid());

    math::m4x4 world{};
    math::m4x4 inverse_world{};
    transform::get_transform_matrices(entity.get_id(), world, inverse_world);

    CHECK(math::is_equal(world._11, 2.f));
    CHECK(math::is_equal(world._22, 4.f));
    CHECK(math::is_equal(world._33, 8.f));
    CHECK(math::is_equal(world._41, 1.f));
    CHECK(math::is_equal(world._42, 2.f));
    CHECK(math::is_equal(world._43, 3.f));
    CHECK(math::is_equal(world._44, 1.f));

    CHECK(math::is_equal(inverse_world._11, 0.5f));
    CHECK(math::is_equal(inverse_world._22, 0.25f));
    CHECK(math::is_equal(inverse_world._33, 0.125f));
    CHECK(math::is_equal(inverse_world._41, 0.f));
    CHECK(math::is_equal(inverse_world._42, 0.f));
    CHECK(math::is_equal(inverse_world._43, 0.f));

    game_entity::remove(entity.get_id());
}

TEST_CASE("a reused slot gets the new entity's transform")
{
    constexpr u32 count{id::min_deleted_elements + 100};
    utl::vector<game_entity::Entity> entities;
    for (u32 i{0}; i < count; ++i)
    {
        entities.push_back(create_entity({9.f, 9.f, 9.f}, {9.f, 9.f, 9.f}));
    }
    for (const auto &entity : entities)
    {
        game_entity::remove(entity.get_id());
    }

    const game_entity::Entity entity{create_entity({1.f, 2.f, 3.f})};
    REQUIRE(id::generation(entity.get_id()) > 0U);

    CHECK(is_equal(entity.position(), {1.f, 2.f, 3.f}));
    CHECK(is_equal(entity.scale(), {1.f, 1.f, 1.f}));

    math::m4x4 world{};
    math::m4x4 inverse_world{};
    transform::get_transform_matrices(entity.get_id(), world, inverse_world);
    CHECK(math::is_equal(world._11, 1.f));
    CHECK(math::is_equal(world._41, 1.f));

    game_entity::remove(entity.get_id());
}

TEST_CASE("transform::update applies only the flagged parts")
{
    const game_entity::Entity entity{create_entity({1.f, 2.f, 3.f})};
    REQUIRE(entity.is_valid());

    const transform::ComponentCache cache{
        .rotation = {1.f, 0.f, 0.f, 0.f},
        .position = {7.f, 8.f, 9.f},
        .scale = {5.f, 5.f, 5.f},
        .id = entity.transform().get_id(),
        .flags = transform::ComponentFlags::position | transform::ComponentFlags::scale,
    };
    transform::update(&cache, 1);

    CHECK(is_equal(entity.position(), {7.f, 8.f, 9.f}));
    CHECK(is_equal(entity.scale(), {5.f, 5.f, 5.f}));
    // The rotation wasn't flagged.
    CHECK(entity.rotation().w == 1.f);
    CHECK(entity.rotation().x == 0.f);

    SUBCASE("the matrices follow")
    {
        math::m4x4 world{};
        math::m4x4 inverse_world{};
        transform::get_transform_matrices(entity.get_id(), world, inverse_world);
        CHECK(math::is_equal(world._11, 5.f));
        CHECK(math::is_equal(world._41, 7.f));
    }

    SUBCASE("the change is reported once per frame")
    {
        const game_entity::EntityId id{entity.get_id()};
        u8 flags{0};
        transform::get_updated_components_flags(&id, 1, &flags);
        CHECK((flags & transform::ComponentFlags::position) != 0);
        CHECK((flags & transform::ComponentFlags::scale) != 0);

        // The next update starts a new frame: only its own changes are reported after it.
        const transform::ComponentCache next{
            .rotation = {},
            .position = {},
            .scale = {2.f, 2.f, 2.f},
            .id = entity.transform().get_id(),
            .flags = transform::ComponentFlags::scale,
        };
        transform::update(&next, 1);
        transform::get_updated_components_flags(&id, 1, &flags);
        CHECK(flags == transform::ComponentFlags::scale);
    }

    game_entity::remove(entity.get_id());
}

TEST_SUITE_END();

TEST_SUITE_BEGIN("script");

TEST_CASE("REGISTER_SCRIPT registers the class under the hash of its name")
{
    const script::detail::script_creator creator{
        script::detail::get_script_creator(script::detail::string_hash()("MovingScript"))};

    CHECK(creator == &script::detail::create_script<MovingScript>);
}

TEST_CASE("an entity with a script owns one script instance")
{
    const u32 alive_before{MovingScript::alive};

    const game_entity::Entity entity{create_scripted_entity()};
    REQUIRE(entity.is_valid());
    CHECK(entity.script().is_valid());
    CHECK(MovingScript::alive == alive_before + 1);

    game_entity::remove(entity.get_id());
    CHECK(MovingScript::alive == alive_before);
}

TEST_CASE("script::update runs every script and then applies their transform changes")
{
    const game_entity::Entity first{create_scripted_entity()};
    const game_entity::Entity second{create_scripted_entity()};
    const game_entity::Entity plain{create_entity()};
    const u32 updates_before{MovingScript::update_count};

    script::update(0.5f);

    CHECK(MovingScript::update_count == updates_before + 2);
    CHECK(is_equal(first.position(), {0.5f, 0.f, 0.f}));
    CHECK(is_equal(second.position(), {0.5f, 0.f, 0.f}));
    CHECK(is_equal(plain.position(), {0.f, 0.f, 0.f}));

    script::update(0.25f);
    CHECK(is_equal(first.position(), {0.75f, 0.f, 0.f}));

    SUBCASE("a removed entity's script no longer runs")
    {
        game_entity::remove(first.get_id());
        const u32 updates{MovingScript::update_count};

        script::update(1.f);

        CHECK(MovingScript::update_count == updates + 1);
        CHECK(is_equal(second.position(), {1.75f, 0.f, 0.f}));
        game_entity::remove(second.get_id());
    }

    SUBCASE("removing in the other order")
    {
        game_entity::remove(second.get_id());
        const u32 updates{MovingScript::update_count};

        script::update(1.f);

        CHECK(MovingScript::update_count == updates + 1);
        CHECK(is_equal(first.position(), {1.75f, 0.f, 0.f}));
        game_entity::remove(first.get_id());
    }

    game_entity::remove(plain.get_id());
}

TEST_CASE("update_component adds, replaces and removes a script")
{
    const game_entity::Entity entity{create_entity()};
    const u32 alive_before{MovingScript::alive};
    script::InitInfo script_info{&script::detail::create_script<MovingScript>};

    CHECK(game_entity::update_component(entity.get_id(), {.script = &script_info}, ComponentType::script));
    CHECK(entity.script().is_valid());
    CHECK(MovingScript::alive == alive_before + 1);

    // Replacing destroys the old instance.
    CHECK(game_entity::update_component(entity.get_id(), {.script = &script_info}, ComponentType::script));
    CHECK(MovingScript::alive == alive_before + 1);

    // No script in the info: the component is removed, which update_component reports as false.
    CHECK_FALSE(game_entity::update_component(entity.get_id(), {}, ComponentType::script));
    CHECK_FALSE(entity.script().is_valid());
    CHECK(MovingScript::alive == alive_before);

    game_entity::remove(entity.get_id());
}

TEST_CASE("script slots are used again after many scripted entities are removed")
{
    constexpr u32 count{id::min_deleted_elements + 200};
    const u32 alive_before{MovingScript::alive};

    for (u32 round{0}; round < 3; ++round)
    {
        utl::vector<game_entity::Entity> entities;
        for (u32 i{0}; i < count; ++i)
        {
            entities.push_back(create_scripted_entity());
        }
        CHECK(MovingScript::alive == alive_before + count);

        const u32 updates_before{MovingScript::update_count};
        script::update(1.f);
        CHECK(MovingScript::update_count == updates_before + count);

        // Remove from the middle outwards, so the script system's swap-with-last bookkeeping is exercised.
        for (u32 i{0}; i < count; ++i)
        {
            const u32 index{(i % 2) ? (count / 2 + i / 2) : (count / 2 - 1 - i / 2)};
            CHECK(entities[index].script().is_valid());
            game_entity::remove(entities[index].get_id());
        }
        CHECK(MovingScript::alive == alive_before);
    }
}

TEST_SUITE_END();
