#!/usr/bin/env python3
# Copyright (c) 2026 Charles Smith
# Distributed under the MIT license. See the LICENSE file in the project root for more information.
# Generates clang-cl compile databases for the Windows (.vcxproj) builds, so clang-tidy can process
# Windows-only code on Linux. Usage: gen_windb.py <repo> <xwin dir> <out dir>
import json, os, re, sys, xml.etree.ElementTree as ET

repo, xwin, out = (os.path.abspath(a) for a in sys.argv[1:4])
ns = {"m": "http://schemas.microsoft.com/developer/msbuild/2003"}
d3d12_inc = f"{xwin}/packages/Microsoft.Direct3D.D3D12.1.619.4/build/native/include"
dxc_inc = f"{xwin}/packages/Microsoft.Direct3D.DXC.1.9.2602.24/build/native/include"
vk_inc = f"{xwin}/vk-include"

def sources(vcxproj):
    root = ET.parse(os.path.join(repo, vcxproj)).getroot()
    base = os.path.dirname(vcxproj)
    files = []
    for item in root.iterfind(".//m:ClCompile", ns):
        inc = item.get("Include")
        if not inc or not inc.endswith(".cpp"):
            continue
        files.append(resolve(os.path.normpath(os.path.join(base, inc.replace("\\", "/")))))
    return files

def resolve(rel):
    # Windows paths are case-insensitive; find the real spelling on disk.
    cur = repo
    for part in rel.split("/"):
        match = [e for e in os.listdir(cur) if e.lower() == part.lower()]
        assert match, rel
        cur = os.path.join(cur, match[0])
    return os.path.relpath(cur, repo)

def command(file, defines, includes, directory):
    args = ["clang-cl", "--target=x86_64-pc-windows-msvc", "/winsysroot", f"{xwin}/sysroot", "/std:c++23preview",
            "/EHsc", "/arch:AVX2", "/fp:fast", "/DUNICODE", "/D_UNICODE", "/DPRIMAL_GRAPHICS_VULKAN=1",
            "-Wno-unknown-pragmas", "-Wno-microsoft-include",
            "-Wno-c++11-narrowing"]  # MSVC accepts narrowing in braced initializers; clang-cl errors
    args += [f"/D{d}" for d in defines]
    args += [f"-I{i}" for i in includes]
    args += ["/c", os.path.join(repo, file)]
    return {"directory": directory, "file": os.path.join(repo, file), "arguments": args}

engine = sources("Engine/Engine.vcxproj")
engine_inc = [f"{repo}/Engine", f"{repo}/Engine/Common", d3d12_inc, vk_inc, f"{repo}/Engine/ThirdParty/vma"]
test_inc = [f"{repo}/Engine/Common", f"{repo}/Engine", f"{repo}/EngineDLL", dxc_inc, d3d12_inc, vk_inc,
            f"{repo}/Engine/ThirdParty/vma"]

dbs = {
    "engine-debug": [command(f, ["_DEBUG", "_LIB"], engine_inc, f"{repo}/Engine") for f in engine],
    "engine-editor": [command(f, ["_DEBUG", "_LIB", "USE_WITH_EDITOR"], engine_inc, f"{repo}/Engine") for f in engine],
    "engine-release": [command(f, ["NDEBUG", "_LIB"], engine_inc, f"{repo}/Engine") for f in engine],
    "enginedll": [command(f, ["_DEBUG", "ENGINEDLL_EXPORTS", "_WINDOWS", "_USRDLL", "USE_WITH_EDITOR"],
                          [f"{repo}/Engine", f"{repo}/Engine/Common", dxc_inc, d3d12_inc, vk_inc], f"{repo}/EngineDLL")
                  for f in sources("EngineDLL/EngineDLL.vcxproj")],
    "contenttools": [command(f, ["_DEBUG", "CONTENTTOOLS_EXPORTS", "_WINDOWS", "_USRDLL"],
                             [f"{repo}/Engine", f"{repo}/Engine/Common", d3d12_inc, f"{xwin}/directxtex", f"{xwin}/contenttools-inc"],
                             f"{repo}/ContentTools")
                     for f in sources("ContentTools/ContentTools.vcxproj")],
    "shadertool": [command(f, ["_DEBUG", "_CONSOLE"], test_inc, f"{repo}/ShaderCompilerTool")
                   for f in ["ShaderCompilerTool/Main.cpp", "EngineDLL/ShaderCompilation.cpp"]],
}
tests = ["ENTITY_COMPONENTS", "WINDOW", "RENDERER", "SURFACES"]
for t in tests:
    files = ["EngineTest/Main.cpp"]
    if t == "RENDERER":
        files += ["EngineTest/Lights.cpp", "EngineTest/RenderItem.cpp", "EngineTest/Scripts.cpp", "EngineTest/TestRenderer.cpp"]
    defines = ["_DEBUG", "_CONSOLE"] + [f"TEST_{x}={int(x == t)}" for x in tests]
    dbs[f"enginetest-{t.lower()}"] = [command(f, defines, test_inc, f"{repo}/EngineTest") for f in files]

for name, entries in dbs.items():
    os.makedirs(f"{out}/{name}", exist_ok=True)
    with open(f"{out}/{name}/compile_commands.json", "w") as fp:
        json.dump(entries, fp, indent=1)
    print(name, len(entries))
