#!/usr/bin/env python3
# Copyright (c) 2026 Charles Smith
# Distributed under the MIT license. See the LICENSE file in the project root for more information.
# Reports include cycles between the project's own headers. Every #include "..." counts, whatever #if it is
# in. Exit code 1 if there is a cycle. Usage: check_include_cycles.py [repo]
import os, re, sys

repo = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else ".")
tops = ["Engine", "EngineDLL", "EngineTest", "ContentTools", "ShaderCompilerTool"]
skip = re.compile(r"ThirdParty/|MikkTSpace/")
# The include directories of the builds (Engine/CMakeLists.txt, the .vcxproj files), after the including file's own.
search = ["Engine", "Engine/Common", "EngineDLL", "ContentTools"]
include = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.M)

files = [os.path.relpath(os.path.join(folder, n), repo)
         for top in tops for folder, _, names in os.walk(os.path.join(repo, top))
         for n in names if n.endswith((".h", ".cpp", ".hlsli"))]
files = {f for f in files if not skip.search(f)}

def resolve(src, inc):
    for base in [os.path.dirname(src)] + search:
        path = os.path.normpath(os.path.join(base, inc))
        if path in files:
            return path
    return None

graph = {f: sorted({r for i in include.findall(open(os.path.join(repo, f), errors="replace").read())
                    if (r := resolve(f, i))}) for f in files}

cycles, state, stack = [], {}, []
def visit(f):
    state[f] = 1
    stack.append(f)
    for dep in graph[f]:
        if state.get(dep) == 1:
            cycles.append(stack[stack.index(dep):] + [dep])
        elif dep not in state:
            visit(dep)
    stack.pop()
    state[f] = 2

for f in sorted(files):
    if f not in state:
        visit(f)
for c in cycles:
    print("include cycle: " + " -> ".join(c))
print(f"{len(files)} files, {len(cycles)} include cycle(s)")
sys.exit(1 if cycles else 0)
