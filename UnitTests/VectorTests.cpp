// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "Utilities/Utilities.h"

#include <doctest.h>

#include <string>
#include <utility>

using namespace primal;

TEST_SUITE_BEGIN("utl::vector");

namespace
{

// Counts its live instances. It has a move constructor and isn't trivially copyable, so utl::vector relocates it
// by moving (not with realloc()/memcpy()).
struct Counted
{
    static inline s32 alive{0};

    explicit Counted(u32 v = 0)
        : value{v}
    {
        ++alive;
    }
    Counted(const Counted &o)
        : value{o.value}
    {
        ++alive;
    }
    Counted(Counted &&o) noexcept
        : value{o.value}
    {
        ++alive;
    }
    auto operator=(const Counted &) -> Counted & = default;
    auto operator=(Counted &&) noexcept -> Counted & = default;
    ~Counted()
    {
        --alive;
    }

    u32 value;
};

// Counts its live instances and can't be copied or moved: utl::vector relocates it bitwise.
struct Pinned
{
    static inline s32 alive{0};

    DISABLE_COPY_AND_MOVE(Pinned);
    explicit Pinned(u32 v)
        : value{v}
    {
        ++alive;
    }
    ~Pinned()
    {
        --alive;
    }

    u32 value;
};

template <typename T>
auto values_are(const utl::vector<T> &v, std::initializer_list<u32> expected) -> bool
{
    if (v.size() != expected.size())
    {
        return false;
    }
    u64 i{0};
    for (const u32 value : expected)
    {
        if (v[i].value != value)
        {
            return false;
        }
        ++i;
    }
    return true;
}

auto make_strings() -> utl::vector<std::string>
{
    utl::vector<std::string> v;
    v.emplace_back("one");
    v.emplace_back("two");
    return v;
}

auto make_counted(std::initializer_list<u32> values) -> utl::vector<Counted>
{
    utl::vector<Counted> v;
    for (const u32 value : values)
    {
        v.emplace_back(value);
    }
    return v;
}

}

TEST_CASE("a new vector is empty and owns no memory")
{
    const utl::vector<u32> v;

    CHECK(v.empty());
    CHECK(v.size() == 0U);
    CHECK(v.capacity() == 0U);
    CHECK(v.data() == nullptr);
    CHECK(v.begin() == v.end());
}

TEST_CASE("an empty vector can be iterated, cleared, copied and moved")
{
    utl::vector<std::string> v;
    u32 count{0};
    for ([[maybe_unused]] const auto &item : v)
    {
        ++count;
    }
    CHECK(count == 0U);

    v.clear();
    const utl::vector<std::string> copy{v};
    const utl::vector<std::string> moved{std::move(v)};
    CHECK(copy.empty());
    CHECK(moved.empty());
}

TEST_CASE("the count constructors")
{
    SUBCASE("count: items get their default value")
    {
        const utl::vector<u32> v(5);
        REQUIRE(v.size() == 5U);
        for (const u32 item : v)
        {
            CHECK(item == 0U);
        }
    }

    SUBCASE("count and value: items are copies of the value")
    {
        const utl::vector<std::string> v(3, std::string{"abc"});
        REQUIRE(v.size() == 3U);
        for (const auto &item : v)
        {
            CHECK(item == "abc");
        }
    }
}

TEST_CASE("push_back and emplace_back add at the end")
{
    utl::vector<std::string> v;
    const std::string copied{"copied"};
    std::string moved{"moved, and long enough not to fit in the small-string buffer"};

    v.push_back(copied);
    v.push_back(std::move(moved));
    const std::string &emplaced{v.emplace_back(3, 'x')};

    REQUIRE(v.size() == 3U);
    CHECK(v[0] == "copied");
    CHECK(v[1] == "moved, and long enough not to fit in the small-string buffer");
    CHECK(v[2] == "xxx");
    CHECK(&emplaced == &v[2]);
    CHECK(v.front() == "copied");
    CHECK(v.back() == "xxx");
    CHECK(copied == "copied");
}

