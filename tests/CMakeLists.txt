# Test suite.
#
# Catch2 arrives with -DGTOP_FETCH_DEPS=ON (ROADMAP.md task 1.1). Until then the
# suite is plain assert-based executables, so `ctest` is useful offline and on a
# CI runner with no network and no GPU.

add_executable(gtop_tokens_test unit/tokens_test.cpp)
target_link_libraries(gtop_tokens_test PRIVATE gtop_render)
add_test(NAME tokens COMMAND gtop_tokens_test)

if(TARGET Catch2::Catch2WithMain)
    message(STATUS "Catch2 available — Catch2-based suites may be added")
endif()
