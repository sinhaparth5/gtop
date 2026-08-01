#pragma once
//
// Process identity and termination, for the per-process VRAM table.
//
// The two operating systems are not symmetric here, and the interface says so
// rather than papering over it. POSIX has SIGTERM: the target gets a chance to
// flush and exit. Windows has TerminateProcess, which is unconditional and
// closest in spirit to SIGKILL — there is no graceful equivalent to offer.
//
// So supports_graceful_terminate() is part of the contract. The UI asks before
// it offers the action, and labels the Windows button honestly instead of
// promising a courtesy the OS cannot deliver. See ROADMAP.md task 1.3.
//
// Every operation is unprivileged-safe: killing another user's process returns
// kPermissionDenied, never a crash and never a silent no-op.
//
#include <cstdint>
#include <optional>
#include <string>

namespace gtop::platform {

using ProcessId = std::uint32_t;

enum class TerminateMode {
    kGraceful,  // SIGTERM; unavailable on Windows
    kForceful,  // SIGKILL / TerminateProcess
};

enum class TerminateStatus {
    kOk,
    kPermissionDenied,  // not our process, and we are not privileged
    kNoSuchProcess,     // already gone — usually benign, the table was stale
    kUnsupportedMode,   // kGraceful on a platform without it
    kFailed,
};

[[nodiscard]] ProcessId current_process_id() noexcept;

// Human-readable name for a PID, without the path. Returns nullopt when the
// process has exited or is not readable — both are ordinary on a table that
// refreshes once a second.
[[nodiscard]] std::optional<std::string> process_name(ProcessId pid);

// False on Windows. Ask before offering kGraceful anywhere in the UI.
[[nodiscard]] bool supports_graceful_terminate() noexcept;

[[nodiscard]] TerminateStatus terminate_process(ProcessId pid, TerminateMode mode) noexcept;

}  // namespace gtop::platform
