# Test suite.
#
# Catch2 arrives with -DGTOP_FETCH_DEPS=ON (ROADMAP.md task 1.1). Until then the
# suite is plain assert-based executables, so `ctest` is useful offline and on a
# CI runner with no network and no GPU.

# Every suite here checks with assert(), and RelWithDebInfo defines NDEBUG,
# which would compile all of them away into a test that passes by doing
# nothing. Undefined per target rather than globally so the code under test is
# still built exactly as it ships.
function(gtop_add_test name source)
    add_executable(${name} ${source})
    if(MSVC)
        target_compile_options(${name} PRIVATE /UNDEBUG)
    else()
        target_compile_options(${name} PRIVATE -UNDEBUG)
    endif()
endfunction()

gtop_add_test(gtop_tokens_test unit/tokens_test.cpp)
target_link_libraries(gtop_tokens_test PRIVATE gtop_render)
add_test(NAME tokens COMMAND gtop_tokens_test)

gtop_add_test(gtop_core_test unit/core_test.cpp)
target_link_libraries(gtop_core_test PRIVATE gtop_core gtop_warnings)
add_test(NAME core COMMAND gtop_core_test)

# The mock backend lives in tests/mock/ rather than src/, so it cannot end up
# linked into the shipping binary. This include path is what makes it reachable
# as "mock/mock_driver.hpp".
gtop_add_test(gtop_driver_test unit/driver_test.cpp)
target_link_libraries(gtop_driver_test PRIVATE gtop_driver gtop_warnings)
target_include_directories(gtop_driver_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME driver COMMAND gtop_driver_test)

gtop_add_test(gtop_platform_test unit/platform_test.cpp)
target_link_libraries(gtop_platform_test PRIVATE gtop_platform gtop_warnings)
add_test(NAME platform COMMAND gtop_platform_test)

# A library and an exported symbol that are guaranteed present on the host, so
# the loader's success path is exercised without depending on a GPU driver
# being installed. Chosen here rather than by #ifdef in the test, matching how
# src/platform/ selects its implementation.
if(WIN32)
    set(gtop_test_library "kernel32.dll")
    set(gtop_test_symbol "GetTickCount")
else()
    set(gtop_test_library "libm.so.6")
    set(gtop_test_symbol "cos")
endif()

target_compile_definitions(gtop_platform_test PRIVATE
    GTOP_TEST_LIBRARY="${gtop_test_library}"
    GTOP_TEST_SYMBOL="${gtop_test_symbol}")

# FTXUI smoke test — ROADMAP.md Phase 1 exit criterion. Builds and runs a
# minimal FTXUI render offline, without a terminal, so a CI runner can prove
# the dependency compiles and links before Phase 6 depends on it.
if(TARGET ftxui::dom)
    gtop_add_test(gtop_ftxui_smoke smoke/ftxui_smoke.cpp)
    target_link_libraries(gtop_ftxui_smoke PRIVATE ftxui::dom ftxui::screen)
    add_test(NAME ftxui_smoke COMMAND gtop_ftxui_smoke)
endif()
