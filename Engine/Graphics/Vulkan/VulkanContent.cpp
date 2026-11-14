// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "VulkanContent.h"

#include "Content/ContentToEngine.h"
#include "Utilities/IOStream.h"
#include "VulkanCore.h"
#include "VulkanGPass.h"
#include "VulkanResources.h"
#include "VulkanUpload.h"

#include <bit>
#include <numeric>

namespace primal::graphics::vulkan::content
{
namespace
{

struct PsoId
{
    id::id_type gpass_pso_id{id::invalid_id};
    id::id_type depth_pso_id{id::invalid_id};
};

struct SubmeshView
{
    vkx::BufferRange position_buffer{};
    vkx::BufferRange element_buffer{};
    submesh::IndexBufferView index_buffer_view{};
    VkPrimitiveTopology primitive_topology{VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    u32 elements_type{};
    u32 resource_index{u32_invalid_id}; // index of the mesh buffer resource.
};

struct VulkanRenderItem
{
    id::id_type entity_id;
    id::id_type submesh_gpu_id;
    id::id_type material_id;
    id::id_type pso_id;
    id::id_type depth_pso_id;
};

utl::free_list<VulkanBuffer> mesh_buffers{};
utl::free_list<SubmeshView> submesh_views{};
std::mutex mesh_mutex{};

utl::free_list<VulkanTexture> textures;
utl::free_list<u32> descriptor_indices;
std::mutex texture_mutex{};

utl::vector<vkx::RootSignature> root_signatures;
std::unordered_map<u64, id::id_type>
    mtl_rs_map; // maps a material's type and shader flags to an index in the array of root signatures.
utl::free_list<std::unique_ptr<u8[]>> materials;
std::mutex material_mutex{};

utl::free_list<VulkanRenderItem> render_items;
utl::free_list<std::unique_ptr<id::id_type[]>> render_item_ids;
std::mutex render_item_mutex{};

utl::vector<VkPipeline> pipeline_states;
std::unordered_map<u64, id::id_type> pso_map;
std::mutex pso_mutex{};

struct
{
    utl::vector<primal::content::LodOffset> lod_offsets;
    utl::vector<id::id_type> geometry_ids;
} frame_cache;

auto create_root_signature(MaterialType::Type type, ShaderFlags::Flags flags) -> id::id_type;

// Same layout as d3d12_material_stream.
class VulkanMaterialStream
{
  public:
    DISABLE_COPY_AND_MOVE(VulkanMaterialStream);
    explicit VulkanMaterialStream(u8 *const material_buffer)
        : _buffer{material_buffer}
    {
        initialize();
    }

    explicit VulkanMaterialStream(std::unique_ptr<u8[]> &material_buffer, MaterialInitInfo info)
    {
        assert(!material_buffer);

        u32 shader_count{0};
        u32 flags{0};
        for (u32 i{0}; i < ShaderType::count; ++i)
        {
            if (id::is_valid(info.shader_ids[i]))
            {
                ++shader_count;
                flags |= (1 << i);
            }
        }

        assert(shader_count && flags);

        const u32 buffer_size{
            static_cast<u32>(
                sizeof(MaterialType::Type) +         // material type
                sizeof(ShaderFlags::Flags) +         // shader flags
                sizeof(id::id_type) +                // root signature id
                sizeof(u32) +                        // texture count
                sizeof(MaterialSurface) +            // PBR material properties
                sizeof(id::id_type) * shader_count + // shader ids
                (sizeof(id::id_type) + sizeof(u32)) *
                    info.texture_count) // texture ids and descriptor indices (maybe 0 if no textures used).
        };

        material_buffer = std::make_unique<u8[]>(buffer_size);
        _buffer = material_buffer.get();
        u8 *const buffer{_buffer};

        *reinterpret_cast<MaterialType::Type *>(buffer) = info.type;
        *reinterpret_cast<ShaderFlags::Flags *>(&buffer[shader_flags_index]) = static_cast<ShaderFlags::Flags>(flags);
        *reinterpret_cast<id::id_type *>(&buffer[root_signature_index]) =
            create_root_signature(info.type, static_cast<ShaderFlags::Flags>(flags));
        *reinterpret_cast<u32 *>(&buffer[texture_count_index]) = info.texture_count;
        *reinterpret_cast<MaterialSurface *>(&buffer[material_surface_index]) = info.surface;

        initialize();

        if (info.texture_count)
        {
            std::memcpy(_texture_ids, info.texture_ids, info.texture_count * sizeof(id::id_type));
            texture::get_descriptor_indices(_texture_ids, info.texture_count, _descriptor_indices);
        }

        u32 shader_index{0};
        for (u32 i{0}; i < ShaderType::count; ++i)
        {
            if (id::is_valid(info.shader_ids[i]))
            {
                _shader_ids[shader_index] = info.shader_ids[i];
                ++shader_index;
            }
        }

        assert(shader_index == static_cast<u32>(std::popcount(static_cast<u32>(_shader_flags))));
    }

