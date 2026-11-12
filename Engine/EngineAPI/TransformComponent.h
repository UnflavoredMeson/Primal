// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "../Common/CommonHeaders.h"

namespace primal::transform
{

struct Space
{
    enum Frame : u32
    {
        absolute,
        local,
        world,
    };
};

DEFINE_TYPED_ID(TransformId);

class Component final
{
  public:
    constexpr explicit Component(TransformId id)
        : _id{id}
    {
    }
    constexpr Component()
        : _id{id::invalid_id}
    {
    }
    constexpr auto get_id() const -> TransformId
    {
        return _id;
    }
    constexpr auto is_valid() const -> bool
    {
        return id::is_valid(_id);
    }

    // TODO: we might want to add a parameter to rotation and position
    //       to get them with respect to the local frame or world space.
    auto rotation() const -> math::v4;
    auto position() const -> math::v3;
    auto scale() const -> math::v3;
    auto right() const -> math::v3;
    auto up() const -> math::v3;
    auto front() const -> math::v3;
    auto local_frame() const -> math::m3x3;

    auto calculate_local_position(math::v3 delta) const -> math::vec;
    auto calculate_absolute_rotation(math::v3 rotation) const -> math::vec;
    auto calculate_local_rotation(math::v3 delta) const -> math::vec;
    auto calculate_world_rotation(math::v3 delta) const -> math::vec;

  private:
    TransformId _id;
};

}
