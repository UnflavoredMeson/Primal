// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "Common/PrimitiveTypes.h"
#include "Core/Log.h"

#include <doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

import primal.jobs;

using namespace primal;

// NOTE: every test that starts the pool stops it again (Pool). PRIMAL_JOBS in the environment would change the
//       worker counts the tests ask for: leave it unset. Jobs don't call doctest's CHECK; they count, and the test
//       checks the counts on its own thread.

TEST_SUITE_BEGIN("jobs");

namespace
{

// Starts the job system for one test, with the log's info lines turned off.
class Pool
{
  public:
    explicit Pool(u32 workers)
        : _level{log::level()}
    {
        log::set_level(log::Level::warning);
        REQUIRE(jobs::initialize(workers));
    }

    ~Pool()
    {
        jobs::shutdown();
        log::set_level(_level);
    }

    Pool(const Pool &) = delete;
    auto operator=(const Pool &) -> Pool & = delete;
    Pool(Pool &&) = delete;
    auto operator=(Pool &&) -> Pool & = delete;

  private:
    log::Level _level;
};

// Keeps a thread busy for about this long without sleeping, so the job stays on its thread.
auto spin_for(std::chrono::microseconds duration) -> void
{
    const auto end{std::chrono::steady_clock::now() + duration};
    while (std::chrono::steady_clock::now() < end)
    {
        std::this_thread::yield();
    }
}

// Remembers the largest value a count reaches.
auto raise_to(std::atomic<u32> &maximum, u32 value) -> void
{
    u32 previous{maximum.load()};
    while (previous < value && !maximum.compare_exchange_weak(previous, value))
    {
    }
}

}

TEST_CASE("without a pool every job runs at once on its caller, in order")
{
    CHECK(jobs::worker_count() == 1);
    CHECK(jobs::worker_index() == 0);

    std::vector<u32> order;
    jobs::Counter counter;
    for (u32 i{0}; i < 5; ++i)
    {
        jobs::run(counter, [&order, i] { order.push_back(i); });
        CHECK(order.size() == i + 1);
        CHECK(counter.is_done());
    }
    jobs::run(counter, [&order] { order.push_back(5); }, jobs::Lane::background);
    jobs::wait(counter);
    CHECK(order == std::vector<u32>{0, 1, 2, 3, 4, 5});

    std::vector<u32> batches;
    jobs::parallel_for(
        10,
        4,
        [&batches](u32 begin, u32 end)
        {
            batches.push_back(begin);
            batches.push_back(end);
        });
    CHECK(batches == std::vector<u32>{0, 4, 4, 8, 8, 10});
}

TEST_CASE("zero workers is inline mode")
{
    const Pool pool{0};
    CHECK(jobs::worker_count() == 1);

    u32 value{0};
    jobs::Counter counter;
    jobs::run(counter, [&value] { value = 7; });
    CHECK(value == 7);
}

TEST_CASE("a counter reaches zero when all its jobs are done")
{
    const Pool pool{4};
    CHECK(jobs::worker_count() == 5);
    CHECK(jobs::worker_index() == 0);

    constexpr u32 job_count{2000};
    std::atomic<u32> done{0};
    jobs::Counter counter;
    for (u32 i{0}; i < job_count; ++i)
    {
        jobs::run(counter, [&done] { done.fetch_add(1); });
    }
    jobs::wait(counter);
    CHECK(counter.is_done());
    CHECK(done.load() == job_count);

    // A counter can be used again.
    jobs::run(counter, [&done] { done.fetch_add(1); });
    jobs::wait(counter);
    CHECK(done.load() == job_count + 1);
}

