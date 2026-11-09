// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "CommonHeaders.h"
#if !defined(SHIPPING)
namespace primal::content
{
auto load_game() -> bool;
auto unload_game() -> void;

auto load_engine_shaders(std::unique_ptr<u8[]> &shaders, u64 &size) -> bool;
}
#endif
