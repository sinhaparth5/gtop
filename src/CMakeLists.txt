# Layer targets.
#
# Each architectural layer is its own CMake target, and target_link_libraries
# spells out the permitted dependency direction:
#
#     ui  ──▶ render ──▶ core
#      │                   ▲
#      ├──▶ driver ────────┤
#      └──▶ platform ◀─────┘
#
# Nothing may point upward. core and platform depend on nothing but the standard
# library and the OS. Adding a link edge that reverses this arrow is the review
# signal that a layer boundary is being crossed.
#
# Layers are INTERFACE targets while they remain header-only; each converts to
# STATIC when its first .cpp lands (see the note in each layer's README).
# platform is STATIC as of Phase 1, core and driver as of Phase 2; render and ui
# are still header-only.

# -- layer 0: platform — OS abstraction, the only place with #ifdef ------------
#
# The OS split is made here, by choosing a source directory, rather than in the
# sources by preprocessor. A translation unit therefore never contains both
# implementations, and the conditional-compilation grep in CLAUDE.md stays
# satisfiable by construction rather than by discipline.
if(WIN32)
    set(gtop_platform_impl win32)
else()
    set(gtop_platform_impl posix)
endif()

add_library(gtop_platform STATIC
    platform/dynamic_library.cpp
    platform/terminal_setup.cpp
    platform/${gtop_platform_impl}/dynamic_library.cpp
    platform/${gtop_platform_impl}/process_control.cpp
    platform/${gtop_platform_impl}/sys_info.cpp
    platform/${gtop_platform_impl}/terminal_setup.cpp)

if(WIN32)
    target_sources(gtop_platform PRIVATE platform/win32/wide_string.cpp)
endif()

target_include_directories(gtop_platform PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(gtop_platform PUBLIC cxx_std_20)
target_link_libraries(gtop_platform PRIVATE gtop_warnings)

# libdl on platforms that still separate it; empty on glibc ≥ 2.34 and on
# Windows. Public because the symbols are reachable through this target.
target_link_libraries(gtop_platform PUBLIC ${CMAKE_DL_LIBS})

# -- layer 1: core — domain types and state, no I/O ----------------------------
add_library(gtop_core STATIC
    core/config.cpp
    core/json_export.cpp
    core/types.cpp)

target_include_directories(gtop_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(gtop_core PUBLIC cxx_std_20)
target_link_libraries(gtop_core PRIVATE gtop_warnings)

# The version lives in project(VERSION ...) and nowhere else. PUBLIC because
# core/version.hpp is a header: anything including it needs the same value, and
# a target that disagreed would report a different version than it is.
target_compile_definitions(gtop_core PUBLIC GTOP_VERSION="${PROJECT_VERSION}")

# -- layer 2: driver — vendor telemetry, runtime-loaded ------------------------
add_library(gtop_driver STATIC driver/driver_registry.cpp)

target_include_directories(gtop_driver PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(gtop_driver PUBLIC cxx_std_20)
target_link_libraries(gtop_driver PRIVATE gtop_warnings)
target_link_libraries(gtop_driver PUBLIC gtop_core gtop_platform gtop_vendor_headers)

# -- layer 3: render — design tokens and drawing primitives --------------------
add_library(gtop_render INTERFACE)
target_include_directories(gtop_render INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(gtop_render INTERFACE cxx_std_20)
target_link_libraries(gtop_render INTERFACE gtop_warnings gtop_core)

# -- layer 4: ui — FTXUI components and panels ---------------------------------
add_library(gtop_ui INTERFACE)
target_include_directories(gtop_ui INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(gtop_ui INTERFACE cxx_std_20)
target_link_libraries(gtop_ui INTERFACE
    gtop_warnings gtop_render gtop_core gtop_platform)

# FTXUI is the one third-party runtime dependency, and it arrives only with
# -DGTOP_FETCH_DEPS=ON. Until Phase 6 puts components in this layer, the edge is
# declared here so turning deps on is the only step required.
if(TARGET ftxui::component)
    target_link_libraries(gtop_ui INTERFACE ftxui::component ftxui::dom ftxui::screen)
endif()

# -- application ---------------------------------------------------------------
add_executable(gtop main.cpp)
target_link_libraries(gtop PRIVATE gtop_ui gtop_driver)
