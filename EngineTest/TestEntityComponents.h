// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once

#include "Test.h"
#include "../Engine/Components/Entity.h"
#include "../Engine/Components/Transform.h"

#include <iostream>
#include <ctime>

using namespace primal;

class EngineTest : public Test
{
  public:
    auto initialize() -> bool override
    {
        std::srand(static_cast<u32>(std::time(nullptr))); // NOLINT(bugprone-random-generator-seed): a test, any seed
        return true;
    }

    auto run() -> void override
    {
        do
        {
            for (u32 i{0}; i < 10000; ++i)
            {
                create_random();
                remove_random();
                _num_entities = static_cast<u32>(_entities.size());
            }
            print_results();
        } while (std::getchar() != 'q');
    }

    auto shutdown() -> void override
    {
    }

  private:
    auto create_random() -> void
    {
        u32 count = std::rand() % 20;
        if (_entities.empty())
        {
            count = 1000;
        }
        transform::InitInfo transform_info{};
        const game_entity::EntityInfo entity_info{
            &transform_info,
        };

        while (count > 0)
        {
            ++_added;
            const game_entity::Entity entity{game_entity::create(entity_info)};
            assert(entity.is_valid() && id::is_valid(entity.get_id()));
            _entities.push_back(entity);
            assert(game_entity::is_alive(entity.get_id()));
            --count;
        }
    }

    auto remove_random() -> void
    {
        u32 count = std::rand() % 20;
        if (_entities.size() < 1000)
        {
            return;
        }
        while (count > 0)
        {
            const u32 index{static_cast<u32>(std::rand()) % static_cast<u32>(_entities.size())};
            const game_entity::Entity entity{_entities[index]};
            assert(entity.is_valid() && id::is_valid(entity.get_id()));
            if (entity.is_valid())
            {
                game_entity::remove(entity.get_id());
                _entities.erase(_entities.begin() + index);
                assert(!game_entity::is_alive(entity.get_id()));
                ++_removed;
            }
            --count;
        }
    }

    auto print_results() const -> void
    {
        std::cout << "Entities created: " << _added << "\n";
        std::cout << "Entities deleted: " << _removed << "\n";
    }

    utl::vector<game_entity::Entity> _entities;

    u32 _added{0};
    u32 _removed{0};
    u32 _num_entities{0};
};
