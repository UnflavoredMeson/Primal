// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
// Port of Direct3D12/D3D12Light.cpp (Copyright (c) Arash Khatami). Only the light buffers differ (see
// vulkan_light_buffer).
#include "VulkanLight.h"

#include <algorithm>
#include "VulkanCore.h"
#include "VulkanResources.h"
#include "Graphics/Shaders/SharedTypes.h"
#include "EngineAPI/GameEntity.h"
#include "Components/Transform.h"
#include "VulkanContent.h"

namespace primal::graphics::vulkan::light
{
namespace
{

template <u32 n>
struct U32SetBits
{
    static_assert(n > 0 && n <= 32);
    constexpr static const u32 bits{U32SetBits<n - 1>::bits | (1 << (n - 1))};
};

template <>
struct U32SetBits<0>
{
    constexpr static const u32 bits{0};
};

static_assert(U32SetBits<frame_buffer_count>::bits < (1 << 8), "That's quite a large frame buffer count!");

constexpr u8 dirty_bits_mask{(u8)U32SetBits<frame_buffer_count>::bits};

struct LightOwner
{
    game_entity::EntityId entity_id{id::invalid_id};
    u32 data_index{u32_invalid_id};
    graphics::Light::Type type;
    bool is_enabled;
};

// NOTE (to myself): don't forget to #undef CONSTEXPR when you copy/paste this block of code!
#if USE_STL_VECTOR
#define CONSTEXPR
#else
#define CONSTEXPR constexpr
#endif

class LightSet
{
  public:
    constexpr auto add(const LightInitInfo &info) -> graphics::Light
    {
        if (info.type == graphics::Light::directional)
        {
            u32 index{u32_invalid_id};
            // Find an available slot in the array if any.
            for (u32 i{0}; i < _non_cullable_owners.size(); ++i)
            {
                if (!id::is_valid(_non_cullable_owners[i]))
                {
                    index = i;
                    break;
                }
            }

            if (index == u32_invalid_id)
            {
                index = (u32)_non_cullable_owners.size();
                _non_cullable_owners.emplace_back();
                _non_cullable_lights.emplace_back();
            }

            hlsl::DirectionalLightParameters &params{_non_cullable_lights[index]};
            params.Color = info.color;
            params.Intensity = info.intensity;

            const LightOwner owner{game_entity::EntityId{info.entity_id}, index, info.type, info.is_enabled};
            const LightId id{_owners.add(owner)};
            _non_cullable_owners[index] = id;

            return graphics::Light{id, info.light_set_key};
        }
        else if (info.type == graphics::Light::ambient)
        {
            static_assert(sizeof(graphics::AmbientParams) / sizeof(id::id_type) == 3);
            u32 indices[3]{};
            content::texture::get_descriptor_indices(&info.ambient_params.diffuse_texture_id, 3, &indices[0]);
            assert(!id::is_valid(_ambient_light_id) && _ambient_light.DiffuseSrvIndex == u32_invalid_id);

            _ambient_light.Intensity = info.intensity;
            _ambient_light.DiffuseSrvIndex = indices[0];
            _ambient_light.SpecularSrvIndex = indices[1];
            _ambient_light.BrdfLutSrvIndex = indices[2];

            const LightOwner owner{game_entity::EntityId{info.entity_id}, u32_invalid_id, info.type, info.is_enabled};
            _ambient_light_id = LightId{_owners.add(owner)};
            return graphics::Light{_ambient_light_id, info.light_set_key};
        }
        else
        {
            u32 index{u32_invalid_id};

            // Try to find an empty slot
            for (u32 i{_enabled_light_count}; i < _cullable_owners.size(); ++i)
            {
                if (!id::is_valid(_cullable_owners[i]))
                {
                    index = i;
                    break;
                }
            }

            // If no empty slot was found then add a new item
            if (index == u32_invalid_id)
            {
                index = (u32)_cullable_owners.size();
                _cullable_lights.emplace_back();
                _culling_info.emplace_back();
                _bounding_spheres.emplace_back();
                _cullable_entity_ids.emplace_back();
                _cullable_owners.emplace_back();
                _dirty_bits.emplace_back();
                assert(_cullable_owners.size() == _cullable_lights.size());
                assert(_cullable_owners.size() == _culling_info.size());
                assert(_cullable_owners.size() == _bounding_spheres.size());
                assert(_cullable_owners.size() == _cullable_entity_ids.size());
                assert(_cullable_owners.size() == _dirty_bits.size());
            }

            add_cullable_light_parameters(info, index);
            add_light_culling_info(info, index);
            const LightId id{
                _owners.add(LightOwner{game_entity::EntityId{info.entity_id}, index, info.type, info.is_enabled})};
            _cullable_entity_ids[index] = _owners[id].entity_id;
            _cullable_owners[index] = id;
            make_dirty(index);
            enable(id, info.is_enabled);
            update_transform(index);

            return graphics::Light{id, info.light_set_key};
        }
    }

