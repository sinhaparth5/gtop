# Vendored vendor headers

**Types and enums only. Nothing here is ever linked.**

Every one of these libraries is opened at runtime through
`platform::DynamicLibrary`. These headers exist so the code can name a struct
layout and an enum value at compile time — not so it can call a function
directly. Adding any of them to `target_link_libraries` breaks the guarantee
that gtop starts on a machine with no GPU and no drivers, which is verified by
inspecting the link map (`ldd` on Linux, `objdump -p` on the Windows build).

They are included as a **SYSTEM** directory (see `cmake/Dependencies.cmake`).
That is not cosmetic: gtop builds with `-Werror -Wconversion -Wold-style-cast`,
and these are C headers full of C-style casts. Treating them as system headers
is what keeps our own warning level maximal instead of negotiated down to
whatever a vendor's headers happen to tolerate.

## Provenance

| Path | Source | Version | License |
| --- | --- | --- | --- |
| `nvidia/nvml.h` | CUDA Toolkit 13.1, `targets/x86_64-linux/include/nvml.h` | NVML 13.0 | NVIDIA redistributable header notice (in file) |
| `intel/ze_api.h`, `intel/zes_api.h` | [oneapi-src/level-zero](https://github.com/oneapi-src/level-zero) `include/` | v1.17 | MIT |
| `amd/*.h` | [GPUOpen-LibrariesAndSDKs/ADLX](https://github.com/GPUOpen-LibrariesAndSDKs/ADLX) `SDK/Include/` | 1.5.0.124 | MIT |

Only the ADLX headers gtop actually reaches are vendored — `ADLX.h`,
`ADLXDefines.h`, `ADLXStructures.h`, `ADLXVersion.h`, `ICollections.h`,
`ILog.h`, `IChangedEvent.h`, `ISystem.h`, `IPerformanceMonitoring.h` — rather
than all 43 files in the SDK. Adding another one is a copy, but check first
whether the metric it exposes has anywhere to go in `core::DeviceSample`.

## Local modifications

Keep this list short and keep it accurate. A header that has been edited without
being recorded here is one nobody can safely re-sync from upstream.

- `amd/ADLXDefines.h` — `#include <Windows.h>` lowercased to `<windows.h>`.
  MSVC's filesystem is case-insensitive so the original works there; mingw-w64
  ships only the lowercase name, so the `windows-mingw` cross-build cannot
  compile the file as shipped. Lowercase is correct on both.

Nothing else is patched.
