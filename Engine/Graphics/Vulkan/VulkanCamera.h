// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
// Port of Direct3D12/D3D12Camera.h (Copyright (c) Arash Khatami); API-independent apart from the names.
#pragma once
#include "VulkanCommonHeaders.h"

namespace primal::graphics::vulkan::camera
{

class VulkanCamera
{
  public:
    explicit VulkanCamera(CameraInitInfo info);

    auto update() -> void;
    auto up(math::v3 up) -> void;
    constexpr auto field_of_view(f32 fov) -> void;
    constexpr auto aspect_ratio(f32 aspect_ratio) -> void;
    constexpr auto view_width(f32 width) -> void;
    constexpr auto view_height(f32 height) -> void;
    constexpr auto near_z(f32 near_z) -> void;
    constexpr auto far_z(f32 far_z) -> void;

    [[nodiscard]] constexpr auto view() const -> DirectX::XMMATRIX
    {
        return _view;
    }
    [[nodiscard]] constexpr auto projection() const -> DirectX::XMMATRIX
    {
        return _projection;
    }
    [[nodiscard]] constexpr auto inverse_projection() const -> DirectX::XMMATRIX
    {
        return _inverse_projection;
    }
    [[nodiscard]] constexpr auto view_projection() const -> DirectX::XMMATRIX
    {
        return _view_projection;
    }
    [[nodiscard]] constexpr auto inverse_view_projection() const -> DirectX::XMMATRIX
    {
        return _inverse_view_projection;
    }
    [[nodiscard]] constexpr auto position() const -> DirectX::XMVECTOR
    {
        return _position;
    }
    [[nodiscard]] constexpr auto direction() const -> DirectX::XMVECTOR
    {
        return _direction;
    }
    [[nodiscard]] constexpr auto up() const -> DirectX::XMVECTOR
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
    DirectX::XMMATRIX _view;
    DirectX::XMMATRIX _projection;
    DirectX::XMMATRIX _inverse_projection;
    DirectX::XMMATRIX _view_projection;
    DirectX::XMMATRIX _inverse_view_projection;
    DirectX::XMVECTOR _position{};
    DirectX::XMVECTOR _direction{};
    DirectX::XMVECTOR _up;
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
[[nodiscard]] auto get(CameraId id) -> VulkanCamera &;
}
