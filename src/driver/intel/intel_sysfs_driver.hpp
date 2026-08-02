#pragma once
//
// Intel telemetry on Linux, through i915/xe sysfs.
//
// No library is loaded and none is needed: both Intel DRM drivers publish
// frequency, throttle state and idle residency as plain unprivileged files.
// Level Zero Sysman offers richer per-engine data and works on Linux too, but
// it is an enhancement — sysfs stays the dependency-free default path, so gtop
// reports Intel GPUs on a machine with no compute runtime installed.
//
// Two things here are worth knowing before changing anything:
//
//   * There is no "GPU busy %" file. i915 exposes RC6 residency — how long the
//     render engine spent in its sleep state — and utilization is derived by
//     differencing it against wall time. That makes this driver stateful, and
//     it means the first sample after construction has no previous reading to
//     difference against and reports no utilization at all.
//
//   * The integrated parts usually have no thermal sensor. `card2/device/hwmon`
//     does not exist on the Iris Xe this was developed against, which is the
//     canonical case for rendering "—" instead of a confident 0 °C.
//
// See ROADMAP.md §3.3.
//
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/types.hpp"
#include "driver/gpu_driver.hpp"
#include "driver/sysfs/drm_sysfs.hpp"

namespace gtop::driver::intel {

// One driver per i915 or xe card node. Empty on a machine with no Intel GPU,
// and — unlike the vendor-library backends — this cannot fail for any other
// reason, because there is nothing to initialise.
[[nodiscard]] std::vector<std::unique_ptr<IGpuDriver>> probe();

class IntelSysfsDriver final : public IGpuDriver {
public:
    explicit IntelSysfsDriver(sysfs::DrmDevice device);

    [[nodiscard]] std::string_view backend_name() const noexcept override {
        return "i915-sysfs";
    }
    [[nodiscard]] const core::GpuStaticInfo& static_info() const noexcept override {
        return info_;
    }
    [[nodiscard]] core::DeviceSample sample() override;
    [[nodiscard]] bool healthy() const noexcept override { return healthy_; }
    [[nodiscard]] core::PowerState power_state() const noexcept override;

private:
    void read_static_info();
    [[nodiscard]] std::optional<float> busy_percent(core::TimePoint now);
    [[nodiscard]] core::ThrottleFlags read_throttle() const;

    sysfs::DrmDevice device_;
    core::GpuStaticInfo info_;

    // The "gt/gt0" tile prefix, resolved once. i915 nests its per-tile
    // attributes under gt/gt0; older kernels put the same names flat on the
    // card node; xe uses a third layout. Whichever exists wins at construction
    // so sample() is a straight-line read.
    std::string gt_path_;
    std::string hwmon_path_;

    // RC6 differencing state. Utilization is the one metric here that is not a
    // single file read.
    std::uint64_t last_rc6_ms_{};
    core::TimePoint last_sample_{};
    bool have_previous_{false};

    bool healthy_{true};
};

}  // namespace gtop::driver::intel
