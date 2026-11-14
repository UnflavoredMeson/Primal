// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "Components/Entity.h"
#include "Components/Script.h"
#include "Content/ContentLoader.h"

#include <doctest.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>
#include <vector>

using namespace primal;

// content::load_game() reads "game.bin" from the working directory and creates its entities. The tests write small
// hand-made files into a temporary folder and make it the working directory.
//
// The file: u32 entity_count, then per entity: u32 entity_type (not used yet), u32 component_count, then per
// component: u32 component_type and its data.
//   transform: f32 position[3], f32 rotation[3] (Euler angles: pitch, yaw, roll), f32 scale[3]
//   script:    u32 name_length, char name[name_length] (the name the script was registered under)

namespace
{

// Remembers the entities it was created for, so a test can find what load_game() made.
class LoadedScript : public script::EntityScript
{
  public:
    static inline std::vector<game_entity::EntityId> entities{};

    explicit LoadedScript(game_entity::Entity entity)
        : script::EntityScript{entity}
    {
        entities.push_back(entity.get_id());
    }
    DISABLE_COPY_AND_MOVE(LoadedScript);
    ~LoadedScript() override
    {
        std::erase(entities, get_id());
    }
};

PRIMAL_SCRIPT(LoadedScript);

struct Float3
{
    f32 x, y, z;
};

class GameBlob
{
  public:
    auto u32_value(u32 value) -> GameBlob &
    {
        return append(&value, sizeof(value));
    }

    auto entity(u32 component_count) -> GameBlob &
    {
        constexpr u32 entity_type{0};
        return u32_value(entity_type).u32_value(component_count);
    }

    auto transform(Float3 position, Float3 rotation = {}, Float3 scale = {1.f, 1.f, 1.f}) -> GameBlob &
    {
        u32_value(ComponentType::transform);
        append(&position, sizeof(position));
        append(&rotation, sizeof(rotation));
        return append(&scale, sizeof(scale));
    }

    auto script(std::string_view name) -> GameBlob &
    {
        u32_value(ComponentType::script).u32_value(static_cast<u32>(name.size()));
        return append(name.data(), name.size());
    }

    [[nodiscard]] auto bytes() const -> const std::vector<u8> &
    {
        return _bytes;
    }

  private:
    auto append(const void *data, size_t size) -> GameBlob &
    {
        // NOTE: not vector::insert(): GCC 16 -O2 reports a false -Wstringop-overflow inside it.
        const size_t offset{_bytes.size()};
        _bytes.resize(offset + size);
        std::memcpy(_bytes.data() + offset, data, size);
        return *this;
    }

    std::vector<u8> _bytes;
};

// A temporary folder that is the working directory while the object lives.
class GameFolder
{
  public:
    GameFolder()
        : _previous{std::filesystem::current_path()}
    {
        std::random_device random;
        _folder = std::filesystem::temp_directory_path() / ("primal_unit_tests_" + std::to_string(random()));
        std::filesystem::create_directories(_folder);
        std::filesystem::current_path(_folder);
    }
    DISABLE_COPY_AND_MOVE(GameFolder);
    ~GameFolder()
    {
        std::error_code error;
        std::filesystem::current_path(_previous, error);
        try
        {
            std::filesystem::remove_all(_folder, error);
        }
        catch (...) // NOLINT(bugprone-empty-catch): a folder left in the temporary directory is not worth failing for
        {
        }
    }

    auto write_game(const GameBlob &blob) const -> void
    {
        std::ofstream file{_folder / "game.bin", std::ios::out | std::ios::binary | std::ios::trunc};
        const std::vector<u8> &bytes{blob.bytes()};
        file.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        REQUIRE(file.good());
    }

  private:
    std::filesystem::path _previous;
    std::filesystem::path _folder;
};

auto is_near(f32 a, f32 b) -> bool
{
    return std::abs(a - b) < 1e-4f;
}

}

TEST_SUITE_BEGIN("content::load_game");

TEST_CASE("no game.bin in the working directory")
{
    const GameFolder folder{};

    CHECK_FALSE(content::load_game());
    content::unload_game();
}

