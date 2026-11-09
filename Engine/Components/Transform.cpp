// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "Transform.h"

namespace primal::transform
{
namespace
{

// NOTE: three vectors instead of a union with math::m3x3: an anonymous struct whose members have constructors
//       isn't allowed in a union by GCC (MSVC/clang extension). component::local_frame() builds the matrix.
struct LocalFrame
{
    math::v3 right{1.f, 0.f, 0.f};
    math::v3 up{0.f, 1.f, 0.f};
    math::v3 front{0.f, 0.f, 1.f};
};

utl::vector<math::m4x4> to_world;
utl::vector<math::m4x4> inv_world;
utl::vector<math::v4> rotations;
utl::vector<LocalFrame> local_frames;
utl::vector<math::v3> positions;
utl::vector<math::v3> scales;
utl::vector<u8> has_transform;
utl::vector<u8> changes_from_previous_frame;
u8 read_write_flag;

auto calculate_transform_matrices(id::id_type index) -> void
{
    assert(rotations.size() > index);
    assert(positions.size() > index);
    assert(scales.size() > index);

    using namespace DirectX;
    const XMVECTOR r{XMLoadFloat4(&rotations[index])};
    const XMVECTOR t{XMLoadFloat3(&positions[index])};
    const XMVECTOR s{XMLoadFloat3(&scales[index])};

    XMMATRIX world{XMMatrixAffineTransformation(s, XMQuaternionIdentity(), r, t)};
    XMStoreFloat4x4(&to_world[index], world);

    // NOTE: (F. Luna) Intro to DirectX 12, section 8.2.2
    world.r[3] = XMVectorSet(0.f, 0.f, 0.f, 1.f);
    const XMMATRIX inverse_world{XMMatrixInverse(nullptr, world)};
    XMStoreFloat4x4(&inv_world[index], inverse_world);

    has_transform[index] = 1;
}

auto calculate_local_frame(const math::v4 &rotation, LocalFrame &result) -> void
{
    using namespace DirectX;

    const LocalFrame frame{};

    XMVECTOR right{XMLoadFloat3(&frame.right)};
    XMVECTOR up{XMLoadFloat3(&frame.up)};
    const XMVECTOR rotation_quat{XMLoadFloat4(&rotation)};

    right = XMVector3Normalize(XMVector3Rotate(right, rotation_quat));
    up = XMVector3Normalize(XMVector3Rotate(up, rotation_quat));
    const XMVECTOR front{XMVector3Normalize(XMVector3Cross(right, up))};

    XMStoreFloat3(&result.right, right);
    XMStoreFloat3(&result.up, up);
    XMStoreFloat3(&result.front, front);
}

auto set_rotation(TransformId id, const math::v4 &rotation_quaternion) -> void
{
    const u32 index{id::index(id)};
    rotations[index] = rotation_quaternion;
    calculate_local_frame(rotation_quaternion, local_frames[index]);
    has_transform[index] = 0;
    changes_from_previous_frame[index] |= ComponentFlags::rotation;
}

auto set_position(TransformId id, const math::v3 &position) -> void
{
    const u32 index{id::index(id)};
    positions[index] = position;
    has_transform[index] = 0;
    changes_from_previous_frame[index] |= ComponentFlags::position;
}

auto set_scale(TransformId id, const math::v3 &scale) -> void
{
    const u32 index{id::index(id)};
    scales[index] = scale;
    has_transform[index] = 0;
    changes_from_previous_frame[index] |= ComponentFlags::scale;
}

}

auto create(InitInfo info, game_entity::Entity entity) -> Component
{
    assert(entity.is_valid());
    const id::id_type entity_index{id::index(entity.get_id())};

    if (positions.size() > entity_index)
    {
        const math::v4 rotation{info.rotation};
        rotations[entity_index] = rotation;
        calculate_local_frame(rotation, local_frames[entity_index]);
        positions[entity_index] = math::v3{info.position};
        scales[entity_index] = math::v3{info.scale};
        has_transform[entity_index] = 0;
        changes_from_previous_frame[entity_index] = (u8)ComponentFlags::all;
    }
    else
    {
        assert(positions.size() == entity_index);
        to_world.emplace_back();
        inv_world.emplace_back();
        rotations.emplace_back(info.rotation);
        local_frames.emplace_back();
        calculate_local_frame(rotations.back(), local_frames.back());
        positions.emplace_back(info.position);
        scales.emplace_back(info.scale);
        has_transform.emplace_back((u8)0);
        changes_from_previous_frame.emplace_back((u8)ComponentFlags::all);
    }

    // NOTE: each entity has a transform component. Therefor, id's for transform components
    //       are exactly the same as entity ids.
    return Component{TransformId{entity.get_id()}};
}

auto remove([[maybe_unused]] Component c) -> void
{
    assert(c.is_valid());
}

auto get_transform_matrices(const game_entity::EntityId id, math::m4x4 &world, math::m4x4 &inverse_world) -> void
{
    assert(game_entity::Entity{id}.is_valid());

    const id::id_type entity_index{id::index(id)};
    if (!has_transform[entity_index])
    {
        calculate_transform_matrices(entity_index);
    }

    world = to_world[entity_index];
    inverse_world = inv_world[entity_index];
}

auto get_updated_components_flags(const game_entity::EntityId *const ids, u32 count, u8 *const flags) -> void
{
    assert(ids && count && flags);
    read_write_flag = 1;

    for (u32 i{0}; i < count; ++i)
    {
        assert(game_entity::Entity{ids[i]}.is_valid());
        flags[i] = changes_from_previous_frame[id::index(ids[i])];
    }
}

auto update(const ComponentCache *const cache, u32 count) -> void
{
    assert(cache && count);

    // NOTE: clearing "changes_from_previous_frame" happens once every frame when there will be no reads and the caches
    // are
    //       about to be applied by calling this function (i.e. the rest of the current frame will only have writes).
    if (read_write_flag)
    {
        std::memset(changes_from_previous_frame.data(), 0, changes_from_previous_frame.size());
        read_write_flag = 0;
    }

    for (u32 i{0}; i < count; ++i)
    {
        const ComponentCache &c{cache[i]};
        assert(Component{c.id}.is_valid());

        if (c.flags & ComponentFlags::rotation)
        {
            set_rotation(c.id, c.rotation);
        }

        if (c.flags & ComponentFlags::position)
        {
            set_position(c.id, c.position);
        }

        if (c.flags & ComponentFlags::scale)
        {
            set_scale(c.id, c.scale);
        }
    }
}

auto Component::rotation() const -> math::v4
{
    assert(is_valid());
    return rotations[id::index(_id)];
}

auto Component::position() const -> math::v3
{
    assert(is_valid());
    return positions[id::index(_id)];
}

auto Component::scale() const -> math::v3
{
    assert(is_valid());
    return scales[id::index(_id)];
}

auto Component::right() const -> math::v3
{
    assert(is_valid());
    return local_frames[id::index(_id)].right;
}

auto Component::up() const -> math::v3
{
    assert(is_valid());
    return local_frames[id::index(_id)].up;
}

auto Component::front() const -> math::v3
{
    assert(is_valid());
    return local_frames[id::index(_id)].front;
}

auto Component::local_frame() const -> math::m3x3
{
    assert(is_valid());
    const struct LocalFrame &frame{local_frames[id::index(_id)]};
    // Rows: right, up, front (the layout of the former union).
    return {
        frame.right.x,
        frame.right.y,
        frame.right.z,
        frame.up.x,
        frame.up.y,
        frame.up.z,
        frame.front.x,
        frame.front.y,
        frame.front.z};
}

auto Component::calculate_local_position(math::v3 delta) const -> DirectX::XMVECTOR
{
    assert(is_valid());
    const id::id_type index{id::index(_id)};
    using namespace DirectX;
    const math::m3x3 frame_rows{local_frame()};
    const XMMATRIX frame{XMLoadFloat3x3(&frame_rows)};
    XMVECTOR l_pos{XMLoadFloat3(&delta)};
    l_pos = XMVector3Transform(l_pos, frame);
    const XMVECTOR w_pos{XMLoadFloat3(&positions[index])};

    return w_pos + l_pos;
}

auto Component::calculate_absolute_rotation(math::v3 rotation) const -> DirectX::XMVECTOR
{
    assert(is_valid());
    return DirectX::XMQuaternionRotationRollPitchYawFromVector(XMLoadFloat3(&rotation));
}

auto Component::calculate_local_rotation(math::v3 delta) const -> DirectX::XMVECTOR
{
    assert(is_valid());
    const id::id_type index{id::index(_id)};

    using namespace DirectX;
    const XMVECTOR d{XMQuaternionRotationRollPitchYawFromVector(XMLoadFloat3(&delta))};
    const XMVECTOR q{XMLoadFloat4(&rotations[index])};

    return XMQuaternionMultiply(d, q);
}

auto Component::calculate_world_rotation(math::v3 delta) const -> DirectX::XMVECTOR
{
    assert(is_valid());
    math::v3 axis{1.f, 0.f, 0.f};
    f32 angle{delta.x};

    if (std::abs(delta.x) < math::epsilon)
    {
        if (std::abs(delta.z) < math::epsilon)
        {
            axis = {0.f, 1.f, 0.f};
            angle = delta.y;
        }
        else if (std::abs(delta.y) < math::epsilon)
        {
            axis = {0.f, 0.f, 1.f};
            angle = delta.z;
        }
    }

    const id::id_type index{id::index(_id)};

    using namespace DirectX;
    const XMVECTOR a{XMLoadFloat3(&axis)};
    const XMVECTOR d{XMQuaternionRotationNormal(a, angle)};
    const XMVECTOR q{XMLoadFloat4(&rotations[index])};

    return XMQuaternionMultiply(q, d);
}

}
