// JSON serialisation. See json_export.hpp for the output contract.

#include "core/json_export.hpp"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "core/version.hpp"

namespace gtop::core {

namespace {

constexpr std::array<char, 16> kHexDigits{'0', '1', '2', '3', '4', '5', '6', '7',
                                          '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

// Minimal pretty-printer. Enough for one fixed document shape, and small
// enough that it does not justify a dependency — gtop's whole premise is a
// binary that links almost nothing.
class JsonWriter {
public:
    explicit JsonWriter(std::string& out) noexcept : out_(out) {}

    void begin_object(std::string_view key = {}) {
        item(key);
        out_ += '{';
        open_.push_back(true);
    }

    void begin_array(std::string_view key = {}) {
        item(key);
        out_ += '[';
        open_.push_back(true);
    }

    void end_object() { close('}'); }
    void end_array() { close(']'); }

    void string(std::string_view key, std::string_view value) {
        item(key);
        quote(value);
    }

    void boolean(std::string_view key, bool value) {
        item(key);
        out_ += value ? "true" : "false";
    }

    // The optional overloads are the point of this class: an unset metric
    // writes null. There is deliberately no overload that takes a bare number
    // for a dynamic metric, so a caller cannot accidentally substitute 0.
    void number(std::string_view key, const std::optional<std::uint64_t>& value) {
        item(key);
        if (value.has_value()) {
            out_ += std::to_string(*value);
        } else {
            out_ += "null";
        }
    }

    void number(std::string_view key, const std::optional<std::uint32_t>& value) {
        number(key, value.has_value() ? std::optional<std::uint64_t>{*value} : std::nullopt);
    }

    void number(std::string_view key, const std::optional<float>& value) {
        item(key);
        if (!value.has_value()) {
            out_ += "null";
            return;
        }
        // to_chars rather than to_string: no locale, so a machine set to
        // de_DE cannot emit "12,5" and hand somebody's CI a parse error.
        std::array<char, 32> buffer{};
        const std::to_chars_result result = std::to_chars(
            buffer.data(), buffer.data() + buffer.size(), *value, std::chars_format::fixed, 1);
        if (result.ec == std::errc{}) {
            out_.append(buffer.data(), static_cast<std::size_t>(result.ptr - buffer.data()));
        } else {
            out_ += "null";
        }
    }

    void integer(std::string_view key, std::uint64_t value) {
        item(key);
        out_ += std::to_string(value);
    }

private:
    void item(std::string_view key) {
        if (!open_.empty()) {
            if (open_.back()) {
                open_.back() = false;
            } else {
                out_ += ',';
            }
            out_ += '\n';
            out_.append(2 * open_.size(), ' ');
        }
        if (!key.empty()) {
            quote(key);
            out_ += ": ";
        }
    }

    void close(char bracket) {
        const bool was_empty = open_.back();
        open_.pop_back();
        if (!was_empty) {
            out_ += '\n';
            out_.append(2 * open_.size(), ' ');
        }
        out_ += bracket;
    }

    void quote(std::string_view text) {
        out_ += '"';
        for (const char c : text) {
            const auto byte = static_cast<unsigned char>(c);
            switch (c) {
                case '"':
                    out_ += "\\\"";
                    break;
                case '\\':
                    out_ += "\\\\";
                    break;
                case '\n':
                    out_ += "\\n";
                    break;
                case '\r':
                    out_ += "\\r";
                    break;
                case '\t':
                    out_ += "\\t";
                    break;
                default:
                    if (byte < 0x20) {
                        out_ += "\\u00";
                        out_ += kHexDigits[(byte >> 4) & 0x0F];
                        out_ += kHexDigits[byte & 0x0F];
                    } else {
                        // Bytes above 0x7F pass through. Process names come
                        // from the OS as UTF-8 on both platforms — the Win32
                        // side converts from UTF-16 in platform/win32.
                        out_ += c;
                    }
                    break;
            }
        }
        out_ += '"';
    }

    std::string& out_;
    std::vector<bool> open_;  // one "still empty?" flag per open container
};

void write_processes(JsonWriter& json, const std::vector<ProcessInfo>& processes) {
    json.begin_array("processes");
    for (const ProcessInfo& process : processes) {
        json.begin_object();
        json.integer("pid", process.pid);
        json.string("name", process.name);
        json.number("vram_bytes", process.vram_bytes);
        json.number("gpu_util_percent", process.gpu_util_percent);
        json.begin_array("engines");
        if (process.engines.graphics) {
            json.string({}, "graphics");
        }
        if (process.engines.compute) {
            json.string({}, "compute");
        }
        if (process.engines.encode) {
            json.string({}, "encode");
        }
        if (process.engines.decode) {
            json.string({}, "decode");
        }
        if (process.engines.copy) {
            json.string({}, "copy");
        }
        json.end_array();
        json.end_object();
    }
    json.end_array();
}

void write_sample(JsonWriter& json, const DeviceSample& sample) {
    json.begin_object("sample");

    // steady_clock, so this is monotonic time since an unspecified epoch, not
    // a wall clock. It is here to measure intervals between dumps; it is not a
    // date and must not be treated as one.
    json.integer("monotonic_ms",
                 static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                sample.timestamp.time_since_epoch())
                                                .count()));
    json.string("power_state", to_string(sample.power_state));

