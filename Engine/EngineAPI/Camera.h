// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "CommonHeaders.h"

namespace primal::graphics
{

DEFINE_TYPED_ID(CameraId);

class Camera
{
  public:
    enum Type : u32
    {
        perspective,
        orthographic
    };

    constexpr explicit Camera(CameraId id)
        : _id{id}
    {
    }
    constexpr Camera() = default;
    constexpr auto get_id() const -> CameraId
    {
        return _id;
    }
    constexpr auto is_valid() const -> bool
    {
        return id::is_valid(_id);
    }

    auto up(math::v3 up) const -> void;
    auto field_of_view(f32 fov) const -> void;
    auto aspect_ratio(f32 aspect_ratio) const -> void;
    auto view_width(f32 width) const -> void;
    auto view_height(f32 height) const -> void;
    auto range(f32 near_z, f32 far_z) const -> void;

    auto view() const -> math::m4x4;
    auto projection() const -> math::m4x4;
    auto inverse_projection() const -> math::m4x4;
    auto view_projection() const -> math::m4x4;
    auto inverse_view_projection() const -> math::m4x4;
    auto up() const -> math::v3;
    auto near_z() const -> f32;
    auto far_z() const -> f32;
    auto field_of_view() const -> f32;
    auto aspect_ratio() const -> f32;
    auto view_width() const -> f32;
    auto view_height() const -> f32;
    auto projection_type() const -> Type;
    auto entity_id() const -> id::id_type;

  private:
    CameraId _id{id::invalid_id};
};
}
