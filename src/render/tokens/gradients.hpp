#pragma once
//
// Token layer — gradient stops.
//
// Data only. Interpolation lives in render/gradient.hpp, because *how* you
// blend is behaviour and blending in naive sRGB drags the thermal ramp through
// a muddy desaturated middle. See ROADMAP.md task 5.2.
//
#include <array>

#include "color.hpp"
#include "palette.hpp"

namespace gtop::render::tokens {

struct Stop {
    float pos;  // 0.0 .. 1.0
    Rgb   color;
};

namespace gradients {

// Core compute load — cyan to deep blue.
inline constexpr std::array<Stop, 2> compute{{
    {0.0F, palette::nord8},
    {1.0F, palette::nord10},
}};

// Tensor / matrix engines — purple to amber. Deliberately distinct in hue from
// `compute` so a tensor spike is not mistaken for general load.
inline constexpr std::array<Stop, 2> tensor{{
    {0.0F, palette::nord15},
    {1.0F, palette::nord12},
}};

// Thermals — green to yellow to red.
//
// This ramp is the tool's primary alarm. Green-to-red is the most common
// colour-vision confusion, so anything using it MUST also carry a non-colour
// cue (numeric value or a status glyph). See tokens/glyphs.hpp.
inline constexpr std::array<Stop, 3> thermal{{
    {0.0F, palette::nord14},
    {0.5F, palette::nord13},
    {1.0F, palette::nord11},
}};

// VRAM occupancy — flat informational fill, warming only near capacity.
inline constexpr std::array<Stop, 3> vram{{
    {0.00F, palette::nord9},
    {0.85F, palette::nord9},
    {1.00F, palette::nord13},
}};

}  // namespace gradients
}  // namespace gtop::render::tokens
