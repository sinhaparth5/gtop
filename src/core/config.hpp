#pragma once
//
// Command-line configuration.
//
// Flags only for now; whether v1 also grows a config file is open question 3 in
// ROADMAP.md. Parsing is separated from acting on the result so it can be
// tested without a terminal, a GPU, or a process exit.
//
// Unrecognised arguments are an error rather than a warning. A monitor invoked
// from a script with a misspelled flag should say so, not silently sample at a
// different interval than the author asked for.
//
#include <chrono>
#include <optional>
#include <string>
#include <string_view>

namespace gtop::core {

struct Config {
    enum class Mode {
        kRun,       // the interactive TUI
        kDumpJson,  // one sample to stdout, then exit — the CI harness
        kHelp,
        kVersion,
    };

    Mode mode{Mode::kRun};

    // How often the telemetry worker samples. Vendor calls cost 2–50 ms, so
    // this is a real trade rather than a preference.
    std::chrono::milliseconds interval{1000};
};

// Smallest and largest accepted --interval. The floor is where sampling starts
// costing more CPU than the thing being measured; the ceiling is where a
// "monitor" stops being one.
inline constexpr std::chrono::milliseconds kMinInterval{50};
inline constexpr std::chrono::milliseconds kMaxInterval{60000};

struct ParseResult {
    Config config;

    // Set when the arguments were rejected. The caller prints it to stderr and
    // exits non-zero; there is nothing usable in `config` when this is set.
    std::optional<std::string> error;

    [[nodiscard]] bool ok() const noexcept { return !error.has_value(); }
};

// argv[0] is skipped if present. Never throws.
[[nodiscard]] ParseResult parse_arguments(int argc, const char* const* argv);

[[nodiscard]] std::string_view usage_text() noexcept;

}  // namespace gtop::core
