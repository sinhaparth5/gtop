#pragma once
//
// The Level Zero Sysman entry points gtop uses, bound at runtime.
//
// This is Intel's telemetry API and the Windows half of the Intel support cell.
// It also works on Linux, where it is a strict enhancement over the i915 sysfs
// backend — richer per-engine breakdown, real memory-module state — but sysfs
// stays the default there because it needs nothing installed, and Level Zero
// needs a compute runtime.
//
// Sysman is a flat C API, which makes binding it far less hazardous than NVML:
// there are no versioned symbol chains and no struct layouts that shift under a
// fixed name. What it has instead is `stype`. Every properties and state struct
// carries a structure-type tag as its first field, and the implementation
// rejects a struct whose tag it does not recognise. Zero is not a valid tag, so
// a struct that is merely value-initialised fails — which is why every call
// site here sets it explicitly.
//
// As everywhere else in this layer, nothing is linked; ze_loader is opened
// through platform::DynamicLibrary and a null pointer disables one metric.
//
#include <memory>
#include <optional>
#include <string_view>

#include <intel/zes_api.h>

#include "platform/dynamic_library.hpp"

namespace gtop::driver::intel {

struct LevelZeroApi {
    using InitFn = ze_result_t (*)(zes_init_flags_t);
    using DriverGetFn = ze_result_t (*)(uint32_t*, zes_driver_handle_t*);
    using DeviceGetFn = ze_result_t (*)(zes_driver_handle_t, uint32_t*, zes_device_handle_t*);
    using DeviceGetPropertiesFn = ze_result_t (*)(zes_device_handle_t, zes_device_properties_t*);
    using DevicePciGetPropertiesFn = ze_result_t (*)(zes_device_handle_t, zes_pci_properties_t*);

    InitFn init{};
    DriverGetFn driver_get{};
    DeviceGetFn device_get{};
    DeviceGetPropertiesFn device_properties{};
    DevicePciGetPropertiesFn pci_properties{};

    // -- engines ---------------------------------------------------------------
    using EnumEnginesFn = ze_result_t (*)(zes_device_handle_t, uint32_t*, zes_engine_handle_t*);
    using EngineGetPropertiesFn = ze_result_t (*)(zes_engine_handle_t, zes_engine_properties_t*);
    using EngineGetActivityFn = ze_result_t (*)(zes_engine_handle_t, zes_engine_stats_t*);

    EnumEnginesFn enum_engines{};
    EngineGetPropertiesFn engine_properties{};
    EngineGetActivityFn engine_activity{};

    // -- power ------------------------------------------------------------------
    using EnumPowerFn = ze_result_t (*)(zes_device_handle_t, uint32_t*, zes_pwr_handle_t*);
    using PowerGetPropertiesFn = ze_result_t (*)(zes_pwr_handle_t, zes_power_properties_t*);
    using PowerGetEnergyFn = ze_result_t (*)(zes_pwr_handle_t, zes_power_energy_counter_t*);
    using PowerGetLimitsFn = ze_result_t (*)(zes_pwr_handle_t, zes_power_sustained_limit_t*,
                                             zes_power_burst_limit_t*, zes_power_peak_limit_t*);

    EnumPowerFn enum_power{};
    PowerGetPropertiesFn power_properties{};
    PowerGetEnergyFn power_energy{};
    PowerGetLimitsFn power_limits{};

    // -- temperature ------------------------------------------------------------
    using EnumTempFn = ze_result_t (*)(zes_device_handle_t, uint32_t*, zes_temp_handle_t*);
    using TempGetPropertiesFn = ze_result_t (*)(zes_temp_handle_t, zes_temp_properties_t*);
    using TempGetStateFn = ze_result_t (*)(zes_temp_handle_t, double*);

    EnumTempFn enum_temperature{};
    TempGetPropertiesFn temperature_properties{};
    TempGetStateFn temperature_state{};

    // -- memory ------------------------------------------------------------------
    using EnumMemoryFn = ze_result_t (*)(zes_device_handle_t, uint32_t*, zes_mem_handle_t*);
    using MemoryGetPropertiesFn = ze_result_t (*)(zes_mem_handle_t, zes_mem_properties_t*);
    using MemoryGetStateFn = ze_result_t (*)(zes_mem_handle_t, zes_mem_state_t*);

    EnumMemoryFn enum_memory{};
    MemoryGetPropertiesFn memory_properties{};
    MemoryGetStateFn memory_state{};

    // -- frequency ----------------------------------------------------------------
    using EnumFreqFn = ze_result_t (*)(zes_device_handle_t, uint32_t*, zes_freq_handle_t*);
    using FreqGetPropertiesFn = ze_result_t (*)(zes_freq_handle_t, zes_freq_properties_t*);
    using FreqGetStateFn = ze_result_t (*)(zes_freq_handle_t, zes_freq_state_t*);

    EnumFreqFn enum_frequency{};
    FreqGetPropertiesFn frequency_properties{};
    FreqGetStateFn frequency_state{};

    // -- fans ------------------------------------------------------------------------
    using EnumFansFn = ze_result_t (*)(zes_device_handle_t, uint32_t*, zes_fan_handle_t*);
    using FanGetStateFn = ze_result_t (*)(zes_fan_handle_t, zes_fan_speed_units_t, int32_t*);

    EnumFansFn enum_fans{};
    FanGetStateFn fan_state{};

    [[nodiscard]] std::string_view library_name() const noexcept {
        return library_ ? library_->loaded_name() : std::string_view{};
    }

    // Opens the loader, resolves everything above and calls zesInit. Null when
    // the loader is absent, exports no Sysman API, or reports no driver — all
    // ordinary on a machine with no Intel compute runtime installed.
    [[nodiscard]] static std::shared_ptr<LevelZeroApi> load();

    LevelZeroApi() = default;
    ~LevelZeroApi() = default;

    LevelZeroApi(const LevelZeroApi&) = delete;
    LevelZeroApi& operator=(const LevelZeroApi&) = delete;
    LevelZeroApi(LevelZeroApi&&) = delete;
    LevelZeroApi& operator=(LevelZeroApi&&) = delete;

private:
    std::optional<platform::DynamicLibrary> library_;
};

}  // namespace gtop::driver::intel
