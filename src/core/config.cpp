// Command-line parsing. See config.hpp for the policy.

#include "core/config.hpp"

#include <charconv>
#include <cstddef>
#include <system_error>

namespace gtop::core {

namespace {

constexpr std::string_view kUsage =
    "gtop — a terminal monitor for GPUs\n"
    "\n"
    "Usage: gtop [options]\n"
    "\n"
    "  -j, --dump-json        Print one sample as JSON and exit. Headless: no\n"
    "                         terminal required, no colour, no redraw loop.\n"
    "  -i, --interval MS      Sampling interval in milliseconds (default 1000,\n"
    "                         range 50-60000).\n"
    "  -h, --help             Show this text and exit.\n"
    "  -V, --version          Show the version and exit.\n"
    "\n"
    "gtop links no vendor library. NVML, ADLX, ROCm SMI and Level Zero are\n"
    "loaded at runtime if they are present, so this binary starts on a machine\n"
    "with no GPU and no drivers.\n";

// Splits "--interval=100" into its two halves. Returns nullopt for a flag with
// no '=', which is the caller's cue to look at the next argument instead.
struct SplitFlag {
    std::string_view name;
    std::optional<std::string_view> inline_value;
};

SplitFlag split(std::string_view argument) {
    const std::size_t eq = argument.find('=');
    if (eq == std::string_view::npos) {
        return {argument, std::nullopt};
    }
    return {argument.substr(0, eq), argument.substr(eq + 1)};
}

std::optional<long long> to_integer(std::string_view text) {
    long long value = 0;
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const std::from_chars_result result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    return value;
}

std::string unknown_option(std::string_view argument) {
    std::string message = "unrecognised option '";
    message.append(argument);
    message += "'\nTry 'gtop --help'.";
    return message;
}

}  // namespace

std::string_view usage_text() noexcept { return kUsage; }

ParseResult parse_arguments(int argc, const char* const* argv) {
    ParseResult result;
    if (argc <= 0 || argv == nullptr) {
        return result;
    }

    for (int i = 1; i < argc; ++i) {
        if (argv[i] == nullptr) {
            continue;
        }
        const std::string_view argument{argv[i]};
        const SplitFlag flag = split(argument);

        if (flag.name == "-h" || flag.name == "--help") {
            result.config.mode = Config::Mode::kHelp;
            return result;
        }
        if (flag.name == "-V" || flag.name == "--version") {
            result.config.mode = Config::Mode::kVersion;
            return result;
        }
        if (flag.name == "-j" || flag.name == "--dump-json") {
            result.config.mode = Config::Mode::kDumpJson;
            continue;
        }
        if (flag.name == "-i" || flag.name == "--interval") {
            std::string_view value;
            if (flag.inline_value.has_value()) {
                value = *flag.inline_value;
            } else if (i + 1 < argc && argv[i + 1] != nullptr) {
                value = std::string_view{argv[++i]};
            } else {
                result.error = "option '--interval' needs a value in milliseconds";
                return result;
            }

            const std::optional<long long> parsed = to_integer(value);
            if (!parsed.has_value()) {
                result.error = "'" + std::string{value} + "' is not a number of milliseconds";
                return result;
            }
            const std::chrono::milliseconds interval{*parsed};
            if (interval < kMinInterval || interval > kMaxInterval) {
                result.error = "--interval must be between " +
                               std::to_string(kMinInterval.count()) + " and " +
                               std::to_string(kMaxInterval.count()) + " ms";
                return result;
            }
            result.config.interval = interval;
            continue;
        }

        result.error = unknown_option(argument);
        return result;
    }

    return result;
}

}  // namespace gtop::core
