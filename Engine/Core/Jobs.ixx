// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
module;

#include "../Common/PrimitiveTypes.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

// The job system: a fixed pool of worker threads that run short functions. import primal.jobs;
//
//     jobs::Counter counter;
//     jobs::run(counter, [&] { load_textures(list); }, jobs::Lane::background);
//     jobs::parallel_for(count, 256, [&](u32 begin, u32 end) { ... });
//     jobs::wait(counter);
//
// - A job is a function object of at most 48 bytes that is trivially copyable: capture by reference or pass a
//   pointer. It must not throw (a job that throws ends the program).
// - wait() runs other frame jobs while the counter isn't zero (no fibers), so never wait while holding a lock.
// - Frame jobs are taken before background jobs, and at most half of the workers run background jobs at once.
// - Before initialize(), after shutdown() and with zero workers (PRIMAL_JOBS=0) every job runs at once on the
//   caller ("inline mode").
export module primal.jobs;

export namespace primal::jobs
{

enum class Lane : u8
{
    frame,      // short work the frame waits for
    background, // may take long or block on files
};

namespace detail
{
struct CounterAccess;
}

// Counts the jobs started against it that haven't finished yet. It must outlive those jobs.
class Counter
{
  public:
    Counter() = default;
    ~Counter();
    Counter(const Counter &) = delete;
    Counter(Counter &&) = delete;
    auto operator=(const Counter &) -> Counter & = delete;
    auto operator=(Counter &&) -> Counter & = delete;

    [[nodiscard]] auto is_done() const -> bool
    {
        return _value.load(std::memory_order_acquire) == 0;
    }

  private:
    friend struct detail::CounterAccess;
    std::atomic<u32> _value{0};
};

namespace detail
{

inline constexpr size_t job_data_size{48};
inline constexpr size_t job_data_alignment{16};

using job_function = void (*)(void *data) noexcept;

struct Job
{
    job_function function{nullptr};
    Counter *counter{nullptr};
    alignas(job_data_alignment) std::array<std::byte, job_data_size> data{};
    Lane lane{Lane::frame};
};

template <typename F>
auto call(void *data) noexcept -> void
{
    (*std::launder(static_cast<F *>(data)))();
}

// Queues the job, or runs it at once in inline mode.
auto submit(const Job &job) -> void;

}

// Starts the pool with this many worker threads besides the calling thread, which becomes the frame thread
// (worker_index() 0); without a count, hardware threads - 1. PRIMAL_JOBS=<n> overrides both; 0 is inline mode.
// Returns false when the pool is already running.
auto initialize(u32 worker_count = u32_invalid_id) -> bool;

// Runs every job that is still queued, then stops the workers. Called by the frame thread.
auto shutdown() -> void;

// Workers + the frame thread; 1 in inline mode.
[[nodiscard]] auto worker_count() -> u32;

// 0 .. worker_count() - 1 for the frame thread and the workers, for per-thread data. Another thread gets
// u32_invalid_id while the pool runs, and 0 in inline mode.
[[nodiscard]] auto worker_index() -> u32;

// Starts a job. Any thread may call it, also a job.
template <typename F>
    requires std::invocable<std::decay_t<F> &>
auto run(Counter &counter, F &&function, Lane lane = Lane::frame) -> void
{
    using callable = std::decay_t<F>;
    static_assert(sizeof(callable) <= detail::job_data_size, "Job captures are too large: capture a pointer.");
    static_assert(alignof(callable) <= detail::job_data_alignment, "Job captures are over-aligned.");
    static_assert(
        std::is_trivially_copyable_v<callable> && std::is_trivially_destructible_v<callable>,
        "Job captures must be trivially copyable: capture by reference or pass a pointer.");

    detail::Job job{.function = &detail::call<callable>, .counter = &counter, .lane = lane};
    ::new (static_cast<void *>(job.data.data())) callable(std::forward<F>(function));
    detail::submit(job);
}

// Returns when the counter is zero. The frame thread and the workers run other frame jobs meanwhile; another
// thread just blocks.
auto wait(Counter &counter) -> void;

// Calls function(begin, end) for [0, count) in batches of batch_size (frame jobs), and returns when all are done.
// The caller runs the last batch itself, so a count of at most batch_size never leaves the calling thread.
template <typename F>
    requires std::invocable<F &, u32, u32>
auto parallel_for(u32 count, u32 batch_size, F &&function) -> void
{
    if (count == 0)
    {
        return;
    }
    batch_size = std::max(batch_size, 1u);
    Counter counter;
    u32 begin{0};
    for (; count - begin > batch_size; begin += batch_size)
    {
        run(counter, [&function, begin, end = begin + batch_size] { function(begin, end); });
    }
    function(begin, count);
    wait(counter);
}

}
