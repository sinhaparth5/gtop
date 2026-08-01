// Vendor-neutral helpers for the core vocabulary types.

#include "core/types.hpp"

#include <cctype>
#include <cstddef>

namespace gtop::core {

namespace {

char lower(char c) noexcept {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

bool is_hex(char c) noexcept {
    return std::isxdigit(static_cast<unsigned char>(c)) != 0;
}

// One "bb" / "dd" / "f" field: hex digits, nothing else.
bool all_hex(std::string_view field) noexcept {
    if (field.empty()) {
        return false;
    }
    for (const char c : field) {
        if (!is_hex(c)) {
            return false;
        }
    }
    return true;
}

std::string lowercased(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());
    for (const char c : raw) {
        out.push_back(lower(c));
    }
    return out;
}

}  // namespace

std::string_view to_string(Vendor vendor) noexcept {
    switch (vendor) {
        case Vendor::kNvidia:
            return "NVIDIA";
        case Vendor::kAmd:
            return "AMD";
        case Vendor::kIntel:
            return "Intel";
        case Vendor::kUnknown:
            break;
    }
    return "Unknown";
}

std::string_view to_string(PowerState state) noexcept {
    switch (state) {
        case PowerState::kActive:
            return "active";
        case PowerState::kSuspended:
            return "suspended";
        case PowerState::kUnknown:
            break;
    }
    return "unknown";
}

std::string normalise_pci_bus_id(std::string_view raw) {
    // Trim, because sysfs reads arrive with a trailing newline often enough to
    // matter and a stray '\n' would make two spellings of one card differ.
    while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.front())) != 0) {
        raw.remove_prefix(1);
    }
    while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.back())) != 0) {
        raw.remove_suffix(1);
    }
    if (raw.empty()) {
        return {};
    }

    // Split on ':' — either "domain:bus:device.function" or "bus:device.function".
    const std::size_t first = raw.find(':');
    if (first == std::string_view::npos) {
        return lowercased(raw);
    }
    const std::size_t second = raw.find(':', first + 1);

    std::string_view domain = "0000";
    std::string_view bus;
    std::string_view slot;
    if (second == std::string_view::npos) {
        bus = raw.substr(0, first);
        slot = raw.substr(first + 1);
    } else {
        domain = raw.substr(0, first);
        bus = raw.substr(first + 1, second - first - 1);
        slot = raw.substr(second + 1);
    }

    const std::size_t dot = slot.find('.');
    if (dot == std::string_view::npos) {
        return lowercased(raw);
    }
    const std::string_view device = slot.substr(0, dot);
    const std::string_view function = slot.substr(dot + 1);

    if (!all_hex(domain) || !all_hex(bus) || !all_hex(device) || !all_hex(function)) {
        return lowercased(raw);
    }

    // NVML pads the domain to eight digits; sysfs uses four. Strip leading
    // zeros, then pad back to four, so both spellings converge.
    while (domain.size() > 1 && domain.front() == '0') {
        domain.remove_prefix(1);
    }
    if (domain.size() > 4) {
        return lowercased(raw);
    }

    std::string out;
    out.reserve(13);
    out.append(4 - domain.size(), '0');
    out += lowercased(domain);
    out.push_back(':');
    if (bus.size() == 1) {
        out.push_back('0');
    } else if (bus.size() != 2) {
        return lowercased(raw);
    }
    out += lowercased(bus);
    out.push_back(':');
    if (device.size() == 1) {
        out.push_back('0');
    } else if (device.size() != 2) {
        return lowercased(raw);
    }
    out += lowercased(device);
    out.push_back('.');
    if (function.size() != 1) {
        return lowercased(raw);
    }
    out += lowercased(function);
    return out;
}

}  // namespace gtop::core
