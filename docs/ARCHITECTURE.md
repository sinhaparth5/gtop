# Architecture

Five layers. Dependencies point one direction — downward, always.

```
        ┌──────────────────────────────────────────┐
  L4    │  ui/          FTXUI components, panels   │
        └───────┬───────────────┬──────────────┬───┘
                │               │              │
        ┌───────▼───────┐       │              │
  L3    │  render/      │       │              │
        │  tokens, canvas│      │              │
        └───────┬───────┘       │              │
                │       ┌───────▼──────┐       │
  L2            │       │  driver/     │       │
                │       │  NVML/AMD/Intel│     │
                │       └───┬──────┬───┘       │
        ┌───────▼───────────▼──┐   │           │
  L1    │  core/  types, state │   │           │
        └──────────────────────┘   │           │
        ┌──────────────────────────▼───────────▼───┐
  L0    │  platform/   dlopen | LoadLibrary, etc.  │
        └──────────────────────────────────────────┘
```

## Dependency matrix

| Layer | May depend on | Must not depend on |
| --- | --- | --- |
| `platform/` | std, OS headers | everything else |
| `core/` | std | platform, driver, render, ui |
| `driver/` | core, platform, vendored headers | render, ui |
| `render/` | core | platform, driver, ui |
| `ui/` | render, core, platform | driver |

`ui` not depending on `driver` is the load-bearing one: panels read a published
`Snapshot` and never query hardware. It is what keeps the render thread off
blocking driver calls.

The graph is declared in `src/CMakeLists.txt` via `target_link_libraries`, so
adding an edge is an explicit, reviewable change rather than an accident.

> **Honest limitation:** every layer shares `src/` as its include root, so CMake
> cannot mechanically reject a sideways `#include`. `platform/` carries object
> code as of Phase 1 and so is link-enforced; the four header-only layers above
> it are still checked by the matrix plus review. Splitting into per-layer
> include roots would make it airtight, at the cost of a deeper tree.

## Layer summaries

Each layer directory has its own README with the detail:

| Layer | README |
| --- | --- |
| L0 platform | [`src/platform/README.md`](../src/platform/README.md) |
| L1 core | [`src/core/README.md`](../src/core/README.md) |
| L2 driver | [`src/driver/README.md`](../src/driver/README.md) |
| L3 render | [`src/render/README.md`](../src/render/README.md) |
| L4 ui | [`src/ui/README.md`](../src/ui/README.md) |

## Enforceable invariants

These are checks, not aspirations. Each should end up in CI.

```bash
# 1. No #ifdef outside the platform layer
grep -rn '#ifdef _WIN32' src/ --exclude-dir=platform          # → empty

# 2. No raw hex outside the palette
grep -rn --include=*.hpp --include=*.cpp 'hex(0x' src/ \
  | grep -v 'tokens/palette.hpp'                              # → empty

# 3. UI must not reach into driver
grep -rn '#include "driver/' src/ui/                          # → empty

# 4. Text tokens must clear WCAG AA
ctest --preset linux-release -R tokens
```

## Build

```bash
cmake --preset linux-release
cmake --build --preset linux-release
ctest --preset linux-release
./build/linux-release/bin/gtop
```

Presets exist for `linux-debug` (ASan/UBSan), `linux-release`, `windows-debug`,
and `windows-release`. Third-party fetching is off by default so a fresh clone
configures offline; enable with `-DGTOP_FETCH_DEPS=ON`.

## Directory map

```
src/
├── main.cpp              application entry
├── platform/             L0 — OS abstraction (posix/, win32/) — implemented
├── core/                 L1 — types, state engine, config
├── driver/               L2 — nvml/, amd/, intel/, procattr/
├── render/               L3 — tokens/, canvas, gradients
└── ui/                   L4 — app, panels/, widgets/

third_party/vendor_headers/   nvidia/ amd/ intel/ — headers only, never linked
tests/                        unit/, mock/, fixtures/
cmake/                        CompilerWarnings.cmake, Dependencies.cmake
docs/                         this file, DESIGN-TOKENS.md
```
