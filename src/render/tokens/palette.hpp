#pragma once
//
// Token layer — raw palette (Nord).
//
// ┌──────────────────────────────────────────────────────────────────────────┐
// │  THIS IS THE ONLY FILE IN THE PROJECT PERMITTED TO CONTAIN A HEX COLOUR. │
// └──────────────────────────────────────────────────────────────────────────┘
//
// Everything else refers to a *semantic* role from semantic.hpp. Enforced by:
//
//     grep -rn --include=*.hpp --include=*.cpp 'hex(0x' src/
//         | grep -v 'tokens/palette.hpp'        # must print nothing
//
// Why: a raw hex in a panel is invisible to theming. Route every colour through
// a role and a second theme becomes a data change instead of a refactor.
//
#include "color.hpp"

namespace gtop::render::tokens::palette {

// -- Polar Night — surfaces, borders ----------------------------------------
inline constexpr Rgb nord0 = hex(0x2E3440);  // base surface
inline constexpr Rgb nord1 = hex(0x3B4252);  // raised surface, panel border
inline constexpr Rgb nord2 = hex(0x434C5E);  // selection
inline constexpr Rgb nord3 = hex(0x4C566A);  // disabled / subtle rule

// -- Snow Storm — text -------------------------------------------------------
inline constexpr Rgb nord4 = hex(0xD8DEE9);  // secondary text
inline constexpr Rgb nord5 = hex(0xE5E9F0);
inline constexpr Rgb nord6 = hex(0xECEFF4);  // primary text

// -- Frost — informational, compute -----------------------------------------
inline constexpr Rgb nord7  = hex(0x8FBCBB);
inline constexpr Rgb nord8  = hex(0x88C0D0);  // accent / compute high
inline constexpr Rgb nord9  = hex(0x81A1C1);  // muted accent
inline constexpr Rgb nord10 = hex(0x5E81AC);  // compute low / focus ring

// -- Aurora — status ---------------------------------------------------------
inline constexpr Rgb nord11 = hex(0xBF616A);  // critical
inline constexpr Rgb nord12 = hex(0xD08770);  // tensor high
inline constexpr Rgb nord13 = hex(0xEBCB8B);  // warning
inline constexpr Rgb nord14 = hex(0xA3BE8C);  // nominal
inline constexpr Rgb nord15 = hex(0xB48EAD);  // tensor low

// -- Extension ---------------------------------------------------------------
// Not part of stock Nord. nord11 measures 3.05:1 against nord0 — fine for a bar
// or a border, below AA for body text. The throttle alarm is the most important
// *text* in the interface, so it gets a tint that reaches 4.83:1.
// Fills keep nord11; text uses this. See docs/DESIGN-TOKENS.md.
inline constexpr Rgb nord11_tint = hex(0xD98C93);

}  // namespace gtop::render::tokens::palette
