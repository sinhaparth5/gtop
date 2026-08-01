# gtop — Engineering Roadmap

A btop-style terminal UI for monitoring GPU metrics.
**C++20 · FTXUI · Linux + Windows · NVIDIA + AMD + Intel · zero hard driver dependencies.**

This document expands the architectural blueprint in
`GPU TUI Monitor Engineering Roadmap.pdf` into an executable engineering plan
with tracked progress.

---

## 📊 Progress Dashboard

**Overall: 0 / 122 tasks complete (0%)**

`░░░░░░░░░░░░░░░░░░░░` 0%

| # | Phase | Done | Total | Status |
| --- | --- | --- | --- | --- |
| 1 | Foundation & Platform Abstraction | 0 | 17 | ⬜ Not started |
| 2 | Driver Abstraction & Runtime Loading | 0 | 12 | ⬜ Not started |
| 3 | Vendor Backends (NVIDIA / AMD / Intel) | 0 | 33 | ⬜ Not started |
| 4 | Per-Process Attribution | 0 | 10 | ⬜ Not started |
| 5 | Braille Rendering Engine | 0 | 12 | ⬜ Not started |
| 6 | Terminal UI & Layout | 0 | 15 | ⬜ Not started |
| 7 | Threading & Performance | 0 | 9 | ⬜ Not started |
| 8 | Visual Styling | 0 | 5 | ⬜ Not started |
| 9 | Verification & Release | 0 | 9 | ⬜ Not started |

**Status legend:** ⬜ Not started · 🚧 In progress · ✅ Complete · ⛔ Blocked

> **Keeping this current:** tick the `- [ ]` boxes as you go, then update the
> phase row and the overall count. `grep -c '^- \[x\]' ROADMAP.md` gives the
> completed total; `grep -c '^- \[[ x]\]' ROADMAP.md` gives the denominator.

---

## 0. Ground Rules

These constraints shape every decision below.

| Rule | Rationale |
| --- | --- |
| **No vendor library is linked at compile time.** NVML, ADLX, ROCm SMI, and Level Zero are resolved at runtime (`dlopen` / `LoadLibrary`). | One binary per OS must run on a machine with no NVIDIA driver, no AMD card, or no GPU at all, without a loader error. |
| **The render thread never blocks on driver I/O.** | Driver calls range from ~2 ms to >50 ms. Blocking the UI thread produces visible stutter. |
| **Missing telemetry is a first-class state,** not an error path. | Available metrics vary by vendor, OS, driver version, permissions, and power state. Every metric is `optional`. |
| **Unprivileged by default.** | Must be useful as a normal user. Anything needing root/Administrator degrades gracefully with a visible hint, never a crash. |
| **No `#ifdef` outside the platform layer.** | Vendor and UI code must read as portable C++. All OS divergence is confined to `src/platform/`. |

### Target support matrix

Three vendors × two operating systems. **All six cells are in scope for v1.**

| | **Linux** | **Windows** |
| --- | --- | --- |
| **NVIDIA** | NVML — `libnvidia-ml.so.1` | NVML — `nvml.dll` *(same API)* |
| **AMD** | `amdgpu` sysfs + ROCm SMI | ADLX — `amdadlx64.dll` |
| **Intel** | `i915`/`xe` sysfs | Level Zero Sysman — `ze_loader.dll` |
| **Per-process** | DRM fdinfo (`/proc/*/fdinfo/*`) | PDH GPU counters |

The happy accident worth exploiting: **NVML is the identical API on both
operating systems** — only the library filename differs. One backend
implementation covers two cells, which is why NVIDIA is the cheapest vendor to
support and goes first.

### Available test hardware

| Machine | GPUs | Covers |
| --- | --- | --- |
| **This laptop** (Linux, Ubuntu, GCC 15.2, CMake 4.2.3) | NVIDIA RTX 3060 Mobile (driver 580.173.02) + Intel Iris Xe (TigerLake, `i915`) | NVIDIA/Linux, Intel/Linux, hybrid multi-GPU enumeration, runtime power management |
| **Second laptop** | AMD | AMD/Linux and/or AMD/Windows |
| **Windows install** (needed) | Any of the above | All Windows cells |

Every one of the six cells is reachable with hardware you own — so nothing ships
as "untested". This changes the plan materially versus a single-machine project:
**AMD is a first-class, verifiable backend, not a best-effort guess.**

