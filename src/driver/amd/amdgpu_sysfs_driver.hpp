#pragma once
//
// AMD telemetry on Linux, through amdgpu sysfs.
//
// This is the most complete of the three sysfs paths. amdgpu publishes a real
// busy percentage, VRAM totals, three temperature sensors, power draw against
// the enforced cap, fan duty and the active DPM clock levels — all as
// unprivileged files, with no library to load. ROCm SMI adds very little on top
// of that for a monitor, which is why it is an optional enhancement rather than
// this backend's foundation: gtop reports AMD GPUs on a machine with no ROCm
// installation at all.
//
// The hwmon half is shared with Intel (driver/sysfs/drm_sysfs.hpp) because
// hwmon is a kernel-wide standard and both drivers spell it identically. What
// is genuinely AMD-specific is the device-node half: gpu_busy_percent,
// mem_info_vram_*, and the pp_dpm_* tables.
//
// See ROADMAP.md §3.2.
//
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/types.hpp"
#include "driver/gpu_driver.hpp"
#include "driver/sysfs/drm_sysfs.hpp"

namespace gtop::driver::amd {

// One driver per amdgpu card node. Empty on a machine with no AMD GPU.
[[nodiscard]] std::vector<std::unique_ptr<IGpuDriver>> probe();

class AmdgpuSysfsDriver final : public IGpuDriver {
public:
    explicit AmdgpuSysfsDriver(sysfs::DrmDevice device);

    [[nodiscard]] std::string_view backend_name() const noexcept override {
        return "amdgpu-sysfs";
    }
    [[nodiscard]] const core::GpuStaticInfo& static_info() const noexcept override {
        return info_;
    }
    [[nodiscard]] core::DeviceSample sample() override;
    [[nodiscard]] bool healthy() const noexcept override { return healthy_; }
    [[nodiscard]] core::PowerState power_state() const noexcept override;

private:
    void read_static_info();
    [[nodiscard]] std::optional<std::uint32_t> read_dpm_clock_mhz(const char* attribute) const;

    sysfs::DrmDevice device_;
    core::GpuStaticInfo info_;
    std::string hwmon_path_;
    bool healthy_{true};
};

}  // namespace gtop::driver::amd
