#pragma once
//
// Token layer — glyphs.
//
// A terminal's equivalent of an icon set. Centralised for the same reason an
// icon set is: mixed box-drawing styles or ad-hoc meter characters read as
// sloppy, and the ASCII fallback has to be swappable in one place.
//
#include <array>
#include <cstdint>
#include <string_view>

namespace gtop::render::tokens::glyphs {

// -- Braille ----------------------------------------------------------------
// Code point = kBrailleBase | dot_mask.  U+2800 .. U+28FF, a 2x4 sub-pixel cell.
inline constexpr char32_t kBrailleBase = 0x2800;

// Dot-bit lookup: kDotBit[row][col], row 0 = top.
//
// NOTE: this is NOT raster order. Rows 0-2 use bits 0-2 / 3-5, but row 3 jumps
// to bits 6-7. Assuming raster order yields graphs that look plausibly wrong,
// which is the single most common bug in a Braille renderer.
//
// Verified: full 0xFF -> U+28FF, left column 0x47 -> U+2847, bottom row 0xC0 -> U+28C0.
inline constexpr std::array<std::array<std::uint8_t, 2>, 4> kDotBit{{
    {0x01, 0x08},
    {0x02, 0x10},
    {0x04, 0x20},
    {0x40, 0x80},  // the irregular row
}};

inline constexpr int kCellCols = 2;  // sub-pixels per character cell, x
inline constexpr int kCellRows = 4;  // sub-pixels per character cell, y

// -- Block ramp --------------------------------------------------------------
// Eighth-block partial fills, for bar meters and the ASCII-safe graph fallback.
inline constexpr std::array<std::string_view, 9> kBlockRamp{
    " ", "▁", "▂", "▃", "▄",
    "▅", "▆", "▇", "█",
};

inline constexpr std::string_view kMeterFilled = "▓";  // ▓
inline constexpr std::string_view kMeterEmpty  = "░";  // ░

// -- Panel borders (rounded, per ROADMAP.md §Phase 8) ------------------------
inline constexpr std::string_view kCornerTopLeft     = "╭";  // ╭
inline constexpr std::string_view kCornerTopRight    = "╮";  // ╮
inline constexpr std::string_view kCornerBottomLeft  = "╰";  // ╰
inline constexpr std::string_view kCornerBottomRight = "╯";  // ╯
inline constexpr std::string_view kEdgeHorizontal    = "─";  // ─
inline constexpr std::string_view kEdgeVertical      = "│";  // │
inline constexpr std::string_view kTeeLeft           = "├";  // ├
inline constexpr std::string_view kTeeRight          = "┤";  // ┤

// -- Status markers ----------------------------------------------------------
// The non-colour half of every colour-coded alarm. Deliberately ASCII: U+26A0
// acquires emoji presentation in many terminals, becomes double-width, and
// shears the panel's right border.
inline constexpr std::string_view kMarkerCritical = "[!]";
inline constexpr std::string_view kMarkerWarning  = "[*]";
inline constexpr std::string_view kMarkerNominal  = "[+]";

// Rendered wherever a metric is std::nullopt. Never substitute a zero — an
// absent sensor and a genuine 0 W are different facts.
inline constexpr std::string_view kUnavailable = "—";  // —

}  // namespace gtop::render::tokens::glyphs
