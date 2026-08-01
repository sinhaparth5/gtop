# Layer 4 — `ui/`

FTXUI components, layout, and input handling. The only layer that knows FTXUI
exists.

## Depends on

`render`, `core`, `platform`. Never on `driver` — panels read a published
`Snapshot`, they do not query hardware.

## Layout

| Path | Purpose |
| --- | --- |
| `app.{hpp,cpp}` | Root component, event loop, thread ownership |
| `panels/` | Dashboard zones: header, engine, thermal, process table |
| `widgets/` | Reusable pieces: sparkline row, gauge, meter bar, badge |

`panels/` are composed of `widgets/`; a widget never reaches back into a panel.

## The rule that shapes this layer

**The render thread never calls a driver.** Driver calls take 2–50+ ms; blocking
here is visible as stutter. The worker publishes a snapshot and posts a refresh
event; this layer reads wait-free and draws.

Corollary: 60 FPS is a *ceiling*, not a target. Redraw on new sample or user
input only. An idle gtop at a 1 s interval should redraw about once a second at
under 1% CPU — a monitor that burns a core is a bug, and on a laptop it costs
battery.

## Design obligations

- **No raw colours.** Name a role from `render/tokens`. See that layer's README.
- **Every colour-coded alarm carries a non-colour cue.** The thermal ramp is the
  primary alarm and green→red is the most common colour-vision confusion. Pair
  it with a numeric value or a status marker from `glyphs.hpp`.
- **Confirm before signalling a process**, showing PID and command. Handle
  permission denial with a clear message — never a silent failure.
- **Degrade, don't corrupt.** At 80×24 the layout may drop graphs in favour of
  numbers; it may not produce a broken frame.

*Currently an INTERFACE target; becomes STATIC when the first `.cpp` lands.*
