#pragma once
//
// Intel telemetry through Level Zero Sysman — the Windows half of the Intel
// support cell.
//
// Sysman's model is component enumeration rather than a flat metric list: a
// device has some number of engine groups, power domains, temperature sensors,
// memory modules and frequency domains, and each is a handle with its own
// properties. Which components exist varies by part — an integrated GPU
// typically has no memory module and no fan, a discrete Arc has both — and that
// maps directly onto gtop's rule that every dynamic metric is optional. A
// component that is not enumerated is a metric that stays unset.
//
// The handles are resolved once at construction and cached, because enumerating
// them on every tick would triple the cost of a sample for information that
// does not change.
//
// Two metrics here are rates rather than readings and need two samples to mean
// anything: engine activity is a busy-time counter, and power is an energy
// counter. Both are differenced, so the first sample after construction reports
// neither.
//
// See ROADMAP.md §3.3.
//
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "core/types.hpp"
#include "driver/gpu_driver.hpp"
#include "driver/intel/level_zero_api.hpp"

namespace gtop::driver::intel {

// One driver per Sysman device across every Sysman driver instance. Empty when
// no Level Zero loader is installed, which is the normal case on Linux.
[[nodiscard]] std::vector<std::unique_ptr<IGpuDriver>> level_zero_probe();

class LevelZeroDriver final : public IGpuDriver {
public:
    LevelZeroDriver(std::shared_ptr<LevelZeroApi> api, zes_device_handle_t device);

    [[nodiscard]] std::string_view backend_name() const noexcept override {
        return "level-zero";
    }
    [[nodiscard]] const core::GpuStaticInfo& static_info() const noexcept override {
        return info_;
    }
    [[nodiscard]] core::DeviceSample sample() override;
    [[nodiscard]] bool healthy() const noexcept override { return healthy_; }
    [[nodiscard]] core::PowerState power_state() const noexcept override;

private:
    // An engine group handle plus the last counter reading taken from it.
    // Activity is (busy delta / wall delta), so the previous value has to live
    // as long as the handle does.
    struct Engine {
        zes_engine_handle_t handle{};
        zes_engine_group_t type{};
        std::uint64_t last_active_us{};
        std::uint64_t last_timestamp_us{};
        bool have_previous{false};
    };

    void discover_components();
    void read_static_info();
    void read_engines(core::DeviceSample& out);
    void read_power(core::DeviceSample& out);
    void read_temperatures(core::DeviceSample& out);
    void read_memory(core::DeviceSample& out);
    void read_frequencies(core::DeviceSample& out);
    void read_fan(core::DeviceSample& out);

    std::shared_ptr<LevelZeroApi> api_;
    zes_device_handle_t device_{};
    core::GpuStaticInfo info_;

    std::vector<Engine> engines_;
    std::vector<zes_temp_handle_t> temperature_sensors_;
    std::vector<zes_mem_handle_t> memory_modules_;
    std::vector<zes_freq_handle_t> frequency_domains_;
    std::vector<zes_fan_handle_t> fans_;

    // The card-level power domain, if the device has one. Sysman can report
    // several — package, memory, per-tile — and only the package one is what a
    // user means by "how much is this GPU drawing".
    zes_pwr_handle_t power_domain_{};
    bool have_power_domain_{false};
    std::uint64_t last_energy_uj_{};
    std::uint64_t last_energy_timestamp_us_{};
    bool have_previous_energy_{false};

    bool healthy_{true};
};

}  // namespace gtop::driver::intel
