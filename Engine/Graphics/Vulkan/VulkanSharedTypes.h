// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "VulkanCommonHeaders.h"

// The C++ side of the HLSL types shared with the shaders (counterpart of Direct3D12/Shaders/SharedTypes.h,
// which can't be used here because it includes the D3D12 headers). The shaders are compiled with
// -fvk-use-dx-layout, so these structs have the same layout in both backends.
namespace primal::graphics::vulkan::hlsl {

using float4x4 = math::m4x4a;
using float4 = math::v4;
using float3 = math::v3;
using float2 = math::v2;
using uint4 = math::u32v4;
using uint3 = math::u32v3;
using uint2 = math::u32v2;
using uint = u32;

#include "../Direct3D12/Shaders/CommonTypes.hlsli"

} // namespace primal::graphics::vulkan::hlsl
