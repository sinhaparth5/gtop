// ProcessControl — Win32 (QueryFullProcessImageNameW, TerminateProcess).

#include "platform/process_control.hpp"

#include <windows.h>

#include <array>
#include <string>
#include <string_view>

#include "platform/win32/wide_string.hpp"

namespace gtop::platform {

namespace {

std::wstring_view basename_of(std::wstring_view path) {
    const std::size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring_view::npos ? path : path.substr(slash + 1);
}

// Closes a process handle on scope exit. TerminateProcess has an early-out
// path and leaking the handle there would pin the kernel object for the rest
// of the session.
class HandleGuard {
public:
    explicit HandleGuard(HANDLE handle) noexcept : handle_(handle) {}
    HandleGuard(const HandleGuard&) = delete;
    HandleGuard& operator=(const HandleGuard&) = delete;
    ~HandleGuard() {
        if (handle_ != nullptr) {
            ::CloseHandle(handle_);
        }
    }

private:
    HANDLE handle_;
};

TerminateStatus status_from_last_error() noexcept {
    switch (::GetLastError()) {
        case ERROR_ACCESS_DENIED:
            return TerminateStatus::kPermissionDenied;
        case ERROR_INVALID_PARAMETER:
            return TerminateStatus::kNoSuchProcess;
        default:
            return TerminateStatus::kFailed;
    }
}

}  // namespace

ProcessId current_process_id() noexcept {
    return static_cast<ProcessId>(::GetCurrentProcessId());
}

std::optional<std::string> process_name(ProcessId pid) {
    // QUERY_LIMITED_INFORMATION is the unprivileged right: it is enough for
    // the image name and, unlike PROCESS_QUERY_INFORMATION, it is granted for
    // processes running at a higher integrity level.
    HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                   static_cast<DWORD>(pid));
    if (process == nullptr) {
        return std::nullopt;
    }
    HandleGuard guard(process);

    std::array<wchar_t, MAX_PATH> path{};
    DWORD length = static_cast<DWORD>(path.size());
    if (::QueryFullProcessImageNameW(process, 0, path.data(), &length) == 0) {
        return std::nullopt;
    }

    const std::wstring_view name = basename_of(std::wstring_view(path.data(), length));
    if (name.empty()) {
        return std::nullopt;
    }
    return win32::to_utf8(name);
}

// There is no SIGTERM here. TerminateProcess never gives the target a chance
// to run cleanup, so claiming graceful termination would be a lie the UI would
// then repeat to the user.
bool supports_graceful_terminate() noexcept { return false; }

TerminateStatus terminate_process(ProcessId pid, TerminateMode mode) noexcept {
    if (mode == TerminateMode::kGraceful) {
        return TerminateStatus::kUnsupportedMode;
    }
    if (pid == 0) {
        return TerminateStatus::kFailed;  // the System Idle Process
    }

    HANDLE process = ::OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
    if (process == nullptr) {
        return status_from_last_error();
    }
    HandleGuard guard(process);

    if (::TerminateProcess(process, 1) == 0) {
        return status_from_last_error();
    }
    return TerminateStatus::kOk;
}

}  // namespace gtop::platform
