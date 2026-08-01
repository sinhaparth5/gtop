#pragma once
//
// The vocabulary every other layer speaks.
//
// One rule shapes this file: **every dynamic metric is std::optional**. Six
// vendor/OS cells expose different subsets of telemetry, and what is readable
// changes at runtime with permissions and power state. Optionality is not
// defensive coding here, it is the honest type — and it is what lets one UI
// render all six without a special case per vendor.
//
// An absent metric renders as "—". It never becomes 0: an unreadable sensor
// and a genuine zero watts are different facts, and conflating them is how a
// monitor ends up lying to someone at 3 a.m. See ROADMAP.md task 2.1.
//
// Units are in the field names on purpose. Vendors disagree — NVML reports
// milliwatts, amdgpu sysfs microwatts, ADLX watts — so the name of the field
// is where the conversion is pinned down.
//
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gtop::core {

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

// Matches platform::ProcessId. core depends on nothing, so it owns its own
// alias rather than reaching down a layer for one.
using ProcessId = std::uint32_t;

enum class Vendor { kUnknown, kNvidia, kAmd, kIntel };

[[nodiscard]] std::string_view to_string(Vendor vendor) noexcept;

// Runtime power management state.
//
// A discrete GPU in an Optimus/hybrid laptop spends most of its life in D3cold.
// Polling it wakes it, and waking it costs real battery — so kSuspended is a
// state to *display*, never a problem to fix by querying harder.
enum class PowerState {
    kUnknown,    // backend cannot tell, or does not implement runtime PM
    kActive,     // awake; safe to sample
    kSuspended,  // asleep. Do not sample. Do not wake.
};

[[nodiscard]] std::string_view to_string(PowerState state) noexcept;

// Why the hardware is clocking below what it could sustain.
//
// Vendors report this with wildly different granularity — NVML has nine
// distinct reasons, Intel exposes seven sysfs files, AMD sysfs has almost
// nothing. These four buckets are the intersection that is meaningful to
// somebody watching a training run, and each vendor backend maps its own
// reasons onto them.
struct ThrottleFlags {
    bool thermal{};      // a temperature limit was reached
    bool power{};        // a power or current limit was reached
    bool reliability{};  // the electrical envelope: VR thermal, PROCHOT, power brake
    bool software{};     // driver-imposed clock cap, or plain idle — not a fault

    // Something is holding clocks down.
    [[nodiscard]] constexpr bool any() const noexcept {
        return thermal || power || reliability || software;
    }

    // Something is holding clocks down *and it is the hardware protecting
    // itself*. This is the one that earns a badge in the UI; `software` alone
    // usually just means the GPU is idle.
    [[nodiscard]] constexpr bool fault() const noexcept {
        return thermal || power || reliability;
    }
};

// Which parts of the GPU a process actually touched.
//
// DRM fdinfo reports engine time per class, and the vendor tools do the same on
// Windows. A video transcode and a tensor workload both read as "busy" without
// this breakdown.
struct EngineFlags {
    bool graphics{};
    bool compute{};
    bool encode{};
    bool decode{};
    bool copy{};

    [[nodiscard]] constexpr bool any() const noexcept {
        return graphics || compute || encode || decode || copy;
    }
};

struct ProcessInfo {
    ProcessId pid{};
    std::string name;

    // Per-process memory is generally readable unprivileged. Per-process
    // utilization frequently is not — NVML needs accounting mode, which needs
    // root — so it stays optional and renders as "—" rather than 0%.
    std::optional<std::uint64_t> vram_bytes;
    std::optional<float> gpu_util_percent;

    EngineFlags engines;
};

// Queried once, at device init. Everything here is either expensive (string
// formatting, PCI topology walks) or immutable for the life of the process.
struct GpuStaticInfo {
    Vendor vendor{Vendor::kUnknown};
    std::string name;            // "NVIDIA GeForce RTX 3060 Laptop GPU"
    std::string uuid;            // vendor-assigned, stable across reboots
    std::string pci_bus_id;      // canonical "0000:01:00.0" — the identity key
    std::string driver_version;  // "580.173.02"

    std::optional<std::uint64_t> vram_total_bytes;
    std::optional<std::uint32_t> power_limit_mw;   // enforced limit, not the sticker rating
    std::optional<std::uint32_t> temp_slowdown_c;  // where the hardware starts protecting itself
};

// Queried every tick. Cheap by construction: no strings except process names,
// no allocation beyond the process vector.
struct DeviceSample {
    TimePoint timestamp{};
    PowerState power_state{PowerState::kUnknown};

    std::optional<float> core_util;            // percent, 0..100
    std::optional<float> mem_controller_util;  // percent — bandwidth pressure, not occupancy
    std::optional<float> encoder_util;
    std::optional<float> decoder_util;

    std::optional<std::uint64_t> vram_used_bytes;

    std::optional<std::uint32_t> temp_edge_c;     // package / board
    std::optional<std::uint32_t> temp_hotspot_c;  // junction — the one that throttles
    std::optional<std::uint32_t> temp_mem_c;      // VRAM, usually HBM/GDDR6X only

    std::optional<std::uint32_t> power_draw_mw;
    std::optional<std::uint32_t> power_limit_mw;  // may move at runtime; static_info holds the boot value

    std::optional<std::uint32_t> fan_percent;
    std::optional<std::uint32_t> clock_core_mhz;
    std::optional<std::uint32_t> clock_mem_mhz;

    std::optional<std::uint64_t> pcie_rx_bps;
    std::optional<std::uint64_t> pcie_tx_bps;

    ThrottleFlags throttle;
    std::vector<ProcessInfo> processes;
};

// A device and the reading that came off it. What the registry hands upward,
// and what --dump-json serialises.
struct DeviceReading {
    GpuStaticInfo info;
    DeviceSample sample;
    std::string_view backend;  // which backend produced it, for diagnostics
};

// Canonical form of a PCI address: lowercase "dddd:bb:dd.f".
//
// This is the device identity key, so it has to survive vendors that disagree
// about how to spell it. NVML pads the domain to eight digits
// ("00000000:01:00.0"), sysfs uses four ("0000:01:00.0"), and some tools drop
// the domain entirely ("01:00.0"). All three name the same card, and one
// physical GPU appearing twice in the device list is the bug this prevents.
//
// Input that does not parse is returned lowercased and otherwise untouched:
// a key that is merely unrecognised still compares equal to itself.
[[nodiscard]] std::string normalise_pci_bus_id(std::string_view raw);

}  // namespace gtop::core