TEST_CASE("the entities of game.bin are created with their transforms and scripts")
{
    const GameFolder folder{};
    GameBlob blob{};
    blob.u32_value(3);
    blob.entity(2).transform({1.f, 2.f, 3.f}, {}, {4.f, 5.f, 6.f}).script("LoadedScript");
    // Only a transform.
    blob.entity(1).transform({7.f, 8.f, 9.f});
    // The components in the other order, and a rotation: pitch (around x) of 90 degrees.
    blob.entity(2).script("LoadedScript").transform({-1.f, -2.f, -3.f}, {math::pi * 0.5f, 0.f, 0.f});
    folder.write_game(blob);

    const size_t loaded_before{LoadedScript::entities.size()};
    REQUIRE(content::load_game());
    REQUIRE(LoadedScript::entities.size() == loaded_before + 2);

    const game_entity::Entity first{LoadedScript::entities[loaded_before]};
    const game_entity::Entity third{LoadedScript::entities[loaded_before + 1]};

    CHECK(first.script().is_valid());
    const math::v3 first_position{first.position()};
    CHECK(first_position.x == 1.f);
    CHECK(first_position.y == 2.f);
    CHECK(first_position.z == 3.f);
    const math::v3 first_scale{first.scale()};
    CHECK(first_scale.x == 4.f);
    CHECK(first_scale.y == 5.f);
    CHECK(first_scale.z == 6.f);
    // No rotation is the identity quaternion.
    const math::v4 first_rotation{first.rotation()};
    CHECK(is_near(first_rotation.x, 0.f));
    CHECK(is_near(first_rotation.y, 0.f));
    CHECK(is_near(first_rotation.z, 0.f));
    CHECK(is_near(first_rotation.w, 1.f));

    const math::v3 third_position{third.position()};
    CHECK(third_position.x == -1.f);
    CHECK(third_position.y == -2.f);
    CHECK(third_position.z == -3.f);
    const math::v3 third_scale{third.scale()};
    CHECK(third_scale.x == 1.f);
    CHECK(third_scale.y == 1.f);
    CHECK(third_scale.z == 1.f);
    // The Euler angles became a quaternion: 90 degrees around x is (sin 45, 0, 0, cos 45).
    const math::v4 third_rotation{third.rotation()};
    const f32 half_sqrt2{std::sqrt(0.5f)};
    CHECK(is_near(third_rotation.x, half_sqrt2));
    CHECK(is_near(third_rotation.y, 0.f));
    CHECK(is_near(third_rotation.z, 0.f));
    CHECK(is_near(third_rotation.w, half_sqrt2));

    content::unload_game();
    CHECK(LoadedScript::entities.size() == loaded_before);
}

TEST_CASE("a game can be loaded and unloaded again")
{
    const GameFolder folder{};
    GameBlob blob{};
    blob.u32_value(2);
    blob.entity(2).transform({}).script("LoadedScript");
    blob.entity(2).transform({}).script("LoadedScript");
    folder.write_game(blob);

    const size_t loaded_before{LoadedScript::entities.size()};
    for (u32 round{0}; round < 3; ++round)
    {
        // NOLINTNEXTLINE(misc-const-correctness): the variable is doctest's (reported with the MSVC headers only)
        REQUIRE_MESSAGE(content::load_game(), "round " << round);
        CHECK(LoadedScript::entities.size() == loaded_before + 2);
        content::unload_game();
        CHECK(LoadedScript::entities.size() == loaded_before);
    }

    // Unloading twice removes nothing twice.
    content::unload_game();
    CHECK(LoadedScript::entities.size() == loaded_before);
}

TEST_CASE("a game.bin that can't be used is rejected")
{
    const GameFolder folder{};
    const size_t loaded_before{LoadedScript::entities.size()};
    GameBlob blob{};

    SUBCASE("no entities")
    {
        blob.u32_value(0);
    }

    SUBCASE("an entity without components")
    {
        blob.u32_value(1);
        blob.entity(0);
    }

    SUBCASE("a script without a name")
    {
        blob.u32_value(1);
        blob.entity(2).transform({}).script("");
    }

    SUBCASE("a script that isn't registered")
    {
        blob.u32_value(1);
        blob.entity(2).transform({}).script("NoSuchScript");
    }

    SUBCASE("the entities before the bad one are removed by unload_game")
    {
        blob.u32_value(2);
        blob.entity(2).transform({}).script("LoadedScript");
        blob.entity(2).transform({}).script("NoSuchScript");
    }

    folder.write_game(blob);
    CHECK_FALSE(content::load_game());
    content::unload_game();
    CHECK(LoadedScript::entities.size() == loaded_before);
}

TEST_SUITE_END();
