# Layer 1 — `core/`

Domain types and application state. The vocabulary the rest of the program
speaks.

## Depends on

Nothing. No OS calls, no vendor headers, no I/O — which is what makes this layer
trivially unit-testable and safe for every other layer to depend on.

## Belongs here

| File | Purpose |
| --- | --- |
| `types.{hpp,cpp}` | `DeviceSample`, `GpuStaticInfo`, `ProcessInfo`, `ThrottleFlags`, `Vendor` |
| `config.{hpp,cpp}` | CLI arguments and configuration |
| `json_export.{hpp,cpp}` | `--dump-json` — the CI harness and vendor-comparison tool |
| `version.hpp` | `kVersion`, injected by CMake from `project(VERSION ...)` |
| `state_engine.{hpp,cpp}` | Snapshot publication, history, deltas, EMA smoothing — Phase 7 |
| `history_ring.hpp` | Fixed-capacity SPSC circular buffer — Phase 7 |

## The rule that shapes this layer

**Every dynamic metric is `std::optional`.** Six vendor/OS combinations expose
different subsets of telemetry; permissions and power states change what is
readable at runtime. Optionality is not defensive coding here, it is the honest
type — and it is what lets one UI render all six without special cases.

A metric that is absent renders as `—`. It never becomes `0`: an unreadable
sensor and a genuine zero watts are different facts and must not be conflated.

## Does not belong here

Threads are *coordinated* here (the snapshot swap lives in `state_engine`) but
neither thread is *created* here. Ownership of the worker sits with the
application layer.

## The identity key

`normalise_pci_bus_id()` canonicalises a PCI address, and the driver registry
de-duplicates on the result. It lives here rather than in `driver/` because it
is a property of the vocabulary, not of any vendor: NVML pads the domain to
eight digits, sysfs to four, lspci drops it entirely, and all three name the
same card. Without one spelling, a machine with both ROCm SMI and amdgpu sysfs
installed shows every AMD GPU twice.

*STATIC as of Phase 2.*
