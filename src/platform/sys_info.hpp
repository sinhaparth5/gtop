#pragma once
//
// Host identity — what the header bar prints.
//
// Queried once at startup. Every field is a best effort: a container with no
// hostname set, or a Windows build where the version API is blocked, yields an
// empty string rather than a fabricated one. The UI renders an empty field as
// absent, the same as any other unavailable value.
//
#include <string>

namespace gtop::platform {

struct SystemInfo {
    std::string hostname;    // "" when the OS declines to say
    std::string os_name;     // "Linux", "Windows"
    std::string os_version;  // kernel release, or Windows build number
};

[[nodiscard]] SystemInfo query_system_info();

}  // namespace gtop::platform
