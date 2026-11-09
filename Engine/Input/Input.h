// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "CommonHeaders.h"
#include "EngineAPI/Input.h"

namespace primal::input
{

auto bind(InputSource source) -> void;
auto unbind(InputSource::Type type, InputCode::Code code) -> void;
auto unbind(u64 binding) -> void;
auto set(InputSource::Type type, InputCode::Code code, math::v3 value) -> void;
}
