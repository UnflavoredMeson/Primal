// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
module;

#include "../Common/PrimitiveTypes.h"
#include "../Components/Entity.h"
#include "../Components/Script.h"

#include <atomic>
#include <cassert>

module primal.frame;

namespace primal::frame
{
namespace
{

std::atomic<Phase> current_phase{Phase::between_frames};
std::atomic<u64> frame_index{0};

auto enter(Phase phase) -> void
{
    current_phase.store(phase);
}

}

auto phase() -> Phase
{
    return current_phase.load();
}

auto index() -> u64
{
    return frame_index.load();
}

auto structural_changes_allowed() -> bool
{
    const Phase p{current_phase.load()};
    return p == Phase::between_frames || p == Phase::sync;
}

namespace detail
{

auto run_update_phases(f32 dt) -> void
{
    assert(current_phase.load() == Phase::between_frames && "frame::run() was called inside a frame.");

    enter(Phase::sync);
    game_entity::process_queued_commands();

    enter(Phase::game_update);
    script::update_scripts(dt);

    enter(Phase::transform_update);
    script::apply_transform_changes();

    enter(Phase::render);
}

auto end_frame() -> void
{
    assert(current_phase.load() == Phase::render);
    enter(Phase::between_frames);
    frame_index.fetch_add(1);
}

}

}
