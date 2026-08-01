# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project status: Phase 1 complete (17/123), no telemetry yet

**No GPU backend, canvas, or UI is implemented.** `main.cpp` prints what the
platform layer detected about the host and exits.

What is real today: the build system and presets, `src/render/tokens/`
(complete), and `src/platform/` (complete — `DynamicLibrary`, `SystemInfo`,
`ProcessControl`, `TerminalSession`). Two test suites pass, including under
ASan/UBSan. Phase 2 is next: `IGpuDriver`, the driver registry, and
`--dump-json`.

Caveat that matters: `src/platform/win32/` compiles, but **nothing Windows has
ever been run.** The `windows-mingw` preset cross-builds the whole tree
(including FTXUI) for a Windows target from Linux, so the code is known to be
valid portable C++ — it is not known to be MSVC-conformant, and its runtime
behaviour is untested. Re-run that preset after touching anything under
`platform/win32/`.

`ROADMAP.md` is the specification — architecture, 123 tracked tasks, vendor API
entry points, known traps. **Read it before implementation work.** It encodes
decisions expensive to reverse, notably the platform layer, which exists so
cross-platform support isn't retrofitted after the UI is built.
`docs/ARCHITECTURE.md` covers the layer graph; `docs/DESIGN-TOKENS.md` the tokens.

Note: the original PDF spec was deleted, so `ROADMAP.md` is the sole design
record.

## Build and test

```bash
cmake --preset linux-release          # or linux-debug (ASan/UBSan), windows-{debug,release}
cmake --build --preset linux-release
ctest --preset linux-release          # single suite: ctest --preset linux-release -R platform
./build/linux-release/bin/gtop
```

### Checking the Windows half from Linux

```bash
cmake --preset windows-mingw          # needs x86_64-w64-mingw32-g++ on PATH
cmake --build --preset windows-mingw  # produces build/windows-mingw/bin/gtop.exe
```

This is the only way to compile `src/platform/win32/` on this machine, so run it
whenever that directory changes — otherwise the Windows sources rot silently and
the first real MSVC build inherits every mistake at once. It cross-compiles, so
it builds and links but cannot run; `ctest` has no `windows-mingw` preset for
that reason. Point `GTOP_MINGW_ROOT` at a prefix if the toolchain is not on
`PATH`. Being GCC, it proves portability, not MSVC conformance.

`GTOP_FETCH_DEPS` is **OFF** by default so a fresh clone configures offline and
with no GPU SDK. Turn it on when FTXUI/Catch2 are actually needed; that also
enables the `ftxui_smoke` suite, which is the only check that FTXUI still
compiles and links against this toolchain. Warnings are errors by default
(`-Werror` / `/WX`) — including `-Wconversion` and `-Wold-style-cast`, so new
code must be clean from the start.

Test executables are built with `-UNDEBUG` (see `tests/CMakeLists.txt`). The
suites check with `assert`, and the release presets define `NDEBUG`, which would
otherwise delete every check and leave a test that passes by doing nothing.

## What gtop is

A btop-style terminal UI for GPU monitoring. C++20 + FTXUI. The scope is
deliberately 3 vendors × 2 operating systems = **six support cells, all in scope
for v1**:

| | Linux | Windows |
|---|---|---|
| NVIDIA | NVML `libnvidia-ml.so.1` | NVML `nvml.dll` *(identical API)* |
| AMD | `amdgpu` sysfs + ROCm SMI | ADLX `amdadlx64.dll` |
| Intel | `i915`/`xe` sysfs | Level Zero Sysman `ze_loader.dll` |
| per-process | DRM fdinfo (`/proc/*/fdinfo/*`) | PDH GPU counters |

## Non-negotiable design constraints

These are architectural invariants, not style preferences. Violating any of them
breaks a stated exit criterion in the roadmap.

1. **Never link a vendor library at compile time.** NVML, ADLX, ROCm SMI, and
   Level Zero are all resolved at runtime via `DynamicLibrary` (`dlopen` /
   `LoadLibrary`). Vendored vendor headers are for *types and enums only*. The
   binary must start on a machine with no GPU and no drivers. Verified by
   checking that no vendor library appears in the link map.
2. **No `#ifdef` outside `src/platform/`.** Enforced by
   `grep -r '#ifdef _WIN32' src/ --exclude-dir=platform` returning nothing.
   Vendor and UI code must read as portable C++.
3. **Every dynamic metric is `std::optional`.** This is what lets six
   heterogeneous vendor/OS cells render through one UI without special-casing.
   Absent telemetry renders `—`, never `0`.
4. **The render thread never calls a driver.** Driver calls take 2–50+ ms.
   Two threads only — render + telemetry worker — communicating via an atomic
   snapshot swap.
