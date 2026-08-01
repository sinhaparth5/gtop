# Supported compiler floor.
#
# These are not arbitrary. Each is the first release that ships the C++20
# library pieces gtop's design depends on — <span>, <bit>, designated
# initialisers in practice, and (from Phase 7) std::atomic<std::shared_ptr<T>>,
# which is the last of them to land and the reason the floor is this high.
#
# Failing at configure time is deliberate: a partial C++20 implementation
# produces template errors hundreds of lines deep, and nobody reading those
# guesses that the answer is "your compiler is too old".

set(GTOP_MIN_GNU     12)      # GCC 12
set(GTOP_MIN_Clang   15)      # Clang 15
set(GTOP_MIN_AppleClang 14)
set(GTOP_MIN_MSVC    19.36)   # Visual Studio 2022 17.6

set(_gtop_floor "${GTOP_MIN_${CMAKE_CXX_COMPILER_ID}}")

if(_gtop_floor AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS _gtop_floor)
    message(FATAL_ERROR
        "gtop requires ${CMAKE_CXX_COMPILER_ID} >= ${_gtop_floor}, "
        "found ${CMAKE_CXX_COMPILER_VERSION}.")
elseif(NOT _gtop_floor)
    # clang-cl reports as Clang and is covered above. Anything else is untested
    # rather than known-broken, so this warns instead of failing.
    message(WARNING
        "Untested compiler '${CMAKE_CXX_COMPILER_ID}'. "
        "gtop is verified on GCC, Clang, clang-cl and MSVC.")
endif()

unset(_gtop_floor)
