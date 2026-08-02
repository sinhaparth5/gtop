// AMD telemetry through ADLX. See adlx_driver.hpp.

#include "driver/amd/adlx_driver.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace gtop::driver::amd {
namespace {

// ADLX reports "is this metric available on this GPU" separately from the
// metric itself. Both have to say yes: support can be true while the reading
// fails transiently, and a reading can succeed on a metric the GPU does not
// really have. Requiring both is what keeps a fabricated 0 out of the UI.
template <typename Support, typename SupportFn, typename Metrics, typename ReadFn, typename Value>
[[nodiscard]] bool read_supported(Support* support, SupportFn is_supported, Metrics* metrics,
                                  ReadFn read, Value& out) {
    if (support == nullptr || metrics == nullptr) {
        return false;
    }

    adlx_bool available = false;
    if ((support->*is_supported)(&available) != ADLX_OK || !available) {
        return false;
    }
    return (metrics->*read)(&out) == ADLX_OK;
}

[[nodiscard]] std::optional<float> to_percent(double value) noexcept {
    if (!std::isfinite(value) || value < 0.0) {
        return std::nullopt;
    }
    return static_cast<float>(std::clamp(value, 0.0, 100.0));
}

}  // namespace

AdlxDriver::AdlxDriver(std::shared_ptr<AdlxApi> api, AdlxPtr<adlx::IADLXGPU> gpu)
    : api_(std::move(api)), gpu_(std::move(gpu)) {
    info_.vendor = core::Vendor::kAmd;
    read_support();
    read_static_info();
}

void AdlxDriver::read_support() {
    AdlxPtr<adlx::IADLXPerformanceMonitoringServices> monitoring;
    if (api_->system->GetPerformanceMonitoringServices(monitoring.receive()) != ADLX_OK) {
        return;
    }
    if (monitoring->GetSupportedGPUMetrics(gpu_.get(), support_.receive()) != ADLX_OK) {
        support_.reset();
        return;
    }

    // The fan is reported in RPM, and the UI draws a percentage. ADLX publishes
    // the achievable range, so the conversion is grounded in what the fan can
    // actually do rather than in a guessed maximum. A range that comes back
    // degenerate leaves fan_percent unset instead.
    adlx_int minimum = 0;
    adlx_int maximum = 0;
    if (support_->GetGPUFanSpeedRange(&minimum, &maximum) == ADLX_OK && maximum > minimum) {
        fan_rpm_min_ = minimum;
        fan_rpm_max_ = maximum;
        have_fan_range_ = true;
    }
}

void AdlxDriver::read_static_info() {
    adlx::IADLXGPU* gpu = gpu_.get();

    const char* text = nullptr;
    if (gpu->Name(&text) == ADLX_OK && text != nullptr) {
        info_.name = text;
    }

    // ADLX exposes no PCI address on a GPU, so the identity key has to be built
    // from what it does give: the Windows PNP instance string, which is unique
    // per device, falling back to the driver's own unique id. Without one of
    // these the registry cannot de-duplicate this device against anything —
    // which on Windows costs nothing today, since no other backend enumerates
    // AMD cards.
    const char* pnp = nullptr;
    if (gpu->PNPString(&pnp) == ADLX_OK && pnp != nullptr) {
        info_.uuid = pnp;
    } else {
        adlx_int unique_id = 0;
        if (gpu->UniqueId(&unique_id) == ADLX_OK) {
            info_.uuid = "adlx-gpu-" + std::to_string(unique_id);
        }
    }

    // DriverPath is the registry path of the installed driver, not a version
    // number. ADLX has no version accessor on IADLXGPU, so this field stays
    // empty rather than being filled with something that is not a version.

    adlx_uint vram_mb = 0;
    if (gpu->TotalVRAM(&vram_mb) == ADLX_OK && vram_mb > 0) {
        info_.vram_total_bytes = std::uint64_t{vram_mb} * 1024ULL * 1024ULL;
    }

    if (support_) {
        adlx_int minimum = 0;
        adlx_int maximum = 0;
        // The top of the reportable power range is the board's enforced limit,
        // which is the figure draw should be plotted against.
        if (support_->GetGPUPowerRange(&minimum, &maximum) == ADLX_OK && maximum > 0) {
            info_.power_limit_mw = static_cast<std::uint32_t>(maximum) * 1000U;
        }
        if (support_->GetGPUHotspotTemperatureRange(&minimum, &maximum) == ADLX_OK &&
            maximum > 0) {
            info_.temp_slowdown_c = static_cast<std::uint32_t>(maximum);
        }
    }
}

