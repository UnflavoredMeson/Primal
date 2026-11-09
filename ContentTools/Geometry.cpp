// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "Geometry.h"
#include "MikkTSpace/mikktspace.h"
#include "Utilities/IOStream.h"

namespace primal::tools
{
namespace
{

// using namespace math;
using namespace DirectX;

auto mikk_get_num_faces(const SMikkTSpaceContext *context) -> s32
{
    const Mesh &m{*(Mesh *)(context->m_pUserData)};
    return (s32)m.indices.size() / 3;
}

auto mikk_get_num_vertices_of_face([[maybe_unused]] const SMikkTSpaceContext *context, [[maybe_unused]] s32 face_index)
    -> s32
{
    // We only use triangle meshes.
    return 3;
}

auto mikk_get_position(const SMikkTSpaceContext *context, f32 position[3], s32 face_index, s32 vert_index) -> void
{
    const Mesh &m{*(Mesh *)(context->m_pUserData)};
    const u32 index{m.indices[face_index * 3 + vert_index]};
    const math::v3 &p{m.vertices[index].position};
    position[0] = p.x;
    position[1] = p.y;
    position[2] = p.z;
}

auto mikk_get_normal(const SMikkTSpaceContext *context, f32 normal[3], s32 face_index, s32 vert_index) -> void
{
    const Mesh &m{*(Mesh *)(context->m_pUserData)};
    const u32 index{m.indices[face_index * 3 + vert_index]};
    const math::v3 &n{m.vertices[index].normal};
    normal[0] = n.x;
    normal[1] = n.y;
    normal[2] = n.z;
}

auto mikk_get_tex_coord(const SMikkTSpaceContext *context, f32 texture[2], s32 face_index, s32 vert_index) -> void
{
    const Mesh &m{*(Mesh *)(context->m_pUserData)};
    const u32 index{m.indices[face_index * 3 + vert_index]};
    const math::v2 &uv{m.vertices[index].uv};
    texture[0] = uv.x;
    texture[1] = uv.y;
}

auto mikk_set_tspace_basic(
    const SMikkTSpaceContext *context,
    const f32 tangent[3],
    f32 sign,
    s32 face_index,
    s32 vert_index) -> void
{
    Mesh &m{*(Mesh *)(context->m_pUserData)};
    const u32 index{m.indices[face_index * 3 + vert_index]};
    math::v4 &t{m.vertices[index].tangent};
    t.x = tangent[0];
    t.y = tangent[1];
    t.z = tangent[2];
    t.w = sign;
}

auto calculate_mikk_tspace(Mesh &m) -> void
{
    // Don't use any imported tangents
    m.tangents.clear();

    SMikkTSpaceInterface mikk_interface{};
    mikk_interface.m_getNumFaces = mikk_get_num_faces;
    mikk_interface.m_getNumVerticesOfFace = mikk_get_num_vertices_of_face;
    mikk_interface.m_getPosition = mikk_get_position;
    mikk_interface.m_getNormal = mikk_get_normal;
    mikk_interface.m_getTexCoord = mikk_get_tex_coord;
    mikk_interface.m_setTSpaceBasic = mikk_set_tspace_basic;
    mikk_interface.m_setTSpace = nullptr;

    SMikkTSpaceContext mikk_context{};
    mikk_context.m_pInterface = &mikk_interface;
    mikk_context.m_pUserData = (void *)&m;

    ::genTangSpaceDefault(&mikk_context);
}

auto calculate_tangents(Mesh &m) -> void
{
    // Don't use any imported tangents
    m.tangents.clear();

    const u32 num_indices{(u32)m.raw_indices.size()};
    utl::vector<XMVECTOR> tangents(num_indices, XMVectorZero());
    utl::vector<XMVECTOR> bitangents(num_indices, XMVectorZero());
    utl::vector<XMVECTOR> positions(num_indices);

    for (u32 i{0}; i < num_indices; ++i)
    {
        positions[i] = XMLoadFloat3(&m.vertices[m.indices[i]].position);
    }

    for (u32 i{0}; i < num_indices; i += 3)
    {
        const u32 i0{i + 0};
        const u32 i1{i + 1};
        const u32 i2{i + 2};

        const XMVECTOR &p0{positions[i0]};
        const XMVECTOR &p1{positions[i1]};
        const XMVECTOR &p2{positions[i2]};

        const math::v2 &uv0{m.vertices[m.indices[i0]].uv};
        const math::v2 &uv1{m.vertices[m.indices[i1]].uv};
        const math::v2 &uv2{m.vertices[m.indices[i2]].uv};

        const math::v2 duv1{uv1.x - uv0.x, uv1.y - uv0.y};
        const math::v2 duv2{uv2.x - uv0.x, uv2.y - uv0.y};

        const XMVECTOR dp1{p1 - p0};
        const XMVECTOR dp2{p2 - p0};

        f32 det{duv1.x * duv2.y - duv1.y * duv2.x};
        if (std::abs(det) < math::epsilon)
            det = math::epsilon;

        const f32 inv_det{1.f / det};
        const XMVECTOR t{(dp1 * duv2.y - dp2 * duv1.y) * inv_det};
        const XMVECTOR b{(dp2 * duv1.x - dp1 * duv2.x) * inv_det};

        tangents[i0] += t;
        tangents[i1] += t;
        tangents[i2] += t;
        bitangents[i0] += b;
        bitangents[i1] += b;
        bitangents[i2] += b;
    }

    // Orthonormalize and calculate handedness
    for (u32 i{0}; i < num_indices; ++i)
    {
        const XMVECTOR &t{tangents[i]};
        const XMVECTOR &b{bitangents[i]};
        const XMVECTOR &n{XMLoadFloat3(&m.vertices[m.indices[i]].normal)};

        math::v3 tangent;
        XMStoreFloat3(&tangent, XMVector3Normalize(t - n * XMVector3Dot(n, t)));
        f32 handedness;
        XMStoreFloat(&handedness, XMVector3Dot(XMVector3Cross(t, b), n));

        handedness = handedness > 0.f ? 1.f : -1.f;

        m.vertices[m.indices[i]].tangent = {tangent.x, tangent.y, tangent.z, handedness};
    }
}

auto recalculate_normals(Mesh &m) -> void
{
    const u32 num_indices{(u32)m.raw_indices.size()};
    m.normals.resize(num_indices);

    for (u32 i{0}; i < num_indices; ++i)
    {
        const u32 i0{m.raw_indices[i]};
        const u32 i1{m.raw_indices[++i]};
        const u32 i2{m.raw_indices[++i]};

        XMVECTOR v0{XMLoadFloat3(&m.positions[i0])};
        XMVECTOR v1{XMLoadFloat3(&m.positions[i1])};
        XMVECTOR v2{XMLoadFloat3(&m.positions[i2])};

        XMVECTOR e0{v1 - v0};
        XMVECTOR e1{v2 - v0};
        XMVECTOR n{XMVector3Normalize(XMVector3Cross(e0, e1))};

        XMStoreFloat3(&m.normals[i], n);
        m.normals[i - 1] = m.normals[i];
        m.normals[i - 2] = m.normals[i];
    }
}

auto process_normals(Mesh &m, f32 smoothing_angle) -> void
{
    const f32 cos_alpha{XMScalarCos(math::pi - smoothing_angle * math::pi / 180.f)};
    const bool is_hard_edge{XMScalarNearEqual(smoothing_angle, 180.f, math::epsilon)};
    const bool is_soft_edge{XMScalarNearEqual(smoothing_angle, 0.f, math::epsilon)};
    const u32 num_indices{(u32)m.raw_indices.size()};
    const u32 num_vertices{(u32)m.positions.size()};
    assert(num_indices && num_vertices);

    m.indices.resize(num_indices);

    utl::vector<utl::vector<u32>> idx_ref(num_vertices);
    for (u32 i{0}; i < num_indices; ++i)
        idx_ref[m.raw_indices[i]].emplace_back(i);

    for (u32 i{0}; i < num_vertices; ++i)
    {
        utl::vector<u32> &refs{idx_ref[i]};
        u32 num_refs{(u32)refs.size()};
        for (u32 j{0}; j < num_refs; ++j)
        {
            m.indices[refs[j]] = (u32)m.vertices.size();
            Vertex &v{m.vertices.emplace_back()};
            v.position = m.positions[m.raw_indices[refs[j]]];

            XMVECTOR n1{XMLoadFloat3(&m.normals[refs[j]])};
            if (!is_hard_edge)
            {
                for (u32 k{j + 1}; k < num_refs; ++k)
                {
                    // this value represents the cosine of the angle between normals.
                    f32 cos_theta{0.f};
                    XMVECTOR n2{XMLoadFloat3(&m.normals[refs[k]])};
                    if (!is_soft_edge)
                    {
                        // NOTE: we're accounting for the length of n1 in this calculation because
                        //       it can possibly change in this loop iteration. We assume unit length
                        //       for n2.
                        //       cos(angle) = dot(n1, n2) / (||n1||*||n2||)
                        XMStoreFloat(&cos_theta, XMVector3Dot(n1, n2) * XMVector3ReciprocalLength(n1));
                    }

                    if (is_soft_edge || cos_theta >= cos_alpha)
                    {
                        n1 += n2;

                        m.indices[refs[k]] = m.indices[refs[j]];
                        refs.erase(refs.begin() + k);
                        --num_refs;
                        --k;
                    }
                }
            }
            XMStoreFloat3(&v.normal, XMVector3Normalize(n1));
        }
    }
}

auto process_uvs(Mesh &m) -> void
{
    utl::vector<Vertex> old_vertices;
    old_vertices.swap(m.vertices);
    utl::vector<u32> old_indices(m.indices.size());
    old_indices.swap(m.indices);

    const u32 num_vertices{(u32)old_vertices.size()};
    const u32 num_indices{(u32)old_indices.size()};
    assert(num_vertices && num_indices);

    utl::vector<utl::vector<u32>> idx_ref(num_vertices);
    for (u32 i{0}; i < num_indices; ++i)
        idx_ref[old_indices[i]].emplace_back(i);

    for (u32 i{0}; i < num_vertices; ++i)
    {
        utl::vector<u32> &refs{idx_ref[i]};
        u32 num_refs{(u32)refs.size()};
        for (u32 j{0}; j < num_refs; ++j)
        {
            m.indices[refs[j]] = (u32)m.vertices.size();
            Vertex &v{old_vertices[old_indices[refs[j]]]};
            v.uv = m.uv_sets[0][refs[j]];
            m.vertices.emplace_back(v);

            for (u32 k{j + 1}; k < num_refs; ++k)
            {
                math::v2 &uv1{m.uv_sets[0][refs[k]]};
                if (XMScalarNearEqual(v.uv.x, uv1.x, math::epsilon) && XMScalarNearEqual(v.uv.y, uv1.y, math::epsilon))
                {
                    m.indices[refs[k]] = m.indices[refs[j]];
                    refs.erase(refs.begin() + k);
                    --num_refs;
                    --k;
                }
            }
        }
    }
}

auto process_tangents(Mesh &m) -> void
{
    if (m.tangents.size() != m.raw_indices.size())
    {
        return;
    }

    utl::vector<Vertex> old_vertices;
    old_vertices.swap(m.vertices);
    utl::vector<u32> old_indices(m.indices.size());
    old_indices.swap(m.indices);

    const u32 num_vertices{(u32)old_vertices.size()};
    const u32 num_indices{(u32)old_indices.size()};
    assert(num_vertices && num_indices);

    utl::vector<utl::vector<u32>> idx_ref(num_vertices);
    for (u32 i{0}; i < num_indices; ++i)
        idx_ref[old_indices[i]].emplace_back(i);

    for (u32 i{0}; i < num_vertices; ++i)
    {
        utl::vector<u32> &refs{idx_ref[i]};
        u32 num_refs{(u32)refs.size()};
        for (u32 j{0}; j < num_refs; ++j)
        {

            const math::v4 &tj{m.tangents[refs[j]]};
            Vertex &v{old_vertices[old_indices[refs[j]]]};
            v.tangent = tj;
            m.indices[refs[j]] = (u32)m.vertices.size();
            m.vertices.emplace_back(v);

            XMVECTOR xm_tj{XMLoadFloat4(&tj)};
            XMVECTOR xm_epsilon{XMVectorReplicate(math::epsilon)};
            for (u32 k{j + 1}; k < num_refs; ++k)
            {
                XMVECTOR xm_tangent{XMLoadFloat4(&m.tangents[refs[k]])};
                if (XMVector4NearEqual(xm_tj, xm_tangent, xm_epsilon))
                {
                    m.indices[refs[k]] = m.indices[refs[j]];
                    refs.erase(refs.begin() + k);
                    --num_refs;
                    --k;
                }
            }
        }
    }
}

auto get_vertex_elements_size(elements::ElementsType::Type elements_type) -> u64
{
    using namespace elements;
    switch (elements_type)
    {
        case ElementsType::static_normal: return sizeof(StaticNormal);
        case ElementsType::static_normal_texture: return sizeof(StaticNormalTexture);
        case ElementsType::static_color: return sizeof(StaticColor);
        case ElementsType::skeletal: return sizeof(Skeletal);
        case ElementsType::skeletal_color: return sizeof(SkeletalColor);
        case ElementsType::skeletal_normal: return sizeof(SkeletalNormal);
        case ElementsType::skeletal_normal_color: return sizeof(SkeletalNormalColor);
        case ElementsType::skeletal_normal_texture: return sizeof(SkeletalNormalTexture);
        case ElementsType::skeletal_normal_texture_color: return sizeof(SkeletalNormalTextureColor);
    }

    return 0;
}

auto pack_vertices(Mesh &m) -> void
{
    const u32 num_vertices{(u32)m.vertices.size()};
    assert(num_vertices);

    m.position_buffer.resize(sizeof(math::v3) * num_vertices);
    math::v3 *const position_buffer{(math::v3 *const)m.position_buffer.data()};

    for (u32 i{0}; i < num_vertices; ++i)
    {
        position_buffer[i] = m.vertices[i].position;
    }

    struct u16v2
    {
        u16 x, y;
    };
    struct u8v3
    {
        u8 x, y, z;
    };

    utl::vector<u8> t_signs(num_vertices);
    utl::vector<u16v2> normals(num_vertices);
    utl::vector<u16v2> tangents(num_vertices);
    utl::vector<u8v3> joint_weights(num_vertices);

    if (m.elements_type & elements::ElementsType::static_normal)
    {
        // normals only
        for (u32 i{0}; i < num_vertices; ++i)
        {
            Vertex &v{m.vertices[i]};
            t_signs[i] = (u8)((v.normal.z > 0.f) << 2);
            normals[i] = {
                (u16)math::pack_float<16>(v.normal.x, -1.f, 1.f), (u16)math::pack_float<16>(v.normal.y, -1.f, 1.f)};
        }

        if (m.elements_type & elements::ElementsType::static_normal_texture)
        {
            // full T-space
            for (u32 i{0}; i < num_vertices; ++i)
            {
                Vertex &v{m.vertices[i]};
                t_signs[i] |= (u8)((v.tangent.w > 0.f) | ((v.tangent.z > 0.f) << 1));
                tangents[i] = {
                    (u16)math::pack_float<16>(v.tangent.x, -1.f, 1.f),
                    (u16)math::pack_float<16>(v.tangent.y, -1.f, 1.f)};
            }
        }
    }

    if (m.elements_type & elements::ElementsType::skeletal)
    {
        for (u32 i{0}; i < num_vertices; ++i)
        {
            Vertex &v{m.vertices[i]};
            // pack joint weights (from [0.0, 1.0] to [0..255])
            joint_weights[i] = {
                (u8)math::pack_unit_float<8>(v.joint_weights.x),
                (u8)math::pack_unit_float<8>(v.joint_weights.y),
                (u8)math::pack_unit_float<8>(v.joint_weights.z)};

            // NOTE: w3 will be calculated in shader since joint weights sum to one(1).
        }
    }

    m.element_buffer.resize(get_vertex_elements_size(m.elements_type) * num_vertices);
    using namespace elements;

    switch (m.elements_type)
    {
        case ElementsType::static_color:
        {
            StaticColor *const element_buffer{(StaticColor *const)m.element_buffer.data()};
            for (u32 i{0}; i < num_vertices; ++i)
            {
                Vertex &v{m.vertices[i]};
                element_buffer[i] = {{v.red, v.green, v.blue}, {}};
            }
        }
        break;
        case ElementsType::static_normal:
        {
            StaticNormal *const element_buffer{(StaticNormal *const)m.element_buffer.data()};
            for (u32 i{0}; i < num_vertices; ++i)
            {
                Vertex &v{m.vertices[i]};
                element_buffer[i] = {{v.red, v.green, v.blue}, t_signs[i], {normals[i].x, normals[i].y}};
            }
        }
        break;
        case ElementsType::static_normal_texture:
        {
            StaticNormalTexture *const element_buffer{(StaticNormalTexture *const)m.element_buffer.data()};
            for (u32 i{0}; i < num_vertices; ++i)
            {
                Vertex &v{m.vertices[i]};
                element_buffer[i] = {
                    {v.red, v.green, v.blue},
                    t_signs[i],
                    {normals[i].x, normals[i].y},
                    {tangents[i].x, tangents[i].y},
                    v.uv};
            }
        }
        break;
        case ElementsType::skeletal:
        {
            Skeletal *const element_buffer{(Skeletal *const)m.element_buffer.data()};
            for (u32 i{0}; i < num_vertices; ++i)
            {
                Vertex &v{m.vertices[i]};
                const u16 indices[4]{
                    (u16)v.joint_indices.x, (u16)v.joint_indices.y, (u16)v.joint_indices.z, (u16)v.joint_indices.w};
                element_buffer[i] = {
                    {joint_weights[i].x, joint_weights[i].y, joint_weights[i].z},
                    {},
                    {indices[0], indices[1], indices[2], indices[3]}};
            }
        }
        break;
        case ElementsType::skeletal_color:
        {
            SkeletalColor *const element_buffer{(SkeletalColor *const)m.element_buffer.data()};
            for (u32 i{0}; i < num_vertices; ++i)
            {
                Vertex &v{m.vertices[i]};
                const u16 indices[4]{
                    (u16)v.joint_indices.x, (u16)v.joint_indices.y, (u16)v.joint_indices.z, (u16)v.joint_indices.w};
                element_buffer[i] = {
                    {joint_weights[i].x, joint_weights[i].y, joint_weights[i].z},
                    {},
                    {indices[0], indices[1], indices[2], indices[3]},
                    {v.red, v.green, v.blue},
                    {}};
            }
        }
        break;
        case ElementsType::skeletal_normal:
        {
            SkeletalNormal *const element_buffer{(SkeletalNormal *const)m.element_buffer.data()};
            for (u32 i{0}; i < num_vertices; ++i)
            {
                Vertex &v{m.vertices[i]};
                const u16 indices[4]{
                    (u16)v.joint_indices.x, (u16)v.joint_indices.y, (u16)v.joint_indices.z, (u16)v.joint_indices.w};
                element_buffer[i] = {
                    {joint_weights[i].x, joint_weights[i].y, joint_weights[i].z},
                    t_signs[i],
                    {indices[0], indices[1], indices[2], indices[3]},
                    {normals[i].x, normals[i].y}};
            }
        }
        break;
        case ElementsType::skeletal_normal_color:
        {
            SkeletalNormalColor *const element_buffer{(SkeletalNormalColor *const)m.element_buffer.data()};
            for (u32 i{0}; i < num_vertices; ++i)
            {
                Vertex &v{m.vertices[i]};
                const u16 indices[4]{
                    (u16)v.joint_indices.x, (u16)v.joint_indices.y, (u16)v.joint_indices.z, (u16)v.joint_indices.w};
                element_buffer[i] = {
                    {joint_weights[i].x, joint_weights[i].y, joint_weights[i].z},
                    t_signs[i],
                    {indices[0], indices[1], indices[2], indices[3]},
                    {normals[i].x, normals[i].y},
                    {v.red, v.green, v.blue},
                    {}};
            }
        }
        break;
        case ElementsType::skeletal_normal_texture:
        {
            SkeletalNormalTexture *const element_buffer{(SkeletalNormalTexture *const)m.element_buffer.data()};
            for (u32 i{0}; i < num_vertices; ++i)
            {
                Vertex &v{m.vertices[i]};
                const u16 indices[4]{
                    (u16)v.joint_indices.x, (u16)v.joint_indices.y, (u16)v.joint_indices.z, (u16)v.joint_indices.w};
                element_buffer[i] = {
                    {joint_weights[i].x, joint_weights[i].y, joint_weights[i].z},
                    t_signs[i],
                    {indices[0], indices[1], indices[2], indices[3]},
                    {normals[i].x, normals[i].y},
                    {tangents[i].x, tangents[i].y},
                    v.uv};
            }
        }
        break;
        case ElementsType::skeletal_normal_texture_color:
        {
            SkeletalNormalTextureColor *const element_buffer{
                (SkeletalNormalTextureColor *const)m.element_buffer.data()};
            for (u32 i{0}; i < num_vertices; ++i)
            {
                Vertex &v{m.vertices[i]};
                const u16 indices[4]{
                    (u16)v.joint_indices.x, (u16)v.joint_indices.y, (u16)v.joint_indices.z, (u16)v.joint_indices.w};
                element_buffer[i] = {
                    {joint_weights[i].x, joint_weights[i].y, joint_weights[i].z},
                    t_signs[i],
                    {indices[0], indices[1], indices[2], indices[3]},
                    {normals[i].x, normals[i].y},
                    {tangents[i].x, tangents[i].y},
                    v.uv,
                    {v.red, v.green, v.blue},
                    {}};
            }
        }
        break;
    }
}

auto determine_elements_type(const Mesh &m) -> elements::ElementsType::Type
{
    using namespace elements;
    ElementsType::Type type{};

    if (m.normals.size())
    {
        if (m.uv_sets.size() && m.uv_sets[0].size())
        {
            type = ElementsType::static_normal_texture;
        }
        else
        {
            type = ElementsType::static_normal;
        }
    }
    else if (m.colors.size())
    {
        type = ElementsType::static_color;
    }

    // TODO: we lack data for skeletal meshes. Expand for skeletal meshes later.
    return type;
}

auto process_vertices(Mesh &m, const GeometryImportSettings &settings) -> void
{
    assert((m.raw_indices.size() % 3) == 0);
    if (settings.calculate_normals || m.normals.empty())
    {
        recalculate_normals(m);
    }

    process_normals(m, settings.smoothing_angle);

    if (!m.uv_sets.empty())
    {
        process_uvs(m);
    }

    if ((settings.calculate_tangents || m.tangents.empty()) && !m.uv_sets.empty())
    {
        calculate_mikk_tspace(m);
        // calculate_tangents(m);
    }

    // NOTE: m.tangents contains values of the imported tangent vectors. It will be empty
    //       if tangents where calculated. Therefore, process_tangents is only called
    //       when tangents are imported from the source file.
    if (!m.tangents.empty())
    {
        process_tangents(m);
    }

    m.elements_type = determine_elements_type(m);
    pack_vertices(m);
}

auto get_mesh_size(const Mesh &m) -> u64
{
    const u64 num_vertices{m.vertices.size()};
    const u64 position_buffer_size{m.position_buffer.size()};
    assert(position_buffer_size == sizeof(math::v3) * num_vertices);
    const u64 element_buffer_size{m.element_buffer.size()};
    assert(element_buffer_size == get_vertex_elements_size(m.elements_type) * num_vertices);
    const u64 index_size{(num_vertices < (1 << 16)) ? sizeof(u16) : sizeof(u32)};
    const u64 index_buffer_size{index_size * m.indices.size()};
    constexpr u64 su32{sizeof(u32)};
    const u64 size{
        su32 + m.name.size() + // mesh name length and room for mesh name string
        su32 +                 // lod id
        su32 +                 // vertex element size (vertex size excluding position element)
        su32 +                 // element type enumeration
        su32 +                 // number of vertices
        su32 +                 // index size (16 bit or 32 bit)
        su32 +                 // number of indices
        sizeof(f32) +          // LOD threshold
        position_buffer_size + // room for vertex positions
        element_buffer_size +  // room for vertex elements
        index_buffer_size      // room for indices
    };

    return size;
}

auto get_scene_size(const Scene &scene) -> u64
{
    constexpr u64 su32{sizeof(u32)};
    u64 size{
        su32 +              // name length
        scene.name.size() + // room for scene name string
        su32                // number of LODs
    };

    for (const auto &lod : scene.lod_groups)
    {
        u64 lod_size{
            su32 + lod.name.size() + // LOD name length and room for LPD name string
            su32                     // number of meshes in this LOD
        };

        for (const auto &m : lod.meshes)
        {
            lod_size += get_mesh_size(m);
        }

        size += lod_size;
    }

    return size;
}

auto pack_mesh_data(const Mesh &m, utl::BlobStreamWriter &blob) -> void
{
    // mesh name
    blob.write((u32)m.name.size());
    blob.write(m.name.c_str(), m.name.size());
    // lod id
    blob.write(m.lod_id);
    // vertex element size
    const u32 elements_size{(u32)get_vertex_elements_size(m.elements_type)};
    blob.write(elements_size);
    // elements type enumeration
    blob.write((u32)m.elements_type);
    // number of vertices
    const u32 num_vertices{(u32)m.vertices.size()};
    blob.write(num_vertices);
    // index size (16 bit or 32 bit)
    const u32 index_size{(num_vertices < (1 << 16)) ? sizeof(u16) : sizeof(u32)};
    blob.write(index_size);
    // number of indices
    const u32 num_indices{(u32)m.indices.size()};
    blob.write(num_indices);
    // LOD threshold
    blob.write(m.lod_threshold);
    // position buffer
    assert(m.position_buffer.size() == sizeof(math::v3) * num_vertices);
    blob.write(m.position_buffer.data(), m.position_buffer.size());
    // element buffer
    assert(m.element_buffer.size() == elements_size * num_vertices);
    blob.write(m.element_buffer.data(), m.element_buffer.size());
    // index data
    const u32 index_buffer_size{index_size * num_indices};
    const u8 *data{(const u8 *)m.indices.data()};
    utl::vector<u16> indices;

    if (index_size == sizeof(u16))
    {
        indices.resize(num_indices);
        for (u32 i{0}; i < num_indices; ++i)
            indices[i] = (u16)m.indices[i];
        data = (const u8 *)indices.data();
    }
    blob.write(data, index_buffer_size);
}

auto split_meshes_by_material(u32 material_idx, const Mesh &m, Mesh &submesh) -> bool
{
    submesh.name = m.name;
    submesh.lod_threshold = m.lod_threshold;
    submesh.lod_id = m.lod_id;
    submesh.material_used.emplace_back(material_idx);
    submesh.uv_sets.resize(m.uv_sets.size());

    const u32 num_polys{(u32)m.raw_indices.size() / 3};
    utl::vector<u32> vertex_ref(m.positions.size(), u32_invalid_id);

    for (u32 i{0}; i < num_polys; ++i)
    {
        const u32 mtl_idx{m.material_indices[i]};
        if (mtl_idx != material_idx)
            continue;

        const u32 index{i * 3};
        for (u32 j = index; j < index + 3; ++j)
        {
            const u32 v_idx{m.raw_indices[j]};
            if (vertex_ref[v_idx] != u32_invalid_id)
            {
                submesh.raw_indices.emplace_back(vertex_ref[v_idx]);
            }
            else
            {
                submesh.raw_indices.emplace_back((u32)submesh.positions.size());
                vertex_ref[v_idx] = submesh.raw_indices.back();
                submesh.positions.emplace_back(m.positions[v_idx]);
            }

            if (m.normals.size())
            {
                submesh.normals.emplace_back(m.normals[j]);
            }

            if (m.tangents.size())
            {
                submesh.tangents.emplace_back(m.tangents[j]);
            }

            for (u32 k{0}; k < m.uv_sets.size(); ++k)
            {
                if (m.uv_sets[k].size())
                {
                    submesh.uv_sets[k].emplace_back(m.uv_sets[k][j]);
                }
            }
        }
    }

    assert((submesh.raw_indices.size() % 3) == 0);
    return !submesh.raw_indices.empty();
}

auto split_meshes_by_material(Scene &scene, Progression *const progression) -> void
{
    assert(progression);
    progression->callback(0, 0);

    for (auto &lod : scene.lod_groups)
    {
        utl::vector<Mesh> new_meshes;

        for (const auto &m : lod.meshes)
        {
            // If more than one material is used in this mesh
            // then split it into submeshes.
            const u32 num_materials{(u32)m.material_used.size()};
            if (num_materials > 1)
            {
                for (u32 i{0}; i < num_materials; ++i)
                {
                    Mesh submesh{};
                    if (split_meshes_by_material(m.material_used[i], m, submesh))
                    {
                        new_meshes.emplace_back(submesh);
                    }
                }
            }
            else
            {
                new_meshes.emplace_back(m);
            }
        }

        progression->callback(progression->value(), progression->max_value() + (u32)new_meshes.size());
        new_meshes.swap(lod.meshes);
    }
}

template <typename T>
auto append_to_vector_pod(utl::vector<T> &dst, const utl::vector<T> &src) -> void
{
    if (src.empty())
        return;
    const u32 num_elements{(u32)dst.size()};
    dst.resize(dst.size() + src.size());
    std::memcpy(&dst[num_elements], src.data(), src.size() * sizeof(T));
}

}

auto process_scene(Scene &scene, const GeometryImportSettings &settings, Progression *const progression) -> void
{
    assert(progression);
    split_meshes_by_material(scene, progression);

    for (auto &lod : scene.lod_groups)
        for (auto &m : lod.meshes)
        {
            process_vertices(m, settings);
            progression->callback(progression->value() + 1, progression->max_value());
        }
}

auto pack_data(const Scene &scene, SceneData &data) -> void
{
    const u64 scene_size{get_scene_size(scene)};
    data.buffer_size = (u32)scene_size;
    data.buffer = (u8 *)::CoTaskMemAlloc(scene_size);
    assert(data.buffer);

    utl::BlobStreamWriter blob{data.buffer, data.buffer_size};

    // scene name
    blob.write((u32)scene.name.size());
    blob.write(scene.name.c_str(), scene.name.size());
    // number of LODs
    blob.write((u32)scene.lod_groups.size());

    for (const auto &lod : scene.lod_groups)
    {
        // LOD name
        blob.write((u32)lod.name.size());
        blob.write(lod.name.c_str(), lod.name.size());
        // number of meshes in this LOD
        blob.write((u32)lod.meshes.size());

        for (const auto &m : lod.meshes)
        {
            pack_mesh_data(m, blob);
        }
    }

    assert(scene_size == blob.offset());
}

auto coalesce_meshes(const LodGroup &lod, Mesh &combined_mesh, Progression *const progression) -> bool
{
    assert(lod.meshes.size());
    const Mesh &first_mesh{lod.meshes[0]};
    combined_mesh.name = first_mesh.name;
    combined_mesh.elements_type = determine_elements_type(first_mesh);
    combined_mesh.lod_threshold = first_mesh.lod_threshold;
    combined_mesh.lod_id = first_mesh.lod_id;
    combined_mesh.uv_sets.resize(first_mesh.uv_sets.size());

    for (u32 mesh_idx{0}; mesh_idx < lod.meshes.size(); ++mesh_idx)
    {
        const Mesh &m{lod.meshes[mesh_idx]};

        if (combined_mesh.elements_type != determine_elements_type(m) ||
            combined_mesh.uv_sets.size() != m.uv_sets.size() || combined_mesh.lod_id != m.lod_id ||
            !math::is_equal(combined_mesh.lod_threshold, m.lod_threshold))
        {
            combined_mesh = {};
            return false;
        }
    }

    for (u32 mesh_idx{0}; mesh_idx < lod.meshes.size(); ++mesh_idx)
    {
        const Mesh &m{lod.meshes[mesh_idx]};

        const u32 position_count{(u32)combined_mesh.positions.size()};
        const u32 raw_index_base{(u32)combined_mesh.raw_indices.size()};

        append_to_vector_pod(combined_mesh.positions, m.positions);
        append_to_vector_pod(combined_mesh.normals, m.normals);
        append_to_vector_pod(combined_mesh.tangents, m.tangents);
        append_to_vector_pod(combined_mesh.colors, m.colors);

        for (u32 i{0}; i < combined_mesh.uv_sets.size(); ++i)
        {
            append_to_vector_pod(combined_mesh.uv_sets[i], m.uv_sets[i]);
        }

        append_to_vector_pod(combined_mesh.material_indices, m.material_indices);
        append_to_vector_pod(combined_mesh.raw_indices, m.raw_indices);

        for (u32 i{raw_index_base}; i < combined_mesh.raw_indices.size(); ++i)
        {
            combined_mesh.raw_indices[i] += position_count;
        }

        progression->callback(progression->value(), progression->max_value() > 1 ? progression->max_value() - 1 : 1);
    }

    for (const u32 mtl_idx : combined_mesh.material_indices)
    {
        if (std::find(combined_mesh.material_used.begin(), combined_mesh.material_used.end(), mtl_idx) ==
            combined_mesh.material_used.end())
        {
            combined_mesh.material_used.emplace_back(mtl_idx);
        }
    }

    return true;
}
}
