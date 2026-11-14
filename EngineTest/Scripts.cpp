// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "Components/Entity.h"
#include "Components/Script.h"
#include "Components/Transform.h"
#include "EngineAPI/Input.h"

using namespace primal;

class RotatorScript : public script::EntityScript
{
  public:
    constexpr explicit RotatorScript(game_entity::Entity entity)
        : script::EntityScript{entity}
    {
    }

    auto begin_play() -> void override
    {
    }
    auto update(f32 dt) -> void override
    {
        _angle += 0.1f * dt * math::two_pi;
        if (_angle > math::two_pi)
        {
            _angle -= math::two_pi;
        }
        const math::v3a rot{0.f, _angle, 0.f};
        const math::v4 rot_quat{math::quat_from_euler(rot)};
        set_rotation(rot_quat);
    }

  private:
    f32 _angle{0.f};
};
PRIMAL_SCRIPT(RotatorScript);

class FanScript : public script::EntityScript
{
  public:
    constexpr explicit FanScript(game_entity::Entity entity)
        : script::EntityScript{entity}
    {
    }

    auto begin_play() -> void override
    {
    }
    auto update(f32 dt) -> void override
    {
        _angle -= 1.f * dt * math::two_pi;
        if (_angle > math::two_pi)
        {
            _angle += math::two_pi;
        }
        const math::v3a rot{_angle, 0.f, 0.f};
        const math::v4 rot_quat{math::quat_from_euler(rot)};
        set_rotation(rot_quat);
    }

  private:
    f32 _angle{0.f};
};
PRIMAL_SCRIPT(FanScript);

class WibblyWobblyScript : public script::EntityScript
{
  public:
    constexpr explicit WibblyWobblyScript(game_entity::Entity entity)
        : script::EntityScript{entity}
    {
    }

    auto begin_play() -> void override
    {
    }
    auto update(f32 dt) -> void override
    {
        _angle -= 0.01f * dt * math::two_pi;
        if (_angle > math::two_pi)
        {
            _angle += math::two_pi;
        }
        f32 x{_angle * 2.f - math::pi};
        const f32 s1{0.05f * std::sin(x) * std::sin(std::sin(x / 1.62f) + std::sin(1.62f * x) + std::sin(3.24f * x))};
        x = _angle;
        const f32 s2{0.05f * std::sin(x) * std::sin(std::sin(x / 1.62f) + std::sin(1.62f * x) + std::sin(3.24f * x))};

        const math::v3a rot{s1, 0.f, s2};
        const math::v4 rot_quat{math::quat_from_euler(rot)};
        set_rotation(rot_quat);
        math::v3 pos{position()};
        pos.y = 1.3f + 0.2f * std::sin(x) * std::sin(std::sin(x / 1.62f) + std::sin(1.62f * x) + std::sin(3.24f * x));
        set_position(pos);
    }

  private:
    f32 _angle{0.f};
};
PRIMAL_SCRIPT(WibblyWobblyScript);

class CameraScript : public script::EntityScript
{
  public:
    explicit CameraScript(game_entity::Entity entity)
        : script::EntityScript{entity}
    {
        _input_system.add_handler(input::InputSource::mouse, this, &CameraScript::mouse_move);

        const u64 binding{std::hash<std::string>()("move")};
        _input_system.add_handler(binding, this, &CameraScript::on_move);

        const math::v3 pos{position()};
        _desired_position = _position = math::load(pos);

        const math::v3 dir{front()};
        const f32 theta{math::acos(dir.y)};
        const f32 phi{std::atan2(-dir.z, dir.x)};
        const math::v3 rot{theta - math::half_pi, phi + math::half_pi, 0.f};
        _desired_spherical = _spherical = math::load(rot);
    }

    auto begin_play() -> void override
    {
    }
    auto update(f32 dt) -> void override
    {
        if (_move_magnitude > math::epsilon)
        {
            const f32 fps_scale{dt / 0.016667f};
            const math::v4 rot{rotation()};
            const math::vec d{math::rotate3(_move * 0.05f * fps_scale, math::load(rot))};
            if (_position_acceleration < 1.f)
            {
                _position_acceleration += (0.02f * fps_scale);
            }
            _desired_position += (d * _position_acceleration);
            _move_position = true;
        }
        else if (_move_position)
        {
            _position_acceleration = 0.f;
        }

        if (_move_position || _move_rotation)
        {
            camera_seek(dt);
        }
    }

  private:
    auto on_move(u64 /*binding*/, const input::InputValue &value) -> void
    {
        _move = math::load(value.current);
        _move_magnitude = math::get_x(math::length_sq3(_move));
    }

    auto mouse_move(input::InputSource::Type /*type*/, input::InputCode::Code code, const input::InputValue &mouse_pos)
        -> void
    {
        if (code == input::InputCode::mouse_position)
        {
            input::InputValue value;
            input::get(input::InputSource::mouse, input::InputCode::mouse_left, value);
            if (value.current.z == 0.f)
            {
                return;
            }

            const f32 scale{0.005f};
            const f32 dx{(mouse_pos.current.x - mouse_pos.previous.x) * scale};
            const f32 dy{(mouse_pos.current.y - mouse_pos.previous.y) * scale};

            math::v3 spherical;
            math::store(spherical, _desired_spherical);
            spherical.x += dy;
            spherical.y -= dx;
            spherical.x = math::clamp(spherical.x, 0.0001f - math::half_pi, math::half_pi - 0.0001f);

            _desired_spherical = math::load(spherical);
            _move_rotation = true;
        }
    }

    auto camera_seek(f32 dt) -> void
    {
        const math::vec p{_desired_position - _position};
        const math::vec o{_desired_spherical - _spherical};

        _move_position = (math::get_x(math::length_sq3(p)) > math::epsilon);
        _move_rotation = (math::get_x(math::length_sq3(o)) > math::epsilon);

        const f32 scale{0.2f * dt / 0.016667f};

        if (_move_position)
        {
            _position += (p * scale);
            math::v3 new_pos;
            math::store(new_pos, _position);
            set_position(new_pos);
        }

        if (_move_rotation)
        {
            _spherical += (o * scale);
            math::v3 new_rot;
            math::store(new_rot, _spherical);
            new_rot.x = math::clamp(new_rot.x, 0.0001f - math::half_pi, math::half_pi - 0.0001f);
            _spherical = math::load(new_rot);

            const math::vec quat{math::quat_from_euler(_spherical)};
            math::v4 rot_quat;
            math::store(rot_quat, quat);
            set_rotation(rot_quat);
        }
    }

    input::InputSystem<CameraScript> _input_system{};

    math::vec _desired_position;
    math::vec _desired_spherical;
    math::vec _position;
    math::vec _spherical;
    math::vec _move{};
    f32 _move_magnitude{0.f};
    f32 _position_acceleration{0.f};
    bool _move_position{false};
    bool _move_rotation{false};
};
PRIMAL_SCRIPT(CameraScript);
