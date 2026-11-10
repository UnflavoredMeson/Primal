// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "Utilities/FreeList.h"

#include <doctest.h>

using namespace primal;

TEST_SUITE_BEGIN("utl::free_list");

namespace
{

// Larger than a u32: free_list keeps the index of the next free slot in the first four bytes of a removed item.
struct Item
{
    u32 a{0};
    u32 b{0};
};

struct Counted
{
    static inline s32 alive{0};

    DISABLE_COPY_AND_MOVE(Counted);
    explicit Counted(u64 v)
        : value{v}
    {
        ++alive;
    }
    ~Counted()
    {
        --alive;
    }

    u64 value;
};

}

// NOTE: every test removes what it added. A free_list asserts in its destructor that it is empty.

TEST_CASE("a new free list is empty")
{
    const utl::free_list<Item> list;

    CHECK(list.empty());
    CHECK(list.size() == 0U);
    CHECK(list.capacity() == 0U);
}

TEST_CASE("add returns consecutive ids and stores the item")
{
    utl::free_list<Item> list;

    const u32 first{list.add(Item{1, 10})};
    const u32 second{list.add(Item{2, 20})};
    const u32 third{list.add(3U, 30U)};

    CHECK(first == 0U);
    CHECK(second == 1U);
    CHECK(third == 2U);
    CHECK(list.size() == 3U);
    CHECK(list.capacity() == 3U);
    CHECK_FALSE(list.empty());

    CHECK(list[first].a == 1U);
    CHECK(list[second].b == 20U);
    CHECK(list[third].a == 3U);
    CHECK(list[third].b == 30U);

    list.remove(first);
    list.remove(second);
    list.remove(third);
}

TEST_CASE("items can be changed in place")
{
    utl::free_list<Item> list;
    const u32 id{list.add(Item{1, 2})};

    list[id].b = 99;

    const utl::free_list<Item> &const_list{list};
    CHECK(const_list[id].a == 1U);
    CHECK(const_list[id].b == 99U);

    list.remove(id);
}

TEST_CASE("remove frees the slot and keeps the other ids valid")
{
    utl::free_list<Item> list;
    const u32 first{list.add(Item{1, 10})};
    const u32 second{list.add(Item{2, 20})};
    const u32 third{list.add(Item{3, 30})};

    list.remove(second);

    CHECK(list.size() == 2U);
    CHECK(list.capacity() == 3U);
    CHECK(list[first].a == 1U);
    CHECK(list[third].a == 3U);

    list.remove(first);
    list.remove(third);
    CHECK(list.empty());
    CHECK(list.capacity() == 3U);
}

TEST_CASE("freed slots are used again, the last one freed first")
{
    utl::free_list<Item> list;
    for (u32 i{0}; i < 5; ++i)
    {
        CHECK(list.add(Item{i, i}) == i);
    }

    list.remove(1);
    list.remove(3);
    list.remove(0);

    CHECK(list.add(Item{100, 0}) == 0U);
    CHECK(list.add(Item{101, 0}) == 3U);
    CHECK(list.add(Item{102, 0}) == 1U);
    // No free slot left: the list grows.
    CHECK(list.add(Item{103, 0}) == 5U);

    CHECK(list.size() == 6U);
    CHECK(list.capacity() == 6U);
    CHECK(list[0].a == 100U);
    CHECK(list[1].a == 102U);
    CHECK(list[2].a == 2U);
    CHECK(list[3].a == 101U);
    CHECK(list[4].a == 4U);
    CHECK(list[5].a == 103U);

    for (u32 i{0}; i < 6; ++i)
    {
        list.remove(i);
    }
}

TEST_CASE("the list doesn't grow while there are free slots")
{
    utl::free_list<Item> list;
    for (u32 i{0}; i < 64; ++i)
    {
        list.add(Item{i, i});
    }

    for (u32 round{0}; round < 100; ++round)
    {
        for (u32 i{0}; i < 64; i += 2)
        {
            list.remove(i);
        }
        for (u32 i{0}; i < 32; ++i)
        {
            const u32 id{list.add(Item{round, i})};
            CHECK(id < 64U);
            CHECK(id % 2 == 0U);
        }
    }

    CHECK(list.size() == 64U);
    CHECK(list.capacity() == 64U);
    // The odd slots were never touched.
    for (u32 i{1}; i < 64; i += 2)
    {
        CHECK(list[i].a == i);
    }

    for (u32 i{0}; i < 64; ++i)
    {
        list.remove(i);
    }
}

TEST_CASE("the reserving constructor adds no items")
{
    utl::free_list<Item> list{32};

    CHECK(list.empty());
    CHECK(list.capacity() == 0U);

    const u32 id{list.add(Item{1, 2})};
    CHECK(id == 0U);
    list.remove(id);
}

TEST_CASE("add constructs the item and remove destroys it")
{
    REQUIRE(Counted::alive == 0);
    utl::free_list<Counted> list;

    const u32 first{list.add(u64{11})};
    const u32 second{list.add(u64{22})};
    CHECK(Counted::alive == 2);
    CHECK(list[first].value == 11U);
    CHECK(list[second].value == 22U);

    list.remove(first);
    CHECK(Counted::alive == 1);

    // The reused slot holds a newly constructed item.
    const u32 reused{list.add(u64{33})};
    CHECK(reused == first);
    CHECK(Counted::alive == 2);
    CHECK(list[reused].value == 33U);
    CHECK(list[second].value == 22U);

    list.remove(reused);
    list.remove(second);
    CHECK(Counted::alive == 0);
}

TEST_SUITE_END();