> ⚠️ **To confirm:** which OS does the AMD laptop run? If Linux, the AMD/Windows
> cell needs a Windows install (dual-boot or VM with GPU passthrough) before v1.
> If Windows, AMD/Linux needs the same treatment. Answer determines whether
> task 9.4 or 9.5 is the harder one.

---

## 1. Architecture

Four layers. Data flows one direction. The platform layer is new relative to the
original blueprint and exists solely to keep cross-platform support from leaking
`#ifdef`s into everything else.

```
┌──────────────────────────────────────────────────────────────┐
│  Platform Abstraction Layer  (the ONLY place with #ifdef)    │
│  DynamicLibrary · SysInfo · ProcessControl · TerminalSetup   │
│         POSIX impl              │          Win32 impl        │
└─────────────────────────────────┴────────────────────────────┘
                         ▲ used by ▲
┌──────────────────────────────────────────────────────────────┐
│  Hardware Telemetry Layer                                    │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐                    │
│  │  NVIDIA  │  │   AMD    │  │  Intel   │  each: Linux+Win   │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘                    │
│       └─────────────┼─────────────┘                          │
│                IGpuDriver (abstract)                         │
└─────────────────────┬────────────────────────────────────────┘
                      │  DeviceSample  (poll: 100–1000 ms)
                      ▼             [telemetry worker thread]
┌──────────────────────────────────────────────────────────────┐
│  Unified State Engine                                        │
│   • published snapshot (atomic swap, wait-free readers)      │
│   • per-metric history rings · deltas · EMA smoothing        │
└─────────────────────┬────────────────────────────────────────┘
                      │  const Snapshot&   (wait-free read)
                      ▼             [render thread]
┌──────────────────────────────────────────────────────────────┐
│  Terminal Rendering Engine                                   │
│   • Braille canvas (2×4 sub-pixel) · TrueColor gradients     │
│   • FTXUI component tree · layout zones · input handling     │
└──────────────────────────────────────────────────────────────┘
```

### Source layout

```
gtop/
├── CMakeLists.txt
├── cmake/Dependencies.cmake          # FetchContent: FTXUI, Catch2
├── third_party/vendor_headers/       # nvml.h, adlx, ze_api.h — HEADERS ONLY
├── src/
│   ├── main.cpp
│   ├── platform/                     # ← all #ifdef lives here
│   │   ├── dynamic_library.hpp       #   dlopen  | LoadLibrary
│   │   ├── dynamic_library_posix.cpp
│   │   ├── dynamic_library_win32.cpp
│   │   ├── sys_info.hpp/_posix/_win32      # hostname, OS version
│   │   ├── process_control.hpp/_posix/_win32  # names, signal/terminate
│   │   └── terminal_setup.hpp/_posix/_win32   # VT mode, UTF-8, resize
│   ├── core/
│   │   ├── types.hpp                 # DeviceSample, ProcessInfo, GpuStaticInfo
│   │   ├── state_engine.{hpp,cpp}
│   │   ├── history_ring.hpp
│   │   └── config.{hpp,cpp}
│   ├── driver/
│   │   ├── igpu_driver.hpp
│   │   ├── driver_registry.cpp
│   │   ├── nvml/                     # shared Linux+Windows
│   │   ├── amd/   linux_sysfs.cpp · windows_adlx.cpp
│   │   ├── intel/ linux_sysfs.cpp · windows_l0.cpp
│   │   └── procattr/ drm_fdinfo.cpp · pdh_counters.cpp
│   ├── render/  braille_canvas · gradient · theme
│   └── ui/      app · header_bar · engine_panel · thermal_panel · process_table
└── tests/
```

---

## Phase 1 — Foundation & Platform Abstraction

**Goal:** builds and runs on both operating systems; all OS divergence isolated.
**Progress: 0 / 17**

