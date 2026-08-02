# Cross-compile the Windows build from Linux with mingw-w64.
#
# This is not a substitute for MSVC — it will not catch MSVC-specific
# conformance issues, and it links against mingw's CRT rather than the MSVC
# one. What it does catch is the whole class of error that "written but never
# compiled" leaves behind in src/platform/win32/: wrong Win32 signatures,
# missing includes, bad casts, and warning-flag violations. Until a Windows
# machine exists (ROADMAP open question 2), this is the only thing standing
# between the Win32 sources and rot.
#
#     cmake --preset windows-mingw && cmake --build --preset windows-mingw
#
# Point GTOP_MINGW_ROOT at a prefix if the toolchain is not on PATH; the
# packages extract to <root>/usr/bin.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(GTOP_MINGW_TRIPLE "x86_64-w64-mingw32"
    CACHE STRING "Target triple of the mingw-w64 cross toolchain")
set(GTOP_MINGW_ROOT "" CACHE PATH "Prefix containing usr/bin/<triple>-g++")

if(GTOP_MINGW_ROOT)
    list(APPEND CMAKE_PROGRAM_PATH "${GTOP_MINGW_ROOT}/usr/bin" "${GTOP_MINGW_ROOT}/bin")
endif()

# CMake re-includes this file inside the compiler-check try_compile, and that
# sub-project inherits only a whitelist of variables — not our cache entries.
# Without this, find_program below succeeds for the main project and then fails
# inside the ABI check, which reports as "not a full path to an existing
# compiler tool" and points nowhere useful. Only bites when the toolchain is
# reached through GTOP_MINGW_ROOT rather than PATH.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES GTOP_MINGW_ROOT GTOP_MINGW_TRIPLE)

# The Debian/Ubuntu packages ship -posix and -win32 threading variants and leave
# the unsuffixed name to update-alternatives, which is absent from an extracted
# prefix. -posix is the one that supports std::thread, which Phase 7 needs.
find_program(CMAKE_CXX_COMPILER
    NAMES "${GTOP_MINGW_TRIPLE}-g++" "${GTOP_MINGW_TRIPLE}-g++-posix" REQUIRED)
find_program(CMAKE_C_COMPILER
    NAMES "${GTOP_MINGW_TRIPLE}-gcc" "${GTOP_MINGW_TRIPLE}-gcc-posix" REQUIRED)
find_program(CMAKE_RC_COMPILER NAMES "${GTOP_MINGW_TRIPLE}-windres")

get_filename_component(_gtop_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
get_filename_component(_gtop_prefix "${_gtop_bin}" DIRECTORY)
set(CMAKE_FIND_ROOT_PATH "${_gtop_prefix}/${GTOP_MINGW_TRIPLE}")

# Look for headers and libraries in the target sysroot only; find_program still
# searches the host, because the tools that get run during a build are host
# binaries.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
