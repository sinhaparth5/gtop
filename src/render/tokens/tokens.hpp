#pragma once
//
// Token layer — umbrella header.
//
// UI code includes this one file and nothing else from tokens/. Reaching past
// it (in particular including palette.hpp directly) is the smell that a raw
// colour is about to be used where a role belongs.
//
#include "color.hpp"       // Rgb, hex()
#include "gradients.hpp"   // compute / tensor / thermal / vram stops
#include "glyphs.hpp"      // braille, blocks, borders, status markers
#include "metrics.hpp"     // spacing scale, column widths, limits
#include "semantic.hpp"    // colour roles
#include "text_roles.hpp"  // text styles

// palette.hpp is intentionally NOT included here. It is an implementation
// detail of semantic.hpp and gradients.hpp.

namespace gtop::render {

// The active theme. Phase 8 adds alternates (high-contrast, NO_COLOR); this is
// the single point they get swapped at.
struct Theme {
    tokens::Roles     color = tokens::nord_dark;
    tokens::TextRoles text  = tokens::nord_dark_text;
};

inline constexpr Theme kDefaultTheme{};

}  // namespace gtop::render
