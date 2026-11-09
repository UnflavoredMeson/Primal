// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "VulkanLightCulling.h"
#include "VulkanCamera.h"
#include "VulkanCore.h"
#include "VulkanGPass.h"
#include "VulkanLight.h"
#include "VulkanResources.h"
#include "VulkanShaders.h"
#include "Graphics/Shaders/SharedTypes.h"

namespace primal::graphics::vulkan::delight {
namespace {

// Registers of the light culling root parameters (GridFrustums.hlsl, CullLights.hlsl); same root signature for
// both shaders, like d3d12's light_culling_root_parameter.
struct light_culling_register
{
    enum : u32
    {
        global_shader_data = 0,             // b0
        constants = 1,                      // b1
        frustums_out_or_index_counter = 0,  // u0
        frustums_in = 0,                    // t0
        culling_info = 1,                   // t1
        bounding_spheres = 2,               // t2
        light_grid_opaque = 1,              // u1
        light_index_list_opaque = 3,        // u3
    };
};

struct culling_parameters
{
    vulkan_buffer frustums;
    vulkan_buffer light_grid_and_index_list;
    uav_clearable_buffer light_index_counter;
    hlsl::LightCullingDispatchParameters grid_frustums_dispatch_params{};
    hlsl::LightCullingDispatchParameters light_culling_dispatch_params{};
    u32 frustum_count{ 0 };
    u32 view_width{ 0 };
    u32 view_height{ 0 };
    f32 camera_fov{ 0.f };
    vkx::buffer_range light_grid_opaque_buffer{};
    vkx::buffer_range light_index_list_opaque_buffer{};
    // NOTE: initialize has_lights with 'true' so that the culling shader
    //       is run at least once in order to clear the buffer.
    bool has_lights{ true };
};

struct light_culler
{
    culling_parameters cullers[frame_buffer_count]{};
};

constexpr u32 max_lights_per_tile{ 256 };

vkx::root_signature light_culling_root_signature{};
VkPipeline grid_frustum_pso{ VK_NULL_HANDLE };
VkPipeline light_culling_pso{ VK_NULL_HANDLE };
utl::free_list<light_culler> light_cullers;

// Buffer states (D3D12: UNORDERED_ACCESS, NON_PIXEL_SHADER_RESOURCE | PIXEL_SHADER_RESOURCE).
constexpr vkx::resource_state compute_write{ VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                             VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT };
constexpr vkx::resource_state shader_read{ VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                           VK_ACCESS_2_SHADER_STORAGE_READ_BIT };
// Before overwriting a buffer that was only read: a write after a read only needs an execution dependency.
constexpr vkx::resource_state after_shader_read{ VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                                 VK_ACCESS_2_NONE };

bool
create_root_signatures()
{
    assert(!light_culling_root_signature.is_valid());
    using param = vkx::root_parameter;
    using reg = light_culling_register;
    constexpr VkShaderStageFlags compute{ VK_SHADER_STAGE_COMPUTE_BIT };
    const param parameters[]{
        param::cbv(reg::global_shader_data, compute),
        param::cbv(reg::constants, compute),
        param::uav(reg::frustums_out_or_index_counter, compute),
        param::srv(reg::frustums_in, compute),
        param::srv(reg::culling_info, compute),
        param::srv(reg::bounding_spheres, compute),
        param::uav(reg::light_grid_opaque, compute),
        param::uav(reg::light_index_list_opaque, compute),
    };

    light_culling_root_signature = vkx::create_root_signature(&parameters[0], (u32)std::size(parameters), "Light Culling Root Signature");
    return light_culling_root_signature.is_valid();
}

bool
create_psos()
{
    assert(!grid_frustum_pso && !light_culling_pso);
    grid_frustum_pso = vkx::create_compute_pipeline(light_culling_root_signature.layout,
                                                    shaders::get_engine_shader(shaders::engine_shader::grid_frustums_cs),
                                                    "Grid Frustums PSO");
    light_culling_pso = vkx::create_compute_pipeline(light_culling_root_signature.layout,
                                                     shaders::get_engine_shader(shaders::engine_shader::light_culling_cs),
                                                     "Light Culling PSO");
    return grid_frustum_pso && light_culling_pso;
}

void
resize_buffers(culling_parameters& culler)
{
    const u32 frustum_count{ culler.frustum_count };

    // The light index list follows the light grid in the same buffer. D3D12 puts it at a 16-byte offset; here the
    // offset must also be a multiple of the storage buffer offset alignment (it's bound as a root UAV/SRV).
    const u32 alignment{ core::storage_buffer_offset_alignment() };
    const u32 frustums_buffer_size{ (u32)sizeof(hlsl::Frustum) * frustum_count };
    const u32 light_grid_buffer_size{ (u32)math::align_size_up(sizeof(math::u32v2) * frustum_count, alignment) };
    const u32 light_index_list_buffer_size{ (u32)math::align_size_up<sizeof(math::v4)>(sizeof(u32) * max_lights_per_tile * frustum_count) };
    const u32 light_grid_and_index_list_buffer_size{ light_grid_buffer_size + light_index_list_buffer_size };

    vulkan_buffer_init_info info{};
    info.alignment = sizeof(math::v4);
    info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

    if (frustums_buffer_size > culler.frustums.size())
    {
        info.size = frustums_buffer_size;
        culler.frustums = vulkan_buffer{ info, false };
        NAME_VK_OBJECT_INDEXED(culler.frustums.buffer(), VK_OBJECT_TYPE_BUFFER, frustum_count, "Light Grid Frustums Buffer - count");
    }

    if (light_grid_and_index_list_buffer_size > culler.light_grid_and_index_list.size())
    {
        info.size = light_grid_and_index_list_buffer_size;
        culler.light_grid_and_index_list = vulkan_buffer{ info, false };
        NAME_VK_OBJECT_INDEXED(culler.light_grid_and_index_list.buffer(), VK_OBJECT_TYPE_BUFFER,
                               light_grid_and_index_list_buffer_size, "Light Grid and Index List Buffer - size");

        if (!culler.light_index_counter.buffer())
        {
            info = uav_clearable_buffer::get_default_init_info(1);
            culler.light_index_counter = uav_clearable_buffer{ info };
            NAME_VK_OBJECT_INDEXED(culler.light_index_counter.buffer(), VK_OBJECT_TYPE_BUFFER, core::current_frame_index(),
                                   "Light Index Counter Buffer");
        }
    }

    // NOTE: D3D12 binds the whole buffer from the start (or the index list's start) as root views; root views have
    //       no size. Vulkan ranges do, so the light grid range is limited to the current view's tiles.
    culler.light_grid_opaque_buffer = culler.light_grid_and_index_list.range(0, light_grid_buffer_size);
    culler.light_index_list_opaque_buffer = culler.light_grid_and_index_list.range(light_grid_buffer_size, light_index_list_buffer_size);
}

void
resize(culling_parameters& culler)
{
    constexpr u32 tile_size{ light_culling_tile_size };
    assert(culler.view_width >= tile_size && culler.view_height >= tile_size);
    const math::u32v2 tile_count{ (u32)math::align_size_up<tile_size>(culler.view_width) / tile_size,
                                  (u32)math::align_size_up<tile_size>(culler.view_height) / tile_size };

    culler.frustum_count = tile_count.x * tile_count.y;

    // Dispatch parameters for grid frustums
    {
        hlsl::LightCullingDispatchParameters& params{ culler.grid_frustums_dispatch_params };
        params.NumThreads = tile_count;
        params.NumThreadGroups.x = (u32)math::align_size_up<tile_size>(tile_count.x) / tile_size;
        params.NumThreadGroups.y = (u32)math::align_size_up<tile_size>(tile_count.y) / tile_size;
    }

    // Dispatch parameters for light culling
    {
        hlsl::LightCullingDispatchParameters& params{ culler.light_culling_dispatch_params };
        params.NumThreads.x = tile_count.x * tile_size;
        params.NumThreads.y = tile_count.y * tile_size;
        params.NumThreadGroups = tile_count;
    }

    resize_buffers(culler);
}

void
calculate_grid_frustums(const culling_parameters& culler, VkCommandBuffer cmd, const vulkan_frame_info& vulkan_info,
                        vkx::vulkan_barrier& barriers)
{
    constant_buffer& cbuffer{ core::cbuffer() };
    hlsl::LightCullingDispatchParameters* const buffer{ cbuffer.allocate<hlsl::LightCullingDispatchParameters>() };
    const hlsl::LightCullingDispatchParameters& params{ culler.grid_frustums_dispatch_params };
    memcpy(buffer, &params, sizeof(hlsl::LightCullingDispatchParameters));

    // Make frustums buffer writable
    // TODO: remove the fragment shader stage (it's only there so we can visualize grid frustums).
    barriers.add(culler.frustums.buffer(), after_shader_read, compute_write);
    barriers.apply(cmd);

    using reg = light_culling_register;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, grid_frustum_pso);
    vkx::root_descriptors root{};
    root.cbv(reg::global_shader_data, vulkan_info.global_shader_data);
    root.cbv(reg::constants, cbuffer.range(buffer));
    root.uav(reg::frustums_out_or_index_counter, culler.frustums.range());
    root.push(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, light_culling_root_signature);
    vkCmdDispatch(cmd, params.NumThreadGroups.x, params.NumThreadGroups.y, 1);