    [[nodiscard]] constexpr auto texture_count() const -> u32
    {
        return _texture_count;
    }
    [[nodiscard]] constexpr auto material_type() const -> graphics::MaterialType::Type
    {
        return _type;
    }
    [[nodiscard]] constexpr auto shader_flags() const -> graphics::ShaderFlags::Flags
    {
        return _shader_flags;
    }
    [[nodiscard]] constexpr auto root_signature_id() const -> id::id_type
    {
        return _root_signature_id;
    }
    [[nodiscard]] constexpr auto descriptor_indices() const -> u32 *
    {
        return _descriptor_indices;
    }
    [[nodiscard]] constexpr auto shader_ids() const -> id::id_type *
    {
        return _shader_ids;
    }
    [[nodiscard]] constexpr auto surface() const -> MaterialSurface *
    {
        return _material_surface;
    }

  private:
    auto initialize() -> void
    {
        assert(_buffer);
        u8 *const buffer{_buffer};

        _type = *reinterpret_cast<MaterialType::Type *>(buffer);
        _shader_flags = *reinterpret_cast<ShaderFlags::Flags *>(&buffer[shader_flags_index]);
        _root_signature_id = *reinterpret_cast<id::id_type *>(&buffer[root_signature_index]);
        _texture_count = *reinterpret_cast<u32 *>(&buffer[texture_count_index]);
        _material_surface = reinterpret_cast<MaterialSurface *>(&buffer[material_surface_index]);

        _shader_ids = reinterpret_cast<id::id_type *>(&buffer[material_surface_index + sizeof(MaterialSurface)]);
        _texture_ids = _texture_count ? &_shader_ids[std::popcount(static_cast<u32>(_shader_flags))] : nullptr;
        _descriptor_indices = _texture_count ? static_cast<u32 *>(&_texture_ids[_texture_count]) : nullptr;
    }

    constexpr static u32 shader_flags_index{sizeof(MaterialType::Type)};
    constexpr static u32 root_signature_index{shader_flags_index + sizeof(ShaderFlags::Flags)};
    constexpr static u32 texture_count_index{root_signature_index + sizeof(id::id_type)};
    constexpr static u32 material_surface_index{texture_count_index + sizeof(u32)};

