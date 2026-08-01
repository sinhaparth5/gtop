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

*Currently an INTERFACE target; becomes STATIC when the first `.cpp` lands.*
