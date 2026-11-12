#!/usr/bin/env bash
# Copyright (c) 2026 Charles Smith
# Distributed under the MIT license. See the LICENSE file in the project root for more information.
#
# The Linux CI jobs (.gitea/workflows/linux.yml). The workflow only calls this script, so every job can also be run
# by hand, on the host or in the CI image (README.md, next to this file):
#
#   Tools/CI/ci.sh checks              formatting (clang-format) and include cycles
#   Tools/CI/ci.sh build <preset>      all four EngineTest variants + header_check, then the tests
#   Tools/CI/ci.sh sanitizers          ASan + UBSan build, then the tests
#   Tools/CI/ci.sh graphics <config>   only the graphics tests of an existing build (Debug or Release)
#   Tools/CI/ci.sh tidy                clang-tidy over every Linux configuration, warnings are errors
#
# NOTE: the jobs configure with a different PRIMAL_ENGINE_TEST several times and write to build/ and x64/, like a
#       normal build does. After running one in your working tree, configure again with the test you want.
set -euo pipefail

cd "$(dirname "$0")/../.."

# ENTITY_COMPONENTS is last: it is the one test that runs without a window or a GPU, so its EngineTest is the one
# that is left in x64/<Config>/ to run.
readonly tests=(RENDERER SURFACES WINDOW ENTITY_COMPONENTS)

section() {
    printf '\n=== %s ===\n' "$*"
}

# The graphics tests (GraphicsTests/, doctest) open windows and render with Vulkan. With xvfb-run installed (the CI
# image) they run on a virtual X display, where the image's only Vulkan device is lavapipe (software). Without it
# they run on the session's display and GPU, and show their windows for a few seconds.
# PRIMAL_TEST_REQUIRE_VALIDATION: a debug build without an active validation layer fails instead of passing
# unchecked.
# graphics_test_env: extra environment for the test program alone (job_sanitizers), not for xvfb-run and the X
# server it starts.
graphics_test_env=()
run_graphics_tests() {
    local config=$1
    local -a runner=()
    if command -v xvfb-run > /dev/null; then
        runner=(xvfb-run -a)
    elif [[ -z ${WAYLAND_DISPLAY:-}${DISPLAY:-} ]]; then
        echo "The graphics tests need a display: install xvfb-run (xorg-server-xvfb) or run in a desktop session." >&2
        return 1
    fi
    section "Run the graphics tests (${config}${runner:+, ${runner[*]}})"
    "${runner[@]}" env PRIMAL_TEST_REQUIRE_VALIDATION=1 "${graphics_test_env[@]}" "x64/${config}/GraphicsTests"
}

# The automated tests (UnitTests/, doctest), then EngineTest's entity-components test (it repeats until it reads 'q'
# from stdin: with stdin closed it would never stop), then the graphics tests.
run_tests() {
    local config=$1
    section "Run the unit tests (${config})"
    "x64/${config}/UnitTests"

    section "Run the entity-components test (${config})"
    echo q | "x64/${config}/EngineTest"

    run_graphics_tests "${config}"
}

job_checks() {
    section "clang-format ($(clang-format --version))"
    # Third-party folders are tracked too; their own .clang-format files turn formatting off.
    git ls-files -z -- '*.h' '*.cpp' | xargs -0 clang-format --dry-run --Werror
    echo "Formatting is clean."

    section "Include cycles"
    python3 Tools/check_include_cycles.py .

    section "DirectXMath only behind primal::math"
    python3 Tools/check_math_usage.py .
}

job_build() {
    local preset=${1:?usage: ci.sh build <preset>}
    local config=Debug
    if [[ ${preset} == *release ]]; then
        config=Release
    fi

    # Only EngineTest depends on PRIMAL_ENGINE_TEST, so the builds after the first one are short.
    local test
    for test in "${tests[@]}"; do
        section "Build ${preset}, ${test}"
        cmake --preset "${preset}" -DPRIMAL_ENGINE_TEST="${test}"
        cmake --build --preset "${preset}"
    done

    section "Build ${preset}, header_check"
    cmake --build --preset "${preset}" --target header_check

    run_tests "${config}"
}

job_sanitizers() {
    local dir=build/linux-debug-asan

    section "Build Debug with ASan + UBSan"
    cmake --preset linux-debug -B "${dir}" -DPRIMAL_ENABLE_SANITIZERS=ON -DPRIMAL_ENGINE_TEST=ENTITY_COMPONENTS
    cmake --build "${dir}"

    # Leak checking of the graphics tests: keep the Vulkan driver and layers loaded so that LeakSanitizer can name
    # what they leave allocated (noclose.c), and suppress the third-party leaks listed in lsan.supp.
    cc -shared -fPIC Tools/CI/noclose.c -o "${dir}/libnoclose.so"
    graphics_test_env=(
        "LD_PRELOAD=${PWD}/${dir}/libnoclose.so"
        "LSAN_OPTIONS=suppressions=${PWD}/Tools/CI/lsan.supp:print_suppressions=0"
    )

    # UBSan only prints by default; make it fail the job.
    UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 run_tests Debug
}

# clang-tidy needs the compile database only (configure, no build).
tidy() {
    local dir=$1 test=$2 build_type=$3 files=$4
    section "clang-tidy: ${build_type}, ${test}"
    cmake --preset linux-debug -B "${dir}" -DCMAKE_BUILD_TYPE="${build_type}" -DPRIMAL_ENGINE_TEST="${test}" > /dev/null
    run-clang-tidy -quiet -p "${dir}" -warnings-as-errors='*' "${files}"
}

job_tidy() {
    local dir=build/ci-tidy
    clang-tidy --version

    # The first pass checks every file. Only EngineTest's sources and defines depend on the test, so the other
    # tests check EngineTest/ alone.
    tidy "${dir}" RENDERER Debug '.*'
    local test
    for test in SURFACES WINDOW ENTITY_COMPONENTS; do
        tidy "${dir}" "${test}" Debug '/EngineTest/'
    done
    # Release: other #if branches (asserts and DEBUG_OP are off, typed ids are aliases).
    tidy "${dir}" RENDERER Release '.*'
}

case ${1:-} in
    checks) job_checks ;;
    build) job_build "${2:-}" ;;
    sanitizers) job_sanitizers ;;
    graphics) run_graphics_tests "${2:-Debug}" ;;
    tidy) job_tidy ;;
    *)
        echo "usage: $0 checks | build <preset> | sanitizers | graphics [Debug|Release] | tidy" >&2
        exit 2
        ;;
esac