    constexpr auto remove(LightId id) -> void
    {
        enable(id, false);

        const LightOwner &owner{_owners[id]};

        if (owner.type == graphics::Light::directional)
        {
            _non_cullable_owners[owner.data_index] = LightId{id::invalid_id};
        }
        else if (owner.type == graphics::Light::ambient)
        {
            assert(id == _ambient_light_id);
            _ambient_light = {-1, u32_invalid_id, u32_invalid_id, u32_invalid_id};
            _ambient_light_id = LightId{id::invalid_id};
        }
        else
        {
            assert(_owners[_cullable_owners[owner.data_index]].data_index == owner.data_index);
            _cullable_owners[owner.data_index] = LightId{id::invalid_id};
        }

        _owners.remove(id);
    }

    auto update_transforms() -> void
    {
        // Update direction for non-cullable lights
        for (const auto &id : _non_cullable_owners)
        {
            if (!id::is_valid(id))
            {
                continue;
            }

            const LightOwner &owner{_owners[id]};
            if (owner.is_enabled)
            {
                const game_entity::Entity entity{game_entity::EntityId{owner.entity_id}};
                hlsl::DirectionalLightParameters &params{_non_cullable_lights[owner.data_index]};
                params.Direction = entity.front();
            }
        }

        // Update position and direction of cullable lights

        const u32 count{_enabled_light_count};
        if (!count)
        {
            return;
        }

        assert(_cullable_entity_ids.size() >= count);
        _transform_flags_cache.resize(count);
        transform::get_updated_components_flags(_cullable_entity_ids.data(), count, _transform_flags_cache.data());

        for (u32 i{0}; i < count; ++i)
        {
            if (_transform_flags_cache[i])
            {
                update_transform(i);
            }
        }
    }

    constexpr auto enable(LightId id, bool is_enabled) -> void
    {
        _owners[id].is_enabled = is_enabled;

        if (_owners[id].type == graphics::Light::directional || _owners[id].type == graphics::Light::ambient)
        {
            return;
        }

        // Cullable lights
        const u32 data_index{_owners[id].data_index};

        // NOTE: this is a reference to _enabled_light_count and will change its value!
        u32 &count{_enabled_light_count};

        // NOTE: dirty_bits is going to be set by swap_cullable_lights, so we don't set it here.
        if (is_enabled)
        {
            if (data_index > count)
            {
                assert(count < _cullable_lights.size());
                swap_cullable_lights(data_index, count);
                ++count;
            }
            else if (data_index == count)
            {
                ++count;
            }
        }
        else if (count > 0)
        {
            const u32 last{count - 1};
            if (data_index < last)
            {
                swap_cullable_lights(data_index, last);
                --count;
            }
            else if (data_index == last)
            {
                --count;
            }
        }
    }

    constexpr auto intensity(LightId id, f32 intensity) -> void
    {
        intensity = std::max(intensity, 0.f);

        const LightOwner &owner{_owners[id]};
        const u32 index{owner.data_index};

        if (owner.type == graphics::Light::directional)
        {
            assert(index < _non_cullable_lights.size());
            _non_cullable_lights[index].Intensity = intensity;
        }
        else if (owner.type == graphics::Light::ambient)
        {
            _ambient_light.Intensity = intensity;
        }
        else
        {
            assert(_owners[_cullable_owners[index]].data_index == index);
            assert(index < _cullable_lights.size());
            _cullable_lights[index].Intensity = intensity;
            make_dirty(index);
        }
    }

    constexpr auto color(LightId id, math::v3 color) -> void
    {
        assert(color.x <= 1.f && color.y <= 1.f && color.z <= 1.f);
        assert(color.x >= 0.f && color.y >= 0.f && color.z >= 0.f);

        const LightOwner &owner{_owners[id]};
        const u32 index{owner.data_index};

        if (owner.type == graphics::Light::directional)
        {
            assert(index < _non_cullable_lights.size());
            _non_cullable_lights[index].Color = color;
        }
        else if (owner.type == graphics::Light::ambient)
        {
            // TODO: ambient lights use environment maps. We could use
            //       the SRV indices for RGB values in order to support
            //       ambient lights with solid colors. Maybe use negative
            //       intensity values to indicate that the indices
            //       contains colors instead of pre-filtered cube maps.
        }
        else
        {
            assert(_owners[_cullable_owners[index]].data_index == index);
            assert(index < _cullable_lights.size());
            _cullable_lights[index].Color = color;
            make_dirty(index);
        }
    }

    CONSTEXPR auto attenuation(LightId id, math::v3 attenuation) -> void
    {
        assert(attenuation.x >= 0.f && attenuation.y >= 0.f && attenuation.z >= 0.f);
        const LightOwner &owner{_owners[id]};
        const u32 index{owner.data_index};
        assert(_owners[_cullable_owners[index]].data_index == index);
        assert(owner.type != graphics::Light::directional);
        assert(index < _cullable_lights.size());
        _cullable_lights[index].Attenuation = attenuation;
        make_dirty(index);
    }

