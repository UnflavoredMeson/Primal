// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "ContentLoader.h"
#include "Components/Entity.h"
#include "Components/Transform.h"
#include "Components/Script.h"
#include "Graphics/Renderer.h"

#if !defined(SHIPPING)

#include <fstream>
#include <filesystem>
#include <iterator>

namespace primal::content
{
namespace
{

utl::vector<game_entity::Entity> entities;
transform::InitInfo transform_info{};
script::InitInfo script_info{};

// NOTE: reads 4 bytes. (Reading *data directly only reads the lowest byte of the u32.)
auto read_u32(const u8 *&data) -> u32
{
    u32 value;
    std::memcpy(&value, data, sizeof(u32));
    data += sizeof(u32);
    return value;
}

auto read_transform(const u8 *&data, game_entity::EntityInfo &info) -> bool
{
    using namespace DirectX;
    f32 rotation[3];

    assert(!info.transform);
    std::memcpy(&transform_info.position[0], data, sizeof(transform_info.position));
    data += sizeof(transform_info.position);
    std::memcpy(&rotation[0], data, sizeof(rotation));
    data += sizeof(rotation);
    std::memcpy(&transform_info.scale[0], data, sizeof(transform_info.scale));
    data += sizeof(transform_info.scale);

    const XMFLOAT3A rot{&rotation[0]};
    const XMVECTOR quat{XMQuaternionRotationRollPitchYawFromVector(XMLoadFloat3A(&rot))};
    XMFLOAT4A rot_quat{};
    XMStoreFloat4A(&rot_quat, quat);
    std::memcpy(&transform_info.rotation[0], &rot_quat.x, sizeof(transform_info.rotation));

    info.transform = &transform_info;

    return true;
}

auto read_script(const u8 *&data, game_entity::EntityInfo &info) -> bool
{
    assert(!info.script);
    const u32 name_length{read_u32(data)};
    if (!name_length)
    {
        return false;
    }
    // if a script name is longer than 255 characters then something is probably
    // very wrong, either with the binary writer or the game programmer.
    assert(name_length < 256);
    char script_name[256]{};
    std::memcpy(&script_name[0], data, name_length);
    data += name_length;
    // make the name a zero-terminated c-string.
    script_name[name_length] = 0;
    script_info.script_creator = script::detail::get_script_creator(script::detail::string_hash()(script_name));
    info.script = &script_info;
    return script_info.script_creator != nullptr;
}

auto read_geometry(const u8 *&, game_entity::EntityInfo &) -> bool
{
    return false;
}

using component_reader = bool (*)(const u8 *&, game_entity::EntityInfo &);
component_reader component_readers[]{
    read_transform,
    read_script,
    read_geometry,
};
static_assert(std::size(component_readers) == ComponentType::count);

auto read_file(const std::filesystem::path &path, std::unique_ptr<u8[]> &data, u64 &size) -> bool
{
    if (!std::filesystem::exists(path))
    {
        return false;
    }

    size = std::filesystem::file_size(path);
    assert(size);
    if (!size)
    {
        return false;
    }
    data = std::make_unique<u8[]>(size);
    std::ifstream file{path, std::ios::in | std::ios::binary};
    if (!file || !file.read(reinterpret_cast<char *>(data.get()), size))
    {
        file.close();
        return false;
    }

    file.close();
    return true;
}

}

auto load_game() -> bool
{
    // read game.bin and create the entities.
    std::unique_ptr<u8[]> game_data{};
    u64 size{0};
    if (!read_file("game.bin", game_data, size))
    {
        return false;
    }
    assert(game_data.get());
    const u8 *at{game_data.get()};
    constexpr u32 su32{sizeof(u32)};
    const u32 num_entities{read_u32(at)};
    if (!num_entities)
    {
        return false;
    }

    for (u32 entity_index{0}; entity_index < num_entities; ++entity_index)
    {
        game_entity::EntityInfo info{}; // NOLINT(misc-const-correctness): modified through component_readers[]
        // const u32 entity_type{ *at };
        //  skip over entity type (for now):
        at += su32;
        const u32 num_components{read_u32(at)};
        if (!num_components)
        {
            return false;
        }

        for (u32 component_index{0}; component_index < num_components; ++component_index)
        {
            const u32 component_type{read_u32(at)};
            assert(component_type < ComponentType::count);
            if (!component_readers[component_type](at, info))
            {
                return false;
            }
        }

        assert(info.transform);
        const game_entity::Entity entity{game_entity::create(info)};
        if (!entity.is_valid())
        {
            return false;
        }
        entities.emplace_back(entity);
    }

    assert(at == game_data.get() + size);
    return true;
}

auto unload_game() -> void
{
    for (const auto entity : entities)
    {
        game_entity::remove(entity.get_id());
    }
}

auto load_engine_shaders(std::unique_ptr<u8[]> &shaders, u64 &size) -> bool
{
    auto path = graphics::get_engine_shaders_path();
    return read_file(path, shaders, size);
}

}
#endif
