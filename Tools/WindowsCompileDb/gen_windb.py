#!/usr/bin/env python3
# Copyright (c) 2026 Charles Smith
# Distributed under the MIT license. See the LICENSE file in the project root for more information.
# Generates clang-cl compile databases for the Windows (.vcxproj) builds, so clang-tidy can process
# Windows-only code on Linux. Usage: gen_windb.py <repo> <xwin dir> <out dir>
import json, os, re, subprocess, sys, xml.etree.ElementTree as ET

repo, xwin, out = (os.path.abspath(a) for a in sys.argv[1:4])
ns = {"m": "http://schemas.microsoft.com/developer/msbuild/2003"}
d3d12_inc = f"{xwin}/packages/Microsoft.Direct3D.D3D12.1.619.4/build/native/include"
dxc_inc = f"{xwin}/packages/Microsoft.Direct3D.DXC.1.9.2602.24/build/native/include"
vk_inc = f"{xwin}/vk-include"
# doctest.h as downloaded by the Linux CMake configure (cmake/PrimalDependencies.cmake).
doctest_inc = f"{repo}/build/linux-debug/_deps/doctest-src"

def sources(vcxproj, extension=".cpp"):
    root = ET.parse(os.path.join(repo, vcxproj)).getroot()
    base = os.path.dirname(vcxproj)
    files = []
    for item in root.iterfind(".//m:ClCompile", ns):
        inc = item.get("Include")
        if not inc or not inc.endswith(extension):
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
# The engine's named modules (module interface units). Every database gets them precompiled with its own defines
# (see the end of this file), so that files that import them can be parsed.
interfaces = sources("Engine/Engine.vcxproj", ".ixx")
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
dbs["unittests"] = [command(f, ["_DEBUG", "_CONSOLE"], test_inc + [doctest_inc],
                            f"{repo}/UnitTests") for f in sources("UnitTests/UnitTests.vcxproj")]
tests = ["ENTITY_COMPONENTS", "WINDOW", "RENDERER", "SURFACES"]
for t in tests:
    files = ["EngineTest/Main.cpp"]
    if t == "RENDERER":
        files += ["EngineTest/Lights.cpp", "EngineTest/RenderItem.cpp", "EngineTest/Scripts.cpp", "EngineTest/TestRenderer.cpp"]
    defines = ["_DEBUG", "_CONSOLE"] + [f"TEST_{x}={int(x == t)}" for x in tests]
    dbs[f"enginetest-{t.lower()}"] = [command(f, defines, test_inc, f"{repo}/EngineTest") for f in files]

# "headers": every header in a translation unit of its own. The Linux build checks the
# portable ones with the CMake target header_check; this database adds the Windows-only ones.
# FbxImporter.h needs the FBX SDK; IncludeWindowCpp.h includes a .cpp into the platform TU.
header_inc = test_inc + [f"{repo}/ContentTools", f"{xwin}/directxtex", f"{xwin}/contenttools-inc"]
header_defines = ["_DEBUG", "_LIB", "USE_WITH_EDITOR"] + [f"TEST_{x}=0" for x in tests]
os.makedirs(f"{out}/headers/tu", exist_ok=True)
dbs["headers"] = []
for top in ["Engine", "EngineDLL", "EngineTest", "ContentTools"]:
    for folder, _, names in sorted(os.walk(f"{repo}/{top}")):
        for h in sorted(n for n in names if n.endswith(".h")):
            rel = os.path.relpath(f"{folder}/{h}", repo)
            if re.search(r"ThirdParty/|MikkTSpace/|FbxImporter\.h$|IncludeWindowCpp\.h$", rel):
                continue
            tu = f"{out}/headers/tu/{re.sub('[/.]', '_', rel)}.cpp"
            with open(tu, "w") as fp:
                fp.write(f'#include "{repo}/{rel}"\n')
            entry = command(rel, header_defines, header_inc, f"{repo}/{top}")
            entry["file"] = entry["arguments"][-1] = tu
            dbs["headers"].append(entry)

def module_name(file):
    return re.search(r"^export\s+module\s+([\w.:]+)\s*;", open(os.path.join(repo, file)).read(), re.M).group(1)

# Precompiles the module interfaces for one database and returns the arguments that tell clang where they are.
# NOTE: in the order of the .vcxproj: an interface that imports another one has to come after it.
def precompile_modules(name, entry):
    args = [a for a in entry["arguments"][:-2]]  # without "/c <file>"
    module_args = []
    for file in interfaces:
        pcm = f"{out}/{name}/bmi/{module_name(file)}.pcm"
        os.makedirs(os.path.dirname(pcm), exist_ok=True)
        subprocess.run(args + module_args + ["-x", "c++-module", "--precompile", f"/clang:-o{pcm}",
                                             os.path.join(repo, file)], check=True, cwd=entry["directory"])
        module_args.append(f"-fmodule-file={module_name(file)}={pcm}")
    return module_args

for name, entries in dbs.items():
    os.makedirs(f"{out}/{name}", exist_ok=True)
    if interfaces and entries:
        module_args = precompile_modules(name, entries[0])
        for entry in entries:
            entry["arguments"][-2:-2] = module_args
        # The interfaces themselves are checked with the engine's databases.
        if name.startswith("engine-"):
            for file in interfaces:
                entry = command(file, [], [], f"{repo}/Engine")
                entry["arguments"] = entries[0]["arguments"][:-2] + ["-x", "c++-module", "/c", os.path.join(repo, file)]
                entries.append(entry)
    with open(f"{out}/{name}/compile_commands.json", "w") as fp:
        json.dump(entries, fp, indent=1)
    print(name, len(entries))