    CONSTEXPR auto range(LightId id, f32 range) -> void
    {
        assert(range > 0.f);
        const LightOwner &owner{_owners[id]};
        const u32 index{owner.data_index};
        assert(_owners[_cullable_owners[index]].data_index == index);
        assert(owner.type != graphics::Light::directional);
        assert(index < _cullable_lights.size());
        _cullable_lights[index].Range = range;
        _culling_info[index].Range = range;
        _culling_info[index].CosPenumbra = -1.f;

        _bounding_spheres[index].Radius = range;
        make_dirty(index);

        if (owner.type == graphics::Light::spot)
        {
            calculate_cone_bounding_sphere(_cullable_lights[index], _bounding_spheres[index]);
            _culling_info[index].CosPenumbra = _cullable_lights[index].CosPenumbra;
        }
    }

    auto umbra(LightId id, f32 umbra) -> void
    {
        const LightOwner &owner{_owners[id]};
        const u32 index{owner.data_index};
        assert(_owners[_cullable_owners[index]].data_index == index);
        assert(owner.type == graphics::Light::spot);
        assert(index < _cullable_lights.size());

        umbra = math::clamp(umbra, 0.f, math::pi - math::epsilon);
        _cullable_lights[index].CosUmbra = DirectX::XMScalarCos(umbra * 0.5f);
        make_dirty(index);

        if (penumbra(id) < umbra)
        {
            penumbra(id, umbra);
        }
    }

    auto penumbra(LightId id, f32 penumbra) -> void
    {
        const LightOwner &owner{_owners[id]};
        const u32 index{owner.data_index};
        assert(_owners[_cullable_owners[index]].data_index == index);
        assert(owner.type == graphics::Light::spot);
        assert(index < _cullable_lights.size());

        penumbra = math::clamp(penumbra, umbra(id), math::pi);
        _cullable_lights[index].CosPenumbra = DirectX::XMScalarCos(penumbra * 0.5f);
        calculate_cone_bounding_sphere(_cullable_lights[index], _bounding_spheres[index]);

        _culling_info[index].CosPenumbra = _cullable_lights[index].CosPenumbra;
        make_dirty(index);
    }

    constexpr auto is_enabled(LightId id) const -> bool
    {
        return _owners[id].is_enabled;
    }

    constexpr auto intensity(LightId id) const -> f32
    {
        const LightOwner &owner{_owners[id]};
        const u32 index{owner.data_index};
        if (owner.type == graphics::Light::directional)
        {
            assert(index < _non_cullable_lights.size());
            return _non_cullable_lights[index].Intensity;
        }

        assert(_owners[_cullable_owners[index]].data_index == index);
        assert(index < _cullable_lights.size());
        return _cullable_lights[index].Intensity;
    }

    constexpr auto color(LightId id) const -> math::v3
    {
        const LightOwner &owner{_owners[id]};
        const u32 index{owner.data_index};
        if (owner.type == graphics::Light::directional)
        {
            assert(index < _non_cullable_lights.size());
            return _non_cullable_lights[index].Color;
        }

        assert(_owners[_cullable_owners[index]].data_index == index);
        assert(index < _cullable_lights.size());
        return _cullable_lights[index].Color;
    }

    CONSTEXPR auto attenuation(LightId id) const -> math::v3
    {
        const LightOwner &owner{_owners[id]};
        const u32 index{owner.data_index};
        assert(_owners[_cullable_owners[index]].data_index == index);
        assert(owner.type != graphics::Light::directional);
        assert(index < _cullable_lights.size());
        return _cullable_lights[index].Attenuation;
    }

    CONSTEXPR auto range(LightId id) const -> f32
    {
        const LightOwner &owner{_owners[id]};
        const u32 index{owner.data_index};
        assert(_owners[_cullable_owners[index]].data_index == index);
        assert(owner.type != graphics::Light::directional);
        assert(index < _cullable_lights.size());
        return _cullable_lights[index].Range;
    }

    auto umbra(LightId id) const -> f32
    {
        const LightOwner &owner{_owners[id]};
        const u32 index{owner.data_index};
        assert(_owners[_cullable_owners[index]].data_index == index);
        assert(owner.type == graphics::Light::spot);
        assert(index < _cullable_lights.size());
        return DirectX::XMScalarACos(_cullable_lights[index].CosUmbra) * 2.f;
    }

    auto penumbra(LightId id) const -> f32
    {
        const LightOwner &owner{_owners[id]};
        const u32 index{owner.data_index};
        assert(_owners[_cullable_owners[index]].data_index == index);
        assert(owner.type == graphics::Light::spot);
        assert(index < _cullable_lights.size());
        return DirectX::XMScalarACos(_cullable_lights[index].CosPenumbra) * 2.f;
    }

    constexpr auto type(LightId id) const -> graphics::Light::Type
    {
        return _owners[id].type;
    }

