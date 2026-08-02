// AMD telemetry from amdgpu sysfs. See amdgpu_sysfs_driver.hpp.

#include "driver/amd/amdgpu_sysfs_driver.hpp"

#include <array>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

#include "platform/pci_power.hpp"

namespace gtop::driver::amd {
namespace {

[[nodiscard]] bool is_directory(const std::string& path) noexcept {
    std::error_code ec;
    return std::filesystem::is_directory(path, ec);
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

AmdgpuSysfsDriver::AmdgpuSysfsDriver(sysfs::DrmDevice device) : device_(std::move(device)) {
    hwmon_path_ = sysfs::find_hwmon(device_.device_path);
    read_static_info();
}

void AmdgpuSysfsDriver::read_static_info() {
    info_.vendor = core::Vendor::kAmd;
    info_.pci_bus_id = device_.pci_bus_id;

    const sysfs::PciNames names = sysfs::lookup_pci_names(device_.vendor_id, device_.device_id);
    if (!names.device.empty()) {
        info_.name = "AMD " + names.device;
    } else {
        std::array<char, 32> fallback{};
        std::snprintf(fallback.data(), fallback.size(), "AMD GPU [%04x:%04x]",
                      device_.vendor_id, device_.device_id);
        info_.name = fallback.data();
    }

    info_.driver_version = device_.driver;

    // Unlike i915, amdgpu reports dedicated VRAM even on APUs — where it is the
    // carve-out from system memory, which is exactly the number a user cares
    // about when something runs out of it.
    info_.vram_total_bytes = sysfs::read_u64(device_.device_path + "/mem_info_vram_total");

    const sysfs::HwmonSample hwmon = sysfs::read_hwmon(hwmon_path_);
    info_.power_limit_mw = hwmon.power_limit_mw;

    // hwmon publishes the emergency and critical thresholds; the critical one is
    // where the hardware begins protecting itself, which is what the UI compares
    // the hotspot reading against.
    if (!hwmon_path_.empty()) {
        if (const std::optional<std::int64_t> millidegrees =
                sysfs::read_i64(hwmon_path_ + "/temp1_crit")) {
            if (*millidegrees > 0) {
                info_.temp_slowdown_c = static_cast<std::uint32_t>(*millidegrees / 1000);
            }
        }
    }
}

core::PowerState AmdgpuSysfsDriver::power_state() const noexcept {
    return to_core(platform::pci_power_state(info_.pci_bus_id));
}

std::optional<std::uint32_t> AmdgpuSysfsDriver::read_dpm_clock_mhz(const char* attribute) const {
    // pp_dpm_sclk is a table of the available DPM states with the active one
    // starred, not a single value — which is why it goes through a parser
    // rather than read_u64. The unit in the table is always MHz.
    const std::optional<std::string> table =
        sysfs::read_text(device_.device_path + "/" + attribute);
    if (!table) {
        return std::nullopt;
    }
    const std::optional<std::uint64_t> mhz = sysfs::parse_active_dpm_level(*table);
    if (!mhz) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(*mhz);
}

core::DeviceSample AmdgpuSysfsDriver::sample() {
    core::DeviceSample out;
    out.timestamp = core::Clock::now();
    out.power_state = core::PowerState::kActive;

    if (const std::optional<std::uint64_t> busy =
            sysfs::read_u64(device_.device_path + "/gpu_busy_percent")) {
        out.core_util = static_cast<float>(*busy);
    }

    // The memory *controller* busy percentage, distinct from how full VRAM is.
    // amdgpu exposes both, and conflating them is a classic misreport: a card
    // can be at 90% VRAM occupancy with an idle memory controller.
    if (const std::optional<std::uint64_t> busy =
            sysfs::read_u64(device_.device_path + "/mem_busy_percent")) {
        out.mem_controller_util = static_cast<float>(*busy);
    }

    out.vram_used_bytes = sysfs::read_u64(device_.device_path + "/mem_info_vram_used");

    const sysfs::HwmonSample hwmon = sysfs::read_hwmon(hwmon_path_);
    out.temp_edge_c = hwmon.temp_edge_c;
    out.temp_hotspot_c = hwmon.temp_hotspot_c;
    out.temp_mem_c = hwmon.temp_mem_c;
    out.power_draw_mw = hwmon.power_draw_mw;
    out.power_limit_mw = hwmon.power_limit_mw;
    out.fan_percent = hwmon.fan_percent;

    out.clock_core_mhz = read_dpm_clock_mhz("pp_dpm_sclk");
    out.clock_mem_mhz = read_dpm_clock_mhz("pp_dpm_mclk");

    // hwmon freq1_input is the shader clock in Hz and is present on parts whose
    // pp_dpm tables are not, so it fills the gap rather than replacing them.
    if (!out.clock_core_mhz.has_value() && !hwmon_path_.empty()) {
        if (const std::optional<std::uint64_t> hz = sysfs::read_u64(hwmon_path_ + "/freq1_input")) {
            out.clock_core_mhz = static_cast<std::uint32_t>(*hz / 1'000'000);
        }
    }

    // amdgpu publishes no throttle-reason attributes. The information exists in
    // the SMU metrics table, which is not exported as sysfs — so AMD gets no
    // throttle badge from this backend, and ThrottleFlags stays all-false rather
    // than guessed at from temperature.

    healthy_ = is_directory(device_.node_path);
    return out;
}

std::vector<std::unique_ptr<IGpuDriver>> probe() {
    std::vector<std::unique_ptr<IGpuDriver>> drivers;

    for (sysfs::DrmDevice& device : sysfs::enumerate_drm_devices()) {
        if (device.driver != "amdgpu") {
            continue;
        }
        drivers.push_back(std::make_unique<AmdgpuSysfsDriver>(std::move(device)));
    }

    return drivers;
}

}  // namespace gtop::driver::amd
