// Intel telemetry through Level Zero Sysman. See level_zero_driver.hpp.

#include "driver/intel/level_zero_driver.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

#include "platform/pci_power.hpp"

namespace gtop::driver::intel {
namespace {

// Enumerating a Sysman component type is always the same two-call shape: ask
// for the count, then ask again with storage. A type the device does not have
// answers zero, which is a fact rather than a failure.
template <typename EnumFn, typename Handle>
[[nodiscard]] std::vector<Handle> enumerate(EnumFn function, zes_device_handle_t device) {
    std::vector<Handle> handles;
    if (function == nullptr) {
        return handles;
    }

    uint32_t count = 0;
    if (function(device, &count, nullptr) != ZE_RESULT_SUCCESS || count == 0) {
        return handles;
    }

    handles.resize(count);
    if (function(device, &count, handles.data()) != ZE_RESULT_SUCCESS) {
        handles.clear();
        return handles;
    }
    handles.resize(count);
    return handles;
}

[[nodiscard]] std::string trimmed_copy(const char* text, std::size_t capacity) {
    // Sysman string properties are fixed-size char arrays that are not
    // guaranteed to be terminated when the value fills them exactly.
    const std::size_t length = ::strnlen(text, capacity);
    std::string_view view(text, length);
    while (!view.empty() && (view.back() == ' ' || view.back() == '\0')) {
        view.remove_suffix(1);
    }
    return std::string(view);
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

// Sysman's throttle reasons are Intel's own, and they line up with what the
// i915 sysfs backend reads — PL1/PL2/PL4 are the same three power limits, and
// the thermal and voltage-regulator asserts are the same silicon. Mapping them
// the same way keeps a discrete Arc on Windows and an integrated part on Linux
// showing the same badge for the same condition.
[[nodiscard]] core::ThrottleFlags to_flags(zes_freq_throttle_reason_flags_t reasons) noexcept {
    core::ThrottleFlags flags;

    flags.power = (reasons & ZES_FREQ_THROTTLE_REASON_FLAG_AVE_PWR_CAP) != 0 ||
                  (reasons & ZES_FREQ_THROTTLE_REASON_FLAG_BURST_PWR_CAP) != 0 ||
                  (reasons & ZES_FREQ_THROTTLE_REASON_FLAG_CURRENT_LIMIT) != 0 ||
                  (reasons & ZES_FREQ_THROTTLE_REASON_FLAG_POWER) != 0;

    flags.thermal = (reasons & ZES_FREQ_THROTTLE_REASON_FLAG_THERMAL_LIMIT) != 0 ||
                    (reasons & ZES_FREQ_THROTTLE_REASON_FLAG_THERMAL) != 0;

    flags.reliability = (reasons & ZES_FREQ_THROTTLE_REASON_FLAG_PSU_ALERT) != 0 ||
                        (reasons & ZES_FREQ_THROTTLE_REASON_FLAG_VOLTAGE) != 0;

    // A driver- or user-imposed frequency range is a cap, not a fault — the
    // same distinction NVML's "applications clocks setting" gets.
    flags.software = (reasons & ZES_FREQ_THROTTLE_REASON_FLAG_SW_RANGE) != 0 ||
                     (reasons & ZES_FREQ_THROTTLE_REASON_FLAG_HW_RANGE) != 0;

    return flags;
}

// Percentage from two counter readings, guarding the two ways a counter pair
// can be useless: no elapsed time, and a counter that went backwards because it
// wrapped or the device reset.
[[nodiscard]] std::optional<float> rate_percent(std::uint64_t busy_now, std::uint64_t busy_then,
                                                std::uint64_t time_now,
                                                std::uint64_t time_then) noexcept {
    if (time_now <= time_then || busy_now < busy_then) {
        return std::nullopt;
    }
    const auto busy = static_cast<double>(busy_now - busy_then);
    const auto elapsed = static_cast<double>(time_now - time_then);
    return static_cast<float>(std::clamp(busy / elapsed, 0.0, 1.0) * 100.0);
}

}  // namespace

LevelZeroDriver::LevelZeroDriver(std::shared_ptr<LevelZeroApi> api, zes_device_handle_t device)
    : api_(std::move(api)), device_(device) {
    info_.vendor = core::Vendor::kIntel;
    discover_components();
    read_static_info();
}

void LevelZeroDriver::discover_components() {
    const LevelZeroApi& api = *api_;

    for (zes_engine_handle_t handle :
         enumerate<LevelZeroApi::EnumEnginesFn, zes_engine_handle_t>(api.enum_engines, device_)) {
        Engine engine;
        engine.handle = handle;

        if (api.engine_properties != nullptr) {
            zes_engine_properties_t properties{};
            properties.stype = ZES_STRUCTURE_TYPE_ENGINE_PROPERTIES;
            if (api.engine_properties(handle, &properties) == ZE_RESULT_SUCCESS) {
                engine.type = properties.type;
            }
        }
        engines_.push_back(engine);
    }

    temperature_sensors_ = enumerate<LevelZeroApi::EnumTempFn, zes_temp_handle_t>(
        api.enum_temperature, device_);
    memory_modules_ =
        enumerate<LevelZeroApi::EnumMemoryFn, zes_mem_handle_t>(api.enum_memory, device_);
    frequency_domains_ =
        enumerate<LevelZeroApi::EnumFreqFn, zes_freq_handle_t>(api.enum_frequency, device_);
    fans_ = enumerate<LevelZeroApi::EnumFansFn, zes_fan_handle_t>(api.enum_fans, device_);

    // Several power domains can exist — package, memory, per-tile. The one
    // without a subdevice is the card as a whole, which is what a power reading
    // is expected to mean; a per-tile number would read as implausibly low.
    for (zes_pwr_handle_t handle :
         enumerate<LevelZeroApi::EnumPowerFn, zes_pwr_handle_t>(api.enum_power, device_)) {
        if (api.power_properties == nullptr) {
            continue;
        }
        zes_power_properties_t properties{};
        properties.stype = ZES_STRUCTURE_TYPE_POWER_PROPERTIES;
        if (api.power_properties(handle, &properties) != ZE_RESULT_SUCCESS) {
            continue;
        }
        if (properties.onSubdevice == 0) {
            power_domain_ = handle;
            have_power_domain_ = true;

            // defaultLimit is -1 when the part does not publish one.
            if (properties.defaultLimit > 0) {
                info_.power_limit_mw = static_cast<std::uint32_t>(properties.defaultLimit);
            }
            break;
        }
    }
}

void LevelZeroDriver::read_static_info() {
    const LevelZeroApi& api = *api_;

    if (api.device_properties != nullptr) {
        zes_device_properties_t properties{};
        properties.stype = ZES_STRUCTURE_TYPE_DEVICE_PROPERTIES;
        if (api.device_properties(device_, &properties) == ZE_RESULT_SUCCESS) {
            std::string model = trimmed_copy(properties.modelName, ZES_STRING_PROPERTY_SIZE);
            const std::string brand =
                trimmed_copy(properties.brandName, ZES_STRING_PROPERTY_SIZE);
            if (model.empty()) {
                model = trimmed_copy(properties.core.name, ZE_MAX_DEVICE_NAME);
            }

            info_.name = brand.empty() ? model : (brand + " " + model);
            if (info_.name.empty()) {
                info_.name = "Intel GPU";
            }
            info_.driver_version =
                trimmed_copy(properties.driverVersion, ZES_STRING_PROPERTY_SIZE);

            // Sysman has no UUID field of its own, but the core device
            // properties carry one. It is the fallback identity key for a
            // device whose PCI address cannot be read.
            const auto& uuid = properties.core.uuid.id;
            std::string text;
            text.reserve(sizeof(uuid) * 2);
            for (const std::uint8_t byte : uuid) {
                std::array<char, 3> hex{};
                std::snprintf(hex.data(), hex.size(), "%02x", byte);
                text += hex.data();
            }
            info_.uuid = text;
        }
    }

    if (api.pci_properties != nullptr) {
        zes_pci_properties_t properties{};
        properties.stype = ZES_STRUCTURE_TYPE_PCI_PROPERTIES;
        if (api.pci_properties(device_, &properties) == ZE_RESULT_SUCCESS) {
            // Sysman gives the address as four integers rather than a string,
            // so it is formatted into the canonical spelling here — the same
            // one sysfs uses, which is what lets the two Intel backends
            // de-duplicate against each other on Linux.
            std::array<char, 32> text{};
            std::snprintf(text.data(), text.size(), "%04x:%02x:%02x.%x",
                          properties.address.domain, properties.address.bus,
                          properties.address.device, properties.address.function);
            info_.pci_bus_id = core::normalise_pci_bus_id(text.data());
        }
    }

    // Total memory comes from the module properties and does not change.
    if (api.memory_properties != nullptr) {
        std::uint64_t total = 0;
        for (zes_mem_handle_t handle : memory_modules_) {
            zes_mem_properties_t properties{};
            properties.stype = ZES_STRUCTURE_TYPE_MEM_PROPERTIES;
            if (api.memory_properties(handle, &properties) != ZE_RESULT_SUCCESS) {
                continue;
            }
            // System memory shared with the host is not VRAM, and counting it
            // would report an integrated GPU as having gigabytes of dedicated
            // memory it does not have.
            if (properties.location == ZES_MEM_LOC_DEVICE) {
                total += properties.physicalSize;
            }
        }
        if (total > 0) {
            info_.vram_total_bytes = total;
        }
    }

    if (api.temperature_properties != nullptr) {
        for (zes_temp_handle_t handle : temperature_sensors_) {
            zes_temp_properties_t properties{};
            properties.stype = ZES_STRUCTURE_TYPE_TEMP_PROPERTIES;
            if (api.temperature_properties(handle, &properties) != ZE_RESULT_SUCCESS) {
                continue;
            }
            if (properties.type == ZES_TEMP_SENSORS_GPU && properties.maxTemperature > 0) {
                info_.temp_slowdown_c = static_cast<std::uint32_t>(properties.maxTemperature);
                break;
            }
        }
    }
}

core::PowerState LevelZeroDriver::power_state() const noexcept {
    return to_core(platform::pci_power_state(info_.pci_bus_id));
}

void LevelZeroDriver::read_engines(core::DeviceSample& out) {
    const LevelZeroApi& api = *api_;
    if (api.engine_activity == nullptr) {
        return;
    }

    // Sysman reports several overlapping groups: an ALL roll-up, per-class
    // roll-ups, and one handle per physical engine. Taking the maximum within
    // each class rather than the sum is what keeps a device with four copy
    // engines from reporting 400% busy.
    std::optional<float> all;
    std::optional<float> compute;
    std::optional<float> render;
    std::optional<float> media;

    const auto keep_max = [](std::optional<float>& slot, std::optional<float> value) {
        if (!value) {
            return;
        }
        slot = slot ? std::max(*slot, *value) : *value;
    };

    for (Engine& engine : engines_) {
        zes_engine_stats_t stats{};
        if (api.engine_activity(engine.handle, &stats) != ZE_RESULT_SUCCESS) {
            continue;
        }

        const std::uint64_t previous_active = engine.last_active_us;
        const std::uint64_t previous_time = engine.last_timestamp_us;
        const bool had_previous = engine.have_previous;

        engine.last_active_us = stats.activeTime;
        engine.last_timestamp_us = stats.timestamp;
        engine.have_previous = true;

        if (!had_previous) {
            continue;
        }

        const std::optional<float> busy =
            rate_percent(stats.activeTime, previous_active, stats.timestamp, previous_time);

        switch (engine.type) {
            case ZES_ENGINE_GROUP_ALL:
                keep_max(all, busy);
                break;
            case ZES_ENGINE_GROUP_COMPUTE_ALL:
            case ZES_ENGINE_GROUP_COMPUTE_SINGLE:
                keep_max(compute, busy);
                break;
            case ZES_ENGINE_GROUP_RENDER_ALL:
            case ZES_ENGINE_GROUP_RENDER_SINGLE:
                keep_max(render, busy);
                break;
            case ZES_ENGINE_GROUP_MEDIA_ALL:
            case ZES_ENGINE_GROUP_MEDIA_CODEC_SINGLE:
            case ZES_ENGINE_GROUP_MEDIA_ENHANCEMENT_SINGLE:
                keep_max(media, busy);
                break;
            default:
                // Copy engines and the deprecated groups. Neither has a home in
                // DeviceSample, and inventing one would be worse than omitting
                // them.
                break;
        }
    }

    // The ALL group is the honest answer for overall utilization where it
    // exists; compute and render are the fallback for parts that omit it.
    if (all) {
        out.core_util = all;
    } else if (compute || render) {
        out.core_util = std::max(compute.value_or(0.0F), render.value_or(0.0F));
    }

    // Sysman does not split encode from decode — one media group covers both —
    // so the same figure fills both fields rather than one being fabricated as
    // zero.
    if (media) {
        out.encoder_util = media;
        out.decoder_util = media;
    }
}

void LevelZeroDriver::read_power(core::DeviceSample& out) {
    const LevelZeroApi& api = *api_;
    if (!have_power_domain_) {
        return;
    }

    if (api.power_energy != nullptr) {
        zes_power_energy_counter_t counter{};
        if (api.power_energy(power_domain_, &counter) == ZE_RESULT_SUCCESS) {
            const std::uint64_t previous_energy = last_energy_uj_;
            const std::uint64_t previous_time = last_energy_timestamp_us_;
            const bool had_previous = have_previous_energy_;

            last_energy_uj_ = counter.energy;
            last_energy_timestamp_us_ = counter.timestamp;
            have_previous_energy_ = true;

            // Sysman reports accumulated energy, not instantaneous power: there
            // is no "watts" query. Power is the derivative, in microjoules over
            // microseconds — which is watts directly, so the conversion to
            // milliwatts is a single factor of 1000.
            if (had_previous && counter.timestamp > previous_time &&
                counter.energy >= previous_energy) {
                const auto joules = static_cast<double>(counter.energy - previous_energy);
                const auto seconds = static_cast<double>(counter.timestamp - previous_time);
                out.power_draw_mw = static_cast<std::uint32_t>((joules / seconds) * 1000.0);
            }
        }
    }

    if (api.power_limits != nullptr) {
        zes_power_sustained_limit_t sustained{};
        if (api.power_limits(power_domain_, &sustained, nullptr, nullptr) ==
                ZE_RESULT_SUCCESS &&
            sustained.enabled != 0 && sustained.power > 0) {
            out.power_limit_mw = static_cast<std::uint32_t>(sustained.power);
        }
    }

    if (!out.power_limit_mw.has_value()) {
        out.power_limit_mw = info_.power_limit_mw;
    }
}

void LevelZeroDriver::read_temperatures(core::DeviceSample& out) {
    const LevelZeroApi& api = *api_;
    if (api.temperature_state == nullptr || api.temperature_properties == nullptr) {
        return;
    }

    for (zes_temp_handle_t handle : temperature_sensors_) {
        zes_temp_properties_t properties{};
        properties.stype = ZES_STRUCTURE_TYPE_TEMP_PROPERTIES;
        if (api.temperature_properties(handle, &properties) != ZE_RESULT_SUCCESS) {
            continue;
        }

        double celsius = 0.0;
        if (api.temperature_state(handle, &celsius) != ZE_RESULT_SUCCESS || celsius <= 0.0) {
            continue;
        }
        const auto value = static_cast<std::uint32_t>(celsius);

        switch (properties.type) {
            case ZES_TEMP_SENSORS_GLOBAL:
            case ZES_TEMP_SENSORS_GPU_BOARD:
                out.temp_edge_c = value;
                break;
            case ZES_TEMP_SENSORS_GPU:
                out.temp_hotspot_c = value;
                break;
            case ZES_TEMP_SENSORS_MEMORY:
                out.temp_mem_c = value;
                break;
            default:
                // The _MIN variants and the voltage-regulator sensor. Real
                // readings, but not the ones the UI has a place for.
                break;
        }
    }

    // A part with only a GPU sensor still has something to show in the edge
    // slot, which is the row the UI always draws.
    if (!out.temp_edge_c.has_value()) {
        out.temp_edge_c = out.temp_hotspot_c;
    }
}

void LevelZeroDriver::read_memory(core::DeviceSample& out) {
    const LevelZeroApi& api = *api_;
    if (api.memory_state == nullptr || api.memory_properties == nullptr) {
        return;
    }

    std::uint64_t used = 0;
    bool any = false;

    for (zes_mem_handle_t handle : memory_modules_) {
        zes_mem_properties_t properties{};
        properties.stype = ZES_STRUCTURE_TYPE_MEM_PROPERTIES;
        if (api.memory_properties(handle, &properties) != ZE_RESULT_SUCCESS ||
            properties.location != ZES_MEM_LOC_DEVICE) {
            continue;
        }

        zes_mem_state_t state{};
        state.stype = ZES_STRUCTURE_TYPE_MEM_STATE;
        if (api.memory_state(handle, &state) != ZE_RESULT_SUCCESS || state.size < state.free) {
            continue;
        }

        used += state.size - state.free;
        any = true;
    }

    if (any) {
        out.vram_used_bytes = used;
    }
}

void LevelZeroDriver::read_frequencies(core::DeviceSample& out) {
    const LevelZeroApi& api = *api_;
    if (api.frequency_state == nullptr || api.frequency_properties == nullptr) {
        return;
    }

    for (zes_freq_handle_t handle : frequency_domains_) {
        zes_freq_properties_t properties{};
        properties.stype = ZES_STRUCTURE_TYPE_FREQ_PROPERTIES;
        if (api.frequency_properties(handle, &properties) != ZE_RESULT_SUCCESS) {
            continue;
        }

        zes_freq_state_t state{};
        state.stype = ZES_STRUCTURE_TYPE_FREQ_STATE;
        if (api.frequency_state(handle, &state) != ZE_RESULT_SUCCESS) {
            continue;
        }

        // `actual` is -1 when the domain cannot report it; `request` is what the
        // driver asked for and is the only value some parts publish.
        const double mhz = state.actual >= 0.0 ? state.actual : state.request;

        if (properties.type == ZES_FREQ_DOMAIN_GPU) {
            if (mhz >= 0.0) {
                out.clock_core_mhz = static_cast<std::uint32_t>(mhz);
            }
            // Throttle reasons hang off the GPU frequency domain, which is why
            // they are read here rather than from the device.
            out.throttle = to_flags(state.throttleReasons);
        } else if (properties.type == ZES_FREQ_DOMAIN_MEMORY && mhz >= 0.0) {
            out.clock_mem_mhz = static_cast<std::uint32_t>(mhz);
        }
    }
}

void LevelZeroDriver::read_fan(core::DeviceSample& out) {
    const LevelZeroApi& api = *api_;
    if (api.fan_state == nullptr) {
        return;
    }

    for (zes_fan_handle_t handle : fans_) {
        std::int32_t percent = 0;
        // Asking for percent rather than RPM: RPM cannot be turned into the
        // gauge the UI draws without knowing the fan's maximum, which Sysman
        // does not reliably publish. A fan that only reports RPM answers
        // UNSUPPORTED_FEATURE here and the metric stays unset, which is honest.
        if (api.fan_state(handle, ZES_FAN_SPEED_UNITS_PERCENT, &percent) == ZE_RESULT_SUCCESS &&
            percent >= 0) {
            out.fan_percent = static_cast<std::uint32_t>(std::min(percent, 100));
            return;
        }
    }
}

core::DeviceSample LevelZeroDriver::sample() {
    core::DeviceSample out;
    out.timestamp = core::Clock::now();
    out.power_state = core::PowerState::kActive;

    read_engines(out);
    read_power(out);
    read_temperatures(out);
    read_memory(out);
    read_frequencies(out);
    read_fan(out);

    // Per-process attribution is Phase 4. Sysman does expose it through
    // zesDeviceProcessesGetState, which is where that will come from on Windows.

    return out;
}

std::vector<std::unique_ptr<IGpuDriver>> level_zero_probe() {
    std::vector<std::unique_ptr<IGpuDriver>> drivers;

    std::shared_ptr<LevelZeroApi> api = LevelZeroApi::load();
    if (!api) {
        return drivers;
    }

    uint32_t driver_count = 0;
    if (api->driver_get(&driver_count, nullptr) != ZE_RESULT_SUCCESS || driver_count == 0) {
        return drivers;
    }

    std::vector<zes_driver_handle_t> driver_handles(driver_count);
    if (api->driver_get(&driver_count, driver_handles.data()) != ZE_RESULT_SUCCESS) {
        return drivers;
    }
    driver_handles.resize(driver_count);

    for (zes_driver_handle_t driver_handle : driver_handles) {
        uint32_t device_count = 0;
        if (api->device_get(driver_handle, &device_count, nullptr) != ZE_RESULT_SUCCESS ||
            device_count == 0) {
            continue;
        }

        std::vector<zes_device_handle_t> devices(device_count);
        if (api->device_get(driver_handle, &device_count, devices.data()) !=
            ZE_RESULT_SUCCESS) {
            continue;
        }
        devices.resize(device_count);

        for (zes_device_handle_t device : devices) {
            drivers.push_back(std::make_unique<LevelZeroDriver>(api, device));
        }
    }

    return drivers;
}

}  // namespace gtop::driver::intel
