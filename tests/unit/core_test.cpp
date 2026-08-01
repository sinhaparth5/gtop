// Core layer: argument parsing and the --dump-json contract.
//
// The JSON checks are deliberately about the *shape* of the output rather than
// the values. Scripts parse this — the Phase 3 exit criterion is comparing it
// field by field against nvidia-smi and rocm-smi — so a renamed key or a
// missing metric silently breaking somebody's comparison is the failure mode
// worth guarding. ROADMAP.md tasks 2.1 and 2.2.

#include <cassert>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "core/config.hpp"
#include "core/json_export.hpp"
#include "core/types.hpp"

namespace core = gtop::core;

namespace {

bool contains(std::string_view haystack, std::string_view needle) {
    return haystack.find(needle) != std::string_view::npos;
}

core::ParseResult parse(std::vector<const char*> args) {
    args.insert(args.begin(), "gtop");
    return core::parse_arguments(static_cast<int>(args.size()), args.data());
}

// -- configuration ------------------------------------------------------------

void test_defaults() {
    std::puts("argument defaults");

    const core::ParseResult result = parse({});
    assert(result.ok());
    assert(result.config.mode == core::Config::Mode::kRun);
    assert(result.config.interval == std::chrono::milliseconds{1000});
}

void test_modes() {
    std::puts("modes");

    assert(parse({"--dump-json"}).config.mode == core::Config::Mode::kDumpJson);
    assert(parse({"-j"}).config.mode == core::Config::Mode::kDumpJson);
    assert(parse({"--help"}).config.mode == core::Config::Mode::kHelp);
    assert(parse({"-h"}).config.mode == core::Config::Mode::kHelp);
    assert(parse({"--version"}).config.mode == core::Config::Mode::kVersion);
    assert(parse({"-V"}).config.mode == core::Config::Mode::kVersion);

    // --help wins over whatever follows it, and does not have to parse.
    const core::ParseResult help = parse({"--help", "--nonsense"});
    assert(help.ok());
    assert(help.config.mode == core::Config::Mode::kHelp);
}

void test_interval() {
    std::puts("interval");

    for (const char* form : {"--interval", "-i"}) {
        const core::ParseResult separate = parse({form, "250"});
        assert(separate.ok());
        assert(separate.config.interval == std::chrono::milliseconds{250});
    }

    const core::ParseResult inline_form = parse({"--interval=100"});
    assert(inline_form.ok());
    assert(inline_form.config.interval == std::chrono::milliseconds{100});
}

void test_bad_arguments_are_rejected() {
    std::puts("argument errors");

    // Silently sampling at a different rate than the caller asked for is worse
    // than refusing to start, because a script never finds out.
    assert(!parse({"--interval"}).ok());
    assert(!parse({"--interval", "soon"}).ok());
    assert(!parse({"--interval", "0"}).ok());
    assert(!parse({"--interval", "999999"}).ok());
    assert(!parse({"--interval", "10.5"}).ok());
    assert(!parse({"--dump-jsonn"}).ok());
    assert(!parse({"-x"}).ok());

    const core::ParseResult unknown = parse({"--colour"});
    assert(!unknown.ok());
    assert(contains(*unknown.error, "--colour"));

    // The bounds are inclusive.
    assert(parse({"--interval", "50"}).ok());
    assert(parse({"--interval", "60000"}).ok());
}

void test_usage_mentions_every_flag() {
    std::puts("usage text");

    const std::string_view usage = core::usage_text();
    for (const char* flag : {"--dump-json", "--interval", "--help", "--version"}) {
        assert(contains(usage, flag));
    }
}

// -- JSON ---------------------------------------------------------------------

core::DeviceReading sparse_reading() {
    core::DeviceReading reading;
    reading.backend = "mock";
    reading.info.vendor = core::Vendor::kIntel;
    reading.info.name = "Iris Xe";
    reading.info.pci_bus_id = "0000:00:02.0";
    reading.sample.power_state = core::PowerState::kActive;
    reading.sample.core_util = 12.5F;
    // Everything else stays unset — this is the Iris Xe, whose hwmon directory
    // is empty, so it genuinely has no temperature to report.
    return reading;
}

void test_absent_metrics_are_null() {
    std::puts("absent metrics");

    const std::string json = core::to_json({sparse_reading()});

    // The whole reason every metric is optional. A sensor that cannot be read
    // must never come back as a plausible-looking zero.
    assert(contains(json, "\"temp_edge_c\": null"));
    assert(contains(json, "\"power_draw_mw\": null"));
    assert(contains(json, "\"fan_percent\": null"));
    assert(!contains(json, "\"temp_edge_c\": 0"));

    assert(contains(json, "\"core_util\": 12.5"));
    assert(contains(json, "\"power_state\": \"active\""));
    assert(contains(json, "\"vendor\": \"Intel\""));
    assert(contains(json, "\"backend\": \"mock\""));
}

void test_key_names_are_stable() {
    std::puts("json keys");

    // Renaming any of these breaks somebody's comparison script without an
    // error. Adding keys is fine; these have to keep their names.
    const std::string json = core::to_json({sparse_reading()});
    for (const char* key : {"\"gtop\"", "\"host\"", "\"os\"", "\"devices\"", "\"backend\"",
                            "\"vendor\"", "\"name\"", "\"uuid\"", "\"pci_bus_id\"",
                            "\"driver_version\"", "\"vram_total_bytes\"", "\"sample\"",
                            "\"monotonic_ms\"", "\"power_state\"", "\"core_util\"",
                            "\"mem_controller_util\"", "\"vram_used_bytes\"",
                            "\"temp_hotspot_c\"", "\"power_limit_mw\"", "\"clock_core_mhz\"",
                            "\"pcie_rx_bps\"", "\"throttle\"", "\"processes\""}) {
        assert(contains(json, key));
    }
}

void test_no_devices_is_still_valid_json() {
    std::puts("empty dump");

    // A CI runner with no GPU has to get a parseable document, not an error.
    const std::string json = core::to_json({}, core::DumpMetadata{"runner", "Linux 6.8"});
    assert(contains(json, "\"devices\": []"));
    assert(contains(json, "\"host\": \"runner\""));
    assert(json.front() == '{');
    assert(json.back() == '\n');
}

void test_strings_are_escaped() {
    std::puts("string escaping");

    core::DeviceReading reading = sparse_reading();
    reading.info.name = "GPU \"quoted\"\\ and\ttabbed";

    core::ProcessInfo process;
    process.pid = 4242;
    process.name = "line\nbreak";
    process.vram_bytes = 1073741824U;
    process.engines.compute = true;
    process.engines.encode = true;
    reading.sample.processes.push_back(process);

    const std::string json = core::to_json({reading});
    assert(contains(json, "GPU \\\"quoted\\\"\\\\ and\\ttabbed"));
    assert(contains(json, "line\\nbreak"));
    assert(!contains(json, "line\nbreak"));

    assert(contains(json, "\"pid\": 4242"));
    assert(contains(json, "\"vram_bytes\": 1073741824"));
    assert(contains(json, "\"compute\""));
    assert(contains(json, "\"encode\""));
    assert(!contains(json, "\"decode\""));
}

void test_throttle_flags_round_trip() {
    std::puts("throttle flags");

    core::DeviceReading reading = sparse_reading();
    reading.sample.throttle.thermal = true;
    assert(reading.sample.throttle.any());
    assert(reading.sample.throttle.fault());

    const std::string json = core::to_json({reading});
    assert(contains(json, "\"thermal\": true"));
    assert(contains(json, "\"power\": false"));

    // A clock cap with no fault behind it is not a badge-worthy event; idle
    // GPUs report it constantly.
    core::ThrottleFlags idle;
    idle.software = true;
    assert(idle.any());
    assert(!idle.fault());
}

}  // namespace

int main() {
    std::puts("core layer");

    test_defaults();
    test_modes();
    test_interval();
    test_bad_arguments_are_rejected();
    test_usage_mentions_every_flag();

    test_absent_metrics_are_null();
    test_key_names_are_stable();
    test_no_devices_is_still_valid_json();
    test_strings_are_escaped();
    test_throttle_flags_round_trip();

    std::puts("ok");
    return 0;
}