    constexpr auto entity_id(LightId id) const -> id::id_type
    {
        return _owners[id].entity_id;
    }

    // Return the number of enabled directional lights
    CONSTEXPR auto non_cullable_light_count() const -> u32
    {
        u32 count{0};
        for (const auto &id : _non_cullable_owners)
        {
            if (id::is_valid(id) && _owners[id].is_enabled)
            {
                ++count;
            }
        }

        return count;
    }

    CONSTEXPR auto ambient_light() -> hlsl::AmbientLightParameters
    {
        if (id::is_valid(_ambient_light_id) && _owners[_ambient_light_id].is_enabled)
        {
            assert(_owners[_ambient_light_id].type == graphics::Light::ambient);
            return _ambient_light;
        }

        return {-1.f, u32_invalid_id, u32_invalid_id, u32_invalid_id};
    }

    CONSTEXPR auto non_cullable_lights(hlsl::DirectionalLightParameters *const lights, [[maybe_unused]] u32 buffer_size)
        const -> void
    {
        assert(
            buffer_size >= math::align_size_up<ConstantBuffer::alignment>(
                               non_cullable_light_count() * sizeof(hlsl::DirectionalLightParameters)));
        const u32 count{(u32)_non_cullable_owners.size()};
        u32 index{0};
        for (u32 i{0}; i < count; ++i)
        {
            if (!id::is_valid(_non_cullable_owners[i]))
            {
                continue;
            }

            const LightOwner &owner{_owners[_non_cullable_owners[i]]};
            if (owner.is_enabled)
            {
                assert(_owners[_non_cullable_owners[i]].data_index == i);
                lights[index] = _non_cullable_lights[i];
                ++index;
            }
        }
    }

    constexpr auto cullable_light_count() const -> u32
    {
        return _enabled_light_count;
    }

    constexpr auto has_lights() const -> bool
    {
        return !_owners.empty();
    }

  private:
    auto calculate_cone_bounding_sphere(const hlsl::LightParameters &params, hlsl::Sphere &sphere) -> void
    {
        using namespace DirectX;

        const XMVECTOR tip{XMLoadFloat3(&params.Position)};
        const XMVECTOR direction{XMLoadFloat3(&params.Direction)};
        const f32 cone_cos{params.CosPenumbra};
        assert(cone_cos > -0.001f); // use a small negative value, so cos(180) wouldn't trigger the assertion.

        if (cone_cos >= 0.707107f)
        {
            sphere.Radius = params.Range / (2.f * cone_cos);
            XMStoreFloat3(&sphere.Center, tip + sphere.Radius * direction);
        }
        else
        {
            XMStoreFloat3(&sphere.Center, tip + cone_cos * params.Range * direction);
            const f32 cone_sin{std::sqrt(1.f - cone_cos * cone_cos)};
            sphere.Radius = cone_sin * params.Range;
        }
    }

    auto update_transform(u32 index) -> void
    {
        const game_entity::Entity entity{game_entity::EntityId{_cullable_entity_ids[index]}};
        hlsl::LightParameters &params{_cullable_lights[index]};
        params.Position = entity.position();

        hlsl::LightCullingLightInfo &culling_info{_culling_info[index]};
        culling_info.Position = _bounding_spheres[index].Center = params.Position;

        if (_owners[_cullable_owners[index]].type == graphics::Light::spot)
        {
            culling_info.Direction = params.Direction = entity.front();
            calculate_cone_bounding_sphere(params, _bounding_spheres[index]);
        }

        make_dirty(index);
    }

    CONSTEXPR auto add_cullable_light_parameters(const LightInitInfo &info, u32 index) -> void
    {
        using graphics::Light;
        assert(info.type != Light::directional && index < _cullable_lights.size());

        hlsl::LightParameters &params{_cullable_lights[index]};

        params.Color = info.color;
        params.Intensity = info.intensity;

        if (info.type == Light::point)
        {
            const PointLightParams &p{info.point_params};
            params.Attenuation = p.attenuation;
            params.Range = p.range;
        }
        else if (info.type == Light::spot)
        {
            const SpotLightParams &p{info.spot_params};
            params.Attenuation = p.attenuation;
            params.Range = p.range;
            params.CosUmbra = DirectX::XMScalarCos(p.umbra * 0.5f);
            params.CosPenumbra = DirectX::XMScalarCos(p.penumbra * 0.5f);
        }
    }

    CONSTEXPR auto add_light_culling_info(const LightInitInfo &info, u32 index) -> void
    {
        using graphics::Light;
        assert(info.type != Light::directional && index < _culling_info.size());

        const hlsl::LightParameters &params{_cullable_lights[index]};
        hlsl::LightCullingLightInfo &culling_info{_culling_info[index]};
        culling_info.Range = _bounding_spheres[index].Radius = params.Range;
        culling_info.CosPenumbra = -1.f;

        if (info.type == Light::spot)
        {
            culling_info.CosPenumbra = params.CosPenumbra;
        }
    }