core::DeviceSample AdlxDriver::sample() {
    core::DeviceSample out;
    out.timestamp = core::Clock::now();
    out.power_state = core::PowerState::kActive;

    AdlxPtr<adlx::IADLXPerformanceMonitoringServices> monitoring;
    if (api_->system->GetPerformanceMonitoringServices(monitoring.receive()) != ADLX_OK) {
        return out;
    }

    AdlxPtr<adlx::IADLXGPUMetrics> metrics;
    if (monitoring->GetCurrentGPUMetrics(gpu_.get(), metrics.receive()) != ADLX_OK) {
        // The GPU was removed, or the driver restarted underneath us. Either
        // way this handle will not start working again, which is exactly what
        // healthy() is for.
        healthy_ = false;
        return out;
    }

    adlx::IADLXGPUMetricsSupport* support = support_.get();
    adlx::IADLXGPUMetrics* current = metrics.get();

    double usage = 0.0;
    if (read_supported(support, &adlx::IADLXGPUMetricsSupport::IsSupportedGPUUsage, current,
                       &adlx::IADLXGPUMetrics::GPUUsage, usage)) {
        out.core_util = to_percent(usage);
    }

    adlx_int clock_mhz = 0;
    if (read_supported(support, &adlx::IADLXGPUMetricsSupport::IsSupportedGPUClockSpeed, current,
                       &adlx::IADLXGPUMetrics::GPUClockSpeed, clock_mhz) &&
        clock_mhz >= 0) {
        out.clock_core_mhz = static_cast<std::uint32_t>(clock_mhz);
    }

    if (read_supported(support, &adlx::IADLXGPUMetricsSupport::IsSupportedGPUVRAMClockSpeed,
                       current, &adlx::IADLXGPUMetrics::GPUVRAMClockSpeed, clock_mhz) &&
        clock_mhz >= 0) {
        out.clock_mem_mhz = static_cast<std::uint32_t>(clock_mhz);
    }

    double celsius = 0.0;
    if (read_supported(support, &adlx::IADLXGPUMetricsSupport::IsSupportedGPUTemperature, current,
                       &adlx::IADLXGPUMetrics::GPUTemperature, celsius) &&
        celsius > 0.0) {
        out.temp_edge_c = static_cast<std::uint32_t>(celsius);
    }

    if (read_supported(support, &adlx::IADLXGPUMetricsSupport::IsSupportedGPUHotspotTemperature,
                       current, &adlx::IADLXGPUMetrics::GPUHotspotTemperature, celsius) &&
        celsius > 0.0) {
        out.temp_hotspot_c = static_cast<std::uint32_t>(celsius);
    }

    // ADLX has no memory-temperature metric. AMD's own control panel does not
    // show one either, so this is a vendor gap rather than an omission here.

    double watts = 0.0;
    // Total board power is what a wall meter would see and what the limit is
    // enforced against; GPUPower is the ASIC alone and reads noticeably lower.
    // Board power first, ASIC as the fallback for parts that do not report it.
    if (read_supported(support, &adlx::IADLXGPUMetricsSupport::IsSupportedGPUTotalBoardPower,
                       current, &adlx::IADLXGPUMetrics::GPUTotalBoardPower, watts) &&
        watts > 0.0) {
        out.power_draw_mw = static_cast<std::uint32_t>(watts * 1000.0);
    } else if (read_supported(support, &adlx::IADLXGPUMetricsSupport::IsSupportedGPUPower,
                              current, &adlx::IADLXGPUMetrics::GPUPower, watts) &&
               watts > 0.0) {
        out.power_draw_mw = static_cast<std::uint32_t>(watts * 1000.0);
    }
    out.power_limit_mw = info_.power_limit_mw;

    adlx_int vram_mb = 0;
    if (read_supported(support, &adlx::IADLXGPUMetricsSupport::IsSupportedGPUVRAM, current,
                       &adlx::IADLXGPUMetrics::GPUVRAM, vram_mb) &&
        vram_mb >= 0) {
        out.vram_used_bytes = static_cast<std::uint64_t>(vram_mb) * 1024ULL * 1024ULL;
    }

    adlx_int fan_rpm = 0;
    if (have_fan_range_ &&
        read_supported(support, &adlx::IADLXGPUMetricsSupport::IsSupportedGPUFanSpeed, current,
                       &adlx::IADLXGPUMetrics::GPUFanSpeed, fan_rpm) &&
        fan_rpm >= 0) {
        const double span = static_cast<double>(fan_rpm_max_ - fan_rpm_min_);
        const double above = static_cast<double>(fan_rpm - fan_rpm_min_);
        out.fan_percent =
            static_cast<std::uint32_t>(std::clamp(above / span, 0.0, 1.0) * 100.0);
    }

    // ADLX reports no throttle reasons and no encoder/decoder utilization, and
    // per-process attribution comes from PDH counters in Phase 4. All of those
    // stay unset rather than being approximated.

    return out;
}

std::vector<std::unique_ptr<IGpuDriver>> adlx_probe() {
    std::vector<std::unique_ptr<IGpuDriver>> drivers;

    std::shared_ptr<AdlxApi> api = AdlxApi::load();
    if (!api) {
        return drivers;
    }

    AdlxPtr<adlx::IADLXGPUList> gpus;
    if (api->system->GetGPUs(gpus.receive()) != ADLX_OK) {
        return drivers;
    }

    for (adlx_uint index = gpus->Begin(); index != gpus->End(); ++index) {
        AdlxPtr<adlx::IADLXGPU> gpu;
        if (gpus->At(index, gpu.receive()) != ADLX_OK || !gpu) {
            // A GPU that vanished between enumeration and access. The rest of
            // the list is still good.
            continue;
        }
        drivers.push_back(std::make_unique<AdlxDriver>(api, std::move(gpu)));
    }

    return drivers;
}

}  // namespace gtop::driver::amd
