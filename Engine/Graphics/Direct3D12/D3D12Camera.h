// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "D3D12CommonHeaders.h"

namespace primal::graphics::d3d12::camera
{

class D3D12Camera
{
  public:
    explicit D3D12Camera(CameraInitInfo info);

    auto update() -> void;
    auto up(math::v3 up) -> void;
    constexpr auto field_of_view(f32 fov) -> void;
    constexpr auto aspect_ratio(f32 aspect_ratio) -> void;
    constexpr auto view_width(f32 width) -> void;
    constexpr auto view_height(f32 height) -> void;
    constexpr auto near_z(f32 near_z) -> void;
    constexpr auto far_z(f32 far_z) -> void;

    [[nodiscard]] constexpr auto view() const -> math::mat
    {
        return _view;
    }
    [[nodiscard]] constexpr auto projection() const -> math::mat
    {
        return _projection;
    }
    [[nodiscard]] constexpr auto inverse_projection() const -> math::mat
    {
        return _inverse_projection;
    }
    [[nodiscard]] constexpr auto view_projection() const -> math::mat
    {
        return _view_projection;
    }
    [[nodiscard]] constexpr auto inverse_view_projection() const -> math::mat
    {
        return _inverse_view_projection;
    }
    [[nodiscard]] constexpr auto position() const -> math::vec
    {
        return _position;
    }
    [[nodiscard]] constexpr auto direction() const -> math::vec
    {
        return _direction;
    }
    [[nodiscard]] constexpr auto up() const -> math::vec
    {
        return _up;
    }
    [[nodiscard]] constexpr auto near_z() const -> f32
    {
        return _near_z;
    }
    [[nodiscard]] constexpr auto far_z() const -> f32
    {
        return _far_z;
    }
    [[nodiscard]] constexpr auto field_of_view() const -> f32
    {
        return _field_of_view;
    }
    [[nodiscard]] constexpr auto aspect_ratio() const -> f32
    {
        return _aspect_ratio;
    }
    [[nodiscard]] constexpr auto view_width() const -> f32
    {
        return _view_width;
    }
    [[nodiscard]] constexpr auto view_height() const -> f32
    {
        return _view_height;
    }
    [[nodiscard]] constexpr auto projection_type() const -> graphics::Camera::Type
    {
        return _projection_type;
    }
    [[nodiscard]] constexpr auto entity_id() const -> id::id_type
    {
        return _entity_id;
    }

  private:
    math::mat _view;
    math::mat _projection;
    math::mat _inverse_projection;
    math::mat _view_projection;
    math::mat _inverse_view_projection;
    math::vec _position{};
    math::vec _direction{};
    math::vec _up;
    f32 _near_z;
    f32 _far_z;
    union
    {
        f32 _field_of_view; // The field of view for perspective camera
        f32 _view_width;    // View width in pixels for orthographic camera
    };
    union
    {
        f32 _aspect_ratio; // Width/height aspect ratio for perspective camera
        f32 _view_height;  // View height in pixels for orthographic camera
    };
    graphics::Camera::Type _projection_type;
    id::id_type _entity_id;
    bool _is_dirty{true};
};

auto create(CameraInitInfo info) -> graphics::Camera;
auto remove(CameraId id) -> void;
auto set_parameter(CameraId id, CameraParameter::Parameter parameter, const void *const data, u32 data_size) -> void;
auto get_parameter(CameraId id, CameraParameter::Parameter parameter, void *const data, u32 data_size) -> void;
[[nodiscard]] auto get(CameraId id) -> D3D12Camera &;
}
