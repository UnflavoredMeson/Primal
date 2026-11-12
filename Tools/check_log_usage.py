#!/usr/bin/env python3
# Copyright (c) 2026 Charles Smith
# Distributed under the MIT license. See the LICENSE file in the project root for more information.
# Reports output that bypasses the log (issue #19): OutputDebugString, printf/fprintf/puts/fputs and std::cout/cerr.
# Everything goes through primal::log (Engine/Core/Log.h), so that it reaches the terminal, the debugger, the log
# file and the editor. Only Engine/Core/Log.cpp writes to stderr itself.
# Exit code 1 if something is found. Usage: check_log_usage.py [repo]
import os, re, sys

repo = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else ".")
tops = ["Engine", "EngineDLL", "EngineTest", "ContentTools", "ShaderCompilerTool", "GraphicsTests"]
skip = re.compile(r"ThirdParty/|MikkTSpace/|^Engine/Core/Log\.cpp$")
direct = re.compile(r"\bOutputDebugString[AW]?\b|\b(?:std::)?(?:v?f?printf|f?puts)\s*\(|\bstd::(?:cout|cerr|clog)\b")
comment = re.compile(r"//.*$")

found = []
for top in tops:
    for folder, _, names in os.walk(os.path.join(repo, top)):
        for name in names:
            path = os.path.relpath(os.path.join(folder, name), repo).replace(os.sep, "/")
            if not name.endswith((".h", ".cpp")) or skip.search(path):
                continue
            for number, line in enumerate(open(os.path.join(repo, path), errors="replace"), 1):
                if match := direct.search(comment.sub("", line)):
                    found.append(f"{path}:{number}: {match.group(0)}")

for line in sorted(found):
    print(line)
if found:
    print(f"\n{len(found)} direct output call(s). Use primal::log instead (Engine/Core/Log.h).")
    sys.exit(1)
print("All output goes through primal::log.")
