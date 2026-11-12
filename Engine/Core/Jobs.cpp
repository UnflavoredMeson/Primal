// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
module;

#include "../Common/PlatformDetection.h"
#include "../Common/PrimitiveTypes.h"
#include "Log.h"

#if PRIMAL_PLATFORM_LINUX
#include <pthread.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <charconv>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

module primal.jobs;

// How it works
// - Every thread of the pool (the frame thread is 0, the workers 1..n) has a queue per lane. A thread pushes to its
//   own queue and takes from its back (frame lane), and steals from the front of the others. Each queue has a
//   short mutex; a lock-free deque can replace it later behind the same interface.
// - queued[lane] counts the jobs in all queues. A thread that finds nothing sleeps on a condition variable:
//   workers on work_cv, threads in wait() on done_cv. Whoever makes work or finishes a counter wakes them, and
//   takes sleep_mutex only when someone sleeps (the sleeping_* counts). Every atomic here uses the default
//   sequentially consistent order: that makes "I sleep" / "there is work" visible to one side or the other.
// - background_running counts the threads in a background job. A thread reserves a slot before it takes one
//   and gives it back afterwards, and while it waits inside the job (so a background job may wait on others).

namespace primal::jobs
{

namespace detail
{

struct CounterAccess
{
    static auto add(Counter &counter) -> void
    {
        counter._value.fetch_add(1);
    }

    // Returns true when this was the last job. The counter may be gone right after this.
    static auto finish(Counter &counter) -> bool
    {
        return counter._value.fetch_sub(1) == 1;
    }

    [[nodiscard]] static auto value(const Counter &counter) -> u32
    {
        return counter._value.load();
    }
};

}

namespace
{

using detail::Job;

constexpr u32 lane_count{2};

[[nodiscard]] constexpr auto lane_index(Lane lane) -> u32
{
    return std::to_underlying(lane);
}

struct Queue
{
    std::mutex mutex;
    std::array<std::deque<Job>, lane_count> jobs;
};

struct State
{
    std::vector<std::thread> threads;
    std::unique_ptr<Queue[]> queues; // NOLINT(*-avoid-c-arrays): a mutex can't move, so no vector
    u32 queue_count{0};

    std::atomic<bool> running{false};
    std::atomic<bool> stop{false};
    std::array<std::atomic<u32>, lane_count> queued{};
    std::atomic<u32> background_running{0};
    std::atomic<u32> background_limit{1};