### 1.1 Build system
- [ ] CMake ≥ 3.24, `CMAKE_CXX_STANDARD 20`, `cxx_std_20` target requirement
- [ ] Compiler matrix: GCC ≥ 12, Clang ≥ 15, **MSVC ≥ 19.36** (VS 2022), clang-cl
- [ ] Warning flags abstracted: `-Wall -Wextra -Wpedantic` ↔ `/W4`; warnings-as-errors in dev
- [ ] MSVC needs `/utf-8` explicitly, or the Braille string literals in source are mangled
- [ ] FTXUI via `FetchContent`, pinned to a release tag — never `master`
- [ ] Catch2 via `FetchContent` for tests
- [ ] CMake presets: `linux-debug-asan`, `linux-release`, `windows-debug`, `windows-release`
- [ ] Verify FTXUI builds clean under MSVC before committing to it

### 1.2 `platform/dynamic_library`
The single most important abstraction in the project.

```cpp
class DynamicLibrary {
public:
    // Tries each name in order; first success wins. Never throws.
    static std::optional<DynamicLibrary> open(std::span<const char* const> names);
    template <typename Fn> Fn symbol(const char* name) const noexcept;
    ~DynamicLibrary();   // dlclose | FreeLibrary
};
```
- [ ] POSIX impl: `dlopen(RTLD_LAZY | RTLD_LOCAL)` / `dlsym` / `dlclose`
- [ ] Win32 impl: `LoadLibraryExW` / `GetProcAddress` / `FreeLibrary`
- [ ] Candidate-name lists per platform (see per-vendor sections for exact sonames)
- [ ] Missing symbols must be **non-fatal** — an older driver simply lacks newer entry points; that disables one metric, never the whole backend
- [ ] Unit test with a deliberately absent library and an absent symbol

### 1.3 `platform/sys_info`, `process_control`, `terminal_setup`
- [ ] `sys_info`: hostname + OS/kernel version — `uname`/`gethostname` ↔ `RtlGetVersion`/`GetComputerNameW`
- [ ] `process_control`: PID → process name — `/proc/<pid>/comm`+`cmdline` ↔ `QueryFullProcessImageNameW`
- [ ] `process_control`: terminate — `kill(SIGTERM/SIGKILL)` ↔ `OpenProcess(PROCESS_TERMINATE)` + `TerminateProcess`
- [ ] `terminal_setup`: **Windows requires explicit enablement** — `SetConsoleMode(ENABLE_VIRTUAL_TERMINAL_PROCESSING)` and `SetConsoleOutputCP(CP_UTF8)`, or TrueColor escapes print as literal garbage and Braille renders as `?`

> **Semantic gap to design around:** Windows has no `SIGTERM` equivalent.
> `TerminateProcess` is unconditionally forceful, like `SIGKILL`. The UI must not
> offer a "graceful terminate" it cannot deliver — label the Windows action
> honestly and warn that it is immediate.

**Exit criteria:** hello-world FTXUI app builds and runs on Ubuntu/GCC and
Windows/MSVC; `grep -r '#ifdef _WIN32' src/ --exclude-dir=platform` returns nothing.

---

## Phase 2 — Driver Abstraction & Runtime Loading

**Goal:** enumerate whatever GPUs exist; exit cleanly when there are none.
**Progress: 0 / 12**

### 2.1 The `IGpuDriver` contract
```cpp
struct GpuStaticInfo {                   // queried once at init
    std::string name, uuid, pci_bus_id, driver_version;
    Vendor vendor;
    std::optional<uint64_t> vram_total_bytes;
    std::optional<uint32_t> power_limit_mw, temp_slowdown_c;
};

struct DeviceSample {                    // queried every tick
    std::chrono::steady_clock::time_point timestamp;
    std::optional<float>    core_util, mem_controller_util;
    std::optional<float>    encoder_util, decoder_util;
    std::optional<uint64_t> vram_used_bytes;
    std::optional<uint32_t> temp_edge_c, temp_hotspot_c, temp_mem_c;
    std::optional<uint32_t> power_draw_mw, power_limit_mw;
    std::optional<uint32_t> fan_percent, clock_core_mhz, clock_mem_mhz;
    std::optional<uint64_t> pcie_rx_bps, pcie_tx_bps;
    ThrottleFlags           throttle;
    std::vector<ProcessInfo> processes;
};

class IGpuDriver {
public:
    virtual ~IGpuDriver() = default;
    virtual std::string_view backend_name() const = 0;
    virtual const GpuStaticInfo& static_info() const = 0;
    virtual DeviceSample sample() = 0;       // must not throw
    virtual bool healthy() const = 0;        // false ⇒ registry may retire it
};
```
- [ ] Define `types.hpp` — `DeviceSample`, `GpuStaticInfo`, `ProcessInfo`, `ThrottleFlags`, `Vendor`
- [ ] Define `IGpuDriver` and document the contract in-header
- [ ] **Every dynamic field is `std::optional`** — this is what makes six heterogeneous vendor/OS cells render through one UI without special-casing
- [ ] `sample()` returns partial data rather than signalling failure; only unrecoverable driver loss flips `healthy()`
- [ ] Static info separated from per-tick sampling so expensive string queries happen once

