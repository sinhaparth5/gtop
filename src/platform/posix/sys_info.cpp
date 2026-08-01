// SystemInfo — POSIX (gethostname / uname).

#include "platform/sys_info.hpp"

#include <sys/utsname.h>
#include <unistd.h>

#include <array>
#include <cstddef>

namespace gtop::platform {

SystemInfo query_system_info() {
    SystemInfo info;

    // POSIX does not promise termination when the name does not fit, so the
    // buffer is oversized and the last byte is forced null.
    std::array<char, 256> host{};
    if (::gethostname(host.data(), host.size() - 1) == 0) {
        host[host.size() - 1] = '\0';
        info.hostname.assign(host.data());
    }

    ::utsname uts{};
    if (::uname(&uts) == 0) {
        info.os_name.assign(static_cast<const char*>(uts.sysname));
        info.os_version.assign(static_cast<const char*>(uts.release));
    }

    return info;
}

}  // namespace gtop::platform
