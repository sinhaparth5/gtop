#pragma once
//
// Token layer — text roles.
//
// A terminal has no type scale: every cell is one size. Hierarchy therefore has
// to come from weight, colour and spacing alone, which makes an explicit set of
// roles more important here than on the web, not less — there is no font-size
// escape hatch to fall back on.
//
#include "color.hpp"
#include "semantic.hpp"

namespace gtop::render::tokens {

struct TextStyle {
    Rgb  fg;
    bool bold{false};
    bool dim{false};
};

struct TextRoles {
    TextStyle panel_title;   // panel headings
    TextStyle metric_label;  // "COMPUTE", "MEMORY CTRL"
    TextStyle metric_value;  // "87%" — the number the user actually reads
    TextStyle metric_unit;   // "GB", "MHz", "W"
    TextStyle table_header;  // "PID", "PROCESS"
    TextStyle table_cell;
    TextStyle footer_hint;   // keybinding bar
    TextStyle unavailable;   // the "—" of a nullopt metric
    TextStyle alarm;         // throttle badge
};

inline constexpr TextRoles text_roles_for(const Roles& c) {
    return TextRoles{
        .panel_title  = {.fg = c.info,           .bold = true},
        .metric_label = {.fg = c.text_secondary},
        .metric_value = {.fg = c.text_primary,   .bold = true},
        .metric_unit  = {.fg = c.text_muted},
        .table_header = {.fg = c.text_muted,     .bold = true},
        .table_cell   = {.fg = c.text_primary},
        .footer_hint  = {.fg = c.text_muted},
        .unavailable  = {.fg = c.text_disabled,  .dim = true},
        .alarm        = {.fg = c.critical_text,  .bold = true},
    };
}

inline constexpr TextRoles nord_dark_text = text_roles_for(nord_dark);

}  // namespace gtop::render::tokens
