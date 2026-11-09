// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "Components/Entity.h"
#include "Components/Transform.h"
#include "Components/Script.h"
#include "EngineAPI/Input.h"

using namespace primal;

class RotatorScript;
REGISTER_SCRIPT(RotatorScript);
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
            _angle -= math::two_pi;
        math::v3a rot{0.f, _angle, 0.f};
        DirectX::XMVECTOR quat{DirectX::XMQuaternionRotationRollPitchYawFromVector(DirectX::XMLoadFloat3A(&rot))};
        math::v4 rot_quat{};
        DirectX::XMStoreFloat4(&rot_quat, quat);
        set_rotation(rot_quat);
    }

  private:
    f32 _angle{0.f};
};

class FanScript;
REGISTER_SCRIPT(FanScript);
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
            _angle += math::two_pi;
        math::v3a rot{_angle, 0.f, 0.f};
        DirectX::XMVECTOR quat{DirectX::XMQuaternionRotationRollPitchYawFromVector(DirectX::XMLoadFloat3A(&rot))};
        math::v4 rot_quat{};
        DirectX::XMStoreFloat4(&rot_quat, quat);
        set_rotation(rot_quat);
    }

  private:
    f32 _angle{0.f};
};

class WibblyWobblyScript;
REGISTER_SCRIPT(WibblyWobblyScript);
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
            _angle += math::two_pi;
        f32 x{_angle * 2.f - math::pi};
        const f32 s1{0.05f * std::sin(x) * std::sin(std::sin(x / 1.62f) + std::sin(1.62f * x) + std::sin(3.24f * x))};
        x = _angle;
        const f32 s2{0.05f * std::sin(x) * std::sin(std::sin(x / 1.62f) + std::sin(1.62f * x) + std::sin(3.24f * x))};

        math::v3a rot{s1, 0.f, s2};
        DirectX::XMVECTOR quat{DirectX::XMQuaternionRotationRollPitchYawFromVector(DirectX::XMLoadFloat3A(&rot))};
        math::v4 rot_quat{};
        DirectX::XMStoreFloat4(&rot_quat, quat);
        set_rotation(rot_quat);
        math::v3 pos{position()};
        pos.y = 1.3f + 0.2f * std::sin(x) * std::sin(std::sin(x / 1.62f) + std::sin(1.62f * x) + std::sin(3.24f * x));
        set_position(pos);
    }

  private:
    f32 _angle{0.f};
};

class CameraScript;
REGISTER_SCRIPT(CameraScript);
class CameraScript : public script::EntityScript
{
  public:
    explicit CameraScript(game_entity::Entity entity)
        : script::EntityScript{entity}
    {
        _input_system.add_handler(input::InputSource::mouse, this, &CameraScript::mouse_move);

        const u64 binding{std::hash<std::string>()("move")};
        _input_system.add_handler(binding, this, &CameraScript::on_move);

        math::v3 pos{position()};
        _desired_position = _position = DirectX::XMLoadFloat3(&pos);

        math::v3 dir{front()};
        f32 theta{DirectX::XMScalarACos(dir.y)};
        f32 phi{std::atan2(-dir.z, dir.x)};
        math::v3 rot{theta - math::half_pi, phi + math::half_pi, 0.f};
        _desired_spherical = _spherical = DirectX::XMLoadFloat3(&rot);
    }

    auto begin_play() -> void override
    {
    }
    auto update(f32 dt) -> void override
    {
        using namespace DirectX;

        if (_move_magnitude > math::epsilon)
        {
            const f32 fps_scale{dt / 0.016667f};
            math::v4 rot{rotation()};
            XMVECTOR d{XMVector3Rotate(_move * 0.05f * fps_scale, XMLoadFloat4(&rot))};
            if (_position_acceleration < 1.f)
                _position_acceleration += (0.02f * fps_scale);
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
    auto on_move(u64 binding, const input::InputValue &value) -> void
    {
        using namespace DirectX;

        _move = XMLoadFloat3(&value.current);
        _move_magnitude = XMVectorGetX(XMVector3LengthSq(_move));
    }

    auto mouse_move(input::InputSource::Type type, input::InputCode::Code code, const input::InputValue &mouse_pos)
        -> void
    {
        using namespace DirectX;

        if (code == input::InputCode::mouse_position)
        {
            input::InputValue value;
            input::get(input::InputSource::mouse, input::InputCode::mouse_left, value);
            if (value.current.z == 0.f)
                return;

            const f32 scale{0.005f};
            const f32 dx{(mouse_pos.current.x - mouse_pos.previous.x) * scale};
            const f32 dy{(mouse_pos.current.y - mouse_pos.previous.y) * scale};

            math::v3 spherical;
            DirectX::XMStoreFloat3(&spherical, _desired_spherical);
            spherical.x += dy;
            spherical.y -= dx;
            spherical.x = math::clamp(spherical.x, 0.0001f - math::half_pi, math::half_pi - 0.0001f);

            _desired_spherical = DirectX::XMLoadFloat3(&spherical);
            _move_rotation = true;
        }
    }

    auto camera_seek(f32 dt) -> void
    {
        using namespace DirectX;
        XMVECTOR p{_desired_position - _position};
        XMVECTOR o{_desired_spherical - _spherical};

        _move_position = (XMVectorGetX(XMVector3LengthSq(p)) > math::epsilon);
        _move_rotation = (XMVectorGetX(XMVector3LengthSq(o)) > math::epsilon);

        const f32 scale{0.2f * dt / 0.016667f};

        if (_move_position)
        {
            _position += (p * scale);
            math::v3 new_pos;
            XMStoreFloat3(&new_pos, _position);
            set_position(new_pos);
        }

        if (_move_rotation)
        {
            _spherical += (o * scale);
            math::v3 new_rot;
            XMStoreFloat3(&new_rot, _spherical);
            new_rot.x = math::clamp(new_rot.x, 0.0001f - math::half_pi, math::half_pi - 0.0001f);
            _spherical = DirectX::XMLoadFloat3(&new_rot);

            DirectX::XMVECTOR quat{DirectX::XMQuaternionRotationRollPitchYawFromVector(_spherical)};
            math::v4 rot_quat;
            DirectX::XMStoreFloat4(&rot_quat, quat);
            set_rotation(rot_quat);
        }
    }

    input::InputSystem<CameraScript> _input_system{};

    DirectX::XMVECTOR _desired_position;
    DirectX::XMVECTOR _desired_spherical;
    DirectX::XMVECTOR _position;
    DirectX::XMVECTOR _spherical;
    DirectX::XMVECTOR _move{};
    f32 _move_magnitude{0.f};
    f32 _position_acceleration{0.f};
    bool _move_position{false};
    bool _move_rotation{false};
};
