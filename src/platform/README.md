# Layer 0 — `platform/`

OS abstraction. **The only directory in the project permitted to contain
`#ifdef _WIN32`.**

## Depends on

Nothing. Standard library and OS headers only.

## Belongs here

| Concern | POSIX | Win32 |
| --- | --- | --- |
| `DynamicLibrary` | `dlopen` / `dlsym` / `dlclose` | `LoadLibraryExW` / `GetProcAddress` / `FreeLibrary` |
| `SysInfo` | `uname`, `gethostname` | `RtlGetVersion`, `GetComputerNameW` |
| `ProcessControl` | `/proc/<pid>/comm`, `kill` | `QueryFullProcessImageNameW`, `TerminateProcess` |
| `TerminalSetup` | termios, `SIGWINCH` | `SetConsoleMode`, `SetConsoleOutputCP` |

## Does not belong here

Anything that knows what a GPU is. This layer has no notion of vendors,
metrics, or panels — it exposes OS capabilities and nothing more.

## Structure

Public headers sit at the top level of this directory; implementations go in
`posix/` and `win32/`. `src/CMakeLists.txt` picks one of those directories as a
source list, so a translation unit never contains both — the OS branch is a
build-system decision, not a preprocessor one.

Include `platform.hpp` rather than an individual header.

| File | Contents |
| --- | --- |
| `dynamic_library.{hpp,cpp}` | Runtime loading; `.cpp` holds lifetime only |
| `sys_info.hpp` | Hostname, OS name, OS version |
| `process_control.hpp` | PID → name, terminate, capability query |
| `terminal_setup.{hpp,cpp}` | Capability detection; RAII console state |
| `posix/`, `win32/` | One implementation file per header |
| `win32/wide_string.hpp` | UTF-16 → UTF-8. Private to `win32/` |

Anything shared between the two implementations that involves no OS call — move
constructors, RAII teardown — lives in the top-level `.cpp` so it is written
once.

## Notes

`TerminateProcess` has no graceful mode — there is no Windows equivalent of
`SIGTERM`. `supports_graceful_terminate()` is therefore part of the interface:
the UI asks before offering the action, so the Windows button can be labelled
honestly rather than promising a courtesy the OS will not deliver. See
ROADMAP.md task 1.3.

A missing library and a missing symbol both return an empty value rather than
signalling. `DynamicLibrary::open()` takes a list of candidate names, which is
how NVML's `_v3` → `_v2` → v1 soname fallback is expressed.

*STATIC as of Phase 1. The Win32 sources are written but have never been
compiled — no Windows toolchain is available yet.*
