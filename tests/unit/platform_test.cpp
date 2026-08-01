// Platform layer invariants — ROADMAP.md tasks 1.2 and 1.3.
//
// The important cases here are the negative ones. A vendor library that is not
// installed, and an entry point an older driver does not export, are both
// ordinary facts about the machine gtop happens to be running on. If either
// one throws, aborts, or leaks a handle, every backend built on top inherits
// the bug — so they are tested before anything is built on top.
//
// The library and symbol names come from the build system rather than a
// preprocessor branch here, for the same reason src/platform/ splits by
// directory: no translation unit should have to know which OS it is on.

#include <cassert>
#include <cstdio>
#include <cstring>
#include <optional>
#include <span>
#include <string>

#include "platform/platform.hpp"

namespace plat = gtop::platform;

namespace {

// -- DynamicLibrary ----------------------------------------------------------

void absent_library_is_not_an_error() {
    static const char* const nowhere[] = {
        "libgtop-definitely-not-installed.so.99",
        "gtop-definitely-not-installed.dll",
    };

    auto library = plat::DynamicLibrary::open(std::span<const char* const>(nowhere));
    assert(!library.has_value() && "opening absent libraries must return nullopt");

    assert(!plat::DynamicLibrary::open(nullptr).has_value());
    assert(!plat::DynamicLibrary::open(std::span<const char* const>{}).has_value());

    std::puts("  absent library        -> nullopt");
}

// The fallback chain is how NVML's _v3 -> _v2 -> v1 soname search is expressed,
// so a miss followed by a hit has to select the hit and report which name won.
void candidate_list_falls_through_to_the_first_hit() {
    static const char* const candidates[] = {
        "libgtop-definitely-not-installed.so.99",
        GTOP_TEST_LIBRARY,
    };

    auto library = plat::DynamicLibrary::open(std::span<const char* const>(candidates));
    assert(library.has_value() && "expected " GTOP_TEST_LIBRARY " to be loadable");
    assert(library->valid());
    assert(library->loaded_name() == GTOP_TEST_LIBRARY);

    std::printf("  fallback chain        -> %s\n", std::string(library->loaded_name()).c_str());
}

void present_symbol_resolves_and_absent_symbol_returns_null() {
    auto library = plat::DynamicLibrary::open(GTOP_TEST_LIBRARY);
    assert(library.has_value());

    using AnyFn = void (*)();
    const AnyFn present = library->symbol<AnyFn>(GTOP_TEST_SYMBOL);
    assert(present != nullptr && "expected " GTOP_TEST_SYMBOL " to resolve");

    // The case that matters: a driver too old to export what we asked for. It
    // must yield nullptr and leave the library usable for everything else.
    const AnyFn missing = library->symbol<AnyFn>("gtop_symbol_that_does_not_exist");
    assert(missing == nullptr && "absent symbol must resolve to nullptr, not abort");

    const AnyFn null_name = library->symbol<AnyFn>(nullptr);
    assert(null_name == nullptr);

    // Still healthy after the miss.
    assert(library->symbol<AnyFn>(GTOP_TEST_SYMBOL) == present);

    std::puts("  absent symbol         -> nullptr, library still usable");
}

// Run under linux-debug (ASan) this is what catches a double dlclose.
void ownership_transfers_exactly_once() {
    auto source = plat::DynamicLibrary::open(GTOP_TEST_LIBRARY);
    assert(source.has_value());

    plat::DynamicLibrary moved(std::move(*source));
    assert(moved.valid());
    assert(moved.loaded_name() == GTOP_TEST_LIBRARY);
    assert(!source->valid() && "moved-from handle must be surrendered");

    auto other = plat::DynamicLibrary::open(GTOP_TEST_LIBRARY);
    assert(other.has_value());
    *other = std::move(moved);
    assert(other->valid());
    assert(!moved.valid());

    using AnyFn = void (*)();
    assert(other->symbol<AnyFn>(GTOP_TEST_SYMBOL) != nullptr);

    std::puts("  move semantics        -> single ownership");
}

// -- SystemInfo --------------------------------------------------------------

void system_info_identifies_the_host() {
    const plat::SystemInfo info = plat::query_system_info();

    // hostname is allowed to be empty (containers), the OS fields are not:
    // both operating systems always answer.
    assert(!info.os_name.empty() && "os_name must be populated");
    assert(!info.os_version.empty() && "os_version must be populated");

    std::printf("  host                  -> %s / %s %s\n",
                info.hostname.empty() ? "(unnamed)" : info.hostname.c_str(),
                info.os_name.c_str(), info.os_version.c_str());
}

// -- ProcessControl ----------------------------------------------------------

void process_lookup_handles_present_and_absent_pids() {
    const plat::ProcessId self = plat::current_process_id();
    assert(self != 0);

    const auto name = plat::process_name(self);
    assert(name.has_value() && "must be able to name our own process");
    assert(!name->empty());
    assert(name->find('/') == std::string::npos && "name must not carry a path");
    assert(name->find('\\') == std::string::npos && "name must not carry a path");

    // Above every plausible pid_max, so the process cannot exist. A stale row
    // in the process table hits this path constantly.
    constexpr plat::ProcessId kImplausible = 0x7FFF'0000u;
    assert(!plat::process_name(kImplausible).has_value());

    std::printf("  self                  -> pid %u, %s\n",
                static_cast<unsigned>(self), name->c_str());
}

// Nothing here signals a process that exists. The only pid used is one that
// cannot, which is what makes the error mapping testable without risk.
void terminate_reports_why_it_failed() {
    constexpr plat::ProcessId kImplausible = 0x7FFF'0000u;

    const auto forceful = plat::terminate_process(kImplausible, plat::TerminateMode::kForceful);
    assert(forceful == plat::TerminateStatus::kNoSuchProcess &&
           "a vanished process must be distinguishable from a permission failure");

    // pid 0 is the process group on POSIX and the idle process on Windows.
    // Neither is ever what a user selecting one table row meant.
    assert(plat::terminate_process(0, plat::TerminateMode::kForceful) !=
           plat::TerminateStatus::kOk);

    // The asymmetry the UI has to respect: Windows has no SIGTERM, and the
    // interface reports that rather than pretending otherwise.
    if (plat::supports_graceful_terminate()) {
        assert(plat::terminate_process(kImplausible, plat::TerminateMode::kGraceful) ==
               plat::TerminateStatus::kNoSuchProcess);
    } else {
        assert(plat::terminate_process(kImplausible, plat::TerminateMode::kGraceful) ==
               plat::TerminateStatus::kUnsupportedMode);
    }

    std::printf("  graceful terminate    -> %s\n",
                plat::supports_graceful_terminate() ? "supported" : "unsupported (forceful only)");
}

// -- TerminalSession ---------------------------------------------------------

// Capabilities depend on the environment, so there is nothing to assert about
// their values. What is asserted is that setup and teardown survive a CI runner
// with no console attached at all.
void terminal_setup_survives_a_headless_runner() {
    {
        const plat::TerminalSession session = plat::TerminalSession::initialise();
        const plat::TerminalCapabilities& caps = session.capabilities();
        std::printf("  terminal              -> tty=%d truecolor=%d utf8=%d no_color=%d\n",
                    caps.is_tty, caps.truecolor, caps.utf8, caps.color_disabled);
    }

    // Restoring twice through a move must not put back console state twice.
    plat::TerminalSession first = plat::TerminalSession::initialise();
    plat::TerminalSession second = std::move(first);
    first = plat::TerminalSession::initialise();
    static_cast<void>(second.capabilities());
}

}  // namespace

int main() {
    std::puts("platform layer:");

    absent_library_is_not_an_error();
    candidate_list_falls_through_to_the_first_hit();
    present_symbol_resolves_and_absent_symbol_returns_null();
    ownership_transfers_exactly_once();
    system_info_identifies_the_host();
    process_lookup_handles_present_and_absent_pids();
    terminate_reports_why_it_failed();
    terminal_setup_survives_a_headless_runner();

    std::puts("platform: ok");
    return 0;
}
