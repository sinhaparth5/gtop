# Third-party dependencies.
#
# Deliberately empty by default: a fresh clone must configure offline and with
# no GPU SDK installed. Enable with -DGTOP_FETCH_DEPS=ON.
#
# NOTE ON VENDOR LIBRARIES — do not add NVML, ADLX, ROCm SMI or Level Zero here.
# They are resolved at runtime through platform/DynamicLibrary. Only their
# headers are vendored, under third_party/vendor_headers/, for type and enum
# definitions. Linking any of them would break the "starts on a machine with no
# driver" guarantee. See ROADMAP.md §0.

include(FetchContent)

if(GTOP_FETCH_DEPS)
    # Pinned to tags, never a moving branch.
    FetchContent_Declare(ftxui
        GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
        GIT_TAG        v5.0.0
        GIT_SHALLOW    TRUE)

    FetchContent_Declare(catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.5.2
        GIT_SHALLOW    TRUE)

    FetchContent_MakeAvailable(ftxui catch2)
endif()

# Header-only interface for the vendored vendor headers.
add_library(gtop_vendor_headers INTERFACE)
target_include_directories(gtop_vendor_headers
    INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vendor_headers")
