// Intel telemetry from i915/xe sysfs. See intel_sysfs_driver.hpp.

#include "driver/intel/intel_sysfs_driver.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <utility>

#include "platform/pci_power.hpp"

namespace gtop::driver::intel {
namespace {

// The throttle files each hold "1" or "0". Names are stable across i915 and xe.
//
// This is the bonus the roadmap flagged: Intel is the only vendor besides NVIDIA
// that says *why* it is clocking down, and it says it in more detail than NVML
// does. PL1/PL2/PL4 are the sustained, burst and peak power limits; RATL is the
// reliability averaging temperature limit; VR_TDC and VR_THERMALERT are the
// voltage regulator's current and thermal asserts.
struct ThrottleFile {
    const char* name;
    // Which of the four core buckets it lands in.
    enum class Bucket { kThermal, kPower, kReliability } bucket;
};

constexpr std::array<ThrottleFile, 8> kThrottleFiles{{
    {"throttle_reason_thermal", ThrottleFile::Bucket::kThermal},
    {"throttle_reason_ratl", ThrottleFile::Bucket::kThermal},
    {"throttle_reason_pl1", ThrottleFile::Bucket::kPower},
    {"throttle_reason_pl2", ThrottleFile::Bucket::kPower},
    {"throttle_reason_pl4", ThrottleFile::Bucket::kPower},
    {"throttle_reason_prochot", ThrottleFile::Bucket::kReliability},
    {"throttle_reason_vr_tdc", ThrottleFile::Bucket::kReliability},
    {"throttle_reason_vr_thermalert", ThrottleFile::Bucket::kReliability},
}};

[[nodiscard]] bool is_directory(const std::string& path) noexcept {
    std::error_code ec;
    return std::filesystem::is_directory(path, ec);
}

// Frequency attribute names differ by kernel layout, so each site asks for the
// name it wants and takes the first spelling that exists.
[[nodiscard]] std::optional<std::uint64_t> read_first(const std::string& directory,
                                                      std::span<const char* const> names) {
    for (const char* name : names) {
        if (std::optional<std::uint64_t> value = sysfs::read_u64(directory + "/" + name)) {
            return value;
        }
    }
    return std::nullopt;
}

[[nodiscard]] core::PowerState to_core(platform::DevicePowerState state) noexcept {
    switch (state) {
        case platform::DevicePowerState::kActive:
            return core::PowerState::kActive;
        case platform::DevicePowerState::kSuspended:
            return core::PowerState::kSuspended;
        case platform::DevicePowerState::kUnknown:
            break;
    }
    return core::PowerState::kUnknown;
}

}  // namespace

IntelSysfsDriver::IntelSysfsDriver(sysfs::DrmDevice device) : device_(std::move(device)) {
    // i915 moved per-tile attributes under gt/gt<N> around 6.2; before that they
    // sat flat on the card node with a "gt_" prefix. Both layouts are still in
    // the field, so the one that exists is resolved once here rather than being
    // probed on every read.
    const std::string tiled = device_.node_path + "/gt/gt0";
    gt_path_ = is_directory(tiled) ? tiled : device_.node_path;

    hwmon_path_ = sysfs::find_hwmon(device_.device_path);
    read_static_info();
}

void IntelSysfsDriver::read_static_info() {
    info_.vendor = core::Vendor::kIntel;
    info_.pci_bus_id = device_.pci_bus_id;

    // sysfs publishes no product name, only the numeric IDs, so the name comes
    // from the system PCI database. Without it a card would render as raw hex.
    const sysfs::PciNames names = sysfs::lookup_pci_names(device_.vendor_id, device_.device_id);
    if (!names.device.empty()) {
        info_.name = "Intel " + names.device;
    } else {
        std::array<char, 32> fallback{};
        std::snprintf(fallback.data(), fallback.size(), "Intel GPU [%04x:%04x]",
                      device_.vendor_id, device_.device_id);
        info_.name = fallback.data();
    }

    // There is no UUID. The PCI address is the identity key and sysfs always
    // has one, so leaving this empty costs nothing.
    info_.driver_version = device_.driver;  // "i915" or "xe" — the variant is the version here

    // Integrated parts have no dedicated VRAM to report; discrete Arc publishes
    // lmem_total_bytes. Absent means "shares system memory", which the UI shows
    // as "—" rather than as zero VRAM.
    if (const std::optional<std::uint64_t> lmem =
            sysfs::read_u64(device_.node_path + "/lmem_total_bytes")) {
        info_.vram_total_bytes = *lmem;
    }

    const sysfs::HwmonSample hwmon = sysfs::read_hwmon(hwmon_path_);
    info_.power_limit_mw = hwmon.power_limit_mw;
}

core::PowerState IntelSysfsDriver::power_state() const noexcept {
    return to_core(platform::pci_power_state(info_.pci_bus_id));
}

std::optional<float> IntelSysfsDriver::busy_percent(core::TimePoint now) {
    // i915 has no busy-percent file. What it has is rc6_residency_ms: cumulative
    // milliseconds the render engine spent asleep. Busy time is therefore wall
    // time minus the growth in residency, which needs two readings — so the
    // first sample after construction reports nothing rather than a number
    // derived from a zero baseline.
    const std::optional<std::uint64_t> rc6 = sysfs::read_u64(gt_path_ + "/rc6_residency_ms");
    if (!rc6) {
        return std::nullopt;
    }

    const std::uint64_t previous_rc6 = last_rc6_ms_;
    const core::TimePoint previous_time = last_sample_;
    const bool had_previous = have_previous_;

    last_rc6_ms_ = *rc6;
    last_sample_ = now;
    have_previous_ = true;

    if (!had_previous) {
        return std::nullopt;
    }

    const auto elapsed_ms = static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now - previous_time).count());
    if (elapsed_ms <= 0) {
        return std::nullopt;
    }

    // The counter is 32-bit on some generations and wraps. A negative delta is
    // the wrap, and one skipped sample is cheaper than reporting a spike that
    // never happened.
    if (*rc6 < previous_rc6) {
        return std::nullopt;
    }

    const auto idle_ms = static_cast<std::int64_t>(*rc6 - previous_rc6);
    const double busy = 1.0 - (static_cast<double>(idle_ms) / static_cast<double>(elapsed_ms));
    return static_cast<float>(std::clamp(busy, 0.0, 1.0) * 100.0);
}

