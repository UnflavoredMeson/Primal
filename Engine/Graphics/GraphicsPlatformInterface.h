// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "CommonHeaders.h"
#include "Renderer.h"

namespace primal::graphics
{
struct PlatformInterface
{
    bool (*initialize)();
    void (*shutdown)();

    void (*set_option)(RendererOption::Option, const void *const, u32);
    void (*get_option)(RendererOption::Option, void *const, u32);

    struct
    {
        // NOTE: the types are qualified because the members are named like them (GCC -Wchanges-meaning).
        graphics::Surface (*create)(platform::Window);
        void (*remove)(SurfaceId);
        void (*resize)(SurfaceId, u32, u32);
        u32 (*width)(SurfaceId);
        u32 (*height)(SurfaceId);
        void (*render)(SurfaceId, FrameInfo);
    } surface;

    struct
    {
        void (*create_light_set)(u64);
        void (*remove_light_set)(u64);
        graphics::Light (*create)(LightInitInfo);
        void (*remove)(LightId, u64);
        void (*set_parameter)(LightId, u64, LightParameter::Parameter, const void *const, u32);
        void (*get_parameter)(LightId, u64, LightParameter::Parameter, void *const, u32);
    } light;

    struct
    {
        graphics::Camera (*create)(CameraInitInfo);
        void (*remove)(CameraId);
        void (*set_parameter)(CameraId, CameraParameter::Parameter, const void *const, u32);
        void (*get_parameter)(CameraId, CameraParameter::Parameter, void *const, u32);
    } camera;

    struct
    {
        void (*add_mesh)(const u8 *const, id::id_type *const, u32);
        void (*remove_mesh)(const id::id_type *const, u32);
        id::id_type (*add_texture)(const u8 *const);
        void (*remove_texture)(id::id_type);
        id::id_type (*add_material)(MaterialInitInfo);
        void (*remove_material)(id::id_type);
        id::id_type (*add_render_item)(id::id_type, id::id_type, u32, const id::id_type *const);
        void (*remove_render_item)(id::id_type);
    } resources;

    GraphicsPlatform platform = static_cast<GraphicsPlatform>(-1);
};
}
