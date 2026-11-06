# Copyright (c) 2026 Charles Smith
# Distributed under the MIT license. See the LICENSE file in the project root for more information.
#
# CMake build for Linux (Engine + EngineTest). On Windows, keep using Primal.slnx / the .vcxproj files.

cmake_minimum_required(VERSION 3.25)
project(Primal LANGUAGES CXX)

if(WIN32)
    message(FATAL_ERROR "On Windows, build with Primal.slnx (Visual Studio / Rider). CMake is used for the Linux build.")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(PRIMAL_ENGINE_TEST "ENTITY_COMPONENTS" CACHE STRING "Which EngineTest test to build (ENTITY_COMPONENTS, WINDOW, RENDERER, SURFACES)")
set_property(CACHE PRIMAL_ENGINE_TEST PROPERTY STRINGS ENTITY_COMPONENTS WINDOW RENDERER SURFACES)
option(PRIMAL_ENABLE_SANITIZERS "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(PrimalDependencies)
include(PrimalBuildSettings)

add_subdirectory(Engine)
add_subdirectory(EngineTest)