### 2.2 Driver registry
- [ ] Probe NVIDIA → AMD → Intel; collect **all** successes (hybrid systems legitimately return devices from several backends)
- [ ] Stable device ordering across restarts — sort by PCI bus ID, not enumeration order
- [ ] De-duplicate: one physical GPU must never appear twice when two backends can both see it
- [ ] Retire unhealthy drivers at runtime without taking down the app
- [ ] **Optimus/hybrid trap:** a discrete GPU may be runtime-suspended (`D3cold`). `nvmlInit` can succeed while device count is 0, or queries return `GPU_IS_LOST`. Treat this as **retryable** — re-probe periodically instead of disabling the backend forever
- [ ] **Never wake a suspended dGPU just to poll it** — that costs real battery life. Poll only when already awake; display `suspended` otherwise
- [ ] `--dump-json` headless mode: print one sample and exit

> Build `--dump-json` **here, not later**. It is the CI test harness, the
> vendor-tool comparison mechanism, and the debugging tool for every phase that
> follows.

**Exit criteria:** with drivers hidden, the binary starts, prints "no supported
GPU found", exits 0. On this laptop it enumerates both the RTX 3060 and the Iris
Xe. `ldd ./gtop` (Linux) and Dependency Walker (Windows) show **no vendor library**.

---

## Phase 3 — Vendor Backends

Six cells, three backends. Implement one fully before starting the next.
**Progress: 0 / 33**

### 3.1 NVIDIA — NVML (Linux + Windows, one implementation) · 0/12


Vendor `nvml.h` for types and enums. **Never link `libnvidia-ml` / `nvml.lib`.**

