# Copyright (c) 2026 Charles Smith
# Distributed under the MIT license. See the LICENSE file in the project root for more information.
#
# header_check: compiles every header in a translation unit of its own, so each header has to include what it uses
# Not part of the default build:
#   cmake --build --preset linux-debug --target header_check
# The Windows-only headers are checked with clang-cl through Tools/WindowsCompileDb (database "headers").

file(GLOB_RECURSE _primal_headers RELATIVE ${PROJECT_SOURCE_DIR} CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/Engine/*.h
    ${PROJECT_SOURCE_DIR}/EngineDLL/*.h
    ${PROJECT_SOURCE_DIR}/EngineTest/*.h
)
# Third-party code, Windows-only headers, and IncludeWindowCpp.h (it includes a .cpp into the platform TU).
list(FILTER _primal_headers EXCLUDE REGEX
    "ThirdParty/|Direct3D12/|InputWin32\\.h$|IncludeWindowCpp\\.h$")

set(_primal_header_tus)
foreach(_header IN LISTS _primal_headers)
    string(REGEX REPLACE "[/.]" "_" _name ${_header})
    set(_tu ${CMAKE_CURRENT_BINARY_DIR}/header_check/${_name}.cpp)
    file(CONFIGURE OUTPUT ${_tu} CONTENT "#include \"${PROJECT_SOURCE_DIR}/${_header}\"\n")
    list(APPEND _primal_header_tus ${_tu})
endforeach()

add_library(header_check OBJECT EXCLUDE_FROM_ALL ${_primal_header_tus})
# Not in compile_commands.json: clang-tidy already sees every header through the real sources, and the generated
# files are outside the folders whose .clang-tidy files apply (EngineTest/.clang-tidy).
set_target_properties(header_check PROPERTIES EXPORT_COMPILE_COMMANDS OFF)
target_link_libraries(header_check PRIVATE Engine ShaderCompiler)
target_include_directories(header_check SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/Engine/ThirdParty/vma)
# The test headers are written for one test at a time; any one of them will do here.
target_compile_definitions(header_check PRIVATE TEST_ENTITY_COMPONENTS=0 TEST_WINDOW=0 TEST_RENDERER=0 TEST_SURFACES=0)
