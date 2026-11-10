// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "Common/Id.h"

#include <doctest.h>

using namespace primal;

TEST_SUITE_BEGIN("id");

// The whole header is constexpr, so most of it is checked by the compiler.
static_assert(!id::is_valid(id::invalid_id));
static_assert(id::is_valid(0));
static_assert(id::index(0x0100'0005) == 5);
static_assert(id::generation(0x0100'0005) == 1);
static_assert(id::new_generation(5) == 0x0100'0005);

TEST_CASE("an id is an index in the low bits and a generation in the high bits")
{
    constexpr id::id_type id{(id::id_type{7} << 24) | 1234};

    CHECK(id::index(id) == 1234U);
    CHECK(id::generation(id) == 7U);
    CHECK(id::is_valid(id));
}

TEST_CASE("a new id has generation 0")
{
    constexpr id::id_type id{42};

    CHECK(id::index(id) == 42U);
    CHECK(id::generation(id) == 0U);
}

TEST_CASE("new_generation keeps the index and adds one to the generation")
{
    id::id_type id{0x00ab'cdef};

    for (u32 generation{1}; generation <= id::max_generation; ++generation)
    {
        id = id::new_generation(id);
        CHECK(id::index(id) == 0x00ab'cdefU);
        CHECK(id::generation(id) == generation);
        CHECK(id::is_valid(id));
    }
}

TEST_CASE("the largest generation leaves room for the invalid id")
{
    // All bits set is the invalid id, so the last generation value is never handed out.
    CHECK(id::max_generation == 254U);
    CHECK(id::generation(id::invalid_id) == 255U);
    CHECK_FALSE(id::is_valid(id::invalid_id));
}

TEST_SUITE_END();
