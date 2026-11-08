# Copyright (c) 2026 Charles Smith
# Distributed under the MIT license. See the LICENSE file in the project root for more information.

set(_primal_tests ENTITY_COMPONENTS WINDOW RENDERER SURFACES)
if(NOT PRIMAL_ENGINE_TEST IN_LIST _primal_tests)
    message(FATAL_ERROR "PRIMAL_ENGINE_TEST must be one of: ${_primal_tests} (got '${PRIMAL_ENGINE_TEST}')")
endif()

add_executable(EngineTest Main.cpp)

# Lights.cpp and Scripts.cpp are only used by the renderer test.
if(PRIMAL_ENGINE_TEST STREQUAL "RENDERER")
    target_sources(EngineTest PRIVATE
        TestRenderer.cpp
        RenderItem.cpp
        Lights.cpp
        Scripts.cpp
    )
endif()

foreach(_test IN LISTS _primal_tests)
    if(_test STREQUAL PRIMAL_ENGINE_TEST)
        target_compile_definitions(EngineTest PRIVATE TEST_${_test}=1)
    else()
        target_compile_definitions(EngineTest PRIVATE TEST_${_test}=0)
    endif()
endforeach()

target_link_libraries(EngineTest PRIVATE Engine)
# The surfaces and renderer tests compile the engine shaders. (EngineTest.vcxproj compiles
# ../EngineDLL/ShaderCompilation.cpp into the test instead.)
if(PRIMAL_ENGINE_TEST STREQUAL "SURFACES" OR PRIMAL_ENGINE_TEST STREQUAL "RENDERER")
    target_link_libraries(EngineTest PRIVATE ShaderCompiler)
endif()

# Same layout as the Windows build (x64/<Config>/) so the relative paths used by the tests
# (../../x64/<assets>, ../../Engine/Graphics/...) resolve the same way.
set_target_properties(EngineTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/x64/$<CONFIG>"
)