    auto swap_cullable_lights(u32 index1, u32 index2) -> void
    {
        assert(index1 != index2);
        assert(index1 < _cullable_owners.size());
        assert(index2 < _cullable_owners.size());
        assert(index1 < _cullable_lights.size());
        assert(index2 < _cullable_lights.size());
        assert(index1 < _culling_info.size());
        assert(index2 < _culling_info.size());
        assert(index1 < _bounding_spheres.size());
        assert(index2 < _bounding_spheres.size());
        assert(index1 < _cullable_entity_ids.size());
        assert(index2 < _cullable_entity_ids.size());
        assert(id::is_valid(_cullable_owners[index1]) || id::is_valid(_cullable_owners[index2]));

        if (!id::is_valid(_cullable_owners[index2]))
        {
            std::swap(index1, index2);
        }

        if (!id::is_valid(_cullable_owners[index1]))
        {
            LightOwner &owner2{_owners[_cullable_owners[index2]]};
            assert(owner2.data_index == index2);
            owner2.data_index = index1;

            _cullable_lights[index1] = _cullable_lights[index2];
            _culling_info[index1] = _culling_info[index2];
            _bounding_spheres[index1] = _bounding_spheres[index2];
            _cullable_entity_ids[index1] = _cullable_entity_ids[index2];
            std::swap(_cullable_owners[index1], _cullable_owners[index2]);
            make_dirty(index1);
            assert(_owners[_cullable_owners[index1]].entity_id == _cullable_entity_ids[index1]);
            assert(!id::is_valid(_cullable_owners[index2]));
        }
        else
        {
            LightOwner &owner1{_owners[_cullable_owners[index1]]};
            LightOwner &owner2{_owners[_cullable_owners[index2]]};
            assert(owner1.data_index == index1);
            assert(owner2.data_index == index2);
            owner1.data_index = index2;
            owner2.data_index = index1;

            std::swap(_cullable_lights[index1], _cullable_lights[index2]);
            std::swap(_culling_info[index1], _culling_info[index2]);
            std::swap(_bounding_spheres[index1], _bounding_spheres[index2]);
            std::swap(_cullable_entity_ids[index1], _cullable_entity_ids[index2]);
            std::swap(_cullable_owners[index1], _cullable_owners[index2]);

            assert(_owners[_cullable_owners[index1]].entity_id == _cullable_entity_ids[index1]);
            assert(_owners[_cullable_owners[index2]].entity_id == _cullable_entity_ids[index2]);

            // set dirty bits
            make_dirty(index1);
            make_dirty(index2);
        }
    }

    CONSTEXPR auto make_dirty(u32 index) -> void
    {
        assert(index < _dirty_bits.size());
        _something_is_dirty = _dirty_bits[index] = dirty_bits_mask;
    }

    // NOTE: these are NOT tightly packed
    utl::free_list<LightOwner> _owners;
    utl::vector<hlsl::DirectionalLightParameters> _non_cullable_lights;
    utl::vector<LightId> _non_cullable_owners;

    // NOTE: these are tightly packed
    utl::vector<hlsl::LightParameters> _cullable_lights;
    utl::vector<hlsl::LightCullingLightInfo> _culling_info;
    utl::vector<hlsl::Sphere> _bounding_spheres;
    utl::vector<game_entity::EntityId> _cullable_entity_ids;
    utl::vector<LightId> _cullable_owners;
    utl::vector<u8> _dirty_bits;

    utl::vector<u8> _transform_flags_cache;
    u32 _enabled_light_count{0}; // number of cullable lights
    u8 _something_is_dirty{0};   // flag is set if any of cullable lights where changed.

    hlsl::AmbientLightParameters _ambient_light{-1.f, u32_invalid_id, u32_invalid_id, u32_invalid_id};
    LightId _ambient_light_id{id::invalid_id};