    u8 *_buffer;
    MaterialSurface *_material_surface;
    id::id_type *_texture_ids;
    u32 *_descriptor_indices;
    id::id_type *_shader_ids;
    id::id_type _root_signature_id;
    u32 _texture_count;
    MaterialType::Type _type;
    ShaderFlags::Flags _shader_flags;
};

constexpr auto get_vk_primitive_topology(PrimitiveTopology::Type type) -> VkPrimitiveTopology
{
    assert(type < PrimitiveTopology::count);

    switch (type)
    {
        case PrimitiveTopology::point_list: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case PrimitiveTopology::line_list: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case PrimitiveTopology::line_strip: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        case PrimitiveTopology::triangle_list: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case PrimitiveTopology::triangle_strip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        default: break;
    }

    return VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
}

auto create_root_signature(MaterialType::Type type, ShaderFlags::Flags flags) -> id::id_type
{
    assert(type < MaterialType::count);
    static_assert(sizeof(type) == sizeof(u32) && sizeof(flags) == sizeof(u32));
    const u64 key{(static_cast<u64>(type) << 32) | flags};
    const auto pair = mtl_rs_map.find(key);
    if (pair != mtl_rs_map.end())
    {
        assert(pair->first == key);
        return pair->second;
    }

    vkx::RootSignature root_signature{};

    switch (type)
    {
        case MaterialType::opaque:
        {
            // Same root parameters as the D3D12 opaque root signature (gpass::opaque_root_parameter), in register
            // order. Visibility: D3D12_SHADER_VISIBILITY_* -> shader stage flags.
            VkShaderStageFlags buffer_visibility{VK_SHADER_STAGE_ALL};
            VkShaderStageFlags data_visibility{VK_SHADER_STAGE_ALL};

            if (flags & ShaderFlags::vertex)
            {
                buffer_visibility = VK_SHADER_STAGE_VERTEX_BIT;
                data_visibility = VK_SHADER_STAGE_VERTEX_BIT;
            }
            else if (flags & ShaderFlags::mesh)
            {
                // TODO: mesh shaders (VK_EXT_mesh_shader) aren't supported by the Vulkan backend yet.
                buffer_visibility = VK_SHADER_STAGE_ALL;
                data_visibility = VK_SHADER_STAGE_ALL;
            }

            if ((flags & ShaderFlags::hull) || (flags & ShaderFlags::geometry) || (flags & ShaderFlags::amplification))
            {
                buffer_visibility = VK_SHADER_STAGE_ALL;
                data_visibility = VK_SHADER_STAGE_ALL;
            }

            if ((flags & ShaderFlags::pixel) || (flags & ShaderFlags::compute))
            {
                data_visibility = VK_SHADER_STAGE_ALL;
            }

            using param = vkx::RootParameter;
            constexpr VkShaderStageFlags pixel{VK_SHADER_STAGE_FRAGMENT_BIT};
            const param parameters[]{
                param::cbv(0, VK_SHADER_STAGE_ALL), // global_shader_data
                param::cbv(1, data_visibility),     // per_object_data
                param::srv(0, buffer_visibility),   // position_buffer
                param::srv(1, buffer_visibility),   // element_buffer
                param::srv(
                    2, pixel), // material_data  TODO: needs to be visible to any stages that need to sample textures.
                param::srv(3, pixel), // directional_lights
                param::srv(4, pixel), // cullable_lights
                param::srv(5, pixel), // light_grid
                param::srv(6, pixel), // light_index_list
                param::sampler(vkx::StaticSampler::point, 0, pixel),
                param::sampler(vkx::StaticSampler::linear, 1, pixel),
                param::sampler(vkx::StaticSampler::anisotropic, 2, pixel),
            };

            char name[64];
            std::snprintf(name, sizeof(name), "GPass Root Signature - key %llu", static_cast<unsigned long long>(key));
            root_signature = vkx::create_root_signature(&parameters[0], static_cast<u32>(std::size(parameters)), name);
        }
        break;
        default: break;
    }

    assert(root_signature.is_valid());
    const id::id_type id{static_cast<id::id_type>(root_signatures.size())};
    root_signatures.emplace_back(root_signature);
    mtl_rs_map[key] = id;
    return id;
}

// Pipelines are cached by a hash of their (zero-initialized) description, like D3D12's PSO streams.
auto create_pso_if_needed(const vkx::GraphicsPipelineDesc &desc, bool is_depth) -> id::id_type
{
    const u64 key{math::calc_crc32_u64(reinterpret_cast<const u8 *>(&desc), sizeof(desc))};

    { // Lock scope to check if PSO already exists
        const std::scoped_lock lock{pso_mutex};
        const auto pair = pso_map.find(key);

        if (pair != pso_map.end())
        {
            assert(pair->first == key);
            return pair->second;
        }
    }

    // Creating a new pipeline is lock-free
    // TODO: a VkPipelineCache saved to disk (drivers also keep their own shader caches).
    const VkPipeline pipeline{vkx::create_graphics_pipeline(desc, is_depth ? "Depth-only Pipeline" : "GPass Pipeline")};
    assert(pipeline);

    { // Lock scope to add the new pipeline and its id
        const std::scoped_lock lock{pso_mutex};
        // Another thread may have created the same pipeline in the meantime. Ours was never used.
        const auto pair = pso_map.find(key);
        if (pair != pso_map.end())
        {
            ::vkDestroyPipeline(core::device(), pipeline, nullptr);
            return pair->second;
        }

        const id::id_type id{static_cast<id::id_type>(pipeline_states.size())};
        pipeline_states.emplace_back(pipeline);
        pso_map[key] = id;
        return id;
    }
}

auto get_shader_type(u32 flag) -> ShaderType::Type
{
    assert(flag);
    return static_cast<ShaderType::Type>(std::countr_zero(flag));
}

auto get_shader_byte_code(
    ShaderType::Type type,
    u32 elements_type,
    const VulkanMaterialStream &material,
    u32 shader_index) -> vkx::ShaderBytecode
{
    // NOTE: each type of shader may have keys that are generated from different properties of the submesh or material.
    //       At the moment, we only have different kinds of vertex shaders depending on elements_type
    const u32 key{type == ShaderType::vertex ? elements_type : u32_invalid_id};
    primal::content::compiled_shader_ptr shader{primal::content::get_shader(material.shader_ids()[shader_index], key)};
    assert(shader);
    assert((reinterpret_cast<uintptr_t>(shader->byte_code()) & 3) == 0 && "SPIR-V must be 4-byte aligned.");
    return {reinterpret_cast<const u32 *>(shader->byte_code()), shader->byte_code_size(), vkx::spirv_entry_point};
}

auto create_pso(id::id_type material_id, VkPrimitiveTopology primitive_topology, u32 elements_type) -> PsoId
{
    // Zero the whole description (padding included) so equal descriptions hash the same.
    alignas(vkx::GraphicsPipelineDesc) u8 desc_storage[sizeof(vkx::GraphicsPipelineDesc)];
    std::memset(&desc_storage[0], 0, sizeof(desc_storage));
    vkx::GraphicsPipelineDesc &desc{*new (&desc_storage[0]) vkx::GraphicsPipelineDesc{}};
    vkx::ShaderBytecode depth_prepass_vertex_shader{};

    { // Lock materials
        const std::scoped_lock lock{material_mutex};
        const VulkanMaterialStream material{materials[material_id].get()};

        desc.render_target_formats[0] = gpass::main_buffer_format;
        desc.render_target_count = 1;
        desc.layout = root_signatures[material.root_signature_id()].layout;
        desc.topology = primitive_topology;
        desc.depth_format = gpass::depth_buffer_format;
        desc.rasterizer = vkx::rasterizer_state.backface_cull;
        desc.depth_stencil = vkx::depth_state.reversed_readonly;
        desc.blend = vkx::blend_state.disabled;

        const ShaderFlags::Flags flags{material.shader_flags()};
        const bool use_position_only_for_depth_prepass{
            !(flags & ((1 << ShaderType::amplification) | (1 << ShaderType::geometry) | (1 << ShaderType::hull) |
                       (1 << ShaderType::domain)))};
        vkx::ShaderBytecode shaders[ShaderType::count]{};
        u32 shader_index{0};
        for (u32 i{0}; i < ShaderType::count; ++i)
        {
            const u32 flag{flags & (1 << i)};

            if (flag)
            {
                const ShaderType::Type type{get_shader_type(flag)};
                shaders[i] = get_shader_byte_code(type, elements_type, material, shader_index);

                ++shader_index;
            }
        }

        // TODO: hull/domain/geometry/amplification/mesh stages (vkx::graphics_pipeline_desc has vs + ps only).
        assert(
            !shaders[ShaderType::hull].is_valid() && !shaders[ShaderType::domain].is_valid() &&
            !shaders[ShaderType::geometry].is_valid() && !shaders[ShaderType::amplification].is_valid() &&
            !shaders[ShaderType::mesh].is_valid() && "Vulkan: only vertex + pixel shader materials are supported.");
        desc.vs = shaders[ShaderType::vertex];
        desc.ps = shaders[ShaderType::pixel];

        depth_prepass_vertex_shader = use_position_only_for_depth_prepass
                                          ? get_shader_byte_code(ShaderType::vertex, 0, material, 0)
                                          : shaders[ShaderType::vertex];
    }
    PsoId id_pair{};
    id_pair.gpass_pso_id = create_pso_if_needed(desc, false);

    // Depth prepass: depth attachment only (D3D12 keeps the render target format; dynamic rendering needs the
    // pipeline's attachment formats to match the pass).
    desc.ps = {};
    desc.vs = depth_prepass_vertex_shader;
    desc.depth_stencil = vkx::depth_state.reversed;
    desc.render_target_formats[0] = VK_FORMAT_UNDEFINED;
    desc.render_target_count = 0;
    id_pair.depth_pso_id = create_pso_if_needed(desc, true);

    return id_pair;
}

// NOTE: expects data to contain
// struct {
//     u32 width, height, array_size (or depth), flags, mip_levels, format,
//     struct {
//         u32 row_pitch, slice_pitch,
//         u8 image[mip_level][slice_pitch * depth_per_mip],
//     } images[]
// } texture
auto create_resource_from_texture_data(const u8 *const data) -> VulkanTexture
{
    assert(data);
    utl::BlobStreamReader blob{data};
    const u32 width{blob.read<u32>()};
    const u32 height{blob.read<u32>()};
    u32 depth{1};
    u32 array_size{blob.read<u32>()};
    const u32 flags{blob.read<u32>()};
    const u32 mip_levels{blob.read<u32>()};
    const u32 dxgi_format{blob.read<u32>()};
    const bool is_3d{(flags & primal::content::TextureFlags::is_volume_map) != 0};

    const vkx::FormatInfo format{vkx::dxgi_format_info(dxgi_format)};
    if (!format.is_valid())
    {
        log::error(
            log::Category::vulkan,
            "Texture format DXGI_FORMAT {} isn't supported yet (vkx::dxgi_format_info).",
            dxgi_format);
        assert(false);
        return {};
    }

    assert(mip_levels <= VulkanTexture::max_mips);

    // D3D12 (feature level 11+) guarantees sampling, copying and linear filtering of the formats textures use. In
    // Vulkan, linear filtering of 32-bit float formats (the IBL cube maps are R32G32B32A32_FLOAT) is optional.
    {
        VkFormatProperties properties{};
        ::vkGetPhysicalDeviceFormatProperties(core::physical_device(), format.format, &properties);
        const VkFormatFeatureFlags features{properties.optimalTilingFeatures};
        constexpr VkFormatFeatureFlags required{
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT};
        if ((features & required) != required)
        {
            log::error(
                log::Category::vulkan,
                "Texture format DXGI_FORMAT {} (VkFormat {}) can't be sampled on this device.",
                dxgi_format,
                static_cast<s32>(format.format));
            assert(false);
            return {};
        }
        if (!(features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
        {
            log::warning(
                log::Category::vulkan,
                "Texture format DXGI_FORMAT {} (VkFormat {}) can't be filtered linearly on this device; "
                "linear/anisotropic sampling of it is undefined.",
                dxgi_format,
                static_cast<s32>(format.format));
        }
    }

    u32 depth_per_mip_level[VulkanTexture::max_mips]{};
    for (u32 i{0}; i < VulkanTexture::max_mips; ++i)
    {
        depth_per_mip_level[i] = 1;
    }

    if (is_3d)
    {
        depth = array_size;
        array_size = 1;
        u32 depth_per_mip{depth};

        for (u32 i{0}; i < mip_levels; ++i)
        {
            depth_per_mip_level[i] = depth_per_mip;
            depth_per_mip = std::max(depth_per_mip >> 1, static_cast<u32>(1));
        }
    }

    assert(!(flags & primal::content::TextureFlags::is_cube_map && (array_size % 6)));
    const u32 subresource_count{array_size * mip_levels};
    assert(subresource_count);

    // Buffer -> image copies: one region per subresource (array layer, mip), in the blob's order. Each
    // subresource's data is copied as is into the upload buffer; the row pitch in the blob becomes
    // bufferRowLength (in texels). Offsets must be a multiple of the texel block size and of 4.
    const u32 offset_alignment{std::lcm(format.block_size, 4u)};
    struct Subresource
    {
        const u8 *data;
        u32 size;
    };
    utl::vector<Subresource> subresources{};
    utl::vector<VkBufferImageCopy> regions{};
    u64 upload_size{0};

    for (u32 i{0}; i < array_size; ++i)
    {
        for (u32 j{0}; j < mip_levels; ++j)
        {
            const u32 row_pitch{blob.read<u32>()};
            const u32 slice_pitch{blob.read<u32>()};
            const u32 size{slice_pitch * depth_per_mip_level[j]};
            assert(row_pitch && row_pitch % format.block_size == 0 && slice_pitch % row_pitch == 0);

            upload_size = math::align_size_up(upload_size, offset_alignment);

            VkBufferImageCopy region{};
            region.bufferOffset = upload_size;
            region.bufferRowLength = row_pitch / format.block_size * format.block_width;
            region.bufferImageHeight = slice_pitch / row_pitch * format.block_height;
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, j, i, 1};
            region.imageExtent = {std::max(width >> j, 1u), std::max(height >> j, 1u), depth_per_mip_level[j]};
            regions.emplace_back(region);
            subresources.emplace_back(Subresource{blob.position(), size});

            upload_size += size;
            // skip the rest of slices.
            blob.skip(size);
        }
    }

    VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    // TODO: handle 1D textures.
    image_info.imageType = is_3d ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
    image_info.format = format.format;
    image_info.extent = {width, height, depth};
    image_info.mipLevels = mip_levels;
    image_info.arrayLayers = array_size;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    // The default SRV view is a cube (array) for cube-compatible images with a multiple of 6 layers.
    if (flags & primal::content::TextureFlags::is_cube_map)
    {
        image_info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    }

    VulkanTextureInitInfo info{};
    info.image_info = &image_info;
    VulkanTexture texture{info};
    if (!texture.image())
    {
        return {};
    }

    assert(upload_size && upload_size <= u32_invalid_id);
    upload::VulkanUploadContext context{static_cast<u32>(upload_size)};
    u8 *const cpu_address{static_cast<u8 *>(context.cpu_address())};
    for (u32 i{0}; i < subresource_count; ++i)
    {
        std::memcpy(cpu_address + regions[i].bufferOffset, subresources[i].data, subresources[i].size);
    }

    const VkCommandBuffer cmd{context.command_buffer()};
    const auto &states{vkx::resource_states};
    vkx::transition_image(cmd, texture.image(), VK_IMAGE_ASPECT_COLOR_BIT, states.undefined, states.copy_dest);
    ::vkCmdCopyBufferToImage(
        cmd,
        context.upload_buffer(),
        texture.image(),
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        subresource_count,
        regions.data());
    // The layout the SRV descriptor was written with. Every later use comes after this barrier in queue order.
    constexpr vkx::ResourceState shader_read{
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL};
    vkx::transition_image(cmd, texture.image(), VK_IMAGE_ASPECT_COLOR_BIT, states.copy_dest, shader_read);
    context.end_upload();

    return texture;
}

}

auto initialize() -> bool
{
    return true;
}

auto shutdown() -> void
{
    // NOTE: we only release data that were created as a side-effect to adding resources,
    //       which the user of this module has no control over. The rest of data should be released
    //       by the user, by calling "remove" functions, prior to shutting down the renderer.
    //       That way we make sure the book-keeping of content is correct.

    for (auto &item : root_signatures)
    {
        vkx::release(item);
    }

    mtl_rs_map.clear();
    root_signatures.clear();

    for (auto &item : pipeline_states)
    {
        core::deferred_release(item);
    }

    pso_map.clear();
    pipeline_states.clear();
}

namespace submesh
{

// NOTE: Expects 'data' to contain:
//
// u32 size_of_mesh_data
// struct{
//     u8 positions[sizeof(f32) * 3 * vertex_count],     // sizeof(positions) must be a multiple of 4 bytes. Pad if
//     needed. u8 elements[elements_size * vertex_count],        // sizeof(elements) must be a multiple of 4 bytes. Pad
//     if needed. u8 indices[index_size * index_count],             // sizeof(indices) must be a multiple of 4 bytes.
//     Pad if needed.
// } submesh_data[total_submesh_count]
//
// struct{
//     u32 elements_size, u32 vertex_count,
//     u32 index_count, u32 elements_type, u32 primitive_topology
// } submesh_info[total_submesh_count]
//
// Remarks:
// - Position and element buffers should be padded to be a multiple of 4 bytes in length.
//   This 4 bytes is defined as D3D12_STANDARD_MAXIMUM_ELEMENT_ALIGNMENT_BYTE_MULTIPLE.
auto add(const u8 *const data, id::id_type *const ids, u32 count) -> void
{
    assert(data && ids && count);

    utl::BlobStreamReader blob{data};
    const u32 mesh_data_size{blob.read<u32>()};
    const u8 *const mesh_data{blob.position()};
    blob.skip(mesh_data_size); // skip the mesh data to read submesh info.

    // The asset packs each part at a multiple of 4 bytes (D3D12_STANDARD_MAXIMUM_ELEMENT_ALIGNMENT_BYTE_MULTIPLE),
    // which is all D3D12 root SRVs need. Vulkan storage buffer ranges need minStorageBufferOffsetAlignment (16 on
    // NVIDIA), so the parts are repacked while uploading: source offsets as in the asset, destination offsets
    // aligned to core::storage_buffer_offset_alignment().
    constexpr u32 asset_alignment{4};
    const u32 alignment{core::storage_buffer_offset_alignment()};

    struct SubmeshLayout
    {
        u32 element_size, vertex_count, index_count, elements_type, primitive_topology, index_size;
        u32 sizes[3];       // positions, elements, indices
        u64 src_offsets[3]; // in the asset
        u64 dst_offsets[3]; // in the GPU buffer
    };
    utl::vector<SubmeshLayout> layouts(count);
    utl::vector<VkBufferCopy> regions{};
    u64 src_offset{0};
    u64 dst_offset{0};

    for (u32 i{0}; i < count; ++i)
    {
        SubmeshLayout &l{layouts[i]};
        l.element_size = blob.read<u32>();
        l.vertex_count = blob.read<u32>();
        l.index_count = blob.read<u32>();
        l.elements_type = blob.read<u32>();
        l.primitive_topology = blob.read<u32>();
        l.index_size = static_cast<u32>((l.vertex_count < (1 << 16)) ? sizeof(u16) : sizeof(u32));

        // NOTE: element size may be 0, for position-only vertex formats.
        l.sizes[0] = static_cast<u32>(sizeof(math::v3)) * l.vertex_count;
        l.sizes[1] = l.element_size * l.vertex_count;
        l.sizes[2] = l.index_size * l.index_count;

        for (u32 part{0}; part < 3; ++part)
        {
            dst_offset = math::align_size_up(dst_offset, alignment);
            l.src_offsets[part] = src_offset;
            l.dst_offsets[part] = dst_offset;
            if (l.sizes[part])
            {
                regions.emplace_back(VkBufferCopy{src_offset, dst_offset, l.sizes[part]});
            }
            src_offset += math::align_size_up<asset_alignment>(l.sizes[part]);
            dst_offset += l.sizes[part];
        }
    }

    // If this fails, the asset isn't packed the way this code (and d3d12::content::submesh::add) expects.
    assert(src_offset == mesh_data_size);

    // One buffer for all submeshes: positions and elements are bound as root SRVs, indices as index buffers.
    VulkanBufferInitInfo info{};
    info.size = static_cast<u32>(dst_offset);
    info.alignment = sizeof(math::v4);
    info.usage =
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VulkanBuffer buffer{info, false};
    const VkBuffer vk_buffer{buffer.buffer()};
    NAME_VK_OBJECT_INDEXED(vk_buffer, VK_OBJECT_TYPE_BUFFER, info.size, "Mesh Buffer - size");

    {
        upload::VulkanUploadContext context{mesh_data_size};
        std::memcpy(context.cpu_address(), mesh_data, mesh_data_size);
        ::vkCmdCopyBuffer(
            context.command_buffer(),
            context.upload_buffer(),
            vk_buffer,
            static_cast<u32>(regions.size()),
            regions.data());
        context.end_upload();
    }

    const std::scoped_lock lock{mesh_mutex};

    const u32 resource_id{mesh_buffers.add(std::move(buffer))};

    for (u32 i{0}; i < count; ++i)
    {
        const SubmeshLayout &l{layouts[i]};
        SubmeshView view{};
        view.position_buffer = {vk_buffer, l.dst_offsets[0], l.sizes[0]};

        if (l.element_size)
        {
            view.element_buffer = {vk_buffer, l.dst_offsets[1], l.sizes[1]};
        }

        view.index_buffer_view.buffer = vk_buffer;
        view.index_buffer_view.offset = l.dst_offsets[2];
        view.index_buffer_view.size_in_bytes = l.sizes[2];
        view.index_buffer_view.index_type = (l.index_size == sizeof(u16)) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;

        view.primitive_topology = get_vk_primitive_topology(static_cast<PrimitiveTopology::Type>(l.primitive_topology));
        view.elements_type = l.elements_type;
        view.resource_index = resource_id;

        ids[i] = submesh_views.add(view);
    }
}

auto remove(const id::id_type *const ids, u32 count) -> void
{
    assert(ids && count);
    const std::scoped_lock lock{mesh_mutex};
    const u32 resource_id{submesh_views[ids[0]].resource_index};
    assert(resource_id != u32_invalid_id);

    for (u32 i{0}; i < count; ++i)
    {
        submesh_views.remove(ids[i]);
    }

    // The buffer's destructor defers its release.
    mesh_buffers.remove(resource_id);
}

auto get_views(const id::id_type *const gpu_ids, u32 id_count, const ViewsCache &cache) -> void
{
    assert(gpu_ids && id_count);
    assert(
        cache.position_buffers && cache.element_buffers && cache.index_buffer_views && cache.primitive_topologies &&
        cache.elements_types);

    const std::scoped_lock lock{mesh_mutex};
    for (u32 i{0}; i < id_count; ++i)
    {
        const SubmeshView &view{submesh_views[gpu_ids[i]]};
        cache.position_buffers[i] = view.position_buffer;
        cache.element_buffers[i] = view.element_buffer;
        cache.index_buffer_views[i] = view.index_buffer_view;
        cache.primitive_topologies[i] = view.primitive_topology;
        cache.elements_types[i] = view.elements_type;
    }
}
}

namespace texture
{
// NOTE: expects data to contain
// struct {
//     u32 width, height, array_size (or depth), flags, mip_levels, format,
//     struct {
//         u32 row_pitch, slice_pitch,
//         u8 image[mip_level][slice_pitch * depth_per_mip],
//     } images[]
// } texture
auto add(const u8 *const data) -> id::id_type
{
    assert(data);
    VulkanTexture texture{create_resource_from_texture_data(data)};

    const std::scoped_lock lock{texture_mutex};
    const id::id_type id{textures.add(std::move(texture))};
    descriptor_indices.add(textures[id].srv().index);
    return id;
}

auto remove(id::id_type id) -> void
{
    const std::scoped_lock lock{texture_mutex};
    textures.remove(id);
    descriptor_indices.remove(id);
}

auto get_descriptor_indices(const id::id_type *const texture_ids, u32 id_count, u32 *const indices) -> void
{
    assert(texture_ids && id_count && indices);
    const std::scoped_lock lock{texture_mutex};
    for (u32 i{0}; i < id_count; ++i)
    {
        assert(id::is_valid(texture_ids[i]));
        indices[i] = descriptor_indices[texture_ids[i]];
    }
}
}

namespace material
{

// Output format:
//
// struct {
// material_type::type  type,
// shader_flags::flags  flags,
// id::id_type          root_signature_id,
// u32                  texture_count,
// id::id_type          shader_ids[shader_count],
// id::id_type          texture_ids[texture_count],
// u32*                 descriptor_indices[texture_count]
// } vulkan_material
auto add(MaterialInitInfo info) -> id::id_type
{
    std::unique_ptr<u8[]> buffer;
    const std::scoped_lock lock{material_mutex};
    const VulkanMaterialStream stream{buffer, info};
    assert(buffer);
    return materials.add(std::move(buffer));
}

auto remove(id::id_type id) -> void
{
    const std::scoped_lock lock{material_mutex};
    materials.remove(id);
}

auto get_materials(
    const id::id_type *const material_ids,
    u32 material_count,
    const MaterialsCache &cache,
    u32 &descriptor_index_count) -> void
{
    assert(material_ids && material_count);
    assert(cache.root_signatures && cache.material_types);
    const std::scoped_lock lock{material_mutex};

    u32 total_index_count{0};

    for (u32 i{0}; i < material_count; ++i)
    {
        const VulkanMaterialStream stream{materials[material_ids[i]].get()};
        cache.root_signatures[i] = root_signatures[stream.root_signature_id()];
        cache.material_types[i] = stream.material_type();
        cache.descriptor_indices[i] = stream.descriptor_indices();
        cache.texture_count[i] = stream.texture_count();
        cache.material_surfaces[i] = stream.surface();
        total_index_count += stream.texture_count();
    }

    descriptor_index_count = total_index_count;
}

}

namespace render_item
{

// Creates a buffer that's basically an array of id::id_types.
// buffer[0] = geometry_content_id
// buffer[1 .. n] = vulkan_render_item_ids (n is the number of low-level render item ids which must also equal the
// number of submeshes/material ids). buffer[n + 1] = id::invalid_id (this marks the end of submesh_gpu_id array).
//
auto add(
    id::id_type entity_id,
    id::id_type geometry_content_id,
    u32 material_count,
    const id::id_type *const material_ids) -> id::id_type
{
    assert(id::is_valid(entity_id) && id::is_valid(geometry_content_id));
    assert(material_count && material_ids);
    utl::vector<id::id_type> gpu_ids(material_count);
    primal::content::get_submesh_gpu_ids(geometry_content_id, material_count, gpu_ids.data());

    utl::vector<vkx::BufferRange> position_buffers(material_count);
    utl::vector<vkx::BufferRange> element_buffers(material_count);
    utl::vector<submesh::IndexBufferView> index_buffer_views(material_count);
    utl::vector<VkPrimitiveTopology> primitive_topologies(material_count);
    utl::vector<u32> elements_types(material_count);
    const submesh::ViewsCache views_cache{
        position_buffers.data(),
        element_buffers.data(),
        index_buffer_views.data(),
        primitive_topologies.data(),
        elements_types.data()};

    submesh::get_views(gpu_ids.data(), material_count, views_cache);

    // NOTE: the list of ids starts with geometry id and ends with an invalid id to mark the end of the list.
    std::unique_ptr<id::id_type[]> items{std::make_unique<id::id_type[]>(1 + static_cast<u64>(material_count) + 1)};

    items[0] = geometry_content_id;
    id::id_type *const item_ids{&items[1]};

    utl::vector<VulkanRenderItem> vulkan_items(material_count);

    for (u32 i{0}; i < material_count; ++i)
    {
        VulkanRenderItem &item{vulkan_items[i]};
        item.entity_id = entity_id;
        item.submesh_gpu_id = gpu_ids[i];
        item.material_id = material_ids[i];
        const PsoId id_pair{
            create_pso(item.material_id, views_cache.primitive_topologies[i], views_cache.elements_types[i])};
        item.pso_id = id_pair.gpass_pso_id;
        item.depth_pso_id = id_pair.depth_pso_id;

        assert(id::is_valid(item.submesh_gpu_id) && id::is_valid(item.material_id));
    }

    const std::scoped_lock lock{render_item_mutex};

    for (u32 i{0}; i < material_count; ++i)
    {
        item_ids[i] = render_items.add(vulkan_items[i]);
    }

    // mark the end of ids list.
    item_ids[material_count] = id::invalid_id;

    return render_item_ids.add(std::move(items));
}

auto remove(id::id_type id) -> void
{
    const std::scoped_lock lock{render_item_mutex};
    const id::id_type *const item_ids{&render_item_ids[id][1]};

    // NOTE: the last element in the list of ids is always an invalid id.
    for (u32 i{0}; item_ids[i] != id::invalid_id; ++i)
    {
        render_items.remove(item_ids[i]);
    }

    render_item_ids.remove(id);
}

auto get_vulkan_render_item_ids(const FrameInfo &info, utl::vector<id::id_type> &vulkan_render_item_ids) -> void
{
    assert(info.render_item_ids && info.thresholds && info.render_item_count);
    assert(vulkan_render_item_ids.empty());

    frame_cache.lod_offsets.clear();
    frame_cache.geometry_ids.clear();
    const u32 count{info.render_item_count};

    const std::scoped_lock lock{render_item_mutex};

    for (u32 i{0}; i < count; ++i)
    {
        const id::id_type *const buffer{render_item_ids[info.render_item_ids[i]].get()};
        frame_cache.geometry_ids.emplace_back(buffer[0]);
    }

    primal::content::get_lod_offsets(frame_cache.geometry_ids.data(), info.thresholds, count, frame_cache.lod_offsets);
    assert(frame_cache.lod_offsets.size() == count);

    u32 vulkan_render_item_count{0};
    for (u32 i{0}; i < count; ++i)
    {
        vulkan_render_item_count += frame_cache.lod_offsets[i].count;
    }

    assert(vulkan_render_item_count);
    vulkan_render_item_ids.resize(vulkan_render_item_count);

    u32 item_index{0};
    for (u32 i{0}; i < count; ++i)
    {
        const id::id_type *const item_ids{&render_item_ids[info.render_item_ids[i]][1]};
        const primal::content::LodOffset &lod_offset{frame_cache.lod_offsets[i]};
        std::memcpy(
            &vulkan_render_item_ids[item_index], &item_ids[lod_offset.offset], sizeof(id::id_type) * lod_offset.count);
        item_index += lod_offset.count;
        assert(item_index <= vulkan_render_item_count);
    }

    assert(item_index == vulkan_render_item_count);
}

auto get_items(const id::id_type *const vulkan_render_item_ids, u32 id_count, const ItemsCache &cache) -> void
{
    assert(vulkan_render_item_ids && id_count);
    assert(
        cache.entity_ids && cache.submesh_gpu_ids && cache.material_ids && cache.gpass_pipelines &&
        cache.depth_pipelines);

    const std::scoped_lock lock{render_item_mutex, pso_mutex};

    for (u32 i{0}; i < id_count; ++i)
    {
        const VulkanRenderItem &item{render_items[vulkan_render_item_ids[i]]};
        cache.entity_ids[i] = item.entity_id;
        cache.submesh_gpu_ids[i] = item.submesh_gpu_id;
        cache.material_ids[i] = item.material_id;
        cache.gpass_pipelines[i] = pipeline_states[item.pso_id];
        cache.depth_pipelines[i] = pipeline_states[item.depth_pso_id];
    }
}

}

}