    // Make frustums buffer readable
    // NOTE: cull_lights() will apply this transition.
    barriers.add(culler.frustums.buffer(), compute_write, shader_read);
}

void
resize_and_calculate_grid_frustums(culling_parameters& culler, VkCommandBuffer cmd, const vulkan_frame_info& vulkan_info,
                                   vkx::vulkan_barrier& barriers)
{
    culler.camera_fov = vulkan_info.camera->field_of_view();
    culler.view_width = vulkan_info.surface_width;
    culler.view_height = vulkan_info.surface_height;

    resize(culler);
    calculate_grid_frustums(culler, cmd, vulkan_info, barriers);
}

[[nodiscard]] vkx::buffer_range
valid_or_zero(const vkx::buffer_range& range)
{
    return range.is_valid() ? range : core::zero_buffer();
}

} // anonymous namespace

bool
initialize()
{
    return create_root_signatures() && create_psos() && light::initialize();
}

void
shutdown()
{
    light::shutdown();
    vkx::release(light_culling_root_signature);
    core::deferred_release(grid_frustum_pso);
    core::deferred_release(light_culling_pso);
}

id::id_type
add_culler()
{
    return light_cullers.add();
}

void
remove_culler(id::id_type id)
{
    assert(id::is_valid(id));
    light_cullers.remove(id);
}

