# Copyright (c) 2026 Charles Smith
# Distributed under the MIT license. See the LICENSE file in the project root for more information.
#
# Compiler settings shared by all Primal targets. Mirrors the intent of the .vcxproj settings:
#   - /arch:AVX2           -> -mavx2 -mfma -mf16c (+ SSE4.2 for _mm_crc32_u64)
#   - /fp:fast             -> -ffp-contract=fast -fno-math-errno (NOT -ffast-math, which breaks NaN/Inf handling)
#   - _DEBUG / NDEBUG      -> per configuration

add_library(primal_build_settings INTERFACE)
add_library(primal::build_settings ALIAS primal_build_settings)

target_compile_features(primal_build_settings INTERFACE cxx_std_23)

target_compile_options(primal_build_settings INTERFACE
    -msse4.2 -mavx2 -mfma -mf16c
    -ffp-contract=fast -fno-math-errno
    -Wall
    -Wno-unknown-pragmas  # MSVC-only pragmas (#pragma warning, #pragma comment, ...)
)

target_compile_definitions(primal_build_settings INTERFACE
    $<$<CONFIG:Debug>:_DEBUG>
)

if(PRIMAL_ENABLE_SANITIZERS)
    target_compile_options(primal_build_settings INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(primal_build_settings INTERFACE -fsanitize=address,undefined)
endif()
