// PCI runtime power state — Linux (sysfs runtime PM).
//
// /sys/bus/pci/devices/<addr>/power/runtime_status is maintained by the kernel
// PM core. Reading it is a sysfs attribute read against state the kernel
// already holds; it does not touch the device, which is the whole requirement.

#include "platform/pci_power.hpp"

#include <array>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <string>
#include <string_view>
#include <system_error>

namespace gtop::platform {
namespace {

// sysfs names devices with a four-digit domain. NVML hands out eight and lspci
// hands out none, so both are reshaped here rather than at every call site.
[[nodiscard]] std::string to_sysfs_address(std::string_view raw) {
    std::size_t first = raw.find(':');
    if (first == std::string_view::npos) {
        return {};
    }

    std::string_view domain = "0000";
    const std::size_t second = raw.find(':', first + 1);
    if (second != std::string_view::npos) {
        domain = raw.substr(0, first);
        first = second;
    }

    const std::string_view rest = raw.substr(first + 1);  // "bb:dd.f" or "dd.f"
    if (rest.empty() || rest.size() > 16) {
        return {};
    }

    unsigned domain_value = 0;
    const char* begin = domain.data();
    const char* end = begin + domain.size();
    const std::from_chars_result parsed = std::from_chars(begin, end, domain_value, 16);
    if (parsed.ec != std::errc{} || parsed.ptr != end) {
        return {};
    }
    if (domain_value > 0xFFFFU) {
        return {};  // eight significant digits: not an address sysfs can name
    }

    // Reject anything that could escape the directory. Only hex, ':' and '.'
    // ever appear in a PCI address, and a path separator must never reach the
    // filesystem call below.
    for (const char c : rest) {
        const bool allowed = (std::isxdigit(static_cast<unsigned char>(c)) != 0) || c == ':' ||
                             c == '.';
        if (!allowed) {
            return {};
        }
    }

    std::array<char, 8> prefix{};
    std::snprintf(prefix.data(), prefix.size(), "%04x", domain_value);

    std::string address(prefix.data());
    address += ':';
    address += rest;
    for (char& c : address) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return address;
}

}  // namespace

DevicePowerState pci_power_state(std::string_view pci_bus_id) noexcept {
    try {
        const std::string address = to_sysfs_address(pci_bus_id);
        if (address.empty()) {
            return DevicePowerState::kUnknown;
        }

        const std::string path =
            "/sys/bus/pci/devices/" + address + "/power/runtime_status";

        // stdio rather than ifstream: this runs once per device per tick, and a
        // sysfs attribute is a short, single read that cannot be seeked or
        // sized. FILE* also keeps the whole function allocation-light.
        std::FILE* file = std::fopen(path.c_str(), "rb");
        if (file == nullptr) {
            // No runtime PM on this device, or no such device. Either way the
            // caller should sample normally.
            return DevicePowerState::kUnknown;
        }

        std::array<char, 32> buffer{};
        const std::size_t read = std::fread(buffer.data(), 1, buffer.size() - 1, file);
        std::fclose(file);

        std::string_view status(buffer.data(), read);
        while (!status.empty() &&
               std::isspace(static_cast<unsigned char>(status.back())) != 0) {
            status.remove_suffix(1);
        }

        if (status == "suspended") {
            return DevicePowerState::kSuspended;
        }
        if (status == "active") {
            return DevicePowerState::kActive;
        }
        // "suspending" and "resuming" are transitional. Reporting either as a
        // settled state would make the sampler act on a value that has already
        // changed, so they read as kUnknown and resolve on the next tick.
        return DevicePowerState::kUnknown;
    } catch (...) {
        return DevicePowerState::kUnknown;
    }
}

}  // namespace gtop::platform
