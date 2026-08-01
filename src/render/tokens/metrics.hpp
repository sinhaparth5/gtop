#pragma once
//
// Token layer — layout metrics.
//
// A TUI's spacing scale. The unit is one character cell, not a pixel, so the
// scale is necessarily coarse — which makes consistency matter more, not less:
// a one-cell drift is immediately visible as a ragged column.
//
#include <cstddef>

namespace gtop::render::tokens::metrics {

// -- Spacing scale -----------------------------------------------------------
inline constexpr int kSpaceNone = 0;
inline constexpr int kSpaceXs   = 1;
inline constexpr int kSpaceSm   = 2;
inline constexpr int kSpaceMd   = 4;
inline constexpr int kSpaceLg   = 6;

// -- Panel -------------------------------------------------------------------
inline constexpr int kPanelPaddingX = kSpaceXs;
inline constexpr int kPanelPaddingY = kSpaceNone;  // vertical rows are scarce
inline constexpr int kPanelGap      = kSpaceXs;

// -- Metric row columns ------------------------------------------------------
// Fixed widths so labels, values and sparklines align across every row. These
// are the same figures the interface mockup in README.md was laid out on.
inline constexpr int kLabelColumn = 16;
inline constexpr int kValueColumn = 14;  // right-aligned
inline constexpr int kStatsColumn = 13;  // right-aligned, may be blank
inline constexpr int kColumnGutter = kSpaceSm;

// Sparkline gets whatever remains; below the minimum the graph is dropped in
// favour of the numeric readout rather than rendered uselessly narrow.
inline constexpr int kSparklineMinWidth = 20;

// -- Terminal --------------------------------------------------------------
// Below this, the layout degrades to a single-column stack. It must never
// corrupt — see ROADMAP.md §Phase 6 exit criteria.
inline constexpr int kMinTerminalWidth  = 80;
inline constexpr int kMinTerminalHeight = 24;

// -- History -----------------------------------------------------------------
// Ring capacity per metric. Sized so the widest realistic terminal still has a
// full window of history after a resize, without unbounded growth.
inline constexpr std::size_t kHistoryCapacity = 512;

}  // namespace gtop::render::tokens::metrics
