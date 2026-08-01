# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project status: pre-implementation

**No source code exists yet.** The repo contains `README.md`, `LICENSE` (GPL-3.0),
`.gitignore`, and `ROADMAP.md`. There is no `CMakeLists.txt`, no `src/`, no tests,
and therefore **no build, lint, or test commands yet** — Phase 1 of the roadmap
establishes them. Do not invent or run build commands until they exist.

`ROADMAP.md` is the complete specification: architecture, per-phase task
breakdown, API entry points, and known traps. **Read it before doing
implementation work** — it is the only design document, and it encodes decisions
that are expensive to reverse (notably the platform layer, which exists so
cross-platform support doesn't have to be retrofitted after the UI is built).

Note: `ROADMAP.md` is currently **untracked**. The original PDF spec it was
derived from has been deleted. If the roadmap is lost, the design is lost.

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

## Progress tracking protocol

`ROADMAP.md` tracks 122 tasks as `- [ ]` checkboxes. When completing work,
**update all three places** or the dashboard goes stale:

1. The task checkbox `- [ ]` → `- [x]`
2. That phase's `**Progress: 0 / N**` line
3. The dashboard table row *and* the `**Overall: 0 / 122**` header

Verify counts rather than trusting them:

```bash
grep -c '^- \[x\]'   ROADMAP.md   # completed
grep -c '^- \[[ x]\]' ROADMAP.md   # total (must equal the dashboard denominator)
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

Recommended order is M1 → M2 → M3 → M4 (72 of the 122 tasks), which yields a
working single-GPU monitor; the rest is breadth. NVIDIA goes first because NVML's
identical API covers two cells with one implementation.
