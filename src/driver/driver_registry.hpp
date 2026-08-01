#pragma once
//
// Device discovery and lifecycle.
//
// The registry is the only thing that knows more than one backend exists. It
// probes each in turn, keeps every device it finds, and presents the result as
// one flat ordered list — which is what lets the UI iterate GPUs without ever
// asking who made them.
//
// Four behaviours here are load-bearing, and all four exist because of how real
// machines behave rather than how a clean design would prefer them to:
//
//   * **Every backend that succeeds contributes.** This laptop has an NVIDIA
//     dGPU and an Intel iGPU; a workstation may have three vendors. Stopping at
//     the first backend that works would hide half the hardware.
//
//   * **One physical GPU appears once.** Backends overlap: amdgpu sysfs and
//     ROCm SMI both see the same card, and a machine with both installed would
//     otherwise show it twice. The PCI address is the identity key, and the
//     first backend to claim an address wins.
//
//   * **Order is by PCI bus ID, not enumeration order.** Vendor libraries do
//     not promise a stable index across driver reloads, and "GPU 0" silently
//     becoming a different card between runs makes every saved layout and every
//     screenshot a lie.
//
//   * **A backend that finds nothing gets asked again.** On a hybrid laptop
//     nvmlInit() can succeed while the device count is zero, because the
//     discrete GPU is in D3cold. That is a temporary state, not an absent
//     card. Backends contributing no devices are re-probed on an interval;
//     backends that found something are left alone.
//
// See ROADMAP.md task 2.2.
//
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/types.hpp"
#include "driver/gpu_driver.hpp"

namespace gtop::driver {

// Constructs every driver a vendor library can see, or returns empty when the
// library is absent, the driver is not loaded, or the machine has no such
// hardware. All three are ordinary and none is an error.
using ProbeFn = std::function<std::vector<std::unique_ptr<IGpuDriver>>()>;

struct Backend {
    core::Vendor vendor{core::Vendor::kUnknown};
    std::string_view name;
    ProbeFn probe;
};

// NVIDIA, then AMD, then Intel. The order decides who wins a duplicate PCI
// address, so it is API, not preference: the vendor's own library beats a
// generic sysfs reader for the same card.
[[nodiscard]] std::vector<Backend> builtin_backends();

class DriverRegistry {
public:
    // How long before a backend that found nothing is asked again. Five seconds
    // is short enough that plugging in an eGPU or waking a dGPU shows up while
    // you are still looking at the screen, and long enough that probing costs
    // nothing measurable.
    static constexpr auto kRetryInterval = std::chrono::seconds(5);

    DriverRegistry() : DriverRegistry(builtin_backends()) {}
    explicit DriverRegistry(std::vector<Backend> backends);

    // Probe every backend that is due. Cheap and idempotent — call it on a
    // timer. Devices already present are not re-created.
    void probe(core::TimePoint now = core::Clock::now());

    // Read every live device once.
    //
    // Suspended devices are reported, not polled: the returned sample carries
    // kSuspended and no metrics. Waking a sleeping dGPU to fill in a number
    // nobody asked for is a battery-life bug, and doing it once a second is a
    // serious one.
    //
    // Drivers that report unhealthy are retired here, and their backend is
    // scheduled for a re-probe.
    [[nodiscard]] std::vector<core::DeviceReading> sample_all(
        core::TimePoint now = core::Clock::now());

    [[nodiscard]] std::size_t size() const noexcept { return devices_.size(); }
    [[nodiscard]] bool empty() const noexcept { return devices_.empty(); }
    [[nodiscard]] const IGpuDriver& device(std::size_t index) const;

    // How many devices have been retired since construction. Diagnostics: a
    // number that keeps climbing means a backend is flapping.
    [[nodiscard]] std::size_t retired_count() const noexcept { return retired_; }

private:
    struct Entry {
        std::unique_ptr<IGpuDriver> driver;
        std::size_t backend{};  // index into backends_, for retry scheduling
        std::string key;        // normalised PCI address, or a uuid fallback
    };

    struct BackendState {
        Backend backend;
        core::TimePoint next_probe{};  // epoch — the first probe is always due
        std::size_t live{};
    };

    // False when this device is already known, in which case the driver is
    // dropped. Called with probe order as the tie-break, so the first backend
    // to claim a PCI address keeps it.
    [[nodiscard]] bool adopt(std::unique_ptr<IGpuDriver> driver, std::size_t backend_index);

    void sort_devices();

    std::vector<BackendState> backends_;
    std::vector<Entry> devices_;
    std::size_t retired_{};
    std::size_t anonymous_{};  // suffix for devices with no address and no uuid
};

}  // namespace gtop::driver
