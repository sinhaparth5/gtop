// FTXUI availability check — ROADMAP.md Phase 1 exit criterion.
//
// Renders one frame to an off-screen buffer rather than to a terminal, so this
// runs on a CI machine with no TTY. It proves three things before Phase 6
// starts building panels on top: FTXUI compiles under this toolchain, it links,
// and its screen buffer handles the multi-byte glyphs gtop's canvas is made of.
//
// Built only when -DGTOP_FETCH_DEPS=ON. Deliberately not linked into the gtop
// binary — a fresh clone still configures and builds offline.

#include <cassert>
#include <cstdio>
#include <string>

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

int main() {
    using namespace ftxui;

    // A Braille glyph and a rounded border character: the two things that
    // silently degrade to "?" when a toolchain or console mishandles UTF-8.
    auto document = vbox({
                        text("gtop") | bold,
                        separator(),
                        text("⣿⣿⣇⣀⡀"),
                    }) |
                    border;

    Screen screen = Screen::Create(Dimension::Fixed(24), Dimension::Fixed(6));
    Render(screen, document);

    const std::string frame = screen.ToString();
    assert(!frame.empty());
    assert(frame.find("gtop") != std::string::npos);
    assert(frame.find("⣿") != std::string::npos && "UTF-8 survived the render path");

    std::puts("ftxui: renders, links, and keeps multi-byte glyphs intact");
    return 0;
}
