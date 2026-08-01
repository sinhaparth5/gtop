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
`posix/` and `win32/`. CMake selects the implementation per platform, so a
translation unit never contains both.

## Notes

`TerminateProcess` has no graceful mode — there is no Windows equivalent of
`SIGTERM`. The interface must not promise one, and the UI labels the action
honestly. See ROADMAP.md task 1.3.

*Currently an INTERFACE target; becomes STATIC when the first `.cpp` lands.*
