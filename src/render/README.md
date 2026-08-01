# Layer 3 — `render/`

Design tokens and drawing primitives. Turns numbers into glyphs and colours,
with no knowledge of FTXUI or of where the numbers came from.

## Depends on

`core` only.

## Layout

| Path | Purpose |
| --- | --- |
| `tokens/` | The design system — colours, glyphs, spacing, text roles |
| `braille_canvas.{hpp,cpp}` | 2×4 sub-pixel canvas |
| `gradient.{hpp,cpp}` | Colour interpolation and capability detection |

## `tokens/` — the variables layer

| File | Holds | Rule |
| --- | --- | --- |
| `color.hpp` | `Rgb`, `hex()` | `hex()` is `consteval` — hex literals cannot reach runtime |
| `palette.hpp` | The Nord ramp | **The only file allowed to contain a hex colour** |
| `semantic.hpp` | Roles: `panel_border`, `critical`, `text_muted`… | UI names roles, never palette entries |
| `gradients.hpp` | Compute / tensor / thermal / VRAM stops | Data only; blending lives in `gradient.hpp` |
| `glyphs.hpp` | Braille dot table, block ramp, borders, status markers | The terminal's icon set |
| `metrics.hpp` | Spacing scale, column widths, limits | One character cell is the unit |
| `text_roles.hpp` | Weight and colour per text purpose | A terminal has no type scale — hierarchy comes from these |
| `tokens.hpp` | Umbrella include | Include this; reaching past it to `palette.hpp` is the smell |

Enforcement:

```bash
grep -rn --include=*.hpp --include=*.cpp 'hex(0x' src/ | grep -v 'tokens/palette.hpp'
# must print nothing
```

`tests/unit/tokens_test.cpp` additionally asserts that every text role clears
WCAG AA (4.5:1) against `panel_surface`, so a colour change cannot quietly
degrade legibility.

## Two things that are easy to get wrong

**Braille bit order is not raster order.** Rows 0–2 use bits 0–2 and 3–5; row 3
jumps to bits 6–7. Assuming raster order yields graphs that look plausible and
are wrong. The table is in `glyphs.hpp` and tested first.

**Blend in Oklab or linear RGB, never naive sRGB.** A straight sRGB lerp drags
the green→amber→red thermal ramp through a muddy desaturated middle, right where
the user most needs to read it.

*Currently an INTERFACE target (tokens are header-only); becomes STATIC when
`braille_canvas.cpp` lands.*
