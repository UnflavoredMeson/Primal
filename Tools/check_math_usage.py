#!/usr/bin/env python3
# Copyright (c) 2026 Charles Smith
# Distributed under the MIT license. See the LICENSE file in the project root for more information.
# Reports DirectXMath used outside the math library. Only Engine/Utilities/Math*.h may name it: everything else
# goes through primal::math (MathTypes.h, MathSimd.h), so that the library behind it can be replaced (issue #6).
# Exit code 1 if something is found. Usage: check_math_usage.py [repo]
import os, re, sys

repo = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else ".")
tops = ["Engine", "EngineDLL", "EngineTest", "UnitTests", "GraphicsTests", "ContentTools", "ShaderCompilerTool"]
skip = re.compile(r"ThirdParty/|MikkTSpace/|^Engine/Utilities/Math\w*\.h$")
# DirectXTex is in namespace DirectX too. These files use it, and may name the namespace.
directxtex = {"ContentTools/TextureImporter.cpp", "ContentTools/NormalMapIdentification.cpp",
              "ContentTools/EnvMapProcessing.cpp"}

# XMVECTOR, XMLoadFloat3, XM_PI, XM_CALLCONV, ... and the header itself.
math_names = re.compile(r"\bXM(?:[A-Z]|_[A-Z])\w*|\bDirectXMath\w*\.h\b")
namespace = re.compile(r"\bDirectX\s*::|\busing\s+namespace\s+DirectX\b")

found = []
for top in tops:
    for folder, _, names in os.walk(os.path.join(repo, top)):
        for name in names:
            path = os.path.relpath(os.path.join(folder, name), repo).replace(os.sep, "/")
            if not name.endswith((".h", ".cpp")) or skip.search(path):
                continue
            for number, line in enumerate(open(os.path.join(repo, path), errors="replace"), 1):
                match = math_names.search(line)
                if not match and path not in directxtex:
                    match = namespace.search(line)
                if match:
                    found.append(f"{path}:{number}: {match.group(0)}")

for line in sorted(found):
    print(line)
if found:
    print(f"\n{len(found)} use(s) of DirectXMath outside Engine/Utilities/Math*.h. Use primal::math instead "
          "(add what is missing to Engine/Utilities/MathSimd.h).")
    sys.exit(1)
print("DirectXMath is only used by the math library.")
