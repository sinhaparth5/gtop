#pragma once
//
// The NVML entry points gtop uses, bound at runtime.
//
// One implementation covers two of the six support cells: NVML is the same API
// on Linux and Windows and only the library filename differs, so everything
// OS-specific about NVIDIA support is the candidate list in nvml_api.cpp.
//
// Nothing here is linked. `nvml.h` is vendored for its types, enums and struct
// layouts; every function is resolved through platform::DynamicLibrary, and a
// null pointer is the expected answer for an entry point this driver
// generation does not export. That is what "a missing symbol is normal"
// (ROADMAP §0) means in practice: check the pointer, skip the metric, keep the
// device.
//
// The versioned chains are the sharp part. NVML has revised several functions
// in place — nvmlDeviceGetComputeRunningProcesses exists as v1, _v2 and _v3 —
// and the struct the caller passes must match *the symbol that resolved*, not
// the newest one the header knows about. Getting that pairing wrong does not
// fail to compile and does not fail to run; it silently reads adjacent memory.
// So the version that won is recorded alongside the pointer, and the sampling
// code branches on it.
//
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <nvidia/nvml.h>

#include "platform/dynamic_library.hpp"

namespace gtop::driver::nvml {

// Which running-process symbol resolved, and therefore which struct layout the
// library expects back. v2 and v3 share a layout (pid, usedGpuMemory,
// gpuInstanceId, computeInstanceId); v1 is the two-field original.
enum class ProcessApi {
    kNone,  // neither symbol present — process listing is simply unavailable
    kV1,
    kV2,  // covers _v2 and _v3, which agree on nvmlProcessInfo_v2_t
};

// Every pointer may be null. The only ones whose absence is fatal to the
// backend are init, shutdown, device count and device handle — without those
// there is nothing to enumerate, which `NvmlApi::load` treats as "NVML is not
// usable here" rather than as an error.
struct NvmlApi {
    // -- lifecycle ------------------------------------------------------------
    using InitFn = nvmlReturn_t (*)();
    using ShutdownFn = nvmlReturn_t (*)();
    using ErrorStringFn = const char* (*)(nvmlReturn_t);

    InitFn init{};
    ShutdownFn shutdown{};
    ErrorStringFn error_string{};

    // -- system ---------------------------------------------------------------
    using SystemGetDriverVersionFn = nvmlReturn_t (*)(char*, unsigned int);
    using SystemGetProcessNameFn = nvmlReturn_t (*)(unsigned int, char*, unsigned int);

    SystemGetDriverVersionFn driver_version{};
    SystemGetProcessNameFn process_name{};

    // -- enumeration -----------------------------------------------------------
    using DeviceGetCountFn = nvmlReturn_t (*)(unsigned int*);
    using DeviceGetHandleByIndexFn = nvmlReturn_t (*)(unsigned int, nvmlDevice_t*);

    DeviceGetCountFn device_count{};
    DeviceGetHandleByIndexFn device_handle{};

    // -- static identity --------------------------------------------------------
    using DeviceGetNameFn = nvmlReturn_t (*)(nvmlDevice_t, char*, unsigned int);
    using DeviceGetUuidFn = nvmlReturn_t (*)(nvmlDevice_t, char*, unsigned int);
    using DeviceGetPciInfoFn = nvmlReturn_t (*)(nvmlDevice_t, nvmlPciInfo_t*);

    DeviceGetNameFn device_name{};
    DeviceGetUuidFn device_uuid{};
    DeviceGetPciInfoFn pci_info{};

