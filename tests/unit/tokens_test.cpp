// Token layer invariants.
//
// The Braille dot table is checked first and hardest, because it is the one
// place where a wrong constant produces output that looks plausible instead of
// obviously broken. ROADMAP.md task 5.1 requires this test before the canvas.

#include <cassert>
#include <cmath>
#include <cstdio>

#include "render/tokens/tokens.hpp"

namespace tok = gtop::render::tokens;

// -- WCAG contrast -----------------------------------------------------------

namespace {

double channel(unsigned char v) {
    const double s = static_cast<double>(v) / 255.0;
    return s <= 0.03928 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
}

double luminance(tok::Rgb c) {
    return 0.2126 * channel(c.r) + 0.7152 * channel(c.g) + 0.0722 * channel(c.b);
}

double contrast(tok::Rgb a, tok::Rgb b) {
    const double la = luminance(a);
    const double lb = luminance(b);
    const double hi = la > lb ? la : lb;
    const double lo = la > lb ? lb : la;
    return (hi + 0.05) / (lo + 0.05);
}

// Text must clear WCAG AA. A terminal cell is small type; there is no
// large-text exemption available to us.
void require_text_contrast(const char* role, tok::Rgb fg, tok::Rgb bg) {
    const double ratio = contrast(fg, bg);
    std::printf("  %-14s %5.2f:1\n", role, ratio);
    assert(ratio >= 4.5 && "text role below WCAG AA against panel surface");
}

}  // namespace

// -- compile-time ------------------------------------------------------------

// Dot bits are not raster order: rows 0-2 use bits 0-2 and 3-5, row 3 jumps to 6-7.
static_assert(tok::glyphs::kDotBit[0][0] == 0x01);
static_assert(tok::glyphs::kDotBit[1][0] == 0x02);
static_assert(tok::glyphs::kDotBit[2][0] == 0x04);
static_assert(tok::glyphs::kDotBit[3][0] == 0x40, "row 3 is irregular");
static_assert(tok::glyphs::kDotBit[0][1] == 0x08);
static_assert(tok::glyphs::kDotBit[3][1] == 0x80, "row 3 is irregular");

static_assert(tok::glyphs::kCellCols == 2 && tok::glyphs::kCellRows == 4);
static_assert(tok::glyphs::kBrailleBase == 0x2800);
static_assert(tok::glyphs::kBlockRamp.size() == 9, "empty..full inclusive");

// Gradient stops must be normalised and ascending.
static_assert(tok::gradients::compute.front().pos == 0.0F);
static_assert(tok::gradients::compute.back().pos == 1.0F);
static_assert(tok::gradients::thermal[0].pos < tok::gradients::thermal[1].pos);
static_assert(tok::gradients::thermal[1].pos < tok::gradients::thermal[2].pos);

// Layout must fit the minimum terminal.
static_assert(tok::metrics::kLabelColumn + tok::metrics::kValueColumn +
                  tok::metrics::kStatsColumn + tok::metrics::kSparklineMinWidth <=
              tok::metrics::kMinTerminalWidth);

// Alarm text must be distinguishable from nominal without colour.
static_assert(tok::glyphs::kMarkerCritical != tok::glyphs::kMarkerNominal);

int main() {
    // Every dot set must yield the last code point in the Braille block.
    unsigned mask = 0;
    for (const auto& row : tok::glyphs::kDotBit) {
        for (const auto bit : row) {
            mask |= bit;
        }
    }
    assert(mask == 0xFF);
    assert((static_cast<unsigned>(tok::glyphs::kBrailleBase) | mask) == 0x28FFU);

    // No two dot bits may collide.
    unsigned seen = 0;
    for (const auto& row : tok::glyphs::kDotBit) {
        for (const auto bit : row) {
            assert((seen & bit) == 0 && "duplicate dot bit");
            seen |= bit;
        }
    }

    // Accessibility regression guard: changing a colour token must not silently
    // push readable text below AA.
    const auto& c = tok::nord_dark;
    const auto& t = tok::nord_dark_text;
    const auto  bg = c.panel_surface;

    std::puts("contrast against panel_surface:");
    require_text_contrast("panel_title", t.panel_title.fg, bg);
    require_text_contrast("metric_label", t.metric_label.fg, bg);
    require_text_contrast("metric_value", t.metric_value.fg, bg);
    require_text_contrast("metric_unit", t.metric_unit.fg, bg);
    require_text_contrast("table_header", t.table_header.fg, bg);
    require_text_contrast("table_cell", t.table_cell.fg, bg);
    require_text_contrast("footer_hint", t.footer_hint.fg, bg);
    require_text_contrast("alarm", t.alarm.fg, bg);

    // Non-text roles only need the 3:1 UI-component threshold.
    assert(contrast(c.critical, bg) >= 3.0 && "critical fill below UI threshold");
    assert(contrast(c.panel_border_focus, bg) >= 3.0 && "focus ring must be visible");

    // `unavailable` is deliberately exempt: a dimmed em-dash is the absence of a
    // value, not information. The value it stands in for is never encoded in it.

    std::puts("tokens: ok");
    return 0;
}
