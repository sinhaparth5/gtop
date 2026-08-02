#pragma once
//
// AMD telemetry on Windows, through ADLX.
//
// ADLX is a COM-shaped API: reference-counted interfaces reached through
// vtables, with `Acquire`/`Release` instead of new/delete and `QueryInterface`
// for downcasts. Three consequences shape this file.
//
//   * **Every interface pointer is owned.** A leaked Release is a leaked
//     driver-side object, and this samples once a second forever — so
//     acquisition goes through AdlxPtr, which is the only thing in the file
//     allowed to call Release.
//
//   * **The C++ interface is used, not the C one.** ADLX exposes both; the C++
//     side is pure-virtual single-inheritance classes with an explicit
//     `__stdcall` on every method, which is the same ABI the C vtable
//     describes. Letting the compiler compute the offsets is the entire point:
//     a hand-written vtable struct that drifts one slot out of date does not
//     fail to compile, it calls the wrong function.
//
//   * **Metric support is queried, not assumed.** ADLX publishes, per GPU, which
//     metrics it can report — `IsSupportedGPUFanSpeed` and friends. That lines
//     up exactly with gtop's rule that every dynamic metric is optional, and it
//     is better than the usual alternative of inferring support from whether a
//     call happened to fail.
//
// ADLX has no PCI address on IADLXGPU — the mapping runs the other way, from
// bus/device/function to a GPU. So identity comes from the PNP string and the
// driver-assigned unique id instead, which is why `uuid` is populated here and
// `pci_bus_id` is not. See driver_registry.hpp on what that costs.
//
// Windows-only: this file is compiled solely into the Windows build, so it
// contains no conditional compilation. See ROADMAP.md §3.2.
//
#include <memory>
#include <string_view>
#include <vector>

#include "core/types.hpp"
#include "driver/amd/adlx_api.hpp"
#include "driver/gpu_driver.hpp"

namespace gtop::driver::amd {

// One driver per GPU ADLX reports. Empty when the AMD display driver is not
// installed, when it is too old to ship ADLX, or when there is no AMD GPU.
[[nodiscard]] std::vector<std::unique_ptr<IGpuDriver>> adlx_probe();

class AdlxDriver final : public IGpuDriver {
public:
    AdlxDriver(std::shared_ptr<AdlxApi> api, AdlxPtr<adlx::IADLXGPU> gpu);

    [[nodiscard]] std::string_view backend_name() const noexcept override { return "adlx"; }
    [[nodiscard]] const core::GpuStaticInfo& static_info() const noexcept override {
        return info_;
    }
    [[nodiscard]] core::DeviceSample sample() override;
    [[nodiscard]] bool healthy() const noexcept override { return healthy_; }

private:
    void read_static_info();
    void read_support();

    std::shared_ptr<AdlxApi> api_;
    AdlxPtr<adlx::IADLXGPU> gpu_;

    // Queried once. Which metrics this GPU can report does not change, and
    // asking on every tick would double the call count for a constant answer.
    AdlxPtr<adlx::IADLXGPUMetricsSupport> support_;

    core::GpuStaticInfo info_;

    // The fan reports RPM; the UI wants a percentage. ADLX publishes the range,
    // so the conversion is real rather than invented — but only when the range
    // came back sane, which is what this pair records.
    int fan_rpm_min_{};
    int fan_rpm_max_{};
    bool have_fan_range_{false};

    bool healthy_{true};
};

}  // namespace gtop::driver::amd
