// Binding NVML at runtime. See nvml_api.hpp for why none of this is linked.

#include "driver/nvml/nvml_api.hpp"

#include <array>
#include <span>
#include <utility>

namespace gtop::driver::nvml {
namespace {

// Library candidates, most-preferred first.
//
// Linux: the versioned soname is not merely the safer choice, it is the only
// name present on a stock end-user system. `libnvidia-ml.so` is a development
// symlink shipped by the -dev package, so a backend that asked for it first
// would work on the machine it was written on and fail everywhere else.
//
// Windows: modern drivers put nvml.dll in System32, where the default search
// order finds it. Pre-2017 drivers only installed it under NVSMI, which is not
// on the search path, hence the explicit second candidate.
constexpr std::array<const char*, 4> kCandidates{
    "libnvidia-ml.so.1",
    "nvml.dll",
    "libnvidia-ml.so",
    "C:\\Program Files\\NVIDIA Corporation\\NVSMI\\nvml.dll",
};

// Resolves the first name that exists, so a fallback chain reads as one call.
// Returns null when none of them does — the normal answer for an entry point
// added after the installed driver was built.
template <typename Fn>
[[nodiscard]] Fn resolve(const platform::DynamicLibrary& library,
                         std::span<const char* const> names) noexcept {
    for (const char* name : names) {
        if (Fn found = library.symbol<Fn>(name); found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

template <typename Fn>
[[nodiscard]] Fn resolve(const platform::DynamicLibrary& library, const char* name) noexcept {
    return library.symbol<Fn>(name);
}

}  // namespace

NvmlApi::~NvmlApi() {
    if (initialised_ && shutdown != nullptr) {
        // Return value ignored on purpose: nothing useful can be done about a
        // failed shutdown during teardown, and the library is closed next.
        (void)shutdown();
    }
}

std::shared_ptr<NvmlApi> NvmlApi::load() {
    std::optional<platform::DynamicLibrary> library =
        platform::DynamicLibrary::open(std::span<const char* const>(kCandidates));
    if (!library) {
        return nullptr;  // no NVIDIA driver on this machine
    }

    auto api = std::make_shared<NvmlApi>();

    // nvmlInit_v2 has been the entry point since driver 331 (2013). The
    // unversioned name is kept as a fallback because it costs one line and
    // covers anything older.
    {
        constexpr std::array<const char*, 2> names{"nvmlInit_v2", "nvmlInit"};
        api->init = resolve<InitFn>(*library, std::span<const char* const>(names));
    }
    api->shutdown = resolve<ShutdownFn>(*library, "nvmlShutdown");
    api->error_string = resolve<ErrorStringFn>(*library, "nvmlErrorString");

    {
        constexpr std::array<const char*, 2> names{"nvmlDeviceGetCount_v2",
                                                   "nvmlDeviceGetCount"};
        api->device_count = resolve<DeviceGetCountFn>(*library,
                                                      std::span<const char* const>(names));
    }
    {
        constexpr std::array<const char*, 2> names{"nvmlDeviceGetHandleByIndex_v2",
                                                   "nvmlDeviceGetHandleByIndex"};
        api->device_handle =
            resolve<DeviceGetHandleByIndexFn>(*library, std::span<const char* const>(names));
    }

    // Without these four there is nothing to enumerate. This is the one place
    // a missing symbol disqualifies the backend rather than a metric — and it
    // means the library that loaded is not NVML at all.
    if (api->init == nullptr || api->shutdown == nullptr || api->device_count == nullptr ||
        api->device_handle == nullptr) {
        return nullptr;
    }

    api->driver_version =
        resolve<SystemGetDriverVersionFn>(*library, "nvmlSystemGetDriverVersion");
    api->process_name = resolve<SystemGetProcessNameFn>(*library, "nvmlSystemGetProcessName");

    {
        constexpr std::array<const char*, 2> names{"nvmlDeviceGetName_v2", "nvmlDeviceGetName"};
        api->device_name = resolve<DeviceGetNameFn>(*library, std::span<const char* const>(names));
    }
    {
        constexpr std::array<const char*, 2> names{"nvmlDeviceGetUUID_v2", "nvmlDeviceGetUUID"};
        api->device_uuid = resolve<DeviceGetUuidFn>(*library, std::span<const char* const>(names));
    }
    {
        // All three fill the same nvmlPciInfo_t. The revisions changed how the
        // busId string is formatted, not the layout, so one type serves.
        constexpr std::array<const char*, 3> names{"nvmlDeviceGetPciInfo_v3",
                                                   "nvmlDeviceGetPciInfo_v2",
                                                   "nvmlDeviceGetPciInfo"};
        api->pci_info = resolve<DeviceGetPciInfoFn>(*library, std::span<const char* const>(names));
    }

    api->utilization =
        resolve<DeviceGetUtilizationFn>(*library, "nvmlDeviceGetUtilizationRates");
    api->memory_v2 = resolve<DeviceGetMemoryInfoV2Fn>(*library, "nvmlDeviceGetMemoryInfo_v2");
    api->memory_v1 = resolve<DeviceGetMemoryInfoFn>(*library, "nvmlDeviceGetMemoryInfo");
    api->power_usage = resolve<DeviceGetUintFn>(*library, "nvmlDeviceGetPowerUsage");
    api->power_limit = resolve<DeviceGetUintFn>(*library, "nvmlDeviceGetEnforcedPowerLimit");
    api->temperature = resolve<DeviceGetTemperatureFn>(*library, "nvmlDeviceGetTemperature");
    api->temperature_threshold =
        resolve<DeviceGetTempThresholdFn>(*library, "nvmlDeviceGetTemperatureThreshold");
    api->fan_speed = resolve<DeviceGetUintFn>(*library, "nvmlDeviceGetFanSpeed");
    api->clock_info = resolve<DeviceGetClockInfoFn>(*library, "nvmlDeviceGetClockInfo");
    api->encoder_util =
        resolve<DeviceGetCodecUtilFn>(*library, "nvmlDeviceGetEncoderUtilization");
    api->decoder_util =
        resolve<DeviceGetCodecUtilFn>(*library, "nvmlDeviceGetDecoderUtilization");
    api->pcie_throughput =
        resolve<DeviceGetPcieThroughputFn>(*library, "nvmlDeviceGetPcieThroughput");
    {
        // Renamed in driver 550: "throttle reasons" became "event reasons",
        // with the old name kept as an alias. Ask for the new one first so a
        // future driver that finally drops the alias still reports throttling.
        constexpr std::array<const char*, 2> names{
            "nvmlDeviceGetCurrentClocksEventReasons",
            "nvmlDeviceGetCurrentClocksThrottleReasons"};
        api->throttle_reasons =
            resolve<DeviceGetThrottleReasonsFn>(*library, std::span<const char* const>(names));
    }

    // The versioned chain that actually matters. _v3 and _v2 both write
    // nvmlProcessInfo_v2_t; the unversioned original writes the two-field
    // nvmlProcessInfo_v1_t. Resolve newest-first and record which layout won,
    // because passing v1 storage to a _v3 symbol would have it write two extra
    // fields past the end of every element.
    {
        constexpr std::array<const char*, 2> compute{"nvmlDeviceGetComputeRunningProcesses_v3",
                                                     "nvmlDeviceGetComputeRunningProcesses_v2"};
        constexpr std::array<const char*, 2> graphics{"nvmlDeviceGetGraphicsRunningProcesses_v3",
                                                      "nvmlDeviceGetGraphicsRunningProcesses_v2"};
        api->compute_processes_v2 =
            resolve<DeviceGetProcessesV2Fn>(*library, std::span<const char* const>(compute));
        api->graphics_processes_v2 =
            resolve<DeviceGetProcessesV2Fn>(*library, std::span<const char* const>(graphics));
    }

    if (api->compute_processes_v2 != nullptr || api->graphics_processes_v2 != nullptr) {
        api->process_api = ProcessApi::kV2;
    } else {
        api->compute_processes_v1 =
            resolve<DeviceGetProcessesV1Fn>(*library, "nvmlDeviceGetComputeRunningProcesses");
        api->graphics_processes_v1 =
            resolve<DeviceGetProcessesV1Fn>(*library, "nvmlDeviceGetGraphicsRunningProcesses");
        if (api->compute_processes_v1 != nullptr || api->graphics_processes_v1 != nullptr) {
            api->process_api = ProcessApi::kV1;
        }
    }

    api->process_utilization =
        resolve<DeviceGetProcessUtilFn>(*library, "nvmlDeviceGetProcessUtilization");

    if (api->init() != NVML_SUCCESS) {
        // A driver present but not loadable — a stale library after an upgrade,
        // or no /dev/nvidia* nodes. Nothing to report and nothing to shut down.
        return nullptr;
    }

    api->library_ = std::move(library);
    api->initialised_ = true;
    return api;
}

}  // namespace gtop::driver::nvml