TEST_CASE("items survive the vector growing")
{
    SUBCASE("trivially copyable items")
    {
        utl::vector<u64> v;
        for (u64 i{0}; i < 1000; ++i)
        {
            v.push_back(i * 3);
            CHECK(v.capacity() >= v.size());
        }
        REQUIRE(v.size() == 1000U);
        for (u64 i{0}; i < 1000; ++i)
        {
            CHECK(v[i] == i * 3);
        }
    }

    // A short std::string points into itself (libstdc++), so a byte copy would break it.
    SUBCASE("items that have to be moved")
    {
        utl::vector<std::string> v;
        for (u32 i{0}; i < 1000; ++i)
        {
            v.emplace_back(std::to_string(i));
        }
        REQUIRE(v.size() == 1000U);
        for (u32 i{0}; i < 1000; ++i)
        {
            CHECK(v[i] == std::to_string(i));
        }
    }

    SUBCASE("items that can't be moved")
    {
        {
            utl::vector<Pinned> v;
            for (u32 i{0}; i < 1000; ++i)
            {
                v.emplace_back(i);
            }
            REQUIRE(v.size() == 1000U);
            CHECK(Pinned::alive == 1000);
            for (u32 i{0}; i < 1000; ++i)
            {
                CHECK(v[i].value == i);
            }
        }
        CHECK(Pinned::alive == 0);
    }
}

TEST_CASE("reserve allocates without adding items")
{
    utl::vector<u32> v;
    v.reserve(100);

    CHECK(v.empty());
    CHECK(v.capacity() >= 100U);

    SUBCASE("no reallocation up to the reserved capacity")
    {
        const u32 *const data{v.data()};
        for (u32 i{0}; i < 100; ++i)
        {
            v.push_back(i);
        }
        CHECK(v.data() == data);
    }

    SUBCASE("a smaller reserve does nothing")
    {
        const u64 capacity{v.capacity()};
        v.reserve(10);
        CHECK(v.capacity() == capacity);
    }
}

TEST_CASE("every item is destroyed exactly once")
{
    REQUIRE(Counted::alive == 0);

    SUBCASE("by the destructor")
    {
        {
            const utl::vector<Counted> v{make_counted({1, 2, 3, 4, 5, 6, 7})};
            CHECK(Counted::alive == 7);
        }
        CHECK(Counted::alive == 0);
    }

    SUBCASE("by clear, which keeps the memory")
    {
        utl::vector<Counted> v{make_counted({1, 2, 3})};
        const u64 capacity{v.capacity()};
        v.clear();

        CHECK(Counted::alive == 0);
        CHECK(v.empty());
        CHECK(v.capacity() == capacity);
    }

    SUBCASE("by resize to a smaller size")
    {
        utl::vector<Counted> v{make_counted({1, 2, 3, 4, 5})};
        v.resize(2);

        CHECK(Counted::alive == 2);
        CHECK(values_are(v, {1, 2}));
    }

    SUBCASE("by erase and erase_unordered")
    {
        utl::vector<Counted> v{make_counted({1, 2, 3, 4, 5})};
        v.erase(u64{1});
        CHECK(Counted::alive == 4);
        v.erase_unordered(u64{0});
        CHECK(Counted::alive == 3);
    }

    SUBCASE("by move assignment over a vector with items")
    {
        utl::vector<Counted> v{make_counted({1, 2, 3})};
        v = make_counted({4, 5});

        CHECK(Counted::alive == 2);
        CHECK(values_are(v, {4, 5}));
    }

    CHECK(Counted::alive >= 0);
}

