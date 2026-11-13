// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
using PrimalEditor.ContentToolsAPIStructs;
using PrimalEditor.DllWrappers;
using PrimalEditor.Utilities;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;

namespace PrimalEditor.Content;

static class DefaultAssets
{
    public static AssetInfo BrdfIntegrationLut { get; private set; }
    public static AssetInfo DefaultGeometry { get; private set; }
    public static AssetInfo DefaultMaterial { get; private set; }
    public static AssetInfo DefaultTexture { get; private set; }
    public static AssetInfo DefaultSpecularIBL { get; private set; }
    public static AssetInfo DefaultDiffuseIBL { get; private set; }

    // The default assets that exist: one that couldn't be made (GenerateDefaultAssets() logs it) is left out.
    public static List<AssetInfo> DefaultAssetsList => [..
        new[] {
            BrdfIntegrationLut,
            DefaultGeometry,
            DefaultMaterial,
            DefaultTexture,
            DefaultSpecularIBL,
            DefaultDiffuseIBL,
        }.Where(x => x != null)];

    /// <summary>
    ///     Generate default assets if necessary.
    /// </summary>
    public static void GenerateDefaultAssets()
    {
        var defaultAssetsPath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, @"Resources\DefaultAssets\");
        if (!Directory.Exists(defaultAssetsPath))
        {
            Directory.CreateDirectory(defaultAssetsPath);
        }

        var brdfLutFileName = $@"{defaultAssetsPath}BrdfIntegrationLut.asset";

        if (!File.Exists(brdfLutFileName))
        {
            ComputeBrdfIntegrationLut(brdfLutFileName);
        }

        var cubeFileName = $@"{defaultAssetsPath}DefaultCube.asset";

        if (!File.Exists(cubeFileName))
        {
            CreateDefaultCube(cubeFileName);
        }

        var mtlFileName = $@"{defaultAssetsPath}DefaultMaterial.asset";

        if (!File.Exists(mtlFileName))
        {
            CreateDefaultMaterial(mtlFileName);
        }

        var textureFileName = $@"{defaultAssetsPath}DefaultTexture.asset";
        var specularIblFileName = $@"{defaultAssetsPath}DefaultSpecularIBL.asset";
        var diffuseIblFileName = $@"{defaultAssetsPath}DefaultDiffuseIBL.asset";

        BrdfIntegrationLut = Asset.GetAssetInfo(brdfLutFileName);
        DefaultGeometry = Asset.GetAssetInfo(cubeFileName);
        DefaultMaterial = Asset.GetAssetInfo(mtlFileName);
        DefaultTexture = Asset.GetAssetInfo(textureFileName);
        DefaultSpecularIBL = Asset.GetAssetInfo(specularIblFileName);
        DefaultDiffuseIBL = Asset.GetAssetInfo(diffuseIblFileName);

        // A default asset that couldn't be made (e.g. the BRDF LUT without a usable GPU) stays null: say so once, and
        // let the rest of the editor work without it.
        foreach (var (asset, file) in new[] {
            (BrdfIntegrationLut, brdfLutFileName), (DefaultGeometry, cubeFileName), (DefaultMaterial, mtlFileName),
            (DefaultTexture, textureFileName), (DefaultSpecularIBL, specularIblFileName), (DefaultDiffuseIBL, diffuseIblFileName) })
        {
            if (asset == null)
            {
                Logger.Log(MessageType.Error, $"Default asset missing: {file}");
            }
        }
    }

    private static void ComputeBrdfIntegrationLut(string file)
    {
        try
        {
            var brdfLut = new Texture() { FullPath = file };
            ContentToolsAPI.ComputeBrdfIntegrationLut(brdfLut);
            brdfLut.Save(file);
        }
        catch (Exception ex)
        {
            Debug.WriteLine(ex.Message);
        }
    }

    private static void CreateDefaultCube(string file)
    {
        try
        {
            var cube = new Geometry();
            var info = new PrimitiveInitInfo()
            {
                Type = PrimitiveMeshType.Cube,
            };

            ContentToolsAPI.CreatePrimitiveMesh(cube, info);
            cube.Save(file);
        }
        catch (Exception ex)
        {
            Debug.WriteLine(ex.Message);
        }
    }

    private static ShaderGroup CompileShaderGroup(ShaderType type, string code, string functionName, string[] defines, uint[] keys)
    {
        var extraArgs = new List<List<string>>();

        foreach (var def in defines)
        {
            extraArgs.Add(!string.IsNullOrEmpty(def.Trim()) ? new() { "-D", def } : new());
        }

        var shaderGroup = new ShaderGroup() { Type = type, Code = code, FunctionName = functionName, ExtraArgs = extraArgs, Keys = [.. keys] };
        EngineAPI.CompileShader(shaderGroup);

        return shaderGroup;
    }

    private static void CreateDefaultMaterial(string file)
    {
        var vsDefines = new[] { "ELEMENTS_TYPE=0", "ELEMENTS_TYPE=1", "ELEMENTS_TYPE=3" };
        var vsKeys = new[] { (uint)ElementsType.PositionOnly, (uint)ElementsType.StaticNormal, (uint)ElementsType.StaticNormalTexture };
        var psDefines = new[] { string.Empty };
        var psKeys = new[] { (uint)ID.INVALID_ID };

        try
        {
            var code = string.Empty;
            var shaderUri = ContentHelper.GetPackUri(@"Resources/MaterialEditor/DefaultMaterialShaders.hlsl", typeof(DefaultAssets));
            var info = System.Windows.Application.GetResourceStream(shaderUri);
            using (var reader = new StreamReader(info.Stream))
                code = reader.ReadToEnd();

            var vertexShaders = CompileShaderGroup(ShaderType.Vertex, code, "MainVS", vsDefines, vsKeys);
            var pixelShaders = CompileShaderGroup(ShaderType.Pixel, code, "MainPS", psDefines, psKeys);

            var mtl = new Material() { MaterialMode = MaterialMode.Default };
            mtl.AddShaderGroup(vertexShaders);
            mtl.AddShaderGroup(pixelShaders);
            mtl.Save(file);
        }
        catch (Exception ex)
        {
            Debug.WriteLine(ex.Message);
        }
    }
}