core::ThrottleFlags IntelSysfsDriver::read_throttle() const {
    core::ThrottleFlags flags;

    // throttle_reason_status is the roll-up. When it reads 0 nothing is
    // asserted, and the eight individual files can be skipped entirely — which
    // is the common case, and worth eight fewer reads per tick.
    const std::optional<std::uint64_t> status =
        sysfs::read_u64(gt_path_ + "/throttle_reason_status");
    if (!status || *status == 0) {
        return flags;
    }

    for (const ThrottleFile& file : kThrottleFiles) {
        const std::optional<std::uint64_t> asserted =
            sysfs::read_u64(gt_path_ + "/" + file.name);
        if (!asserted || *asserted == 0) {
            continue;
        }
        switch (file.bucket) {
            case ThrottleFile::Bucket::kThermal:
                flags.thermal = true;
                break;
            case ThrottleFile::Bucket::kPower:
                flags.power = true;
                break;
            case ThrottleFile::Bucket::kReliability:
                flags.reliability = true;
                break;
        }
    }

    // The roll-up said something is throttling but no individual file agreed —
    // a reason this kernel exposes only in aggregate. Recording it as an
    // unclassified assert beats dropping it.
    if (!flags.any()) {
        flags.reliability = true;
    }
    return flags;
}

core::DeviceSample IntelSysfsDriver::sample() {
    core::DeviceSample out;
    out.timestamp = core::Clock::now();
    out.power_state = core::PowerState::kActive;

    out.core_util = busy_percent(out.timestamp);

    {
        // rps_act_freq_mhz is what the GT is actually running at, rps_cur is
        // what the driver requested. The actual value is the honest one, and a
        // genuine 0 during RC6 is information rather than a missing reading.
        constexpr std::array<const char*, 4> names{"rps_act_freq_mhz", "gt_act_freq_mhz",
                                                   "rps_cur_freq_mhz", "gt_cur_freq_mhz"};
        if (const std::optional<std::uint64_t> mhz =
                read_first(gt_path_, std::span<const char* const>(names))) {
            out.clock_core_mhz = static_cast<std::uint32_t>(*mhz);
        }
    }

    const sysfs::HwmonSample hwmon = sysfs::read_hwmon(hwmon_path_);
    out.temp_edge_c = hwmon.temp_edge_c;
    out.temp_hotspot_c = hwmon.temp_hotspot_c;
    out.temp_mem_c = hwmon.temp_mem_c;
    out.power_draw_mw = hwmon.power_draw_mw;
    out.power_limit_mw = hwmon.power_limit_mw;
    out.fan_percent = hwmon.fan_percent;

    out.throttle = read_throttle();

    // Memory used, encode/decode utilization and per-process attribution are not
    // in sysfs. Per-engine busyness arrives with DRM fdinfo in Phase 4, which is
    // shared with amdgpu; the rest needs Level Zero.

    // The card node disappearing is the only way an unbound or removed GPU
    // shows up here, and it is what healthy() exists to report.
    healthy_ = is_directory(device_.node_path);
    return out;
}

std::vector<std::unique_ptr<IGpuDriver>> probe() {
    std::vector<std::unique_ptr<IGpuDriver>> drivers;

    for (sysfs::DrmDevice& device : sysfs::enumerate_drm_devices()) {
        if (device.driver != "i915" && device.driver != "xe") {
            continue;
        }
        drivers.push_back(std::make_unique<IntelSysfsDriver>(std::move(device)));
    }

    return drivers;
}

}  // namespace gtop::driver::intel
