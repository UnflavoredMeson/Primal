// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
module;

#include "../Common/PrimitiveTypes.h"

#include <concepts>

// The phases of a frame, in order: import primal.frame;
//
//     while (platform::pump_events())            // 1. input and window events (the caller)
//     {
//         frame::run(dt, [&] { surface.render(info); });
//     }
//
// frame::run() runs
//     2. the sync point: entity commands queued since the last frame (game_entity::queue_create/queue_remove);
//     3. the game update: every script's update(dt), which writes transform changes into per-worker caches;
//     4. the transform update: the caches are applied and the changed world matrices computed (job system);
//     5-6. render: the function passed in (render preparation, recording, submit).
// Between phases nothing runs; structural changes (entities created or removed) only happen at the sync point or
// between frames.
export module primal.frame;

export namespace primal::frame
{

enum class Phase : u8
{
    between_frames, // also before the first frame, and in programs that don't call run() (the editor)
    sync,
    game_update,
    transform_update,
    render,
};

// The phase the frame thread is in. Any thread may ask.
[[nodiscard]] auto phase() -> Phase;

// The number of the frame that runs now (0 for the first one); between frames, the number of the next one.
[[nodiscard]] auto index() -> u64;

// True when entities may be created or removed directly (game_entity::create/remove): between frames and at the
// sync point. Scripts and jobs queue the change instead (game_entity::queue_create/queue_remove).
[[nodiscard]] auto structural_changes_allowed() -> bool;

namespace detail
{
// Phases 2 to 4; leaves the frame in Phase::render.
auto run_update_phases(f32 dt) -> void;
// Ends phase 6 and counts the frame.
auto end_frame() -> void;
}

// Runs one frame on the calling thread (the frame thread). dt is passed to the scripts.
template <typename F>
    requires std::invocable<F &>
auto run(f32 dt, F &&render) -> void
{
    detail::run_update_phases(dt);
    render();
    detail::end_frame();
}

}