TEST_CASE("resize")
{
    SUBCASE("to a larger size adds default values")
    {
        utl::vector<u32> v;
        v.push_back(7);
        v.resize(4);

        REQUIRE(v.size() == 4U);
        CHECK(v[0] == 7U);
        CHECK(v[1] == 0U);
        CHECK(v[3] == 0U);
    }

    SUBCASE("to a larger size with a value adds copies of it")
    {
        utl::vector<std::string> v;
        v.emplace_back("first");
        v.resize(3, std::string{"fill"});

        REQUIRE(v.size() == 3U);
        CHECK(v[0] == "first");
        CHECK(v[1] == "fill");
        CHECK(v[2] == "fill");
    }

    SUBCASE("to the same size changes nothing")
    {
        utl::vector<u32> v(3, 9U);
        v.resize(3);
        CHECK(v.size() == 3U);
        CHECK(v[2] == 9U);
    }

    SUBCASE("to zero empties the vector")
    {
        utl::vector<std::string> v(3, std::string{"x"});
        v.resize(0);
        CHECK(v.empty());
    }
}

TEST_CASE("erase keeps the order of the other items")
{
    SUBCASE("in the middle")
    {
        utl::vector<Counted> v{make_counted({1, 2, 3, 4, 5})};
        const Counted *const next{v.erase(u64{1})};

        CHECK(values_are(v, {1, 3, 4, 5}));
        CHECK(next == &v[1]);
    }

    SUBCASE("the first item")
    {
        utl::vector<Counted> v{make_counted({1, 2, 3})};
        v.erase(u64{0});
        CHECK(values_are(v, {2, 3}));
    }

    SUBCASE("the last item")
    {
        utl::vector<Counted> v{make_counted({1, 2, 3})};
        const Counted *const next{v.erase(u64{2})};

        CHECK(values_are(v, {1, 2}));
        CHECK(next == v.end());
    }

    SUBCASE("by pointer")
    {
        utl::vector<Counted> v{make_counted({1, 2, 3})};
        v.erase(&v[1]);
        CHECK(values_are(v, {1, 3}));
    }

    SUBCASE("the only item")
    {
        utl::vector<Counted> v{make_counted({1})};
        v.erase(u64{0});
        CHECK(v.empty());
    }

    SUBCASE("trivially copyable items")
    {
        utl::vector<u32> v;
        for (u32 i{0}; i < 6; ++i)
        {
            v.push_back(i);
        }
        v.erase(u64{2});

        REQUIRE(v.size() == 5U);
        CHECK(v[1] == 1U);
        CHECK(v[2] == 3U);
        CHECK(v[4] == 5U);
    }

    SUBCASE("strings")
    {
        utl::vector<std::string> v;
        for (u32 i{0}; i < 6; ++i)
        {
            v.emplace_back(std::to_string(i));
        }
        v.erase(u64{0});

        REQUIRE(v.size() == 5U);
        CHECK(v[0] == "1");
        CHECK(v[4] == "5");
    }
}

TEST_CASE("erase_unordered moves the last item into the gap")
{
    SUBCASE("in the middle")
    {
        utl::vector<Counted> v{make_counted({1, 2, 3, 4, 5})};
        const Counted *const item{v.erase_unordered(u64{1})};

        CHECK(values_are(v, {1, 5, 3, 4}));
        CHECK(item == &v[1]);
    }

    SUBCASE("the last item")
    {
        utl::vector<Counted> v{make_counted({1, 2, 3})};
        v.erase_unordered(u64{2});
        CHECK(values_are(v, {1, 2}));
    }

    SUBCASE("the only item")
    {
        utl::vector<Counted> v{make_counted({1})};
        v.erase_unordered(u64{0});
        CHECK(v.empty());
    }

    SUBCASE("through utl::erase_unordered")
    {
        utl::vector<std::string> v;
        for (u32 i{0}; i < 4; ++i)
        {
            v.emplace_back(std::to_string(i));
        }
        utl::erase_unordered(v, 0);

        REQUIRE(v.size() == 3U);
        CHECK(v[0] == "3");
        CHECK(v[1] == "1");
        CHECK(v[2] == "2");
    }
}

