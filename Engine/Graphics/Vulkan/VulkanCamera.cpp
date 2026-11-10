// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
// Port of Direct3D12/D3D12Camera.cpp (Copyright (c) Arash Khatami); API-independent apart from the names.
#include "VulkanCamera.h"

#include "EngineAPI/GameEntity.h"

namespace primal::graphics::vulkan::camera
{
namespace
{

utl::free_list<VulkanCamera> cameras;

auto set_up_vector(VulkanCamera &camera, const void *const data, [[maybe_unused]] u32 size) -> void
{
    const math::v3 up_vector{*static_cast<const math::v3 *>(data)};
    assert(sizeof(up_vector) == size);
    camera.up(up_vector);
}

constexpr auto set_field_of_view(VulkanCamera &camera, const void *const data, [[maybe_unused]] u32 size) -> void
{
    assert(camera.projection_type() == graphics::Camera::perspective);
    const f32 fov{*static_cast<const f32 *>(data)};
    assert(sizeof(fov) == size);
    camera.field_of_view(fov);
}

constexpr auto set_aspect_ratio(VulkanCamera &camera, const void *const data, [[maybe_unused]] u32 size) -> void
{
    assert(camera.projection_type() == graphics::Camera::perspective);
    const f32 aspect_ratio{*static_cast<const f32 *>(data)};
    assert(sizeof(aspect_ratio) == size);
    camera.aspect_ratio(aspect_ratio);
}

constexpr auto set_view_width(VulkanCamera &camera, const void *const data, [[maybe_unused]] u32 size) -> void
{
    assert(camera.projection_type() == graphics::Camera::orthographic);
    const f32 view_width{*static_cast<const f32 *>(data)};
    assert(sizeof(view_width) == size);
    camera.view_width(view_width);
}

constexpr auto set_view_height(VulkanCamera &camera, const void *const data, [[maybe_unused]] u32 size) -> void
{
    assert(camera.projection_type() == graphics::Camera::orthographic);
    const f32 view_height{*static_cast<const f32 *>(data)};
    assert(sizeof(view_height) == size);
    camera.view_height(view_height);
}

constexpr auto set_near_z(VulkanCamera &camera, const void *const data, [[maybe_unused]] u32 size) -> void
{
    const f32 near_z{*static_cast<const f32 *>(data)};
    assert(sizeof(near_z) == size);
    camera.near_z(near_z);
}

constexpr auto set_far_z(VulkanCamera &camera, const void *const data, [[maybe_unused]] u32 size) -> void
{
    const f32 far_z{*static_cast<const f32 *>(data)};
    assert(sizeof(far_z) == size);
    camera.far_z(far_z);
}

auto get_view(const VulkanCamera &camera, void *const data, [[maybe_unused]] u32 size) -> void
{
    math::m4x4 *const matrix{static_cast<math::m4x4 *>(data)};
    assert(sizeof(math::m4x4) == size);
    DirectX::XMStoreFloat4x4(matrix, camera.view());
}

auto get_projection(const VulkanCamera &camera, void *const data, [[maybe_unused]] u32 size) -> void
{
    math::m4x4 *const matrix{static_cast<math::m4x4 *>(data)};
    assert(sizeof(math::m4x4) == size);
    DirectX::XMStoreFloat4x4(matrix, camera.projection());
}

auto get_inverse_projection(const VulkanCamera &camera, void *const data, [[maybe_unused]] u32 size) -> void
{
    math::m4x4 *const matrix{static_cast<math::m4x4 *>(data)};
    assert(sizeof(math::m4x4) == size);
    DirectX::XMStoreFloat4x4(matrix, camera.inverse_projection());
}

auto get_view_projection(const VulkanCamera &camera, void *const data, [[maybe_unused]] u32 size) -> void
{
    math::m4x4 *const matrix{static_cast<math::m4x4 *>(data)};
    assert(sizeof(math::m4x4) == size);
    DirectX::XMStoreFloat4x4(matrix, camera.view_projection());
}

auto get_inverse_view_projection(const VulkanCamera &camera, void *const data, [[maybe_unused]] u32 size) -> void
{
    math::m4x4 *const matrix{static_cast<math::m4x4 *>(data)};
    assert(sizeof(math::m4x4) == size);
    DirectX::XMStoreFloat4x4(matrix, camera.inverse_view_projection());
}

auto get_up_vector(const VulkanCamera &camera, void *const data, [[maybe_unused]] u32 size) -> void
{
    math::v3 *const up_vector{static_cast<math::v3 *>(data)};
    assert(sizeof(math::v3) == size);
    DirectX::XMStoreFloat3(up_vector, camera.up());
}

constexpr auto get_field_of_view(const VulkanCamera &camera, void *const data, [[maybe_unused]] u32 size) -> void
{
    assert(camera.projection_type() == graphics::Camera::perspective);
    f32 *const fov{static_cast<f32 *>(data)};
    assert(sizeof(f32) == size);
    *fov = camera.field_of_view();
}

constexpr auto get_aspect_ratio(const VulkanCamera &camera, void *const data, [[maybe_unused]] u32 size) -> void
{
    assert(camera.projection_type() == graphics::Camera::perspective);
    f32 *const aspect_ratio{static_cast<f32 *>(data)};
    assert(sizeof(f32) == size);
    *aspect_ratio = camera.aspect_ratio();
}

constexpr auto get_view_width(const VulkanCamera &camera, void *const data, [[maybe_unused]] u32 size) -> void
{
    assert(camera.projection_type() == graphics::Camera::orthographic);
    f32 *const view_width{static_cast<f32 *>(data)};
    assert(sizeof(f32) == size);
    *view_width = camera.view_width();
}

constexpr auto get_view_height(const VulkanCamera &camera, void *const data, [[maybe_unused]] u32 size) -> void
{
    assert(camera.projection_type() == graphics::Camera::orthographic);
    f32 *const view_height{static_cast<f32 *>(data)};
    assert(sizeof(f32) == size);
    *view_height = camera.view_height();
}

constexpr auto get_near_z(const VulkanCamera &camera, void *const data, [[maybe_unused]] u32 size) -> void
{
    f32 *const near_z{static_cast<f32 *>(data)};
    assert(sizeof(f32) == size);
    *near_z = camera.near_z();
}

constexpr auto get_far_z(const VulkanCamera &camera, void *const data, [[maybe_unused]] u32 size) -> void
{
    f32 *const far_z{static_cast<f32 *>(data)};
    assert(sizeof(f32) == size);
    *far_z = camera.far_z();
}

constexpr auto get_projection_type(const VulkanCamera &camera, void *const data, [[maybe_unused]] u32 size) -> void
{
    graphics::Camera::Type *const type{static_cast<graphics::Camera::Type *>(data)};
    assert(sizeof(graphics::Camera::Type) == size);
    *type = camera.projection_type();
}

constexpr auto get_entity_id(const VulkanCamera &camera, void *const data, [[maybe_unused]] u32 size) -> void
{
    id::id_type *const entity_id{static_cast<id::id_type *>(data)};
    assert(sizeof(id::id_type) == size);
    *entity_id = camera.entity_id();
}

constexpr auto dummy_set(VulkanCamera &, const void *const, u32) -> void
{
}

using set_function = void (*)(VulkanCamera &, const void *const, u32);
using get_function = void (*)(const VulkanCamera &, void *const, u32);
constexpr set_function set_functions[]{
    set_up_vector,
    set_field_of_view,
    set_aspect_ratio,
    set_view_width,
    set_view_height,
    set_near_z,
    set_far_z,
    dummy_set,
    dummy_set,
    dummy_set,
    dummy_set,
    dummy_set,
    dummy_set,
    dummy_set,
};

static_assert(std::size(set_functions) == CameraParameter::count);

constexpr get_function get_functions[]{
    get_up_vector,
    get_field_of_view,
    get_aspect_ratio,
    get_view_width,
    get_view_height,
    get_near_z,
    get_far_z,
    get_view,
    get_projection,
    get_inverse_projection,
    get_view_projection,
    get_inverse_view_projection,
    get_projection_type,
    get_entity_id,
};

static_assert(std::size(get_functions) == CameraParameter::count);

}

VulkanCamera::VulkanCamera(CameraInitInfo info)
    : _up{DirectX::XMLoadFloat3(&info.up)}
    , _near_z{info.near_z}
    , _far_z{info.far_z}
    , _field_of_view{info.field_of_view}
    , _aspect_ratio{info.aspect_ratio}
    , _projection_type{info.type}
    , _entity_id{info.entity_id}
{
    assert(id::is_valid(_entity_id));
    update();
}

auto VulkanCamera::update() -> void
{
    const game_entity::Entity entity{game_entity::EntityId{_entity_id}};
    using namespace DirectX;
    const math::v3 pos{entity.position()};
    const math::v3 dir{entity.front()};
    const math::v3 up{entity.up()};
    _position = XMLoadFloat3(&pos);
    _direction = XMLoadFloat3(&dir);
    _up = XMLoadFloat3(&up);
    _view = XMMatrixLookToRH(_position, _direction, _up);

    if (_is_dirty)
    {
        // NOTE: _near_z and _far_z are swapped because we use inverse depth (reversed Z, as in the d3d12 renderer).
        _projection = (_projection_type == graphics::Camera::perspective)
                          ? XMMatrixPerspectiveFovRH(_field_of_view * XM_PI, _aspect_ratio, _far_z, _near_z)
                          : XMMatrixOrthographicRH(_view_width, _view_height, _far_z, _near_z);
        _inverse_projection = XMMatrixInverse(nullptr, _projection);
        _is_dirty = false;
    }

    _view_projection = XMMatrixMultiply(_view, _projection);
    _inverse_view_projection = XMMatrixInverse(nullptr, _view_projection);
}

auto VulkanCamera::up(math::v3 up) -> void
{
    _up = DirectX::XMLoadFloat3(&up);
}

constexpr auto VulkanCamera::field_of_view(f32 fov) -> void
{
    assert(_projection_type == graphics::Camera::perspective);
    _field_of_view = fov;
    _is_dirty = true;
}

constexpr auto VulkanCamera::aspect_ratio(f32 aspect_ratio) -> void
{
    assert(_projection_type == graphics::Camera::perspective);
    _aspect_ratio = aspect_ratio;
    _is_dirty = true;
}

constexpr auto VulkanCamera::view_width(f32 width) -> void
{
    assert(width);
    assert(_projection_type == graphics::Camera::orthographic);
    _view_width = width;
    _is_dirty = true;
}

constexpr auto VulkanCamera::view_height(f32 height) -> void
{
    assert(height);
    assert(_projection_type == graphics::Camera::orthographic);
    _view_height = height;
    _is_dirty = true;
}

constexpr auto VulkanCamera::near_z(f32 near_z) -> void
{
    _near_z = near_z;
    _is_dirty = true;
}

constexpr auto VulkanCamera::far_z(f32 far_z) -> void
{
    _far_z = far_z;
    _is_dirty = true;
}

auto create(CameraInitInfo info) -> graphics::Camera
{
    return graphics::Camera{CameraId{cameras.add(info)}};
}

auto remove(CameraId id) -> void
{
    assert(id::is_valid(id));
    cameras.remove(id);
}

auto set_parameter(CameraId id, CameraParameter::Parameter parameter, const void *const data, u32 data_size) -> void
{
    assert(data && data_size);
    assert(parameter < CameraParameter::count && set_functions[parameter] != dummy_set);
    VulkanCamera &camera{get(id)}; // NOLINT(misc-const-correctness): set_functions[] take a non-const camera
    set_functions[parameter](camera, data, data_size);
}

auto get_parameter(CameraId id, CameraParameter::Parameter parameter, void *const data, u32 data_size) -> void
{
    assert(data && data_size);
    assert(parameter < CameraParameter::count);
    const VulkanCamera &camera{get(id)};
    get_functions[parameter](camera, data, data_size);
}

auto get(CameraId id) -> VulkanCamera &
{
    assert(id::is_valid(id));
    return cameras[id];
}

}
