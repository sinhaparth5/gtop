#pragma once
//
// Token layer — semantic roles.
//
// Maps the raw palette onto *meaning*. UI code names a role, never a colour:
//
//     border(semantic::panel_border)   // yes
//     border(palette::nord1)           // no — invisible to theming
//     border(hex(...))                 // no — consteval, rejected outside palette
//
// A second theme (high-contrast, light, colour-blind-safe) is then a matter of
// supplying a different Roles instance, not editing panels.
//
#include "color.hpp"
#include "palette.hpp"

namespace gtop::render::tokens {

struct Roles {
    // -- surfaces ------------------------------------------------------------
    Rgb background;
    Rgb panel_surface;
    Rgb panel_border;
    Rgb panel_border_focus;
    Rgb rule;              // horizontal separators inside a panel

    // -- text ----------------------------------------------------------------
    Rgb text_primary;      // values, headings
    Rgb text_secondary;    // labels
    Rgb text_muted;        // units, hints, footer
    Rgb text_disabled;     // unavailable metric ("—")

    // -- status --------------------------------------------------------------
    // `critical` fills bars, borders and glyph blocks (3:1 UI threshold).
    // `critical_text` is for text, which needs 4.5:1. Using the fill colour for
    // the throttle badge would put the interface's most urgent string below AA.
    Rgb nominal;
    Rgb warning;
    Rgb critical;
    Rgb critical_text;
    Rgb info;

    // -- interaction ---------------------------------------------------------
    Rgb selection_surface;
    Rgb focus_ring;
};

// Default theme. Values come from ROADMAP.md §Phase 8 and are contrast-checked
// against panel_surface — see docs/DESIGN-TOKENS.md for the measured ratios.
inline constexpr Roles nord_dark{
    .background         = palette::nord0,
    .panel_surface      = palette::nord0,
    .panel_border       = palette::nord1,
    .panel_border_focus = palette::nord10,
    .rule               = palette::nord3,

    .text_primary       = palette::nord6,
    .text_secondary     = palette::nord4,
    .text_muted         = palette::nord9,
    .text_disabled      = palette::nord3,

    .nominal            = palette::nord14,
    .warning            = palette::nord13,
    .critical           = palette::nord11,
    .critical_text      = palette::nord11_tint,
    .info               = palette::nord8,

    .selection_surface  = palette::nord2,
    .focus_ring         = palette::nord10,
};

}  // namespace gtop::render::tokens
