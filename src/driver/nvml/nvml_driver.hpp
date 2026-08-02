#pragma once
//
// NVIDIA telemetry through NVML — two of the six support cells, one file.
//
// The Linux and Windows implementations are the same code: NVML's API is
// identical on both, so the only platform-specific thing about NVIDIA support
// is the library filename, and that is a list in nvml_api.cpp.
//
// One driver instance owns one nvmlDevice_t and a shared reference to the
// loaded library. NVML is shut down when the last device using it is destroyed,
// which is what lets the registry retire a single GPU without disturbing the
// others.
//
// See ROADMAP.md §3.1.
//
#include <memory>
#include <vector>

#include "core/types.hpp"
#include "driver/gpu_driver.hpp"
#include "driver/nvml/nvml_api.hpp"

namespace gtop::driver::nvml {

// Loads NVML, enumerates every device it reports, and returns one driver each.
//
// Returns an empty vector on any machine without a working NVIDIA driver, and
// also on a hybrid laptop whose discrete GPU is currently powered down — NVML
// initialises fine there and reports zero devices. That second case is why the
// registry re-probes an empty backend rather than concluding it is absent.
[[nodiscard]] std::vector<std::unique_ptr<IGpuDriver>> probe();

class NvmlDriver final : public IGpuDriver {
public:
    NvmlDriver(std::shared_ptr<NvmlApi> api, nvmlDevice_t device);

    [[nodiscard]] std::string_view backend_name() const noexcept override { return "nvml"; }
    [[nodiscard]] const core::GpuStaticInfo& static_info() const noexcept override {
        return info_;
    }
    [[nodiscard]] core::DeviceSample sample() override;
    [[nodiscard]] bool healthy() const noexcept override { return healthy_; }
    [[nodiscard]] core::PowerState power_state() const noexcept override;

private:
    // Called on every NVML return code. Flips healthy_ only for the codes that
    // genuinely mean the device is gone — everything else, NOT_SUPPORTED above
    // all, leaves one optional unset and the driver alive.
    void note(nvmlReturn_t status) noexcept;

    void read_static_info();
    void read_processes(core::DeviceSample& out);

    std::shared_ptr<NvmlApi> api_;
    nvmlDevice_t device_{};
    core::GpuStaticInfo info_;
    bool healthy_{true};
};

}  // namespace gtop::driver::nvml