5. **A missing dynamic symbol is normal, not an error.** Older drivers lack newer
   entry points; that disables one metric, never a whole backend. NVML in
   particular needs `_v3` → `_v2` → v1 fallback chains with matching struct
   layouts.
6. **Unprivileged by default.** Anything requiring root/Administrator degrades
   with a visible hint. Never crash on `EPERM` / `ERROR_ACCESS_DENIED`.
7. **No raw hex colours outside `src/render/tokens/palette.hpp`.** UI names a
   semantic role. `hex()` is `consteval`, so literals cannot reach runtime; a
   grep catches the rest. `critical` (fill, 3.05:1) and `critical_text`
   (4.83:1) are deliberately separate roles — see `docs/DESIGN-TOKENS.md`.
8. **Layers depend downward only:** `ui → render → core`, `driver → core +
   platform`, `ui → platform`. **`ui` must never include `driver/`** — panels
   read a published snapshot, they never query hardware.

### Enforceable invariants

Run these before claiming a change is clean; they are the mechanical form of
rules 2, 7, 8 and the token contrast contract:

```bash
grep -rn '#ifdef _WIN32' src/ --exclude-dir=platform                        # empty
grep -rn --include=*.hpp --include=*.cpp 'hex(0x' src/ \
  | grep -v 'tokens/palette.hpp'                                            # empty
grep -rn '#include "driver/' src/ui/                                        # empty
ctest --preset linux-release -R tokens                                      # passes
```

The token test asserts every text role clears WCAG AA (4.5:1) against
`panel_surface`, so a colour change cannot silently degrade legibility.

Rule 1 has a Windows form too — after a `windows-mingw` build, the import table
must name no vendor DLL:

```bash
x86_64-w64-mingw32-objdump -p build/windows-mingw/bin/gtop.exe | grep 'DLL Name'
# KERNEL32.dll, msvcrt.dll, libgcc_s_seh-1.dll, libstdc++-6.dll — nothing else
```

## Progress tracking protocol

`ROADMAP.md` tracks 123 tasks as `- [ ]` checkboxes. When completing work,
**update all three places** or the dashboard goes stale:

1. The task checkbox `- [ ]` → `- [x]`
2. That phase's `**Progress: 0 / N**` line
3. The dashboard table row *and* the `**Overall: 0 / 123**` header

Verify counts rather than trusting them:

```bash
grep -c '^- \[x\]'   ROADMAP.md   # completed — currently 17
grep -c '^- \[[ x]\]' ROADMAP.md   # total (must equal the dashboard denominator) — 123
```

The phase rows, the milestone table, and the overall count must all sum to the
same total.

## Verified environment facts

These were empirically confirmed on the development machine. They are load-bearing
for implementation — do not re-derive or assume otherwise without re-checking.

- **Dev hardware is an Optimus hybrid:** NVIDIA RTX 3060 Mobile (`card1`, driver
  580.173.02) + Intel Iris Xe TigerLake (`card2`, `i915`). Covers NVIDIA/Linux,
  Intel/Linux, and multi-GPU enumeration. AMD hardware is a second laptop; a
  Windows environment is still needed for the three Windows cells.
- **Toolchain:** GCC 15.2, CMake 4.2.3. CUDA 13.1 is installed and its `nvml.h`
  lives at `/usr/local/cuda-13.1/targets/x86_64-linux/include/nvml.h` — vendor
  that header, but **never link against the CUDA toolkit**.
- **Braille bit order is not raster order.** Row 3 is irregular:
  `{0x01,0x08}, {0x02,0x10}, {0x04,0x20}, {0x40,0x80}`. Confirmed by rendering
  `0xFF`→`⣿`, `0x47`→`⡇`, `0xC0`→`⣀`. Assuming raster order produces graphs that
  look plausibly wrong — write this unit test first.
- **`card2/device/hwmon/` is empty** — the Iris Xe exposes no thermal sensor.
  This is the canonical test case for rendering `—` rather than a fake `0 °C`.
- **Intel exposes `gt/gt0/throttle_reason_*`** (status, thermal, prochot, pl1,
  pl2, pl4, ratl, vr_tdc, vr_thermalert), so Intel gets the same throttle badge
  as NVML for free.
- **DRM fdinfo works unprivileged**, exposing `drm-pdev`, `drm-engine-*` (ns
  counters), and `drm-total-*`. Same keys on `amdgpu`, so build it once as shared
  infrastructure for both Intel and AMD.

## Implementation sequencing

Build `--dump-json` (headless single-sample mode) in Phase 2, **not later** — it
is the CI harness, the vendor-tool comparison mechanism, and the debugging tool
for every subsequent phase.

Recommended order is M1 → M2 → M3 → M4 (72 of the 123 tasks), which yields a
working single-GPU monitor; the rest is breadth. NVIDIA goes first because NVML's
identical API covers two cells with one implementation.
