// gtop — entry point.
//
// Phase 2 of ROADMAP.md: argument parsing, the driver registry, and
// --dump-json. There are still no vendor backends, so builtin_backends() is
// empty and the registry legitimately finds nothing — which is the same code
// path as a machine with no GPU, and exercising it now is the point.
//
// Phase 6 replaces the kRun branch with the FTXUI application.

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "core/config.hpp"
#include "core/json_export.hpp"
#include "core/types.hpp"
#include "core/version.hpp"
#include "driver/driver_registry.hpp"
#include "platform/platform.hpp"

namespace {

constexpr int kExitOk = 0;
constexpr int kExitUsage = 2;

const char* yes_no(bool value) { return value ? "yes" : "no"; }

std::string describe_os(const gtop::platform::SystemInfo& host) {
    std::string text = host.os_name;
    if (!host.os_version.empty()) {
        text += ' ';
        text += host.os_version;
    }
    return text;
}

// Headless. No terminal is touched, no colour is emitted, and the exit status
// is 0 even with zero devices — a CI runner without a GPU still gets valid
// JSON with an empty array, which is a result rather than a failure.
int dump_json(gtop::driver::DriverRegistry& registry) {
    const gtop::platform::SystemInfo host = gtop::platform::query_system_info();

    registry.probe();
    const std::vector<gtop::core::DeviceReading> devices = registry.sample_all();

    const gtop::core::DumpMetadata metadata{host.hostname, describe_os(host)};
    const std::string json = gtop::core::to_json(devices, metadata);
    std::fwrite(json.data(), 1, json.size(), stdout);
    return kExitOk;
}

int run_interactive(gtop::driver::DriverRegistry& registry,
                    std::chrono::milliseconds interval) {
    const gtop::platform::SystemInfo host = gtop::platform::query_system_info();
    const gtop::platform::TerminalSession terminal =
        gtop::platform::TerminalSession::initialise();
    const gtop::platform::TerminalCapabilities& caps = terminal.capabilities();

    std::printf("gtop %.*s\n\n", static_cast<int>(gtop::core::kVersion.size()),
                gtop::core::kVersion.data());
    std::printf("  host        %s (%s)\n",
                host.hostname.empty() ? "unnamed" : host.hostname.c_str(),
                describe_os(host).c_str());
    std::printf("  terminal    tty %s, truecolor %s, utf-8 %s\n", yes_no(caps.is_tty),
                yes_no(caps.truecolor), yes_no(caps.utf8));
    if (caps.color_disabled) {
        std::puts("              NO_COLOR is set — colour output will be suppressed");
    }
    std::printf("  interval    %lld ms\n", static_cast<long long>(interval.count()));
    std::puts("");

    registry.probe();
    if (registry.empty()) {
        // Not an error. A laptop with the discrete GPU powered down, a
        // container with no /dev/dri, a machine with no drivers installed —
        // all of them land here, and none of them is a reason to fail.
        std::puts("No supported GPU found.");
        std::puts("");
        std::puts("gtop loads NVML, ADLX, ROCm SMI and Level Zero at runtime and uses");
        std::puts("whichever are present. None of them answered on this machine.");
        return kExitOk;
    }

    const std::vector<gtop::core::DeviceReading> devices = registry.sample_all();
    for (std::size_t i = 0; i < devices.size(); ++i) {
        const gtop::core::DeviceReading& device = devices[i];
        const std::string_view state = gtop::core::to_string(device.sample.power_state);
        std::printf("  gpu %zu       %s [%s] via %.*s — %.*s\n", i, device.info.name.c_str(),
                    device.info.pci_bus_id.c_str(), static_cast<int>(device.backend.size()),
                    device.backend.data(), static_cast<int>(state.size()), state.data());
    }
    std::puts("");
    std::puts("No vendor backend is implemented yet, so nothing above is telemetry.");
    std::puts("See ROADMAP.md Phase 3; run with --dump-json for the machine-readable form.");
    return kExitOk;
}

}  // namespace

int main(int argc, char** argv) {
    const gtop::core::ParseResult parsed = gtop::core::parse_arguments(argc, argv);
    if (!parsed.ok()) {
        std::fprintf(stderr, "gtop: %s\n", parsed.error->c_str());
        return kExitUsage;
    }

    const gtop::core::Config& config = parsed.config;

    switch (config.mode) {
        case gtop::core::Config::Mode::kHelp: {
            const std::string_view usage = gtop::core::usage_text();
            std::fwrite(usage.data(), 1, usage.size(), stdout);
            return kExitOk;
        }
        case gtop::core::Config::Mode::kVersion:
            std::printf("gtop %.*s\n", static_cast<int>(gtop::core::kVersion.size()),
                        gtop::core::kVersion.data());
            return kExitOk;
        case gtop::core::Config::Mode::kDumpJson: {
            gtop::driver::DriverRegistry registry;
            return dump_json(registry);
        }
        case gtop::core::Config::Mode::kRun:
            break;
    }

    gtop::driver::DriverRegistry registry;
    return run_interactive(registry, config.interval);
}
