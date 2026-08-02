#pragma once
//
// The Linux DRM sysfs reader that the AMD and Intel backends share.
//
// Neither of those vendors needs a library on Linux for the baseline metrics:
// amdgpu and i915 both publish what matters as plain files under
// /sys/class/drm/cardN, readable without privilege. That makes the sysfs path
// the dependency-free default — ROCm SMI and Level Zero are enhancements layered
// on top, not prerequisites — and it is the same reason DRM fdinfo (Phase 4) is
// built once for both vendors rather than twice.
//
// Everything here returns std::optional and nothing throws. A sysfs attribute
// can vanish between two reads (the device was unbound), can exist but return
// EPERM, or can simply never have existed on this generation of hardware. All
// three are the same answer to the caller: no value, carry on.
//
// This file is compiled only on Linux — CMake selects it, so it contains no
// conditional compilation of its own.
//
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gtop::driver::sysfs {

// One DRM render node, before any vendor interpretation.
struct DrmDevice {
    std::string card;         // "card1"
    std::string node_path;    // "/sys/class/drm/card1"
    std::string device_path;  // "/sys/class/drm/card1/device"
    std::string driver;       // "amdgpu", "i915", "xe", "nvidia"
    std::string pci_bus_id;   // canonical "0000:01:00.0"

    std::uint32_t vendor_id{};  // 0x8086, 0x1002, 0x10de
    std::uint32_t device_id{};
};

// Every card node the kernel currently exposes, in card-number order.
//
// Connector nodes (card1-HDMI-A-1) are skipped; only the device itself is
// returned. A device whose driver or PCI address cannot be determined is
// skipped too — without an address it cannot be de-duplicated against the
// vendor-library backends, and a duplicate GPU is worse than a missing one.
[[nodiscard]] std::vector<DrmDevice> enumerate_drm_devices();

// Whole file, trailing newline and spaces removed. Empty optional if the file
// does not exist or cannot be read.
[[nodiscard]] std::optional<std::string> read_text(std::string_view path);

// Decimal integer from a one-line attribute. Fails rather than truncating when
// the file holds something else — a multi-line file like pp_dpm_sclk is not an
// integer, and reading its first token as one silently reports the wrong DPM
// level.
[[nodiscard]] std::optional<std::uint64_t> read_u64(std::string_view path);
[[nodiscard]] std::optional<std::int64_t> read_i64(std::string_view path);

// The hwmon instance under <device_path>/hwmon, e.g.
// "/sys/class/drm/card1/device/hwmon/hwmon4". Empty when the device exposes
// none — which is the normal case for integrated Intel graphics, and precisely
// why temperature is an optional rather than a number.
[[nodiscard]] std::string find_hwmon(std::string_view device_path);

// What a GPU's hwmon node exposes, in gtop's units.
//
// hwmon is a kernel-wide standard, so amdgpu and i915 spell these identically —
// which is the whole reason this reader is shared rather than written twice.
// Units are the hwmon ones on the way in (millidegrees, microwatts, PWM 0-255)
// and gtop's on the way out.
struct HwmonSample {
    std::optional<std::uint32_t> temp_edge_c;
    std::optional<std::uint32_t> temp_hotspot_c;
    std::optional<std::uint32_t> temp_mem_c;
    std::optional<std::uint32_t> power_draw_mw;
    std::optional<std::uint32_t> power_limit_mw;
    std::optional<std::uint32_t> fan_percent;
};

// Reads every sensor the node happens to have. An empty path, or a node with
// no sensors, yields a sample with nothing set — which is the correct reading
// for an integrated GPU with no thermal sensor, not an error.
[[nodiscard]] HwmonSample read_hwmon(std::string_view hwmon_path);

// The active entry in an amdgpu pp_dpm_* table, which looks like
//
//     0: 500Mhz
//     1: 1100Mhz *
//
// Returns the value on the line marked with '*', in whatever unit the table
// uses. Empty when nothing is marked active.
[[nodiscard]] std::optional<std::uint64_t> parse_active_dpm_level(std::string_view contents);

struct PciNames {
    std::string vendor;  // "Intel Corporation"
    std::string device;  // "TigerLake-LP GT2 [Iris Xe Graphics]"
};

// Resolves a PCI ID to human-readable names using the system pci.ids database.
//
// sysfs publishes no product name for a GPU — only the numeric IDs — so without
// this a card renders as "0x8086:0x9a49". pci.ids ships with almost every
// distribution; when it is missing, both fields come back empty and the backend
// falls back to the hex, which is ugly but true.
[[nodiscard]] PciNames lookup_pci_names(std::uint32_t vendor_id, std::uint32_t device_id);

}  // namespace gtop::driver::sysfs
