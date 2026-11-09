// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once

#include "CommonHeaders.h"

// The C++ side of the HLSL types shared with the shaders, used by both graphics backends. The Vulkan backend
// compiles the shaders with -fvk-use-dx-layout, so these structs have the same layout as in Direct3D 12.
namespace primal::graphics::hlsl {

using float4x4 = math::m4x4a;
using float4 = math::v4;
using float3 = math::v3;
using float2 = math::v2;
using uint4 = math::u32v4;
using uint3 = math::u32v3;
using uint2 = math::u32v2;
using uint = u32;

#include "CommonTypes.hlsli"

} // namespace primal::graphics::hlsl
