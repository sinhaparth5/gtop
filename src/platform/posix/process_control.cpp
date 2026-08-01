// ProcessControl — POSIX (/proc, kill).

#include "platform/process_control.hpp"

#include <signal.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <string>
#include <string_view>

namespace gtop::platform {

namespace {

// Reads a small /proc file whole. std::ifstream reports size 0 for procfs
// entries, so this reads until EOF instead of seeking.
std::optional<std::string> read_proc_file(const std::string& path) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return std::nullopt;
    }

    std::string contents;
    char buffer[512];
    while (const std::size_t n = std::fread(buffer, 1, sizeof buffer, file)) {
        contents.append(buffer, n);
    }
    std::fclose(file);
    return contents;
}

// /proc/<pid>/cmdline is NUL-separated; argv[0] is the invoked path.
std::string basename_of(std::string_view path) {
    const std::size_t slash = path.find_last_of('/');
    return std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
}

}  // namespace

ProcessId current_process_id() noexcept {
    return static_cast<ProcessId>(::getpid());
}

std::optional<std::string> process_name(ProcessId pid) {
    const std::string root = "/proc/" + std::to_string(pid) + "/";

    // cmdline first: it holds the full invoked path, so the basename is the
    // real binary name. comm is capped at 15 characters and truncates exactly
    // the long names a GPU process table is full of.
    if (auto cmdline = read_proc_file(root + "cmdline")) {
        const std::string_view argv0(cmdline->c_str());  // stops at the first NUL
        if (!argv0.empty()) {
            return basename_of(argv0);
        }
    }

    // Empty cmdline means a kernel thread, or a process that scrubbed its own
    // argv. comm still answers.
    if (auto comm = read_proc_file(root + "comm")) {
        while (!comm->empty() && (comm->back() == '\n' || comm->back() == '\0')) {
            comm->pop_back();
        }
        if (!comm->empty()) {
            return *comm;
        }
    }

    return std::nullopt;
}

bool supports_graceful_terminate() noexcept { return true; }

TerminateStatus terminate_process(ProcessId pid, TerminateMode mode) noexcept {
    // kill(0) and kill(-1) mean "the whole process group" and "every process
    // we may signal". Neither is ever what a user clicking one table row
    // intended, so they are rejected before they reach the kernel.
    if (pid == 0) {
        return TerminateStatus::kFailed;
    }

    const int signal_number = (mode == TerminateMode::kGraceful) ? SIGTERM : SIGKILL;
    if (::kill(static_cast<pid_t>(pid), signal_number) == 0) {
        return TerminateStatus::kOk;
    }

    switch (errno) {
        case EPERM:
            return TerminateStatus::kPermissionDenied;
        case ESRCH:
            return TerminateStatus::kNoSuchProcess;
        default:
            return TerminateStatus::kFailed;
    }
}

}  // namespace gtop::platform
