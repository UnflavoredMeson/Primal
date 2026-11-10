// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "ToolsCommon.h"

#include <fbxsdk.h>

namespace primal::tools
{

struct SceneData;
struct Scene;
struct Mesh;
struct GeometryImportSettings;

class FbxContext
{
  public:
    FbxContext(const char *file, Scene *scene, SceneData *data, Progression *const progression)
        : _scene{scene}
        , _scene_data{data}
        , _progression{progression}
    {
        assert(file && _scene && _scene_data && _progression);
        if (initialize_fbx())
        {
            load_fbx_file(file);
            assert(is_valid());
        }
    }

    ~FbxContext()
    {
        _fbx_scene->Destroy();
        _fbx_manager->Destroy();
        ZeroMemory(this, sizeof(FbxContext));
    }

    auto get_scene(FbxNode *root = nullptr) -> void;

    constexpr auto is_valid() const -> bool
    {
        return _fbx_manager && _fbx_scene;
    }
    constexpr auto scene_scale() const -> f32
    {
        return _scene_scale;
    }
    constexpr auto get_progression() const -> Progression *
    {
        return _progression;
    }

  private:
    auto initialize_fbx() -> bool;
    auto load_fbx_file(const char *file) -> void;
    auto get_meshes(FbxNode *node, utl::vector<Mesh> &meshes, u32 lod_id, f32 lod_threshold) -> void;
    auto get_mesh(FbxNodeAttribute *attribute, utl::vector<Mesh> &meshes, u32 lod_id, f32 lod_threshold) -> void;
    auto get_lod_group(FbxNodeAttribute *attribute) -> void;
    auto get_mesh_data(FbxMesh *fbx_mesh, Mesh &m) -> bool;

    Scene *_scene{nullptr};
    SceneData *_scene_data{nullptr};
    FbxManager *_fbx_manager{nullptr};
    FbxScene *_fbx_scene{nullptr};
    Progression *_progression{nullptr};
    f32 _scene_scale{1.0f};
};

}
