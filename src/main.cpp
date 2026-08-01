// gtop — entry point.
//
// Currently a stub. It exists so the layer wiring is real and verifiable from
// day one rather than aspirational: this file sits in the application layer and
// reaches the render layer's tokens through the declared CMake dependency.
//
// Phase 1 replaces this with argument parsing and the driver registry.

#include <cstdio>

#include "render/tokens/tokens.hpp"

int main() {
    constexpr auto& theme = gtop::render::kDefaultTheme;
    static_assert(theme.color.critical.r == 0xBF, "token layer reachable from app");

    std::puts("gtop 0.0.1 — GPU TUI monitor");
    std::puts("");
    std::puts("No telemetry backends are implemented yet.");
    std::puts("See ROADMAP.md for the build order; docs/ARCHITECTURE.md for layout.");
    return 0;
}