    std::mutex sleep_mutex;
    std::condition_variable work_cv;
    std::condition_variable done_cv;
    std::atomic<u32> sleeping_workers{0};
    std::atomic<u32> sleeping_waiters{0};
};

State state;

// The pool index of this thread, or u32_invalid_id for a thread outside the pool.
thread_local u32 thread_index{u32_invalid_id};
// Set while this thread runs a background job.
thread_local bool in_background_job{false};

auto wake_one_worker() -> void
{
    if (state.sleeping_workers.load() > 0)
    {
        {
            const std::scoped_lock lock{state.sleep_mutex};
        }
        state.work_cv.notify_one();
    }
}

auto wake_waiters() -> void
{
    if (state.sleeping_waiters.load() > 0)
    {
        {
            const std::scoped_lock lock{state.sleep_mutex};
        }
        state.done_cv.notify_all();
    }
}

auto release_background_slot() -> void
{
    state.background_running.fetch_sub(1);
    if (state.queued[lane_index(Lane::background)].load() > 0)
    {
        wake_one_worker();
        wake_waiters();
    }
}

// Frame jobs: own queue from the back, then the others from the front. Background jobs: always from the front,
// so loads start in the order they were asked for.
[[nodiscard]] auto try_take(u32 self, Lane lane) -> std::optional<Job>
{
    const u32 l{lane_index(lane)};
    if (state.queued[l].load() == 0)
    {
        return std::nullopt;
    }
    for (u32 i{0}; i < state.queue_count; ++i)
    {
        const u32 victim{(self + i) % state.queue_count};
        Queue &queue{state.queues[victim]};
        const std::scoped_lock lock{queue.mutex};
        std::deque<Job> &jobs{queue.jobs[l]};
        if (jobs.empty())
        {
            continue;
        }
        Job job;
        if (i == 0 && lane == Lane::frame)
        {
            job = jobs.back();
            jobs.pop_back();
        }
        else
        {
            job = jobs.front();
            jobs.pop_front();
        }
        state.queued[l].fetch_sub(1);
        return job;
    }
    return std::nullopt;
}

// Takes a background job if a slot is free. The caller gives the slot back after running it.
[[nodiscard]] auto try_take_background(u32 self) -> std::optional<Job>
{
    if (state.queued[lane_index(Lane::background)].load() == 0)
    {
        return std::nullopt;
    }
    if (state.background_running.fetch_add(1) >= state.background_limit)
    {
        release_background_slot();
        return std::nullopt;
    }
    std::optional<Job> job{try_take(self, Lane::background)};
    if (!job)
    {
        release_background_slot();
    }
    return job;
}

auto execute(Job &job) -> void
{
    // Profiling hook: a named scope ("job") goes around this call.
    const bool background{job.lane == Lane::background};
    const bool was_in_background_job{in_background_job};
    in_background_job = background;
    job.function(job.data.data());
    in_background_job = was_in_background_job;

    if (job.counter && detail::CounterAccess::finish(*job.counter))
    {
        wake_waiters();
    }
}

[[nodiscard]] auto worker_can_take_work() -> bool
{
    return state.queued[lane_index(Lane::frame)].load() > 0 ||
           (state.queued[lane_index(Lane::background)].load() > 0 &&
            state.background_running.load() < state.background_limit);
}

[[nodiscard]] auto nothing_queued() -> bool
{
    return state.queued[lane_index(Lane::frame)].load() == 0 && state.queued[lane_index(Lane::background)].load() == 0;
}

auto name_this_thread([[maybe_unused]] u32 index) -> void
{
#if PRIMAL_PLATFORM_LINUX
    // At most 15 characters.
    const std::string name{std::format("primal-job-{}", index)};
    ::pthread_setname_np(::pthread_self(), name.c_str());
#endif
}

auto worker_main(u32 index) -> void
{
    thread_index = index;
    name_this_thread(index);

    while (true)
    {
        if (std::optional<Job> job{try_take(index, Lane::frame)})
        {
            execute(*job);
            continue;
        }
        if (std::optional<Job> job{try_take_background(index)})
        {
            execute(*job);
            release_background_slot();
            continue;
        }
        if (state.stop.load())
        {
            if (nothing_queued())
            {
                break;
            }
            std::this_thread::yield();
            continue;
        }

        std::unique_lock lock{state.sleep_mutex};
        state.sleeping_workers.fetch_add(1);
        state.work_cv.wait(lock, [] { return state.stop.load() || worker_can_take_work(); });
        state.sleeping_workers.fetch_sub(1);
    }
    thread_index = u32_invalid_id;
}

// PRIMAL_JOBS=<n>: the number of workers.
[[nodiscard]] auto worker_count_from_environment() -> std::optional<u32>
{
    constexpr const char *name{"PRIMAL_JOBS"};
#ifdef _MSC_VER
    // std::getenv is deprecated by MSVC (C4996).
    char *env{nullptr};
    size_t size{0};
    if (_dupenv_s(&env, &size, name) != 0 || !env)
    {
        return std::nullopt;
    }
    const std::string text{env};
    std::free(env);
#else
    const char *const env{std::getenv(name)};
    if (!env)
    {
        return std::nullopt;
    }
    const std::string_view text{env};
#endif
    u32 count{0};
    const auto [end, error]{std::from_chars(text.data(), text.data() + text.size(), count)};
    if (error != std::errc{} || end != text.data() + text.size())
    {
        log::warning(log::Category::core, "PRIMAL_JOBS=\"{}\" isn't a number of workers; ignored.", text);
        return std::nullopt;
    }
    return count;
}

}

Counter::~Counter()
{
    assert(is_done() && "A job counter was destroyed while its jobs were still running.");
}

namespace detail
{

auto submit(const Job &job) -> void
{
    if (!state.running.load())
    {
        Job copy{job};
        copy.counter = nullptr;
        execute(copy);
        return;
    }

    CounterAccess::add(*job.counter);
    const u32 self{thread_index == u32_invalid_id ? 0 : thread_index};
    const u32 l{lane_index(job.lane)};
    {
        Queue &queue{state.queues[self]};
        const std::scoped_lock lock{queue.mutex};
        queue.jobs[l].push_back(job);
        state.queued[l].fetch_add(1);
    }
    wake_one_worker();
    wake_waiters();
}

}

auto initialize(u32 worker_count) -> bool
{
    if (state.running.load())
    {
        assert(false && "jobs::initialize() was called twice.");
        return false;
    }

    if (const std::optional<u32> count{worker_count_from_environment()})
    {
        worker_count = *count;
    }
    else if (worker_count == u32_invalid_id)
    {
        worker_count = std::max(std::thread::hardware_concurrency(), 1u) - 1;
    }

    if (worker_count == 0)
    {
        log::info(log::Category::core, "Job system: no workers, every job runs on its caller.");
        return true;
    }

    state.queue_count = worker_count + 1;
    state.queues = std::make_unique<Queue[]>(state.queue_count); // NOLINT(*-avoid-c-arrays)
    state.background_limit = std::max(worker_count / 2, 1u);
    state.stop = false;
    state.running = true;
    thread_index = 0;

    state.threads.reserve(worker_count);
    for (u32 i{1}; i <= worker_count; ++i)
    {
        state.threads.emplace_back(worker_main, i);
    }

    log::info(
        log::Category::core,
        "Job system: {} workers, at most {} in background jobs.",
        worker_count,
        state.background_limit.load());
    return true;
}

auto shutdown() -> void
{
    if (!state.running.load())
    {
        return;
    }
    assert(thread_index == 0 && "jobs::shutdown() must be called by the thread that called initialize().");

    {
        const std::scoped_lock lock{state.sleep_mutex};
        state.stop = true;
        // Let every worker drain the background lane.
        state.background_limit = state.queue_count;
    }
    state.work_cv.notify_all();

    // Help with what is left, then join.
    while (std::optional<Job> job{try_take(0, Lane::frame)})
    {
        execute(*job);
    }
    for (std::thread &thread : state.threads)
    {
        thread.join();
    }
    // A job on a worker may have queued more for the frame thread after the loop above.
    while (!nothing_queued())
    {
        for (const Lane lane : {Lane::frame, Lane::background})
        {
            while (std::optional<Job> job{try_take(0, lane)})
            {
                execute(*job);
            }
        }
    }

    state.threads.clear();
    state.queues.reset();
    state.queue_count = 0;
    state.running = false;
    state.stop = false;
    thread_index = u32_invalid_id;
}

auto worker_count() -> u32
{
    return state.running.load() ? state.queue_count : 1;
}

auto worker_index() -> u32
{
    return state.running.load() ? thread_index : 0;
}

auto wait(Counter &counter) -> void
{
    if (counter.is_done())
    {
        return;
    }
    assert(state.running.load());

    const u32 self{thread_index};
    const bool helps{self != u32_invalid_id};
    // A background job that waits gives its slot back meanwhile, so the jobs it waits for can run, and it helps
    // with background jobs too: with every worker in such a wait nobody else could run them.
    const bool holds_background_slot{in_background_job && helps};
    if (holds_background_slot)
    {
        release_background_slot();
    }

    while (!counter.is_done())
    {
        if (helps)
        {
            if (std::optional<Job> job{try_take(self, Lane::frame)})
            {
                execute(*job);
                continue;
            }
        }
        if (holds_background_slot)
        {
            if (std::optional<Job> job{try_take_background(self)})
            {
                execute(*job);
                release_background_slot();
                continue;
            }
        }
        std::unique_lock lock{state.sleep_mutex};
        state.sleeping_waiters.fetch_add(1);
        state.done_cv.wait(
            lock,
            [&counter, helps, holds_background_slot]
            {
                return detail::CounterAccess::value(counter) == 0 ||
                       (helps && state.queued[lane_index(Lane::frame)].load() > 0) ||
                       (holds_background_slot && state.queued[lane_index(Lane::background)].load() > 0 &&
                        state.background_running.load() < state.background_limit.load());
            });
        state.sleeping_waiters.fetch_sub(1);
    }

    if (holds_background_slot)
    {
        state.background_running.fetch_add(1);
    }
}

}