    friend class VulkanLightBuffer;
};

class VulkanLightBuffer
{
  public:
    VulkanLightBuffer() = default;
    CONSTEXPR auto update_light_buffers(LightSet &set, u64 light_set_key, u32 frame_index) -> void
    {
        const u32 non_cullable_light_count{set.non_cullable_light_count()};

        if (non_cullable_light_count)
        {
            const u32 needed_size{non_cullable_light_count * (u32)sizeof(hlsl::DirectionalLightParameters)};
            const u32 current_size{_buffers[LightBuffer::non_cullable_light].buffer.size()};

            if (current_size < needed_size)
            {
                resize_buffer(LightBuffer::non_cullable_light, needed_size);
            }

            set.non_cullable_lights(
                (hlsl::DirectionalLightParameters *)_buffers[LightBuffer::non_cullable_light].cpu_address,
                _buffers[LightBuffer::non_cullable_light].buffer.size());
        }

        // Update cullable light buffers
        const u32 cullable_light_count{set.cullable_light_count()};

        if (cullable_light_count)
        {
            const u32 needed_light_buffer_size{cullable_light_count * (u32)sizeof(hlsl::LightParameters)};
            const u32 needed_culling_buffer_size{cullable_light_count * (u32)sizeof(hlsl::LightCullingLightInfo)};
            const u32 needed_spheres_buffer_size{cullable_light_count * (u32)sizeof(hlsl::Sphere)};
            const u32 current_light_buffer_size{_buffers[LightBuffer::cullable_light].buffer.size()};

            bool buffers_resized{false};
            if (current_light_buffer_size < needed_light_buffer_size)
            {
                // NOTE: we create buffers about 150% larger than needed to avoid recreating them
                //       everytime a few lights are added.
                resize_buffer(LightBuffer::cullable_light, (needed_light_buffer_size * 3) >> 1);
                resize_buffer(LightBuffer::culling_info, (needed_culling_buffer_size * 3) >> 1);
                resize_buffer(LightBuffer::bounding_spheres, (needed_spheres_buffer_size * 3) >> 1);
                buffers_resized = true;
            }

            const u32 index_mask{1u << frame_index};

            if (buffers_resized || _current_light_set_key != light_set_key)
            {
                std::memcpy(
                    _buffers[LightBuffer::cullable_light].cpu_address,
                    set._cullable_lights.data(),
                    needed_light_buffer_size);
                std::memcpy(
                    _buffers[LightBuffer::culling_info].cpu_address,
                    set._culling_info.data(),
                    needed_culling_buffer_size);
                std::memcpy(
                    _buffers[LightBuffer::bounding_spheres].cpu_address,
                    set._bounding_spheres.data(),
                    needed_spheres_buffer_size);
                _current_light_set_key = light_set_key;

                for (u32 i{0}; i < cullable_light_count; ++i)
                {
                    set._dirty_bits[i] &= ~index_mask;
                }
            }
            else if (set._something_is_dirty)
            {
                for (u32 i{0}; i < cullable_light_count; ++i)
                {
                    if (set._dirty_bits[i] & index_mask)
                    {
                        assert(i * sizeof(hlsl::LightParameters) < needed_light_buffer_size);
                        assert(i * sizeof(hlsl::LightCullingLightInfo) < needed_culling_buffer_size);
                        assert(i * sizeof(hlsl::Sphere) < needed_spheres_buffer_size);
                        u8 *const light_dst{
                            _buffers[LightBuffer::cullable_light].cpu_address + (i * sizeof(hlsl::LightParameters))};
                        u8 *const culling_dst{
                            _buffers[LightBuffer::culling_info].cpu_address +
                            (i * sizeof(hlsl::LightCullingLightInfo))};
                        u8 *const bounding_dst{
                            _buffers[LightBuffer::bounding_spheres].cpu_address + (i * sizeof(hlsl::Sphere))};
                        std::memcpy(light_dst, &set._cullable_lights[i], sizeof(hlsl::LightParameters));
                        std::memcpy(culling_dst, &set._culling_info[i], sizeof(hlsl::LightCullingLightInfo));
                        std::memcpy(bounding_dst, &set._bounding_spheres[i], sizeof(hlsl::Sphere));
                        set._dirty_bits[i] &= ~index_mask;
                    }
                }
            }

            set._something_is_dirty &= ~index_mask;
            assert(_current_light_set_key == light_set_key);
        }
    }

    constexpr auto release() -> void
    {
        for (u32 i{0}; i < LightBuffer::count; ++i)
        {
            _buffers[i].buffer.release();
            _buffers[i].cpu_address = nullptr;
        }
    }

    // NOTE: a buffer doesn't exist until the light set has lights of that kind. Root SRVs need a valid buffer
    //       (D3D12 binds address 0), so core's zero buffer stands in; the shaders don't read it (light count 0).
    [[nodiscard]] auto non_cullable_lights() const -> vkx::BufferRange
    {
        return range(LightBuffer::non_cullable_light);
    }
    [[nodiscard]] auto cullable_lights() const -> vkx::BufferRange
    {
        return range(LightBuffer::cullable_light);
    }
    [[nodiscard]] auto culling_info() const -> vkx::BufferRange
    {
        return range(LightBuffer::culling_info);
    }
    [[nodiscard]] auto bounding_spheres() const -> vkx::BufferRange
    {
        return range(LightBuffer::bounding_spheres);
    }

  private:
    struct LightBuffer
    {
        enum Type : u32
        {
            non_cullable_light,
            cullable_light,
            culling_info,
            bounding_spheres,

            count
        };

        VulkanBuffer buffer;
        u8 *cpu_address{nullptr};
    };

