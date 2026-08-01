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

# -- layer 0: platform — OS abstraction, the only place with #ifdef ------------
add_library(gtop_platform INTERFACE)
target_include_directories(gtop_platform INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(gtop_platform INTERFACE gtop_warnings)

# -- layer 1: core — domain types and state, no I/O ----------------------------
add_library(gtop_core INTERFACE)
target_include_directories(gtop_core INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(gtop_core INTERFACE gtop_warnings)

# -- layer 2: driver — vendor telemetry, runtime-loaded ------------------------
add_library(gtop_driver INTERFACE)
target_include_directories(gtop_driver INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(gtop_driver INTERFACE
    gtop_warnings gtop_core gtop_platform gtop_vendor_headers)

# -- layer 3: render — design tokens and drawing primitives --------------------
add_library(gtop_render INTERFACE)
target_include_directories(gtop_render INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(gtop_render INTERFACE gtop_warnings gtop_core)

# -- layer 4: ui — FTXUI components and panels ---------------------------------
add_library(gtop_ui INTERFACE)
target_include_directories(gtop_ui INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(gtop_ui INTERFACE
    gtop_warnings gtop_render gtop_core gtop_platform)

# -- application ---------------------------------------------------------------
add_executable(gtop main.cpp)
target_link_libraries(gtop PRIVATE gtop_ui gtop_driver)
