# Warning configuration, normalised across GCC/Clang and MSVC.
#
# Applied via the gtop_warnings INTERFACE target rather than global flags, so
# third-party sources pulled in by FetchContent are not held to our settings.

add_library(gtop_warnings INTERFACE)

if(MSVC)
    target_compile_options(gtop_warnings INTERFACE
        /W4
        /permissive-       # standards conformance
        /utf-8             # required: source contains Braille and box-drawing literals
        $<$<BOOL:${GTOP_WARNINGS_AS_ERRORS}>:/WX>
    )
else()
    target_compile_options(gtop_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wsign-conversion
        -Wnon-virtual-dtor      # IGpuDriver is a polymorphic base
        -Wold-style-cast
        -Woverloaded-virtual
        $<$<BOOL:${GTOP_WARNINGS_AS_ERRORS}>:-Werror>
    )
endif()