    auto resize_buffer(LightBuffer::Type type, u32 size) -> void
    {
        assert(type < LightBuffer::count);
        if (!size || _buffers[type].buffer.size() >= math::align_size_up<ConstantBuffer::alignment>(size))
        {
            return;
        }

        // CPU-accessible buffers are persistently mapped (D3D12: Map() of an upload heap buffer).
        _buffers[type].buffer = VulkanBuffer{ConstantBuffer::get_default_init_info(size), true};
        NAME_VK_OBJECT_INDEXED(
            _buffers[type].buffer.buffer(),
            VK_OBJECT_TYPE_BUFFER,
            core::current_frame_index(),
            type == LightBuffer::non_cullable_light ? "Non-cullable Light Buffer"
            : type == LightBuffer::cullable_light   ? "Cullable Light Buffer"
            : type == LightBuffer::culling_info     ? "Light Culling Info Buffer"
                                                    : "Bounding Spheres Buffer");
        _buffers[type].cpu_address = _buffers[type].buffer.cpu_address();
        assert(_buffers[type].cpu_address);
    }

    [[nodiscard]] auto range(LightBuffer::Type type) const -> vkx::BufferRange
    {
        const VulkanBuffer &buffer{_buffers[type].buffer};
        return buffer.buffer() ? buffer.range() : core::zero_buffer();
    }

