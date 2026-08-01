# Design tokens

The design system for a terminal interface. Lives in
[`src/render/tokens/`](../src/render/tokens/); include `tokens.hpp` and nothing
else.

## Why a token layer in a TUI

A terminal removes most of the levers a visual designer usually has. Every cell
is the same size, so there is no type scale. There are no shadows, no radii, no
sub-pixel positioning. Hierarchy has to come from **colour, weight, glyph choice
and spacing alone** — which makes naming those four things properly more
important here than on the web, not less. There is no font-size escape hatch to
fall back on when a layout stops reading clearly.

## Structure

| File | Category | Contents |
| --- | --- | --- |
| `color.hpp` | primitive | `Rgb`, `consteval hex()` |
| `palette.hpp` | primitive | Nord ramp — **the only file with hex colours** |
| `semantic.hpp` | semantic | Roles: surfaces, text, status, interaction |
| `gradients.hpp` | semantic | Compute / tensor / thermal / VRAM stops |
| `glyphs.hpp` | primitive | Braille table, block ramp, borders, markers |
| `metrics.hpp` | primitive | Spacing scale, column widths, limits |
| `text_roles.hpp` | semantic | Weight + colour per text purpose |
| `tokens.hpp` | umbrella | The one include UI code should use |

Primitives are raw values. Semantics map them onto meaning. UI code touches
**only** semantics — the indirection is what makes a second theme a data change
rather than a refactor.

## The one hard rule

```cpp
border(semantic::panel_border);  // yes
border(palette::nord1);          // no  — invisible to theming
border(hex(...));                // no  — consteval, rejected outside palette
```

`hex()` is `consteval`, so a hex literal physically cannot reach runtime. The
grep below catches the rest:

```bash
grep -rn --include=*.hpp --include=*.cpp 'hex(0x' src/ | grep -v 'tokens/palette.hpp'
```

## Colour roles — measured contrast

Every ratio below is measured against `panel_surface` (`#2E3440`) and asserted
in `tests/unit/tokens_test.cpp`. A terminal cell is small type, so **text has no
large-text exemption**: the bar is 4.5:1.

| Role | Palette | Hex | Ratio | Verdict |
| --- | --- | --- | ---: | --- |
| `text_primary` | nord6 | `#ECEFF4` | 10.84:1 | AA text |
| `text_secondary` | nord4 | `#D8DEE9` | 9.25:1 | AA text |
| `text_muted` | nord9 | `#81A1C1` | 4.64:1 | AA text |
| `info` | nord8 | `#88C0D0` | 6.24:1 | AA text |
| `nominal` | nord14 | `#A3BE8C` | 6.13:1 | AA text |
| `warning` | nord13 | `#EBCB8B` | 8.00:1 | AA text |
| `critical_text` | nord11_tint | `#D98C93` | 4.83:1 | AA text |
| `critical` (fill) | nord11 | `#BF616A` | 3.05:1 | UI component only |
| `panel_border_focus` | nord10 | `#5E81AC` | 3.10:1 | UI component only |
| `panel_border` | nord1 | `#3B4252` | 1.24:1 | decorative |
| `rule` | nord3 | `#4C566A` | 1.69:1 | decorative |
| `text_disabled` | nord3 | `#4C566A` | 1.69:1 | see note |

### Why `critical` is split in two

Stock Nord's red measures **3.05:1** — perfectly adequate for a bar, a border or
a filled block, and below AA for text. The throttle badge is the most urgent
*string* in the interface, so rendering it in the fill colour would put the one
thing a user must not miss under the legibility bar.

So the role is split: `critical` fills, `critical_text` (a tint, `#D98C93`,
4.83:1) is for type. This is the token layer doing its job — a contrast problem
solved once, in one place, instead of being rediscovered per panel.

`nord11_tint` is the sole addition to stock Nord and is marked as such in
`palette.hpp`.

### Why `text_disabled` is exempt

It renders the `—` standing in for an absent metric. It is the *absence* of
information, never information itself, so pushing it to AA would make missing
data compete with real readings. It is excluded from the contrast assertion
deliberately, with a comment saying so.

## Gradients

| Token | Stops | Used for |
| --- | --- | --- |
| `compute` | nord8 → nord10 | Core utilisation, memory controller |
| `tensor` | nord15 → nord12 | Tensor / matrix engines |
| `thermal` | nord14 → nord13 → nord11 | Temperatures, throttle proximity |
| `vram` | nord9 → nord9 → nord13 | Occupancy, warming only near capacity |

`tensor` is deliberately a different hue family from `compute` so a tensor spike
is never mistaken for general load.

**Blending is not in this file.** Stops are data; interpolation lives in
`render/gradient.hpp` and must work in Oklab or linear RGB. A naive sRGB lerp
drags the thermal ramp through a muddy desaturated middle — precisely where the
user most needs to read it.

## Accessibility obligations

1. **Colour is never the only channel.** The thermal ramp is the primary alarm
   and green→red is the most common colour-vision confusion. Every colour-coded
   alarm also carries a numeric value or a marker from `glyphs.hpp`
   (`kMarkerCritical` = `[!]`).
2. **Markers are ASCII on purpose.** U+26A0 (⚠) acquires emoji presentation in
   many terminals, becomes double-width, and shears the panel's right border.
3. **`NO_COLOR` must work.** With colour removed, weight, markers and layout
   must still carry the full hierarchy.
4. **TrueColor is not assumed.** Detect `COLORTERM`; degrade to 256-colour.

## Glyph tokens

| Token | Value | Notes |
| --- | --- | --- |
| `kBrailleBase` | `U+2800` | Code point = base &#124; dot mask |
| `kDotBit` | 4×2 table | **Not raster order** — row 3 uses bits 6–7 |
| `kBlockRamp` | 9 steps | Empty → full, for bars and ASCII fallback |
| `kCornerTopLeft` … | `╭ ╮ ╰ ╯ ─ │ ├ ┤` | Rounded, per ROADMAP §Phase 8 |
| `kMarkerCritical` | `[!]` | ASCII, width-stable |
| `kUnavailable` | `—` | For `std::nullopt`. **Never a `0`** |

## Layout metrics

Unit is one character cell. Scale: `0, 1, 2, 4, 6`.

| Token | Value | Notes |
| --- | --- | --- |
| `kLabelColumn` | 16 | Metric row label |
| `kValueColumn` | 14 | Right-aligned |
| `kStatsColumn` | 13 | Right-aligned, may be blank |
| `kSparklineMinWidth` | 20 | Below this, drop the graph and keep the number |
| `kMinTerminalWidth/Height` | 80 × 24 | Degrade below this, never corrupt |
| `kHistoryCapacity` | 512 | Per-metric ring |

The four column widths sum within `kMinTerminalWidth`, asserted at compile time
in `tokens_test.cpp` — so the layout provably fits the smallest supported
terminal.

## Changing a token

1. Edit `palette.hpp` (values) or `semantic.hpp` (mapping).
2. Run `ctest --preset linux-release -R tokens`. The contrast assertions fail
   loudly if a text role drops below AA.
3. Update the measured table above.

Adding a theme means supplying another `Roles` instance — no panel changes.