TEST_CASE("parallel_for calls every index exactly once")
{
    const Pool pool{4};

    struct Case
    {
        u32 count;
        u32 batch_size;
    };
    constexpr std::array cases{
        Case{0, 16},
        Case{1, 16},
        Case{15, 16},
        Case{16, 16},
        Case{17, 16},
        Case{10'007, 64},
        Case{1000, 0},
        Case{100, 1000},
    };
    for (const auto &[count, batch_size] : cases)
    {
        CAPTURE(count);      // NOLINT(misc-const-correctness)
        CAPTURE(batch_size); // NOLINT(misc-const-correctness)

        // Each index is written by one job only: a second write from another thread would be a race (TSan).
        std::vector<u32> calls(count, 0);
        std::atomic<u32> batches{0};
        std::atomic<u32> empty_batches{0};
        jobs::parallel_for(
            count,
            batch_size,
            [&](u32 begin, u32 end)
            {
                if (begin >= end)
                {
                    empty_batches.fetch_add(1);
                }
                for (u32 i{begin}; i < end; ++i)
                {
                    ++calls[i];
                }
                batches.fetch_add(1);
            });
        CHECK(empty_batches.load() == 0);
        CHECK(std::ranges::all_of(calls, [](u32 c) { return c == 1; }));
        const u32 batch{std::max(batch_size, 1u)};
        CHECK(batches.load() == (count + batch - 1) / batch);
    }
}

TEST_CASE("worker_index() is unique per running thread")
{
    const Pool pool{4};

    // Not atomic on purpose: two threads with the same index would race here.
    std::vector<u32> per_worker(jobs::worker_count(), 0);
    std::atomic<u32> out_of_range{0};
    jobs::parallel_for(
        4000,
        8,
        [&](u32 begin, u32 end)
        {
            const u32 index{jobs::worker_index()};
            if (index >= per_worker.size())
            {
                out_of_range.fetch_add(1);
                return;
            }
            per_worker[index] += end - begin;
        });
    CHECK(out_of_range.load() == 0);
    u32 total{0};
    for (const u32 count : per_worker)
    {
        total += count;
    }
    CHECK(total == 4000);
}

TEST_CASE("idle workers steal the jobs of a busy thread")
{
    const Pool pool{4};

    // All jobs are queued by the frame thread; each keeps its thread busy, so others have to take some.
    std::array<std::atomic<u32>, 5> per_worker{};
    jobs::Counter counter;
    for (u32 i{0}; i < 64; ++i)
    {
        jobs::run(
            counter,
            [&per_worker]
            {
                per_worker[jobs::worker_index()].fetch_add(1);
                spin_for(std::chrono::microseconds{500});
            });
    }
    jobs::wait(counter);

    u32 threads_used{0};
    u32 total{0};
    for (const auto &count : per_worker)
    {
        threads_used += count.load() > 0 ? 1 : 0;
        total += count.load();
    }
    CHECK(total == 64);
    CHECK(threads_used >= 2);
}

TEST_CASE("jobs can start jobs and wait for them")
{
    const Pool pool{3};

    std::atomic<u32> leaves{0};
    jobs::Counter outer;
    for (u32 i{0}; i < 8; ++i)
    {
        jobs::run(
            outer,
            [&leaves]
            {
                jobs::Counter middle;
                for (u32 j{0}; j < 8; ++j)
                {
                    jobs::run(
                        middle,
                        [&leaves]
                        {
                            jobs::parallel_for(16, 4, [&leaves](u32 begin, u32 end) { leaves.fetch_add(end - begin); });
                        });
                }
                jobs::wait(middle);
            });
    }
    jobs::wait(outer);
    CHECK(leaves.load() == 8 * 8 * 16);
}

TEST_CASE("at most half of the workers run background jobs at once, and frame jobs still run")
{
    const Pool pool{4};

    std::atomic<u32> running{0};
    std::atomic<u32> most_running{0};
    std::atomic<bool> release{false};
    jobs::Counter background;
    for (u32 i{0}; i < 8; ++i)
    {
        jobs::run(
            background,
            [&]
            {
                raise_to(most_running, running.fetch_add(1) + 1);
                while (!release.load())
                {
                    std::this_thread::yield();
                }
                running.fetch_sub(1);
            },
            jobs::Lane::background);
    }

    // The background jobs block until released; frame work has to get through regardless.
    std::atomic<u32> frame_done{0};
    jobs::parallel_for(1000, 10, [&frame_done](u32 begin, u32 end) { frame_done.fetch_add(end - begin); });
    CHECK(frame_done.load() == 1000);

    release = true;
    jobs::wait(background);
    CHECK(most_running.load() >= 1);
    CHECK(most_running.load() <= 2);
}

TEST_CASE("a background job can wait for background jobs")
{
    // One worker: one background slot, held by the outer job while it waits.
    const Pool pool{1};

    std::atomic<u32> inner_done{0};
    jobs::Counter outer;
    jobs::run(
        outer,
        [&inner_done]
        {
            jobs::Counter inner;
            for (u32 i{0}; i < 4; ++i)
            {
                jobs::run(inner, [&inner_done] { inner_done.fetch_add(1); }, jobs::Lane::background);
            }
            jobs::wait(inner);
        },
        jobs::Lane::background);
    jobs::wait(outer);
    CHECK(inner_done.load() == 4);
}

TEST_CASE("shutdown() runs the jobs that are still queued")
{
    std::atomic<u32> done{0};
    jobs::Counter counter;
    {
        const Pool pool{2};
        for (u32 i{0}; i < 100; ++i)
        {
            jobs::run(counter, [&done] { done.fetch_add(1); }, i % 2 ? jobs::Lane::background : jobs::Lane::frame);
        }
    }
    CHECK(counter.is_done());
    CHECK(done.load() == 100);
    CHECK(jobs::worker_count() == 1);
}

TEST_CASE("a thread outside the pool can start jobs and wait for them")
{
    const Pool pool{2};

    std::atomic<u32> done{0};
    u32 outside_index{0};
    std::thread outside{[&done, &outside_index]
                        {
                            outside_index = jobs::worker_index();
                            jobs::Counter counter;
                            for (u32 i{0}; i < 50; ++i)
                            {
                                jobs::run(counter, [&done] { done.fetch_add(1); });
                            }
                            jobs::wait(counter);
                        }};
    outside.join();
    CHECK(outside_index == u32_invalid_id);
    CHECK(done.load() == 50);
}

TEST_CASE("the pool can be started and stopped many times")
{
    for (u32 round{0}; round < 20; ++round)
    {
        const Pool pool{round % 4 + 1};
        CHECK(jobs::worker_count() == round % 4 + 2);
        std::atomic<u32> sum{0};
        jobs::parallel_for(256, 16, [&sum](u32 begin, u32 end) { sum.fetch_add(end - begin); });
        CHECK(sum.load() == 256);
    }
    CHECK(jobs::worker_count() == 1);
    CHECK(jobs::worker_index() == 0);
}

TEST_SUITE_END();