    LightBuffer _buffers[LightBuffer::count]{};
    u64 _current_light_set_key{0};
};

std::unordered_map<u64, LightSet> light_sets;
VulkanLightBuffer light_buffers[frame_buffer_count];

constexpr auto set_is_enabled(LightSet &set, LightId id, const void *const data, [[maybe_unused]] u32 size) -> void
{
    const bool is_enabled{*(bool *)data};
    assert(sizeof(is_enabled) == size);
    set.enable(id, is_enabled);
}

constexpr auto set_intensity(LightSet &set, LightId id, const void *const data, [[maybe_unused]] u32 size) -> void
{
    const f32 intensity{*(f32 *)data};
    assert(sizeof(intensity) == size);
    set.intensity(id, intensity);
}

constexpr auto set_color(LightSet &set, LightId id, const void *const data, [[maybe_unused]] u32 size) -> void
{
    const math::v3 color{*(math::v3 *)data};
    assert(sizeof(color) == size);
    set.color(id, color);
}

CONSTEXPR auto set_attenuation(LightSet &set, LightId id, const void *const data, [[maybe_unused]] u32 size) -> void
{
    const math::v3 attenuation{*(math::v3 *)data};
    assert(sizeof(attenuation) == size);
    set.attenuation(id, attenuation);
}

CONSTEXPR auto set_range(LightSet &set, LightId id, const void *const data, [[maybe_unused]] u32 size) -> void
{
    const f32 range{*(f32 *)data};
    assert(sizeof(range) == size);
    set.range(id, range);
}

auto set_umbra(LightSet &set, LightId id, const void *const data, [[maybe_unused]] u32 size) -> void
{
    const f32 umbra{*(f32 *)data};
    assert(sizeof(umbra) == size);
    set.umbra(id, umbra);
}

auto set_penumbra(LightSet &set, LightId id, const void *const data, [[maybe_unused]] u32 size) -> void
{
    const f32 penumbra{*(f32 *)data};
    assert(sizeof(penumbra) == size);
    set.penumbra(id, penumbra);
}

constexpr auto get_is_enabled(const LightSet &set, LightId id, void *const data, [[maybe_unused]] u32 size) -> void
{
    bool *const is_enabled{(bool *)data};
    assert(sizeof(bool) == size);
    *is_enabled = set.is_enabled(id);
}

constexpr auto get_intensity(const LightSet &set, LightId id, void *const data, [[maybe_unused]] u32 size) -> void
{
    f32 *const intensity{(f32 *)data};
    assert(sizeof(f32) == size);
    *intensity = set.intensity(id);
}

constexpr auto get_color(const LightSet &set, LightId id, void *const data, [[maybe_unused]] u32 size) -> void
{
    math::v3 *const color{(math::v3 *)data};
    assert(sizeof(math::v3) == size);
    *color = set.color(id);
}

CONSTEXPR auto get_attenuation(const LightSet &set, LightId id, void *const data, [[maybe_unused]] u32 size) -> void
{
    math::v3 *const attenuation{(math::v3 *)data};
    assert(sizeof(math::v3) == size);
    *attenuation = set.attenuation(id);
}

CONSTEXPR auto get_range(const LightSet &set, LightId id, void *const data, [[maybe_unused]] u32 size) -> void
{
    f32 *const range{(f32 *)data};
    assert(sizeof(f32) == size);
    *range = set.range(id);
}

auto get_umbra(const LightSet &set, LightId id, void *const data, [[maybe_unused]] u32 size) -> void
{
    f32 *const umbra{(f32 *)data};
    assert(sizeof(f32) == size);
    *umbra = set.umbra(id);
}

auto get_penumbra(const LightSet &set, LightId id, void *const data, [[maybe_unused]] u32 size) -> void
{
    f32 *const penumbra{(f32 *)data};
    assert(sizeof(f32) == size);
    *penumbra = set.penumbra(id);
}

constexpr auto get_type(const LightSet &set, LightId id, void *const data, [[maybe_unused]] u32 size) -> void
{
    graphics::Light::Type *const type{(graphics::Light::Type *)data};
    assert(sizeof(graphics::Light::Type) == size);
    *type = set.type(id);
}

constexpr auto get_entity_id(const LightSet &set, LightId id, void *const data, [[maybe_unused]] u32 size) -> void
{
    id::id_type *const entity_id{(id::id_type *)data};
    assert(sizeof(id::id_type) == size);
    *entity_id = set.entity_id(id);
}

constexpr auto dummy_set(LightSet &, LightId, const void *const, u32) -> void
{
}

using set_function = void (*)(LightSet &, LightId, const void *const, u32);
using get_function = void (*)(const LightSet &, LightId, void *const, u32);
constexpr set_function set_functions[]{
    set_is_enabled,
    set_intensity,
    set_color,
    set_attenuation,
    set_range,
    set_umbra,
    set_penumbra,
    dummy_set,
    dummy_set,
};

static_assert(std::size(set_functions) == LightParameter::count);

constexpr get_function get_functions[]{
    get_is_enabled,
    get_intensity,
    get_color,
    get_attenuation,
    get_range,
    get_umbra,
    get_penumbra,
    get_type,
    get_entity_id,
};

static_assert(std::size(get_functions) == LightParameter::count);

// Good boy!
#undef CONSTEXPR
}

auto initialize() -> bool
{
    return true;
}

auto shutdown() -> void
{
    assert(light_sets.empty());

    for (u32 i{0}; i < frame_buffer_count; ++i)
    {
        light_buffers[i].release();
    }
}

auto create_light_set(u64 light_set_key) -> void
{
    assert(!light_sets.count(light_set_key));
    light_sets[light_set_key] = {};
}
auto remove_light_set(u64 light_set_key) -> void
{
    assert(light_sets.count(light_set_key));
    assert(!light_sets[light_set_key].has_lights());
    light_sets.erase(light_set_key);
}

auto create(LightInitInfo info) -> graphics::Light
{
    assert(light_sets.count(info.light_set_key));
    assert(id::is_valid(info.entity_id));
    return light_sets[info.light_set_key].add(info);
}

auto remove(LightId id, u64 light_set_key) -> void
{
    assert(light_sets.count(light_set_key));
    light_sets[light_set_key].remove(id);
}

auto set_parameter(
    LightId id,
    u64 light_set_key,
    LightParameter::Parameter parameter,
    const void *const data,
    u32 data_size) -> void
{
    assert(data && data_size);
    assert(light_sets.count(light_set_key));
    assert(parameter < LightParameter::count && set_functions[parameter] != dummy_set);
    set_functions[parameter](light_sets[light_set_key], id, data, data_size);
}

auto get_parameter(LightId id, u64 light_set_key, LightParameter::Parameter parameter, void *const data, u32 data_size)
    -> void
{
    assert(data && data_size);
    assert(light_sets.count(light_set_key));
    assert(parameter < LightParameter::count);
    get_functions[parameter](light_sets[light_set_key], id, data, data_size);
}

auto update_light_buffers(const VulkanFrameInfo &vulkan_info) -> void
{
    const u64 light_set_key{vulkan_info.info->light_set_key};
    assert(light_sets.count(light_set_key));
    LightSet &set{light_sets[light_set_key]};
    if (!set.has_lights())
    {
        return;
    }

    set.update_transforms();
    const u32 frame_index{vulkan_info.frame_index};
    VulkanLightBuffer &light_buffer{light_buffers[frame_index]};
    light_buffer.update_light_buffers(set, light_set_key, frame_index);
}

auto non_cullable_light_buffer(u32 frame_index) -> vkx::BufferRange
{
    const VulkanLightBuffer &light_buffer{light_buffers[frame_index]};
    return light_buffer.non_cullable_lights();
}

auto cullable_light_buffer(u32 frame_index) -> vkx::BufferRange
{
    const VulkanLightBuffer &light_buffer{light_buffers[frame_index]};
    return light_buffer.cullable_lights();
}

auto culling_info_buffer(u32 frame_index) -> vkx::BufferRange
{
    const VulkanLightBuffer &light_buffer{light_buffers[frame_index]};
    return light_buffer.culling_info();
}

auto bounding_spheres_buffer(u32 frame_index) -> vkx::BufferRange
{
    const VulkanLightBuffer &light_buffer{light_buffers[frame_index]};
    return light_buffer.bounding_spheres();
}

auto ambient_light(u64 light_set_key) -> hlsl::AmbientLightParameters
{
    assert(light_sets.count(light_set_key));
    return light_sets[light_set_key].ambient_light();
}

auto non_cullable_light_count(u64 light_set_key) -> u32
{
    assert(light_sets.count(light_set_key));
    return light_sets[light_set_key].non_cullable_light_count();
}

auto cullable_light_count(u64 light_set_key) -> u32
{
    assert(light_sets.count(light_set_key));
    return light_sets[light_set_key].cullable_light_count();
}
}
