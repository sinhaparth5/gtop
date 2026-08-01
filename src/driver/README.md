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
| `amd/` | AMD | `amdgpu` sysfs + ROCm SMI (Linux), ADLX (Windows) |
| `intel/` | Intel | `i915`/`xe` sysfs (Linux), Level Zero Sysman (Windows) |
| `procattr/` | Per-process attribution | DRM fdinfo (Linux), PDH counters (Windows) |

NVML is one implementation serving two platform cells — the API is identical and
only the library filename differs. That is why NVIDIA is the cheapest vendor to
support and ships first.

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
(`driver_registry.cpp`) — probe order is NVIDIA → AMD → Intel, and it decides
which backend keeps a card that two of them can see.

Three obligations the registry cannot enforce for you:

- **Fill in `pci_bus_id`.** It is the identity key, and de-duplication is what
  stops one physical GPU appearing twice. `core::normalise_pci_bus_id` handles
  the spelling differences. Fall back to `uuid` if the vendor gives no address.
- **`power_state()` must not wake the device.** The registry skips `sample()`
  on a suspended GPU precisely so a hybrid laptop's dGPU stays asleep; a backend
  that resumes the card to answer defeats the whole mechanism.
- **A backend that finds nothing is re-probed on an interval.** `nvmlInit()`
  succeeding with zero devices is a D3cold dGPU, not an absent one, so returning
  an empty list is a temporary answer rather than a permanent one.

`tests/mock/mock_driver.hpp` implements the interface for tests. It is not the
Phase 9 `--driver=mock` backend, and it deliberately lives outside `src/`.

*STATIC as of Phase 2.*