- [ ] Vendor `nvml.h` into `third_party/vendor_headers/` (CUDA 13.1's copy is on this machine at `/usr/local/cuda-13.1/targets/x86_64-linux/include/nvml.h`)
- [ ] Library candidates — Linux: `libnvidia-ml.so.1`, then `libnvidia-ml.so`. **Always try the versioned soname first** — the unversioned name exists only when a `-dev` package is installed, so relying on it fails on stock end-user systems
- [ ] Library candidates — Windows: `nvml.dll` (System32 on modern drivers), then `%ProgramFiles%\NVIDIA Corporation\NVSMI\nvml.dll` for older ones
- [ ] `nvmlInit_v2` / `nvmlShutdown` lifecycle wired into constructor/destructor
- [ ] Static info: `nvmlDeviceGetName`, `GetUUID`, `GetPciInfo`, `nvmlSystemGetDriverVersion`
- [ ] Utilization: `nvmlDeviceGetUtilizationRates` (core + memory controller)
- [ ] Memory: `nvmlDeviceGetMemoryInfo_v2`, falling back to `nvmlDeviceGetMemoryInfo`
- [ ] Power: `nvmlDeviceGetPowerUsage`, `nvmlDeviceGetEnforcedPowerLimit`
- [ ] Thermals + fan: `nvmlDeviceGetTemperature`, `GetTemperatureThreshold`, `GetFanSpeed`
- [ ] Clocks, encoder/decoder, PCIe: `GetClockInfo`, `GetEncoderUtilization`, `GetDecoderUtilization`, `GetPcieThroughput`
- [ ] Throttle reasons: `nvmlDeviceGetCurrentClocksThrottleReasons` → `ThrottleFlags`
- [ ] **Versioned-symbol fallback chain:** resolve `nvmlDeviceGetComputeRunningProcesses_v3` → `_v2` → v1 and select the **matching struct layout** for whichever resolved. NVML's ABI evolution is exactly why symbol absence must be non-fatal

Traps:
- **Graphics and compute processes are separate calls.** Query both
  `...ComputeRunningProcesses` and `...GraphicsRunningProcesses`, merge by PID —
  otherwise every game and desktop compositor is invisible.
- **Per-process utilization needs accounting mode**, which needs root/Admin to
  enable. Per-process *memory* generally works unprivileged. Ship memory; show
  utilization only when `nvmlDeviceGetProcessUtilization` actually returns data.

### 3.2 AMD · 0/11

Now a fully testable backend — the second laptop makes this first-class.

**Linux — `amdgpu` sysfs (no dlopen needed for the baseline):**
- [ ] `/sys/class/drm/cardN/device/gpu_busy_percent` → core utilization
- [ ] `/sys/class/drm/cardN/device/mem_info_vram_{total,used}` → VRAM
- [ ] hwmon: `temp1_input` (edge), `temp2_input` (junction), `temp3_input` (memory)
- [ ] hwmon: `power1_average` / `power1_cap`, `fan1_input`, `freq1_input`
- [ ] `pp_dpm_sclk` / `pp_dpm_mclk` → current clock states
- [ ] Optional `librocm_smi64.so` via `DynamicLibrary` for anything sysfs omits

**Windows — ADLX (`amdadlx64.dll`, legacy fallback `atiadlxx.dll`):**
- [ ] Vendor ADLX headers; load `amdadlx64.dll` dynamically
- [ ] Initialize via the ADLX C interface and acquire the GPU list
- [ ] `IADLXGPUMetrics` → utilization, VRAM, temperature, hotspot, power, fan, clocks
- [ ] Map ADLX metric-support queries onto `optional` — ADLX explicitly reports which metrics a given GPU supports, which lines up well with this design
- [ ] Verify exact entry-point names against the ADLX SDK version you vendor; they are more volatile than NVML's

### 3.3 Intel · 0/10

**Linux — `i915` / `xe` sysfs.** *Paths below verified present on this machine (`card2`, `i915`):*
- [ ] Detect driver variant by reading `/sys/class/drm/cardN/device/driver` (resolves to `i915` here; the `xe` driver uses a different node layout)
- [ ] Frequency: `gt/gt0/rps_{cur,act,max}_freq_mhz` ✔ confirmed present
- [ ] **Throttle reasons — a genuine bonus:** `gt/gt0/throttle_reason_{status,thermal,prochot,pl1,pl2,pl4,ratl,vr_tdc,vr_thermalert}` all present. Gives Intel the same throttle-badge treatment as NVML at no extra cost
- [ ] Idle residency: `gt/gt0/rc6_residency_ms` — differencing yields a real "GPU idle %" to cross-check utilization
- [ ] Power via `device/hwmon/hwmon*/power1_*` where exposed
- [ ] Temperature via hwmon `temp1_input`. **Confirmed absent on this laptop** — `card2/device/hwmon/` is empty. Integrated parts frequently expose no GPU thermal sensor, making this the canonical test case for rendering `—` instead of a misleading `0 °C`

**Windows — Level Zero Sysman (`ze_loader.dll`):**
- [ ] Vendor `ze_api.h` / `zes_api.h`; load `ze_loader.dll` dynamically
- [ ] `zesDeviceEnumEngineGroups` → per-engine activity (compute, media, copy)
- [ ] `zesDeviceEnumPowerDomains`, `EnumTemperatureSensors`, `EnumMemoryModules`, `EnumFrequencyDomains`
- [ ] Note: Level Zero Sysman also works on Linux — keep it as an optional Linux enhancement for richer compute metrics, but sysfs stays the dependency-free default path

**Exit criteria (per cell):** `--dump-json` output matches `nvidia-smi` /
`rocm-smi` / `intel_gpu_top` (Linux) or Task Manager / vendor control panel
(Windows) within an eyeballed tolerance, under both idle and load.

---

## Phase 4 — Per-Process Attribution

The most OS-divergent subsystem in the project — two completely different
mechanisms behind one interface.
**Progress: 0 / 10**

### 4.1 Linux — DRM fdinfo
*Verified working unprivileged on this machine.*
- [ ] Walk `/proc/*/fdinfo/*`, keep entries whose `drm-pdev` matches the target card
- [ ] Parse `drm-engine-{render,copy,video,video-enhance}` (nanosecond counters) and `drm-total-*` memory keys
- [ ] Difference consecutive samples to derive per-engine utilization and per-process share
- [ ] Shared infrastructure for **both Intel and AMD** (`amdgpu` exposes the same keys) — build it once in `driver/procattr/`, do not duplicate per vendor
- [ ] Other users' processes are invisible without root — surface as a footer hint, never an error

### 4.2 Windows — PDH GPU counters
- [ ] Query `\GPU Engine(*)\Utilization Percentage` and `\GPU Process Memory(*)\Local Usage` via PDH
- [ ] Parse instance names — they encode `pid_<N>_luid_..._phys_<N>_eng_<N>_engtype_<3D|Compute|Copy|VideoDecode>`
- [ ] Map LUID → the device enumerated by the vendor backend, so processes land on the right GPU tab
- [ ] Aggregate per-PID across engine instances; keep the engine-type breakdown for the detail view
- [ ] This is the same source Task Manager uses, so it is **vendor-neutral** — one implementation covers NVIDIA, AMD, and Intel on Windows

**Cost control (both platforms):** this walk is the most expensive thing gtop
does. Run it on a slower cadence than the metric poll (e.g. 1 s against a 200 ms
tick) and only while the process panel is visible.

---

## Phase 5 — Braille Rendering Engine

Standalone and unit-testable: no FTXUI, no GPU, no OS dependency.
**Progress: 0 / 12**

### 5.1 The dot bit layout — get this right first
Unicode Braille occupies `U+2800`–`U+28FF`; the code point is `0x2800 | dot_mask`.
The bit order is **not** raster order, and assuming it is produces graphs that
look plausibly wrong — the most common bug in this kind of renderer.

```
 dot   bit          col 0   col 1
 (1)   0x01   row0    ●       ●    0x08
 (2)   0x02   row1    ●       ●    0x10
 (3)   0x04   row2    ●       ●    0x20
 (7)   0x40   row3    ●       ●    0x80
```
```cpp
constexpr uint8_t kDotBit[4][2] = {
    {0x01, 0x08}, {0x02, 0x10}, {0x04, 0x20},
    {0x40, 0x80},   // row 3 is the irregular one
};
```
*Verified: full mask `0xFF`→`⣿`, left column `0x47`→`⡇`, bottom row `0xC0`→`⣀`.*

- [ ] Implement `kDotBit` and its unit test **before** the canvas
- [ ] `BrailleCanvas(cell_w, cell_h)` — one `uint8_t` per cell + parallel color buffer
- [ ] `set(px, py)` — bounds-checked, silent no-op out of range
- [ ] `line(x0,y0,x1,y1)` — Bresenham
- [ ] `plot_series(values, vmin, vmax)` — history mapped to canvas width
- [ ] Downsample by **max, not average** — averaging hides exactly the transient spikes this tool exists to show
- [ ] `to_utf8_row(row, gradient)` → encoded string

### 5.2 Gradients
- [ ] 24-bit TrueColor via `ESC[38;2;R;G;Bm`, colored **per character cell** (sub-pixel *position* is Braille's gift; sub-pixel *color* is not achievable in a terminal)
- [ ] Interpolate in **Oklab or at least linear RGB** — a naive sRGB lerp makes the green→amber→red thermal ramp pass through a muddy desaturated middle
- [ ] Capability detection: `COLORTERM=truecolor|24bit`, fall back to 256-color quantization; respect `NO_COLOR`
- [ ] **Windows font caveat:** Cascadia Mono/Code cover Braille; **Consolas does not**. Provide a `--ascii` / block-character fallback and document the recommended terminal

### 5.3 Resize
- [ ] Recompute canvas dimensions and history window on resize — `SIGWINCH` on Linux, `WINDOW_BUFFER_SIZE_EVENT` on Windows (FTXUI abstracts the signal, but the history *view width* is yours). Guard degenerate sizes (width 0, height 1) — that is where the crashes live

**Exit criteria:** tests cover the dot table, bounds clamping, empty/single-sample
series, all-equal values (flat line, no division by zero), NaN/inf inputs, 1×1 canvas.

---

## Phase 6 — Terminal UI & Layout

Four zones, per the blueprint.
**Progress: 0 / 15**

### 6.1 Header bar
- [ ] Hostname, OS/kernel version, driver version (via `platform/sys_info`)
- [ ] Multi-GPU device selector tabs
- [ ] Poll interval + paused indicator

### 6.2 Engine & memory panel
- [ ] Sparklines: core utilization, memory-controller activity, encode/decode, VRAM
- [ ] Per metric: current value + min/max/avg over the window
- [ ] Graceful `—` rendering for every `nullopt` metric

### 6.3 Thermals & power panel
- [ ] Temperature gauges (edge / hotspot / memory)
- [ ] Fan duty, core + memory clocks, PCIe throughput
- [ ] Power draw against enforced limit
- [ ] **Throttle badge** — prominent when thermal/power/reliability throttling is active. Arguably the single most valuable thing this tool can tell someone running a long training job

### 6.4 Interactive process manager
- [ ] Sortable columns (VRAM, utilization, PID, name)
- [ ] vim navigation `j/k/g/G`, `/` to filter
- [ ] Signal/terminate action — **always confirm first**, showing PID and command
- [ ] Handle permission failure cleanly: `EPERM` (Linux) / `ERROR_ACCESS_DENIED` (Windows) → clear message, never a silent failure or crash
- [ ] Platform-honest labelling: "Terminate (forceful)" on Windows, since `TerminateProcess` has no graceful mode

### 6.5 Keybindings
`q` quit · `Tab`/`1..9` switch GPU · `p` pause · `+`/`-` interval · `/` filter ·
`s` sort · `?` help

**Exit criteria:** usable at 80×24 (degraded but not corrupt) and 200×60; live
resizing never corrupts layout on either OS.

---

## Phase 7 — Threading & Performance

**Progress: 0 / 9**

### 7.1 Thread model — two threads, resist a third
- [ ] **Render thread** owns the FTXUI screen and input; never calls a driver
- [ ] **Telemetry worker** polls at the user interval, builds a `Snapshot`, publishes it, wakes the renderer via `ScreenInteractive::PostEvent` with a custom refresh event
- [ ] Clean shutdown: worker joins before the screen tears down (a detached worker touching a destroyed screen is the classic crash-on-exit here)

### 7.2 State handoff
- [ ] **Publication:** worker fills a fresh `Snapshot`, swaps it into an `std::atomic<std::shared_ptr<const Snapshot>>`. Readers are wait-free and always see a *coherent* snapshot, never a half-written frame
- [ ] **History:** fixed-capacity SPSC ring buffers with acquire/release on the write index — no mutex, no steady-state allocation

> Honest tradeoff: the `shared_ptr` swap costs an atomic refcount per frame,
> which is irrelevant at 60 Hz and buys substantial safety over a hand-rolled
> lock-free ring. Start there; optimize only if profiling demands it.

### 7.3 Frame pacing & conservation
- [ ] **60 FPS is a ceiling, not a target.** Redraw only on new sample or user input. An idle gtop at a 1 s interval should redraw ~once a second at ~0% CPU — a monitoring tool that burns a core is a bug, and on a laptop it costs battery
- [ ] Reduce poll rate when the terminal loses focus (focus-tracking `ESC[?1004h` where supported; otherwise back off after idle input)
- [ ] Skip the expensive process walk when the process panel is hidden
- [ ] Never wake a runtime-suspended dGPU to poll it

**Exit criteria:** idle CPU < 1% at 1 s interval on both OSes; no input lag while
a backend is stalled; ASan/TSan clean under a resize-and-input stress run.

---

## Phase 8 — Visual Styling

Palette from the blueprint (Nord-adjacent).
**Progress: 0 / 5**

| Element | Treatment | Purpose |
| --- | --- | --- |
| Panel borders | Rounded box, `#3B4252` | Separation without harsh box-drawing artifacts |
| Compute graphs | `#88C0D0` → `#5E81AC` | Core workload intensity |
| Tensor cores | `#B48EAD` → `#D08770` | Specialized AI/matrix spikes |
| Thermals | `#A3BE8C` → `#EBCB8B` → `#BF616A` | Immediate throttling indication |

- [ ] Implement `theme.hpp` with the palette above
- [ ] Rounded-border panel style
- [ ] Gradient assignments per graph type
- [ ] **Accessibility is not decoration here:** the thermal ramp is the tool's main alarm and green→red is the most common color-vision confusion. Pair every color-encoded alarm with a non-color cue — numeric value, `⚠` badge, or border-style change
- [ ] Honor `NO_COLOR`; ship at least one high-contrast alternate theme

---

## Phase 9 — Verification & Release

**Progress: 0 / 9**

### Test matrix

| Scenario | Method | Platform |
| --- | --- | --- |
| No vendor driver present | Hidden `.so`/`.dll`, clean container/VM | Both |
| Driver init fails mid-run | Fault injection via fake `IGpuDriver` | Both |
| High-rate resize | Scripted resize storm under ASan | Both |
| Multi-GPU + tab switching | This laptop: RTX 3060 + Iris Xe | Linux |
| Terminate unprivileged | Own process, then another user's → clean denial message | Both |
| dGPU runtime-suspended | Idle on battery to D3cold, then start gtop | Linux |
| Metric correctness | `--dump-json` vs. vendor tools under load | All 6 cells |
| Braille + TrueColor | Windows Terminal, ConHost, xterm, kitty, alacritty, tmux | Both |

- [ ] Unit tests: canvas, gradient, ring buffer, sysfs/fdinfo/PDH parsers
- [ ] `--driver=mock` fake backend to test UI and state engine without hardware
- [ ] CI: GitHub Actions **Ubuntu (GCC + Clang) and Windows (MSVC)**; runners have no GPU, which is a feature — it continuously proves the no-driver path
- [ ] Debug CI builds with ASan/UBSan
- [ ] Hardware validation: NVIDIA/Linux + Intel/Linux (this laptop)
- [ ] Hardware validation: AMD (second laptop)
- [ ] Hardware validation: all three vendors on Windows
- [ ] README: screenshots, build instructions per OS, honest support matrix
- [ ] Release artifacts: Linux x86_64 binary + Windows `.exe`

### v1.0 definition of done
All six vendor/OS cells verified on real hardware · runs cleanly with no GPU
present on both OSes · idle CPU < 1% · ASan/UBSan clean · no vendor library in
the link map.

---

## 🗺️ Milestone Sequence

| M | Deliverable | Phases | Tasks |
| --- | --- | --- | --- |
| **M1** | Builds on Linux + Windows; platform layer; `IGpuDriver`; enumeration; empty-system path | 1, 2 | 29 |
| **M2** | NVML backend (both OSes) + `--dump-json` verified vs. `nvidia-smi` | 3.1 | 12 |
| **M3** | Braille canvas + gradients, unit tested, no UI yet | 5 | 12 |
| **M4** | Single-GPU dashboard: header, engine, thermal panels + threading | 6.1–6.3, 7 | 19 |
| **M5** | Intel backend + AMD backend + multi-GPU tabs | 3.2, 3.3 | 21 |
| **M6** | Process attribution (fdinfo + PDH) and process table | 4, 6.4 | 15 |
| **M7** | Theming, conservation, CI, cross-platform release | 8, 9 | 14 |

**Recommended path:** M1 → M2 → M3 → M4 yields a genuinely useful single-GPU
monitor as fast as possible. Everything after is breadth. Getting Windows
working in M1 rather than retrofitting it later is the whole reason the platform
layer exists — retrofitting cross-platform support after the UI is built is
where projects like this typically go wrong.

---

## ❓ Open Questions

1. **AMD laptop OS** — Linux, Windows, or both? Determines which AMD cell is
   verifiable now and which needs an install. *(Blocks final scheduling of 3.2.)*
2. **Windows test environment** — dual-boot, separate machine, or VM with GPU
   passthrough? Needed before the three Windows cells can be validated.
3. **Config file** — TOML at `~/.config/gtop/gtop.toml` (and `%APPDATA%\gtop\`),
   or CLI flags only for v1? Flags are less work now; a config file is hard to
   retrofit gracefully.
4. **Recording mode** — is `--dump-json` on a loop (offline analysis of a long
   training run) an intended feature, or strictly a test harness?
5. **Remote / multi-node** — monitoring GPUs on another host is a common cluster
   ask and a large architectural commitment. Explicitly in or out for v1?
6. **NVIDIA MIG partitions** — out of scope for v1, but the device model should
   probably not *preclude* a device having child devices.
7. **macOS** — out of scope, but worth noting the platform layer makes it
   tractable later (Metal/IOKit). Confirm it stays out for v1.