    // -- telemetry ---------------------------------------------------------------
    using DeviceGetUtilizationFn = nvmlReturn_t (*)(nvmlDevice_t, nvmlUtilization_t*);
    using DeviceGetMemoryInfoV2Fn = nvmlReturn_t (*)(nvmlDevice_t, nvmlMemory_v2_t*);
    using DeviceGetMemoryInfoFn = nvmlReturn_t (*)(nvmlDevice_t, nvmlMemory_t*);
    using DeviceGetUintFn = nvmlReturn_t (*)(nvmlDevice_t, unsigned int*);
    using DeviceGetTemperatureFn = nvmlReturn_t (*)(nvmlDevice_t, nvmlTemperatureSensors_t,
                                                    unsigned int*);
    using DeviceGetTempThresholdFn = nvmlReturn_t (*)(nvmlDevice_t, nvmlTemperatureThresholds_t,
                                                      unsigned int*);
    using DeviceGetClockInfoFn = nvmlReturn_t (*)(nvmlDevice_t, nvmlClockType_t, unsigned int*);
    using DeviceGetCodecUtilFn = nvmlReturn_t (*)(nvmlDevice_t, unsigned int*, unsigned int*);
    using DeviceGetPcieThroughputFn = nvmlReturn_t (*)(nvmlDevice_t, nvmlPcieUtilCounter_t,
                                                       unsigned int*);
    using DeviceGetThrottleReasonsFn = nvmlReturn_t (*)(nvmlDevice_t, unsigned long long*);

    DeviceGetUtilizationFn utilization{};
    DeviceGetMemoryInfoV2Fn memory_v2{};
    DeviceGetMemoryInfoFn memory_v1{};
    DeviceGetUintFn power_usage{};
    DeviceGetUintFn power_limit{};
    DeviceGetTemperatureFn temperature{};
    DeviceGetTempThresholdFn temperature_threshold{};
    DeviceGetUintFn fan_speed{};
    DeviceGetClockInfoFn clock_info{};
    DeviceGetCodecUtilFn encoder_util{};
    DeviceGetCodecUtilFn decoder_util{};
    DeviceGetPcieThroughputFn pcie_throughput{};
    DeviceGetThrottleReasonsFn throttle_reasons{};

    // -- running processes --------------------------------------------------------
    //
    // One field per struct layout, rather than one field cast between them.
    // `process_api` says which pair is populated, and the two are never both
    // set. Typing them separately means a mismatch between symbol and struct
    // is a compile error rather than a silent overread.
    using DeviceGetProcessesV1Fn = nvmlReturn_t (*)(nvmlDevice_t, unsigned int*,
                                                    nvmlProcessInfo_v1_t*);
    using DeviceGetProcessesV2Fn = nvmlReturn_t (*)(nvmlDevice_t, unsigned int*,
                                                    nvmlProcessInfo_v2_t*);
    using DeviceGetProcessUtilFn = nvmlReturn_t (*)(nvmlDevice_t,
                                                    nvmlProcessUtilizationSample_t*,
                                                    unsigned int*, unsigned long long);

    DeviceGetProcessesV1Fn compute_processes_v1{};
    DeviceGetProcessesV1Fn graphics_processes_v1{};
    DeviceGetProcessesV2Fn compute_processes_v2{};
    DeviceGetProcessesV2Fn graphics_processes_v2{};
    DeviceGetProcessUtilFn process_utilization{};
    ProcessApi process_api{ProcessApi::kNone};

    // Which file was opened — "libnvidia-ml.so.1", "nvml.dll". Diagnostics.
    [[nodiscard]] std::string_view library_name() const noexcept {
        return library_ ? library_->loaded_name() : std::string_view{};
    }

    // Opens NVML, resolves everything above, and calls nvmlInit. Returns null
    // when the library is absent, when it exports no usable enumeration API, or
    // when init fails — all three of which are ordinary outcomes on a machine
    // with no NVIDIA GPU.
    //
    // The returned object owns the nvmlShutdown: drivers hold a shared_ptr, and
    // NVML is released when the last device using it goes away.
    [[nodiscard]] static std::shared_ptr<NvmlApi> load();

    NvmlApi() = default;
    ~NvmlApi();

    NvmlApi(const NvmlApi&) = delete;
    NvmlApi& operator=(const NvmlApi&) = delete;
    NvmlApi(NvmlApi&&) = delete;
    NvmlApi& operator=(NvmlApi&&) = delete;

private:
    std::optional<platform::DynamicLibrary> library_;
    bool initialised_{false};
};

}  // namespace gtop::driver::nvml
