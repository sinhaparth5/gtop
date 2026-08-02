// DRM sysfs reading. See drm_sysfs.hpp for the contract.

#include "driver/sysfs/drm_sysfs.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "core/types.hpp"

namespace gtop::driver::sysfs {
namespace {

constexpr const char* kDrmRoot = "/sys/class/drm";

// pci.ids lives in one of two places depending on how the distribution splits
// hwdata. Both are checked; neither existing is not an error.
constexpr std::array<const char*, 3> kPciIdsPaths{
    "/usr/share/hwdata/pci.ids",
    "/usr/share/misc/pci.ids",
    "/usr/share/pci.ids",
};

[[nodiscard]] std::string_view trim(std::string_view text) noexcept {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return text;
}

// "card1" yes, "card1-HDMI-A-1" no, "renderD128" no.
[[nodiscard]] bool is_card_node(std::string_view name) noexcept {
    constexpr std::string_view kPrefix = "card";
    if (!name.starts_with(kPrefix)) {
        return false;
    }
    const std::string_view digits = name.substr(kPrefix.size());
    return !digits.empty() && std::all_of(digits.begin(), digits.end(), [](char c) {
        return std::isdigit(static_cast<unsigned char>(c)) != 0;
    });
}

// sysfs writes IDs as "0x8086\n".
[[nodiscard]] std::optional<std::uint32_t> read_hex_id(const std::string& path) {
    const std::optional<std::string> text = read_text(path);
    if (!text) {
        return std::nullopt;
    }

    std::string_view value = trim(*text);
    if (value.starts_with("0x") || value.starts_with("0X")) {
        value.remove_prefix(2);
    }

    std::uint32_t parsed = 0;
    const char* begin = value.data();
    const char* end = begin + value.size();
    const std::from_chars_result result = std::from_chars(begin, end, parsed, 16);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    return parsed;
}

}  // namespace

std::optional<std::string> read_text(std::string_view path) {
    try {
        // Sysfs attributes have no meaningful size, so they cannot be sized and
        // read in one go the way a regular file can. Reading to EOF is the only
        // correct approach, and every attribute here is a handful of bytes.
        std::ifstream file{std::string(path)};
        if (!file) {
            return std::nullopt;
        }

        std::string contents((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
        if (file.bad()) {
            return std::nullopt;
        }

        const std::string_view trimmed = trim(contents);
        return std::string(trimmed);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::uint64_t> read_u64(std::string_view path) {
    const std::optional<std::string> text = read_text(path);
    if (!text) {
        return std::nullopt;
    }

    const std::string_view value = trim(*text);
    std::uint64_t parsed = 0;
    const char* begin = value.data();
    const char* end = begin + value.size();
    const std::from_chars_result result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    return parsed;
}

std::optional<std::int64_t> read_i64(std::string_view path) {
    const std::optional<std::string> text = read_text(path);
    if (!text) {
        return std::nullopt;
    }

    const std::string_view value = trim(*text);
    std::int64_t parsed = 0;
    const char* begin = value.data();
    const char* end = begin + value.size();
    const std::from_chars_result result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    return parsed;
}

std::string find_hwmon(std::string_view device_path) {
    try {
        const std::filesystem::path root = std::filesystem::path(device_path) / "hwmon";
        std::error_code ec;
        if (!std::filesystem::is_directory(root, ec)) {
            return {};
        }

        for (const std::filesystem::directory_entry& entry :
             std::filesystem::directory_iterator(root, ec)) {
            if (entry.path().filename().string().starts_with("hwmon")) {
                return entry.path().string();
            }
        }
    } catch (...) {
        // A device unbound mid-iteration. No hwmon is a valid answer.
    }
    return {};
}

HwmonSample read_hwmon(std::string_view hwmon_path) {
    HwmonSample out;
    if (hwmon_path.empty()) {
        return out;
    }

    const std::string base(hwmon_path);

    // Sensor index is not a reliable name. amdgpu happens to order them edge,
    // junction, memory, but the kernel only promises that tempN_label says what
    // tempN measures — so the label decides, and the index is the fallback for
    // nodes that publish no labels at all (i915 among them).
    for (int index = 1; index <= 3; ++index) {
        const std::string prefix = base + "/temp" + std::to_string(index);
        const std::optional<std::int64_t> millidegrees = read_i64(prefix + "_input");
        if (!millidegrees || *millidegrees < 0) {
            continue;
        }
        const auto celsius = static_cast<std::uint32_t>(*millidegrees / 1000);

        const std::optional<std::string> label = read_text(prefix + "_label");
        if (label) {
            if (*label == "junction" || *label == "hotspot") {
                out.temp_hotspot_c = celsius;
                continue;
            }
            if (*label == "mem" || *label == "memory" || *label == "vram") {
                out.temp_mem_c = celsius;
                continue;
            }
            if (*label == "edge") {
                out.temp_edge_c = celsius;
                continue;
            }
        }

        switch (index) {
            case 1:
                out.temp_edge_c = celsius;
                break;
            case 2:
                out.temp_hotspot_c = celsius;
                break;
            default:
                out.temp_mem_c = celsius;
                break;
        }
    }

    // power1_average is a windowed mean and power1_input an instantaneous
    // reading; different drivers publish one, the other, or both. Average is
    // preferred because it is what a power *limit* is enforced against, so
    // plotting the two together compares like with like.
    std::optional<std::uint64_t> microwatts = read_u64(base + "/power1_average");
    if (!microwatts) {
        microwatts = read_u64(base + "/power1_input");
    }
    if (microwatts) {
        out.power_draw_mw = static_cast<std::uint32_t>(*microwatts / 1000);
    }

    if (const std::optional<std::uint64_t> cap = read_u64(base + "/power1_cap")) {
        out.power_limit_mw = static_cast<std::uint32_t>(*cap / 1000);
    }

    // pwm1 is the duty cycle the driver is asking for, 0-255. fan1_input is RPM,
    // which cannot be turned into a percentage without knowing the maximum, so
    // it is deliberately not used as a substitute here.
    if (const std::optional<std::uint64_t> pwm = read_u64(base + "/pwm1")) {
        constexpr std::uint64_t kPwmMax = 255;
        out.fan_percent = static_cast<std::uint32_t>(std::min(*pwm, kPwmMax) * 100 / kPwmMax);
    }

    return out;
}

std::optional<std::uint64_t> parse_active_dpm_level(std::string_view contents) {
    std::size_t line_start = 0;
    while (line_start <= contents.size()) {
        const std::size_t line_end = contents.find('\n', line_start);
        const std::string_view line = contents.substr(
            line_start,
            line_end == std::string_view::npos ? std::string_view::npos : line_end - line_start);

        if (line.find('*') != std::string_view::npos) {
            // "1: 1100Mhz *" — the number after the colon, unit suffix ignored.
            const std::size_t colon = line.find(':');
            if (colon != std::string_view::npos) {
                const std::string_view rest = trim(line.substr(colon + 1));
                std::uint64_t value = 0;
                const char* begin = rest.data();
                const std::from_chars_result result =
                    std::from_chars(begin, begin + rest.size(), value);
                if (result.ec == std::errc{}) {
                    return value;
                }
            }
        }

        if (line_end == std::string_view::npos) {
            break;
        }
        line_start = line_end + 1;
    }
    return std::nullopt;
}

PciNames lookup_pci_names(std::uint32_t vendor_id, std::uint32_t device_id) {
    PciNames names;

    std::array<char, 8> vendor_key{};
    std::array<char, 8> device_key{};
    std::snprintf(vendor_key.data(), vendor_key.size(), "%04x", vendor_id);
    std::snprintf(device_key.data(), device_key.size(), "%04x", device_id);

    try {
        for (const char* path : kPciIdsPaths) {
            std::ifstream file{path};
            if (!file) {
                continue;
            }

            // pci.ids is a sorted flat file: vendors at column 0, their devices
            // indented one tab beneath. Scanning it line by line and stopping at
            // the match reads a few hundred KB at worst, once per device at
            // probe time, and avoids taking a dependency on libpci to format a
            // string.
            bool in_vendor = false;
            std::string line;
            while (std::getline(file, line)) {
                if (line.empty() || line[0] == '#') {
                    continue;
                }

                if (line[0] != '\t') {
                    if (in_vendor) {
                        return names;  // left our vendor's block; the device is unlisted
                    }
                    if (line.starts_with(std::string_view(vendor_key.data()))) {
                        in_vendor = true;
                        names.vendor = std::string(trim(std::string_view(line).substr(4)));
                    }
                    continue;
                }

                if (!in_vendor || line[1] == '\t') {
                    continue;  // a subsystem line, or a device under another vendor
                }
                if (line.compare(1, 4, device_key.data(), 4) == 0) {
                    names.device = std::string(trim(std::string_view(line).substr(5)));
                    return names;
                }
            }
            return names;
        }
    } catch (...) {
        // A truncated or unreadable database yields hex names. Cosmetic.
    }
    return names;
}

std::vector<DrmDevice> enumerate_drm_devices() {
    std::vector<DrmDevice> devices;

    try {
        std::error_code ec;
        if (!std::filesystem::is_directory(kDrmRoot, ec)) {
            return devices;  // no DRM subsystem: a container, or a very odd kernel
        }

        for (const std::filesystem::directory_entry& entry :
             std::filesystem::directory_iterator(kDrmRoot, ec)) {
            const std::string name = entry.path().filename().string();
            if (!is_card_node(name)) {
                continue;
            }

            DrmDevice device;
            device.card = name;
            device.node_path = entry.path().string();
            device.device_path = (entry.path() / "device").string();

            // Both of these are symlinks into the PCI and driver trees; the
            // leaf of the resolved path is the answer in each case.
            const std::filesystem::path driver_link =
                std::filesystem::read_symlink(std::filesystem::path(device.device_path) / "driver",
                                              ec);
            if (ec) {
                continue;  // a device with no bound driver has nothing to report
            }
            device.driver = driver_link.filename().string();

            const std::filesystem::path device_link =
                std::filesystem::read_symlink(entry.path() / "device", ec);
            if (ec) {
                continue;
            }
            device.pci_bus_id = core::normalise_pci_bus_id(device_link.filename().string());
            if (device.pci_bus_id.empty()) {
                continue;
            }

            device.vendor_id = read_hex_id(device.device_path + "/vendor").value_or(0);
            device.device_id = read_hex_id(device.device_path + "/device").value_or(0);

            devices.push_back(std::move(device));
        }
    } catch (...) {
        // Enumeration is best effort. Whatever was collected before the throw
        // is still valid and still worth returning.
    }

    std::sort(devices.begin(), devices.end(), [](const DrmDevice& a, const DrmDevice& b) {
        return a.pci_bus_id < b.pci_bus_id;
    });
    return devices;
}

}  // namespace gtop::driver::sysfs
