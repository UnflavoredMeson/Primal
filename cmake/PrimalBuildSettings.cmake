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
    # Warnings: -Wmissing-field-initializers is off: partial aggregate initializers that zero the
    # remaining members ({ a, b }) are intended throughout the engine (Vulkan/D3D12 structs, init infos).
    -Wall -Wextra -Wpedantic
    -Wconversion-null -Wpointer-arith -Wvla -Wwrite-strings -Wunused-result -Wvarargs -Woverlength-strings
    -Wunused-local-typedefs
    -Wno-missing-field-initializers
    -Wno-unknown-pragmas  # MSVC-only pragmas (#pragma warning, #pragma comment, ...)
)

# Warnings are errors. Turn off to build with a newer compiler that adds warnings.
option(PRIMAL_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
if(PRIMAL_WARNINGS_AS_ERRORS)
    target_compile_options(primal_build_settings INTERFACE -Werror)
endif()

target_compile_definitions(primal_build_settings INTERFACE
    $<$<CONFIG:Debug>:_DEBUG>
)

if(PRIMAL_ENABLE_SANITIZERS AND PRIMAL_ENABLE_TSAN)
    message(FATAL_ERROR "PRIMAL_ENABLE_SANITIZERS (ASan) and PRIMAL_ENABLE_TSAN can't be used in the same build.")
endif()
if(PRIMAL_ENABLE_SANITIZERS)
    target_compile_options(primal_build_settings INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(primal_build_settings INTERFACE -fsanitize=address,undefined)
endif()
if(PRIMAL_ENABLE_TSAN)
    target_compile_options(primal_build_settings INTERFACE -fsanitize=thread -fno-omit-frame-pointer)
    target_link_options(primal_build_settings INTERFACE -fsanitize=thread)
endif()
