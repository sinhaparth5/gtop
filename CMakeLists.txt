cmake_minimum_required(VERSION 3.24)

project(gtop
    VERSION 0.0.1
    DESCRIPTION "A btop-style terminal monitor for GPUs"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

option(GTOP_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(GTOP_BUILD_TESTS        "Build the unit test suite"          ON)
# Off by default so a fresh configure works offline. Phase 1 turns this on when
# FTXUI and Catch2 are actually needed.
option(GTOP_FETCH_DEPS         "Fetch third-party dependencies"     OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerMatrix)
include(CompilerWarnings)
include(Dependencies)

add_subdirectory(src)

if(GTOP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

message(STATUS "gtop ${PROJECT_VERSION} | ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} | tests=${GTOP_BUILD_TESTS} deps=${GTOP_FETCH_DEPS}")