    json.number("core_util", sample.core_util);
    json.number("mem_controller_util", sample.mem_controller_util);
    json.number("encoder_util", sample.encoder_util);
    json.number("decoder_util", sample.decoder_util);

    json.number("vram_used_bytes", sample.vram_used_bytes);

    json.number("temp_edge_c", sample.temp_edge_c);
    json.number("temp_hotspot_c", sample.temp_hotspot_c);
    json.number("temp_mem_c", sample.temp_mem_c);

    json.number("power_draw_mw", sample.power_draw_mw);
    json.number("power_limit_mw", sample.power_limit_mw);

    json.number("fan_percent", sample.fan_percent);
    json.number("clock_core_mhz", sample.clock_core_mhz);
    json.number("clock_mem_mhz", sample.clock_mem_mhz);

    json.number("pcie_rx_bps", sample.pcie_rx_bps);
    json.number("pcie_tx_bps", sample.pcie_tx_bps);

    json.begin_object("throttle");
    json.boolean("thermal", sample.throttle.thermal);
    json.boolean("power", sample.throttle.power);
    json.boolean("reliability", sample.throttle.reliability);
    json.boolean("software", sample.throttle.software);
    json.end_object();

    write_processes(json, sample.processes);
    json.end_object();
}

}  // namespace

std::string to_json(const std::vector<DeviceReading>& devices, const DumpMetadata& metadata) {
    std::string out;
    out.reserve(1024 + devices.size() * 1024);

    JsonWriter json(out);
    json.begin_object();
    json.string("gtop", kVersion);
    json.string("host", metadata.host);
    json.string("os", metadata.os);

    json.begin_array("devices");
    for (const DeviceReading& reading : devices) {
        json.begin_object();
        json.string("backend", reading.backend);
        json.string("vendor", to_string(reading.info.vendor));
        json.string("name", reading.info.name);
        json.string("uuid", reading.info.uuid);
        json.string("pci_bus_id", reading.info.pci_bus_id);
        json.string("driver_version", reading.info.driver_version);
        json.number("vram_total_bytes", reading.info.vram_total_bytes);
        json.number("power_limit_mw", reading.info.power_limit_mw);
        json.number("temp_slowdown_c", reading.info.temp_slowdown_c);
        write_sample(json, reading.sample);
        json.end_object();
    }
    json.end_array();

    json.end_object();
    out += '\n';
    return out;
}

}  // namespace gtop::core
