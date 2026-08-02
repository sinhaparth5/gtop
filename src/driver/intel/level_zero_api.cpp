// Binding Level Zero Sysman at runtime. See level_zero_api.hpp.

#include "driver/intel/level_zero_api.hpp"

#include <array>
#include <span>
#include <utility>

namespace gtop::driver::intel {
namespace {

// Loader candidates. The Level Zero loader is what dispatches to whichever
// vendor driver is installed, so this is the only name that has to be found —
// the Intel implementation behind it is never opened directly.
//
// Windows installs ze_loader.dll alongside the graphics driver. Linux ships the
// versioned soname from the level-zero package; the unversioned name is again a
// -dev symlink and cannot be relied on.
constexpr std::array<const char*, 3> kCandidates{
    "ze_loader.dll",
    "libze_loader.so.1",
    "libze_loader.so",
};

template <typename Fn>
[[nodiscard]] Fn resolve(const platform::DynamicLibrary& library, const char* name) noexcept {
    return library.symbol<Fn>(name);
}

}  // namespace

std::shared_ptr<LevelZeroApi> LevelZeroApi::load() {
    std::optional<platform::DynamicLibrary> library =
        platform::DynamicLibrary::open(std::span<const char* const>(kCandidates));
    if (!library) {
        return nullptr;
    }

    auto api = std::make_shared<LevelZeroApi>();

    api->init = resolve<InitFn>(*library, "zesInit");
    api->driver_get = resolve<DriverGetFn>(*library, "zesDriverGet");
    api->device_get = resolve<DeviceGetFn>(*library, "zesDeviceGet");

    // A loader that exports the core API but not the Sysman entry points is a
    // pre-1.5 build. Compute still works there; telemetry does not exist, so
    // there is nothing for this backend to do.
    if (api->init == nullptr || api->driver_get == nullptr || api->device_get == nullptr) {
        return nullptr;
    }

    api->device_properties =
        resolve<DeviceGetPropertiesFn>(*library, "zesDeviceGetProperties");
    api->pci_properties =
        resolve<DevicePciGetPropertiesFn>(*library, "zesDevicePciGetProperties");

    api->enum_engines = resolve<EnumEnginesFn>(*library, "zesDeviceEnumEngineGroups");
    api->engine_properties = resolve<EngineGetPropertiesFn>(*library, "zesEngineGetProperties");
    api->engine_activity = resolve<EngineGetActivityFn>(*library, "zesEngineGetActivity");

    api->enum_power = resolve<EnumPowerFn>(*library, "zesDeviceEnumPowerDomains");
    api->power_properties = resolve<PowerGetPropertiesFn>(*library, "zesPowerGetProperties");
    api->power_energy = resolve<PowerGetEnergyFn>(*library, "zesPowerGetEnergyCounter");
    api->power_limits = resolve<PowerGetLimitsFn>(*library, "zesPowerGetLimits");

    api->enum_temperature = resolve<EnumTempFn>(*library, "zesDeviceEnumTemperatureSensors");
    api->temperature_properties =
        resolve<TempGetPropertiesFn>(*library, "zesTemperatureGetProperties");
    api->temperature_state = resolve<TempGetStateFn>(*library, "zesTemperatureGetState");

    api->enum_memory = resolve<EnumMemoryFn>(*library, "zesDeviceEnumMemoryModules");
    api->memory_properties = resolve<MemoryGetPropertiesFn>(*library, "zesMemoryGetProperties");
    api->memory_state = resolve<MemoryGetStateFn>(*library, "zesMemoryGetState");

    api->enum_frequency = resolve<EnumFreqFn>(*library, "zesDeviceEnumFrequencyDomains");
    api->frequency_properties =
        resolve<FreqGetPropertiesFn>(*library, "zesFrequencyGetProperties");
    api->frequency_state = resolve<FreqGetStateFn>(*library, "zesFrequencyGetState");

    api->enum_fans = resolve<EnumFansFn>(*library, "zesDeviceEnumFans");
    api->fan_state = resolve<FanGetStateFn>(*library, "zesFanGetState");

    // zesInit is idempotent and reference-counted by the loader, so there is no
    // matching shutdown to pair with it — which is why, unlike NvmlApi, this
    // type has nothing to do in its destructor beyond closing the library.
    if (api->init(0) != ZE_RESULT_SUCCESS) {
        return nullptr;
    }

    api->library_ = std::move(library);
    return api;
}

}  // namespace gtop::driver::intel
