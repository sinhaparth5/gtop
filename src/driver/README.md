# Layer 2 — `driver/`

Hardware telemetry. Every vendor backend implements one interface,
`IGpuDriver`, and every vendor library is resolved **at runtime**.

## Depends on

`core` (for `DeviceSample`), `platform` (for `DynamicLibrary`), and the vendored
headers in `third_party/vendor_headers/`.

## Layout

| Directory | Covers | Mechanism |
| --- | --- | --- |
| `nvml/` | NVIDIA, **both** operating systems | `libnvidia-ml.so.1` / `nvml.dll` |
| `amd/` | AMD | `amdgpu` sysfs (Linux), ADLX (Windows) |
| `intel/` | Intel | `i915`/`xe` sysfs (Linux), Level Zero Sysman (both) |
| `sysfs/` | DRM + hwmon reading | shared by the AMD and Intel Linux backends |
| `posix/`, `win32/` | `builtin_backends()` | which backends exist, per OS |
| `procattr/` | Per-process attribution | DRM fdinfo (Linux), PDH counters (Windows) — Phase 4 |

NVML is one implementation serving two platform cells — the API is identical and
only the library filename differs. That is why NVIDIA is the cheapest vendor to
support and shipped first.

`sysfs/` exists because hwmon is a kernel-wide standard: `amdgpu` and `i915`
spell temperature, power and fan identically, so the reader is written once. The
same argument makes DRM fdinfo shared infrastructure in Phase 4.

`builtin_backends()` is split across `posix/` and `win32/` rather than branching,
because the *list* of backends genuinely differs by OS — AMD and Intel are
separate implementations on the two systems. CMake picks the file, which is what
keeps `#ifdef` out of this layer entirely.

## The rule that shapes this layer

**Never link a vendor library.** Vendored headers supply types and enums; every
entry point is bound through `platform::DynamicLibrary`. Verified by checking
that no vendor library appears in the link map.

Two consequences that must be designed for, not patched around later:

- **A missing symbol is normal.** Older drivers do not export newer entry
  points. Resolve newest-first with fallback chains (`_v3` → `_v2` → v1) and
  select the struct layout matching whichever resolved. A missing symbol
  disables one metric; it never fails a backend.
- **`sample()` must not throw.** Return a partially populated `DeviceSample`.
  Only unrecoverable driver loss flips `healthy()`.

## Does not belong here

Formatting, colour, or layout. This layer produces numbers; deciding that 84 °C
should be rendered amber is the render layer's job.

## Adding a backend

`DriverRegistry` owns discovery and lifecycle; a backend supplies one `ProbeFn`
and one `IGpuDriver` per GPU it can see. Register it in `builtin_backends()`
(`posix/backends.cpp` or `win32/backends.cpp`) — probe order is
NVIDIA → AMD → Intel, and it decides which backend keeps a card that two of them
can see. That ordering is why i915 sysfs, not Level Zero, keeps an Intel GPU on
a Linux box with both installed.

Three obligations the registry cannot enforce for you:

- **Fill in `pci_bus_id`.** It is the identity key, and de-duplication is what
  stops one physical GPU appearing twice. `core::normalise_pci_bus_id` handles
  the spelling differences. Fall back to `uuid` if the vendor gives no address —
  ADLX is the real case, since it exposes no PCI address at all.
- **`power_state()` must not wake the device.** Call
  `platform::pci_power_state()`; do not invent a second route. The registry
  skips `sample()` on a suspended GPU precisely so a hybrid laptop's dGPU stays
  asleep, and a backend that resumes the card to answer defeats the mechanism.
- **A backend that finds nothing is re-probed on an interval.** `nvmlInit()`
  succeeding with zero devices is a D3cold dGPU, not an absent one, so returning
  an empty list is a temporary answer rather than a permanent one.

One thing worth expecting: **some metrics are rates, not readings.** Intel
utilization comes from differencing RC6 residency and Level Zero reports busy
time and energy counters rather than percentages and watts. Those backends hold
the previous reading and return nothing on the first sample. If a metric that
should exist is missing, check whether the caller took only one sample before
concluding the backend is broken.

`tests/mock/mock_driver.hpp` implements the interface for tests. It is not the
Phase 9 `--driver=mock` backend, and it deliberately lives outside `src/`.

*STATIC as of Phase 2. All six vendor cells implemented as of Phase 3.*
