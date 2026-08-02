// NVIDIA telemetry. See nvml_driver.hpp for the shape, nvml_api.hpp for why
// nothing here is linked.

#include "driver/nvml/nvml_driver.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "platform/pci_power.hpp"

namespace gtop::driver::nvml {
namespace {

// NVML reports PCIe throughput in KB/s over a 20 ms window. The header says
// "1KB granularity"; the counter is a binary kilobyte, so this is 1024 and not
// 1000. A wrong factor here is a 2.4% error that looks exactly like a plausible
// reading, which is why it is a named constant rather than a literal.
constexpr std::uint64_t kPcieBytesPerKb = 1024;

constexpr unsigned int kNameBufferSize = 96;     // NVML_DEVICE_NAME_V2_BUFFER_SIZE
constexpr unsigned int kUuidBufferSize = 96;     // NVML_DEVICE_UUID_V2_BUFFER_SIZE
constexpr unsigned int kVersionBufferSize = 80;  // NVML_SYSTEM_DRIVER_VERSION_BUFFER_SIZE
constexpr unsigned int kProcessNameBufferSize = 256;

// A GPU that is genuinely gone. Everything else NVML can return — above all
// NVML_ERROR_NOT_SUPPORTED, which is the routine answer for a metric a consumer
// card does not expose — means one unset optional, not a retired device.
[[nodiscard]] constexpr bool is_device_loss(nvmlReturn_t status) noexcept {
    return status == NVML_ERROR_GPU_IS_LOST || status == NVML_ERROR_UNINITIALIZED ||
           status == NVML_ERROR_DRIVER_NOT_LOADED;
}

[[nodiscard]] std::string from_buffer(std::span<const char> buffer) {
    const auto end = std::find(buffer.begin(), buffer.end(), '\0');
    return std::string(buffer.begin(), end);
}

// NVML hands back a full path on Linux and a full path on Windows too. The UI
// has one narrow column, so it gets the leaf.
[[nodiscard]] std::string basename_of(std::string_view path) {
    const std::size_t cut = path.find_last_of("/\\");
    return std::string(cut == std::string_view::npos ? path : path.substr(cut + 1));
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

// NVML has nine reasons; core::ThrottleFlags has four buckets. The mapping is
// the interesting part, so it is one place rather than scattered through
// sample().
[[nodiscard]] core::ThrottleFlags to_flags(unsigned long long reasons) noexcept {
    core::ThrottleFlags flags;

    // Idle and an application-set clock cap are not faults. They are the
    // overwhelmingly common non-zero value — a desktop GPU sitting idle reports
    // GpuIdle constantly — so folding them into a badge would leave it lit
    // permanently and mean nothing.
    flags.software = (reasons & nvmlClocksEventReasonGpuIdle) != 0 ||
                     (reasons & nvmlClocksEventReasonApplicationsClocksSetting) != 0 ||
                     (reasons & nvmlClocksEventReasonSyncBoost) != 0 ||
                     (reasons & nvmlClocksEventReasonDisplayClockSetting) != 0;

    flags.power = (reasons & nvmlClocksEventReasonSwPowerCap) != 0;

    flags.thermal = (reasons & nvmlClocksEventReasonSwThermalSlowdown) != 0 ||
                    (reasons & nvmlClocksThrottleReasonHwThermalSlowdown) != 0;

    // HwSlowdown on its own is the unqualified hardware assert: NVML documents
    // it as temperature, power brake, or "other", without saying which. The
    // specific thermal and power-brake bits are checked above and beside it, so
    // what is left here is genuinely "the hardware intervened" — reliability.
    flags.reliability = (reasons & nvmlClocksThrottleReasonHwSlowdown) != 0 ||
                        (reasons & nvmlClocksThrottleReasonHwPowerBrakeSlowdown) != 0;

    return flags;
}

}  // namespace

NvmlDriver::NvmlDriver(std::shared_ptr<NvmlApi> api, nvmlDevice_t device)
    : api_(std::move(api)), device_(device) {
    info_.vendor = core::Vendor::kNvidia;
    read_static_info();
}

void NvmlDriver::note(nvmlReturn_t status) noexcept {
    if (is_device_loss(status)) {
        healthy_ = false;
    }
}

void NvmlDriver::read_static_info() {
    const NvmlApi& api = *api_;

    if (api.device_name != nullptr) {
        std::array<char, kNameBufferSize> buffer{};
        const nvmlReturn_t status = api.device_name(device_, buffer.data(), buffer.size());
        note(status);
        if (status == NVML_SUCCESS) {
            info_.name = from_buffer(buffer);
        }
    }

    if (api.device_uuid != nullptr) {
        std::array<char, kUuidBufferSize> buffer{};
        const nvmlReturn_t status = api.device_uuid(device_, buffer.data(), buffer.size());
        note(status);
        if (status == NVML_SUCCESS) {
            info_.uuid = from_buffer(buffer);
        }
    }

    if (api.pci_info != nullptr) {
        nvmlPciInfo_t pci{};
        const nvmlReturn_t status = api.pci_info(device_, &pci);
        note(status);
        if (status == NVML_SUCCESS) {
            // busId carries the eight-digit domain NVML prefers
            // ("00000000:01:00.0"); normalise_pci_bus_id reduces it to the
            // four-digit spelling sysfs and every other backend uses, which is
            // what makes this the cross-backend identity key.
            info_.pci_bus_id = core::normalise_pci_bus_id(from_buffer(pci.busId));
        }
    }

    if (api.driver_version != nullptr) {
        std::array<char, kVersionBufferSize> buffer{};
        const nvmlReturn_t status = api.driver_version(buffer.data(), buffer.size());
        note(status);
        if (status == NVML_SUCCESS) {
            info_.driver_version = from_buffer(buffer);
        }
    }

    // Total VRAM does not change, so it is read once here rather than on every
    // tick. The used figure beside it does, and is read in sample().
    if (api.memory_v2 != nullptr) {
        nvmlMemory_v2_t memory{};
        memory.version = nvmlMemory_v2;  // the struct is versioned; unset means rejected
        const nvmlReturn_t status = api.memory_v2(device_, &memory);
        note(status);
        if (status == NVML_SUCCESS) {
            info_.vram_total_bytes = memory.total;
        }
    }
    if (!info_.vram_total_bytes.has_value() && api.memory_v1 != nullptr) {
        nvmlMemory_t memory{};
        const nvmlReturn_t status = api.memory_v1(device_, &memory);
        note(status);
        if (status == NVML_SUCCESS) {
            info_.vram_total_bytes = memory.total;
        }
    }

    if (api.power_limit != nullptr) {
        unsigned int limit_mw = 0;
        const nvmlReturn_t status = api.power_limit(device_, &limit_mw);
        note(status);
        if (status == NVML_SUCCESS) {
            info_.power_limit_mw = limit_mw;
        }
    }

    if (api.temperature_threshold != nullptr) {
        unsigned int threshold_c = 0;
        const nvmlReturn_t status =
            api.temperature_threshold(device_, NVML_TEMPERATURE_THRESHOLD_SLOWDOWN, &threshold_c);
        note(status);
        if (status == NVML_SUCCESS) {
            info_.temp_slowdown_c = threshold_c;
        }
    }
}

core::PowerState NvmlDriver::power_state() const noexcept {
    // Answered from the OS, never from NVML. Any NVML device query resumes a
    // suspended GPU, so asking the library "are you asleep?" is the one way to
    // guarantee the answer is no.
    return to_core(platform::pci_power_state(info_.pci_bus_id));
}

core::DeviceSample NvmlDriver::sample() {
    core::DeviceSample out;
    out.timestamp = core::Clock::now();
    out.power_state = core::PowerState::kActive;

    const NvmlApi& api = *api_;

    if (api.utilization != nullptr) {
        nvmlUtilization_t util{};
        const nvmlReturn_t status = api.utilization(device_, &util);
        note(status);
        if (status == NVML_SUCCESS) {
            out.core_util = static_cast<float>(util.gpu);
            // NVML's "memory" utilization is the fraction of time the memory
            // controller was busy, not how full VRAM is. Two different numbers
            // that both get called "memory %" in other tools; this one is
            // bandwidth pressure.
            out.mem_controller_util = static_cast<float>(util.memory);
        }
    }

    if (api.memory_v2 != nullptr) {
        nvmlMemory_v2_t memory{};
        memory.version = nvmlMemory_v2;
        const nvmlReturn_t status = api.memory_v2(device_, &memory);
        note(status);
        if (status == NVML_SUCCESS) {
            out.vram_used_bytes = memory.used;
        }
    }
    if (!out.vram_used_bytes.has_value() && api.memory_v1 != nullptr) {
        nvmlMemory_t memory{};
        const nvmlReturn_t status = api.memory_v1(device_, &memory);
        note(status);
        if (status == NVML_SUCCESS) {
            out.vram_used_bytes = memory.used;
        }
    }

    if (api.temperature != nullptr) {
        unsigned int temp_c = 0;
        const nvmlReturn_t status = api.temperature(device_, NVML_TEMPERATURE_GPU, &temp_c);
        note(status);
        if (status == NVML_SUCCESS) {
            out.temp_edge_c = temp_c;
        }
    }
    // temp_hotspot_c and temp_mem_c stay unset. NVML's public surface exposes
    // one sensor; junction and memory temperatures are behind the field-value
    // API and are not published for GeForce parts at all. AMD supplies both, so
    // the fields exist — this is a vendor gap, and it renders as "—".

    if (api.power_usage != nullptr) {
        unsigned int draw_mw = 0;
        const nvmlReturn_t status = api.power_usage(device_, &draw_mw);
        note(status);
        if (status == NVML_SUCCESS) {
            out.power_draw_mw = draw_mw;
        }
    }

    if (api.power_limit != nullptr) {
        // Re-read every tick rather than reusing the static value: the enforced
        // limit moves with the laptop's power profile, and plotting draw
        // against a boot-time limit misreports the headroom that matters.
        unsigned int limit_mw = 0;
        const nvmlReturn_t status = api.power_limit(device_, &limit_mw);
        note(status);
        if (status == NVML_SUCCESS) {
            out.power_limit_mw = limit_mw;
        }
    }

    if (api.fan_speed != nullptr) {
        unsigned int percent = 0;
        const nvmlReturn_t status = api.fan_speed(device_, &percent);
        note(status);
        if (status == NVML_SUCCESS) {
            out.fan_percent = percent;
        }
    }

    if (api.clock_info != nullptr) {
        unsigned int mhz = 0;
        nvmlReturn_t status = api.clock_info(device_, NVML_CLOCK_GRAPHICS, &mhz);
        note(status);
        if (status == NVML_SUCCESS) {
            out.clock_core_mhz = mhz;
        }
        status = api.clock_info(device_, NVML_CLOCK_MEM, &mhz);
        note(status);
        if (status == NVML_SUCCESS) {
            out.clock_mem_mhz = mhz;
        }
    }

    if (api.encoder_util != nullptr) {
        unsigned int percent = 0;
        unsigned int sampling_us = 0;
        const nvmlReturn_t status = api.encoder_util(device_, &percent, &sampling_us);
        note(status);
        if (status == NVML_SUCCESS) {
            out.encoder_util = static_cast<float>(percent);
        }
    }

    if (api.decoder_util != nullptr) {
        unsigned int percent = 0;
        unsigned int sampling_us = 0;
        const nvmlReturn_t status = api.decoder_util(device_, &percent, &sampling_us);
        note(status);
        if (status == NVML_SUCCESS) {
            out.decoder_util = static_cast<float>(percent);
        }
    }

    if (api.pcie_throughput != nullptr) {
        unsigned int kb_per_second = 0;
        nvmlReturn_t status =
            api.pcie_throughput(device_, NVML_PCIE_UTIL_RX_BYTES, &kb_per_second);
        note(status);
        if (status == NVML_SUCCESS) {
            out.pcie_rx_bps = std::uint64_t{kb_per_second} * kPcieBytesPerKb;
        }
        status = api.pcie_throughput(device_, NVML_PCIE_UTIL_TX_BYTES, &kb_per_second);
        note(status);
        if (status == NVML_SUCCESS) {
            out.pcie_tx_bps = std::uint64_t{kb_per_second} * kPcieBytesPerKb;
        }
    }

    if (api.throttle_reasons != nullptr) {
        unsigned long long reasons = 0;
        const nvmlReturn_t status = api.throttle_reasons(device_, &reasons);
        note(status);
        if (status == NVML_SUCCESS) {
            out.throttle = to_flags(reasons);
        }
    }

    read_processes(out);
    return out;
}

namespace {

// The two-call sizing dance NVML uses for every list: ask with a null buffer to
// learn the count, then ask again with room. The count can grow between the two
// calls, so the loop retries rather than assuming the first answer holds.
template <typename Fn, typename Info>
[[nodiscard]] nvmlReturn_t collect(Fn function, nvmlDevice_t device, std::vector<Info>& out) {
    unsigned int count = 0;
    nvmlReturn_t status = function(device, &count, nullptr);
    if (status == NVML_SUCCESS) {
        out.clear();
        return NVML_SUCCESS;  // no processes on this device
    }
    if (status != NVML_ERROR_INSUFFICIENT_SIZE) {
        return status;
    }

    for (int attempt = 0; attempt < 3; ++attempt) {
        out.assign(count, Info{});
        status = function(device, &count, out.data());
        if (status == NVML_SUCCESS) {
            out.resize(count);
            return NVML_SUCCESS;
        }
        if (status != NVML_ERROR_INSUFFICIENT_SIZE) {
            out.clear();
            return status;
        }
        // A process started between the sizing call and this one. `count` now
        // holds the new requirement; go round again.
    }

    out.clear();
    return status;
}

}  // namespace

void NvmlDriver::read_processes(core::DeviceSample& out) {
    const NvmlApi& api = *api_;
    if (api.process_api == ProcessApi::kNone) {
        return;
    }

    // Compute and graphics are separate NVML calls with separate lists, and a
    // process can appear in both. Querying only compute — the obvious reading
    // of "running processes" — makes every game and the desktop compositor
    // invisible, so both are merged here by PID.
    std::unordered_map<core::ProcessId, core::ProcessInfo> merged;

    const auto absorb = [&merged](core::ProcessId pid, unsigned long long used_memory,
                                  bool compute) {
        core::ProcessInfo& info = merged[pid];
        info.pid = pid;
        (compute ? info.engines.compute : info.engines.graphics) = true;

        // NVML uses (unsigned long long)-1 for "the WDDM kernel owns this
        // allocation and I cannot see it" — a sentinel, not 16 exabytes.
        constexpr unsigned long long kNotAvailable = ~0ULL;
        if (used_memory != kNotAvailable) {
            info.vram_bytes = used_memory;
        }
    };

    if (api.process_api == ProcessApi::kV2) {
        std::vector<nvmlProcessInfo_v2_t> buffer;
        if (api.compute_processes_v2 != nullptr) {
            note(collect(api.compute_processes_v2, device_, buffer));
            for (const nvmlProcessInfo_v2_t& process : buffer) {
                absorb(process.pid, process.usedGpuMemory, true);
            }
        }
        if (api.graphics_processes_v2 != nullptr) {
            note(collect(api.graphics_processes_v2, device_, buffer));
            for (const nvmlProcessInfo_v2_t& process : buffer) {
                absorb(process.pid, process.usedGpuMemory, false);
            }
        }
    } else {
        std::vector<nvmlProcessInfo_v1_t> buffer;
        if (api.compute_processes_v1 != nullptr) {
            note(collect(api.compute_processes_v1, device_, buffer));
            for (const nvmlProcessInfo_v1_t& process : buffer) {
                absorb(process.pid, process.usedGpuMemory, true);
            }
        }
        if (api.graphics_processes_v1 != nullptr) {
            note(collect(api.graphics_processes_v1, device_, buffer));
            for (const nvmlProcessInfo_v1_t& process : buffer) {
                absorb(process.pid, process.usedGpuMemory, false);
            }
        }
    }

    if (merged.empty()) {
        return;
    }

    // Per-process utilization is best-effort and usually absent: it needs
    // accounting mode, which needs root. Memory works unprivileged, so the
    // table is useful either way and the percentage column renders "—" rather
    // than a fabricated 0%.
    if (api.process_utilization != nullptr) {
        unsigned int count = 0;
        nvmlReturn_t status = api.process_utilization(device_, nullptr, &count, 0);
        if (status == NVML_ERROR_INSUFFICIENT_SIZE && count > 0) {
            std::vector<nvmlProcessUtilizationSample_t> samples(count);
            status = api.process_utilization(device_, samples.data(), &count, 0);
            if (status == NVML_SUCCESS) {
                samples.resize(count);
                for (const nvmlProcessUtilizationSample_t& s : samples) {
                    const auto found = merged.find(s.pid);
                    if (found != merged.end()) {
                        found->second.gpu_util_percent = static_cast<float>(s.smUtil);
                        found->second.engines.encode |= s.encUtil > 0;
                        found->second.engines.decode |= s.decUtil > 0;
                    }
                }
            }
        }
        // NVML_ERROR_NOT_FOUND here just means the sample buffer is empty.
        // Not a device problem, so `note` is deliberately not called.
    }

    out.processes.reserve(merged.size());
    for (auto& [pid, info] : merged) {
        if (api.process_name != nullptr) {
            std::array<char, kProcessNameBufferSize> buffer{};
            if (api.process_name(pid, buffer.data(), buffer.size()) == NVML_SUCCESS) {
                info.name = basename_of(from_buffer(buffer));
            }
        }
        out.processes.push_back(std::move(info));
    }

    // A map iterates in hash order, which would reshuffle the table between
    // frames. PID order is stable and cheap; the UI sorts by whatever column
    // the user picked on top of it.
    std::sort(out.processes.begin(), out.processes.end(),
              [](const core::ProcessInfo& a, const core::ProcessInfo& b) {
                  return a.pid < b.pid;
              });
}

std::vector<std::unique_ptr<IGpuDriver>> probe() {
    std::vector<std::unique_ptr<IGpuDriver>> drivers;

    std::shared_ptr<NvmlApi> api = NvmlApi::load();
    if (!api) {
        return drivers;
    }

    unsigned int count = 0;
    if (api->device_count(&count) != NVML_SUCCESS) {
        return drivers;
    }

    for (unsigned int index = 0; index < count; ++index) {
        nvmlDevice_t device{};
        if (api->device_handle(index, &device) != NVML_SUCCESS) {
            // One unreadable device does not invalidate the others — a MIG
            // partition the caller lacks permission for looks exactly like
            // this, and the physical GPUs beside it are still fine.
            continue;
        }

        auto driver = std::make_unique<NvmlDriver>(api, device);
        if (driver->healthy()) {
            drivers.push_back(std::move(driver));
        }
    }

    return drivers;
}

}  // namespace gtop::driver::nvml
