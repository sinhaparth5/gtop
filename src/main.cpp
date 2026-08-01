// gtop — entry point.
//
// Phase 1 of ROADMAP.md: the platform layer is real, nothing above it is. What
// this prints is the host identity and the terminal capabilities gtop will
// render with — the same facts that decide whether the Braille canvas and
// TrueColor gradients are usable on this machine.
//
// Phase 2 replaces this with argument parsing, the driver registry, and
// --dump-json.

#include <cstdio>

#include "platform/platform.hpp"
#include "render/tokens/tokens.hpp"

namespace {

const char* yes_no(bool value) { return value ? "yes" : "no"; }

}  // namespace

int main() {
    constexpr auto& theme = gtop::render::kDefaultTheme;
    static_assert(theme.color.critical.r == 0xBF, "token layer reachable from app");

    const gtop::platform::SystemInfo host = gtop::platform::query_system_info();
    const gtop::platform::TerminalSession terminal =
        gtop::platform::TerminalSession::initialise();
    const gtop::platform::TerminalCapabilities& caps = terminal.capabilities();

    std::puts("gtop 0.0.1 — GPU TUI monitor");
    std::puts("");
    std::printf("  host        %s (%s %s)\n",
                host.hostname.empty() ? "unnamed" : host.hostname.c_str(),
                host.os_name.c_str(), host.os_version.c_str());
    std::printf("  terminal    tty %s, truecolor %s, utf-8 %s\n",
                yes_no(caps.is_tty), yes_no(caps.truecolor), yes_no(caps.utf8));
    if (caps.color_disabled) {
        std::puts("              NO_COLOR is set — colour output will be suppressed");
    }
    std::printf("  terminate   %s\n",
                gtop::platform::supports_graceful_terminate()
                    ? "graceful (SIGTERM) and forceful"
                    : "forceful only — this OS has no graceful equivalent");
    std::puts("");
    std::puts("No telemetry backends are implemented yet.");
    std::puts("See ROADMAP.md for the build order; docs/ARCHITECTURE.md for layout.");
    return 0;
}
