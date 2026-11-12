// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once

// The SIMD side of the math library: math::vec and math::mat (MathTypes.h) and the functions that work on them.
//
// This header and MathTypes.h are the only places that name DirectXMath (Tools/check_math_usage.py checks that).
// Every function forwards to one DirectXMath function, so the conventions are DirectXMath's: row vectors
// (v * M), matrices stored row-major, quaternions as (x, y, z, w), angles in radians.
//
// Storage types (v3, v4, m4x4, ...) are loaded into a vec or mat, worked on, and stored back:
//     const math::vec p{math::load(position)};
//     math::store(result, math::normalize3(p));
// vec has the operators + - * / (component-wise, and * / with an f32).

#include "../Common/PrimitiveTypes.h"
#include "MathTypes.h"

#include <DirectXMath.h>

namespace primal::math
{

// With MSVC, vec is a class type in the global namespace and its operators are functions in namespace DirectX,
// which argument-dependent lookup doesn't find. Make them visible to everything in namespace primal. (clang and
// GCC use the compiler's own vector operators, and DirectXMath declares none.)
#ifndef _XM_NO_XMVECTOR_OVERLOADS_
namespace vec_operators
{

using DirectX::operator+;
using DirectX::operator-;
using DirectX::operator*;
using DirectX::operator/;
using DirectX::operator+=;
using DirectX::operator-=;
using DirectX::operator*=;
using DirectX::operator/=;

}
#endif

// ---- load: storage type -> vec / mat ------------------------------------------------------------------------

[[nodiscard]] inline auto XM_CALLCONV load(const v2 &v) noexcept -> vec
{
    return DirectX::XMLoadFloat2(&v);
}

[[nodiscard]] inline auto XM_CALLCONV load(const v3 &v) noexcept -> vec
{
    return DirectX::XMLoadFloat3(&v);
}

[[nodiscard]] inline auto XM_CALLCONV load(const v3a &v) noexcept -> vec
{
    return DirectX::XMLoadFloat3A(&v);
}

[[nodiscard]] inline auto XM_CALLCONV load(const v4 &v) noexcept -> vec
{
    return DirectX::XMLoadFloat4(&v);
}

[[nodiscard]] inline auto XM_CALLCONV load(const v4a &v) noexcept -> vec
{
    return DirectX::XMLoadFloat4A(&v);
}

[[nodiscard]] inline auto XM_CALLCONV load(const m3x3 &m) noexcept -> mat
{
    return DirectX::XMLoadFloat3x3(&m);
}

[[nodiscard]] inline auto XM_CALLCONV load(const m4x4 &m) noexcept -> mat
{
    return DirectX::XMLoadFloat4x4(&m);
}

[[nodiscard]] inline auto XM_CALLCONV load(const m4x4a &m) noexcept -> mat
{
    return DirectX::XMLoadFloat4x4A(&m);
}

// ---- store: vec / mat -> storage type -----------------------------------------------------------------------

// Stores the x component.
inline auto XM_CALLCONV store(f32 &dst, DirectX::FXMVECTOR v) noexcept -> void
{
    DirectX::XMStoreFloat(&dst, v);
}

inline auto XM_CALLCONV store(v2 &dst, DirectX::FXMVECTOR v) noexcept -> void
{
    DirectX::XMStoreFloat2(&dst, v);
}

inline auto XM_CALLCONV store(v3 &dst, DirectX::FXMVECTOR v) noexcept -> void
{
    DirectX::XMStoreFloat3(&dst, v);
}

inline auto XM_CALLCONV store(v3a &dst, DirectX::FXMVECTOR v) noexcept -> void
{
    DirectX::XMStoreFloat3A(&dst, v);
}

inline auto XM_CALLCONV store(v4 &dst, DirectX::FXMVECTOR v) noexcept -> void
{
    DirectX::XMStoreFloat4(&dst, v);
}

inline auto XM_CALLCONV store(v4a &dst, DirectX::FXMVECTOR v) noexcept -> void
{
    DirectX::XMStoreFloat4A(&dst, v);
}

inline auto XM_CALLCONV store(m4x4 &dst, DirectX::FXMMATRIX m) noexcept -> void
{
    DirectX::XMStoreFloat4x4(&dst, m);
}

inline auto XM_CALLCONV store(m4x4a &dst, DirectX::FXMMATRIX m) noexcept -> void
{
    DirectX::XMStoreFloat4x4A(&dst, m);
}

[[nodiscard]] inline auto XM_CALLCONV to_v3(DirectX::FXMVECTOR v) noexcept -> v3
{
    v3 result{};
    DirectX::XMStoreFloat3(&result, v);
    return result;
}

[[nodiscard]] inline auto XM_CALLCONV to_v4(DirectX::FXMVECTOR v) noexcept -> v4
{
    v4 result{};
    DirectX::XMStoreFloat4(&result, v);
    return result;
}

// ---- vectors ------------------------------------------------------------------------------------------------

[[nodiscard]] inline auto XM_CALLCONV vec_zero() noexcept -> vec
{
    return DirectX::XMVectorZero();
}

[[nodiscard]] inline auto XM_CALLCONV vec_set(f32 x, f32 y, f32 z, f32 w) noexcept -> vec
{
    return DirectX::XMVectorSet(x, y, z, w);
}

// All four components set to 'value'.
[[nodiscard]] inline auto XM_CALLCONV vec_replicate(f32 value) noexcept -> vec
{
    return DirectX::XMVectorReplicate(value);
}

[[nodiscard]] inline auto XM_CALLCONV get_x(DirectX::FXMVECTOR v) noexcept -> f32
{
    return DirectX::XMVectorGetX(v);
}

// The functions with a 3 work on x, y and z. Those with a scalar result return it in every component: read it
// with get_x() or store() to an f32.
[[nodiscard]] inline auto XM_CALLCONV dot3(DirectX::FXMVECTOR a, DirectX::FXMVECTOR b) noexcept -> vec
{
    return DirectX::XMVector3Dot(a, b);
}

[[nodiscard]] inline auto XM_CALLCONV cross3(DirectX::FXMVECTOR a, DirectX::FXMVECTOR b) noexcept -> vec
{
    return DirectX::XMVector3Cross(a, b);
}

[[nodiscard]] inline auto XM_CALLCONV normalize3(DirectX::FXMVECTOR v) noexcept -> vec
{
    return DirectX::XMVector3Normalize(v);
}

[[nodiscard]] inline auto XM_CALLCONV length_sq3(DirectX::FXMVECTOR v) noexcept -> vec
{
    return DirectX::XMVector3LengthSq(v);
}

// A faster, less exact length.
[[nodiscard]] inline auto XM_CALLCONV length_est3(DirectX::FXMVECTOR v) noexcept -> vec
{
    return DirectX::XMVector3LengthEst(v);
}

[[nodiscard]] inline auto XM_CALLCONV reciprocal_length3(DirectX::FXMVECTOR v) noexcept -> vec
{
    return DirectX::XMVector3ReciprocalLength(v);
}

// Rotates v by a quaternion.
[[nodiscard]] inline auto XM_CALLCONV rotate3(DirectX::FXMVECTOR v, DirectX::FXMVECTOR quaternion) noexcept -> vec
{
    return DirectX::XMVector3Rotate(v, quaternion);
}

// Transforms the point v (w = 1) by m. The result's w is not divided out.
[[nodiscard]] inline auto XM_CALLCONV transform3(DirectX::FXMVECTOR v, DirectX::FXMMATRIX m) noexcept -> vec
{
    return DirectX::XMVector3Transform(v, m);
}

// True when all four components of a and b differ by at most the matching component of 'epsilon'.
[[nodiscard]] inline auto XM_CALLCONV
near_equal4(DirectX::FXMVECTOR a, DirectX::FXMVECTOR b, DirectX::FXMVECTOR epsilon) noexcept -> bool
{
    return DirectX::XMVector4NearEqual(a, b, epsilon);
}

// ---- quaternions --------------------------------------------------------------------------------------------

[[nodiscard]] inline auto XM_CALLCONV quat_identity() noexcept -> vec
{
    return DirectX::XMQuaternionIdentity();
}

// From Euler angles in radians: x is pitch, y is yaw, z is roll. Roll is applied first, then pitch, then yaw.
[[nodiscard]] inline auto XM_CALLCONV quat_from_euler(DirectX::FXMVECTOR angles) noexcept -> vec
{
    return DirectX::XMQuaternionRotationRollPitchYawFromVector(angles);
}

// The same for storage types.
[[nodiscard]] inline auto quat_from_euler(const v3 &angles) noexcept -> v4
{
    return to_v4(DirectX::XMQuaternionRotationRollPitchYawFromVector(DirectX::XMLoadFloat3(&angles)));
}

// 'normal_axis' must have length 1.
[[nodiscard]] inline auto XM_CALLCONV quat_from_axis_angle(DirectX::FXMVECTOR normal_axis, f32 angle) noexcept -> vec
{
    return DirectX::XMQuaternionRotationNormal(normal_axis, angle);
}

// The rotation a followed by the rotation b.
[[nodiscard]] inline auto XM_CALLCONV quat_mul(DirectX::FXMVECTOR a, DirectX::FXMVECTOR b) noexcept -> vec
{
    return DirectX::XMQuaternionMultiply(a, b);
}

// ---- matrices -----------------------------------------------------------------------------------------------

[[nodiscard]] inline auto XM_CALLCONV mat_identity() noexcept -> mat
{
    return DirectX::XMMatrixIdentity();
}

// The transform a followed by the transform b.
[[nodiscard]] inline auto XM_CALLCONV mul(DirectX::FXMMATRIX a, DirectX::CXMMATRIX b) noexcept -> mat
{
    return DirectX::XMMatrixMultiply(a, b);
}

[[nodiscard]] inline auto XM_CALLCONV inverse(DirectX::FXMMATRIX m) noexcept -> mat
{
    return DirectX::XMMatrixInverse(nullptr, m);
}

inline auto XM_CALLCONV set_row(mat &m, u32 row, DirectX::FXMVECTOR v) noexcept -> void
{
    m.r[row] = v;
}

// Scale, then rotate around 'rotation_origin', then translate.
[[nodiscard]] inline auto XM_CALLCONV affine_transform(
    DirectX::FXMVECTOR scale,
    DirectX::FXMVECTOR rotation_origin,
    DirectX::FXMVECTOR rotation_quaternion,
    DirectX::GXMVECTOR translation) noexcept -> mat
{
    return DirectX::XMMatrixAffineTransformation(scale, rotation_origin, rotation_quaternion, translation);
}

// Right-handed view matrix.
[[nodiscard]] inline auto XM_CALLCONV
look_to_rh(DirectX::FXMVECTOR eye_position, DirectX::FXMVECTOR eye_direction, DirectX::FXMVECTOR up_direction) noexcept
    -> mat
{
    return DirectX::XMMatrixLookToRH(eye_position, eye_direction, up_direction);
}

// Right-handed projections to a depth range of [0, 1]. 'fov_y' is in radians. The engine passes the far plane as
// 'near_z' and the near plane as 'far_z' to get reversed Z.
[[nodiscard]] inline auto XM_CALLCONV perspective_fov_rh(f32 fov_y, f32 aspect_ratio, f32 near_z, f32 far_z) noexcept
    -> mat
{
    return DirectX::XMMatrixPerspectiveFovRH(fov_y, aspect_ratio, near_z, far_z);
}

[[nodiscard]] inline auto XM_CALLCONV orthographic_rh(f32 view_width, f32 view_height, f32 near_z, f32 far_z) noexcept
    -> mat
{
    return DirectX::XMMatrixOrthographicRH(view_width, view_height, near_z, far_z);
}

// ---- scalars ------------------------------------------------------------------------------------------------

// Fast polynomial approximations (not the <cmath> functions). Angles are in radians.
[[nodiscard]] inline auto sin(f32 angle) noexcept -> f32
{
    return DirectX::XMScalarSin(angle);
}

[[nodiscard]] inline auto cos(f32 angle) noexcept -> f32
{
    return DirectX::XMScalarCos(angle);
}

[[nodiscard]] inline auto acos(f32 value) noexcept -> f32
{
    return DirectX::XMScalarACos(value);
}

}

#ifndef _XM_NO_XMVECTOR_OVERLOADS_
namespace primal
{

using namespace math::vec_operators;

}
#endif
