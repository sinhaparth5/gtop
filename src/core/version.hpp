#pragma once
//
// The version string, defined once — by CMake, from project(VERSION ...).
//
// GTOP_VERSION is injected as a compile definition on the core target, so the
// number in CMakeLists.txt is the only place it exists. A fallback is provided
// for the case where a translation unit is compiled outside the build system,
// and it is deliberately not a plausible version number.
//
#include <string_view>

#ifndef GTOP_VERSION
#define GTOP_VERSION "0.0.0-unconfigured"
#endif

namespace gtop::core {

inline constexpr std::string_view kVersion = GTOP_VERSION;

}  // namespace gtop::core