TEST_CASE("copying makes an independent vector")
{
    utl::vector<std::string> original;
    original.emplace_back("one");
    original.emplace_back("two");

    SUBCASE("copy constructor")
    {
        utl::vector<std::string> copy{original};
        copy[0] = "changed";

        REQUIRE(copy.size() == 2U);
        CHECK(copy[1] == "two");
        CHECK(original[0] == "one");
        CHECK(copy.data() != original.data());
    }

    SUBCASE("copy assignment replaces what was there")
    {
        utl::vector<std::string> copy(5, std::string{"old"});
        copy = original;

        REQUIRE(copy.size() == 2U);
        CHECK(copy[0] == "one");
        CHECK(copy[1] == "two");
        CHECK(original.size() == 2U);
    }
}

// One test case per move (not SUBCASEs): the static analyzer follows a path through several SUBCASEs and then
// sees the same vector moved twice.
TEST_CASE("the move constructor takes the memory and leaves the source empty")
{
    utl::vector<std::string> source{make_strings()};
    const std::string *const data{source.data()};

    const utl::vector<std::string> target{std::move(source)};

    CHECK(target.data() == data);
    CHECK(target.size() == 2U);
    // NOLINTBEGIN(bugprone-use-after-move, clang-analyzer-cplusplus.Move): the moved-from state is what's tested
    CHECK(source.empty());
    CHECK(source.capacity() == 0U);
    CHECK(source.data() == nullptr);
    // NOLINTEND(bugprone-use-after-move, clang-analyzer-cplusplus.Move)
}

TEST_CASE("move assignment takes the memory and leaves the source empty")
{
    utl::vector<std::string> source{make_strings()};
    const std::string *const data{source.data()};

    utl::vector<std::string> target(3, std::string{"old"});
    target = std::move(source);

    CHECK(target.data() == data);
    CHECK(target.size() == 2U);
    CHECK(target[1] == "two");
    // NOLINTNEXTLINE(bugprone-use-after-move, clang-analyzer-cplusplus.Move): the moved-from state is what's tested
    CHECK(source.empty());
}

TEST_CASE("a moved-from vector can be used again")
{
    utl::vector<std::string> source{make_strings()};

    const utl::vector<std::string> target{std::move(source)};
    // NOLINTNEXTLINE(bugprone-use-after-move, clang-analyzer-cplusplus.Move): the moved-from state is what's tested
    source.emplace_back("again");

    CHECK(source.size() == 1U);
    CHECK(source[0] == "again");
    CHECK(target.size() == 2U);
}

TEST_CASE("swap exchanges the contents")
{
    utl::vector<u32> a(2, 1U);
    utl::vector<u32> b(5, 2U);
    const u32 *const a_data{a.data()};
    const u32 *const b_data{b.data()};

    a.swap(b);

    CHECK(a.size() == 5U);
    CHECK(a[0] == 2U);
    CHECK(a.data() == b_data);
    CHECK(b.size() == 2U);
    CHECK(b[0] == 1U);
    CHECK(b.data() == a_data);
}

TEST_CASE("begin and end cover the items in order")
{
    utl::vector<u32> v;
    for (u32 i{0}; i < 10; ++i)
    {
        v.push_back(i);
    }

    CHECK(v.end() - v.begin() == 10);
    CHECK(v.begin() == v.data());

    u32 expected{0};
    for (const u32 item : v)
    {
        CHECK(item == expected);
        ++expected;
    }
    CHECK(expected == 10U);

    for (u32 &item : v)
    {
        item *= 2;
    }
    CHECK(v[9] == 18U);
}

TEST_CASE("a vector that doesn't destruct its items leaves them alone")
{
    REQUIRE(Counted::alive == 0);
    {
        utl::vector<Counted, false> v;
        v.emplace_back(1U);
        v.emplace_back(2U);
        CHECK(Counted::alive == 2);

        v.erase_unordered(u64{0});
        v.clear();
        CHECK(Counted::alive == 2);
    }
    CHECK(Counted::alive == 2);

    // Nothing owns the two instances any more: put the counter back for the other tests.
    Counted::alive = 0;
}

TEST_SUITE_END();