void
cull_lights(VkCommandBuffer cmd, const vulkan_frame_info& vulkan_info, vkx::vulkan_barrier& barriers)
{
    const id::id_type id{ vulkan_info.light_culling_id };
    assert(id::is_valid(id) && vulkan_info.camera);
    culling_parameters& culler{ light_cullers[id].cullers[vulkan_info.frame_index] };

    if (vulkan_info.surface_width != culler.view_width || vulkan_info.surface_height != culler.view_height ||
        !math::is_equal(vulkan_info.camera->field_of_view(), culler.camera_fov))
    {
        resize_and_calculate_grid_frustums(culler, cmd, vulkan_info, barriers);
    }

    hlsl::LightCullingDispatchParameters& params{ culler.light_culling_dispatch_params };
    params.NumLights = light::cullable_light_count(vulkan_info.info->light_set_key);
    params.DepthBufferSrvIndex = gpass::depth_buffer().srv().index;

    // NOTE: we update culler.has_lights after this statement, so the light culling shader
    //       will run once to clear the buffers when there're no lights.
    if (!params.NumLights && !culler.has_lights) return;

    culler.has_lights = params.NumLights > 0;

    constant_buffer& cbuffer{ core::cbuffer() };
    hlsl::LightCullingDispatchParameters* const buffer{ cbuffer.allocate<hlsl::LightCullingDispatchParameters>() };
    memcpy(buffer, &params, sizeof(hlsl::LightCullingDispatchParameters));

    // Make light grid and light index buffers writable, and the index counter clearable.
    // NOTE: this also applies the barriers the caller added (gpass: depth buffer -> depth_read, which this pass samples).
    barriers.add(culler.light_grid_and_index_list.buffer(), after_shader_read, compute_write);
    barriers.add(culler.light_index_counter.buffer(), compute_write, vkx::resource_states.copy_dest);
    barriers.apply(cmd);

    const math::u32v4 clear_value{ 0, 0, 0, 0 };
    culler.light_index_counter.clear_uav(cmd, &clear_value.x);
    // D3D12's ClearUnorderedAccessViewUint needs no barrier before the dispatch in the same state; a fill (transfer)
    // does.
    barriers.add(culler.light_index_counter.buffer(), vkx::resource_states.copy_dest, compute_write);
    barriers.apply(cmd);

    using reg = light_culling_register;
    const u32 frame_index{ vulkan_info.frame_index };
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, light_culling_pso);
    vkx::root_descriptors root{};
    root.cbv(reg::global_shader_data, vulkan_info.global_shader_data);
    root.cbv(reg::constants, cbuffer.range(buffer));
    root.uav(reg::frustums_out_or_index_counter, culler.light_index_counter.range());
    root.srv(reg::frustums_in, culler.frustums.range());
    root.srv(reg::culling_info, light::culling_info_buffer(frame_index));
    root.srv(reg::bounding_spheres, light::bounding_spheres_buffer(frame_index));
    root.uav(reg::light_grid_opaque, culler.light_grid_opaque_buffer);
    root.uav(reg::light_index_list_opaque, culler.light_index_list_opaque_buffer);
    root.push(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, light_culling_root_signature);
    // The shader reads the depth buffer from the resource heap.
    vkx::bind_resource_heap(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, light_culling_root_signature);

    vkCmdDispatch(cmd, params.NumThreadGroups.x, params.NumThreadGroups.y, 1);

    // Make light grid and light index buffers readable
    // NOTE: this transition barrier will be applied by the caller of this function.
    barriers.add(culler.light_grid_and_index_list.buffer(), compute_write, shader_read);
}

// TODO: temporary for visualizing light culling. Remove later.
// NOTE: before the first culling of a surface the buffers don't exist; core's zero buffer stands in.
vkx::buffer_range
frustums(id::id_type light_culling_id, u32 frame_index)
{
    assert(frame_index < frame_buffer_count && id::is_valid(light_culling_id));
    const vulkan_buffer& buffer{ light_cullers[light_culling_id].cullers[frame_index].frustums };
    return buffer.buffer() ? buffer.range() : core::zero_buffer();
}

vkx::buffer_range
light_grid_opaque(id::id_type light_culling_id, u32 frame_index)
{
    assert(frame_index < frame_buffer_count && id::is_valid(light_culling_id));
    return valid_or_zero(light_cullers[light_culling_id].cullers[frame_index].light_grid_opaque_buffer);
}

vkx::buffer_range
light_index_list_opaque(id::id_type light_culling_id, u32 frame_index)
{
    assert(frame_index < frame_buffer_count && id::is_valid(light_culling_id));
    return valid_or_zero(light_cullers[light_culling_id].cullers[frame_index].light_index_list_opaque_buffer);
}

} // namespace primal::graphics::vulkan::delight
