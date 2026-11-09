// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "CommonHeaders.h"

namespace primal::graphics
{

DEFINE_TYPED_ID(LightId);

class Light
{
  public:
    enum Type : u32
    {
        directional,
        point,
        spot,
        ambient,

        count
    };

    constexpr explicit Light(LightId id, u64 light_set_key)
        : _light_set_key{light_set_key}
        , _id{id}
    {
    }
    constexpr Light() = default;
    constexpr auto get_id() const -> LightId
    {
        return _id;
    }
    constexpr auto light_set_key() const -> u64
    {
        return _light_set_key;
    }
    constexpr auto is_valid() const -> bool
    {
        return id::is_valid(_id);
    }

    auto is_enabled(bool is_enabled) const -> void;
    auto intensity(f32 intensity) const -> void;
    auto color(math::v3 color) const -> void;
    auto attenuation(math::v3 attenuation) const -> void;
    auto range(f32 range) const -> void;
    auto cone_angles(f32 umbra, f32 penumbra) const -> void;

    auto is_enabled() const -> bool;
    auto intensity() const -> f32;
    auto color() const -> math::v3;
    auto attenuation() const -> math::v3;
    auto range() const -> f32;
    auto umbra() const -> f32;
    auto penumbra() const -> f32;
    auto light_type() const -> Type;
    auto entity_id() const -> id::id_type;

  private:
    u64 _light_set_key{0};
    LightId _id{id::invalid_id};
};
}
