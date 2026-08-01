#pragma once
//
// The one interface every vendor backend implements.
//
// A backend owns exactly one GPU. Backends that can see several — NVML on a
// multi-card box — construct one driver per device at probe time, so nothing
// above this line ever has to think about a vendor's enumeration model.
//
// The contract, in full:
//
//   * sample() must not throw, and must not fail. A driver that cannot read
//     the fan returns a DeviceSample with fan_percent unset and everything
//     else populated. Partial data is the normal outcome, not an error path.
//
//   * healthy() is the only failure signal, and it means one thing: this
//     device is gone and will not come back without re-probing. A card that
//     was hot-removed, a driver that unloaded, NVML answering GPU_IS_LOST.
//     It does not mean "one query failed" — that is what an empty optional is
//     for. The registry retires an unhealthy driver and re-probes its backend,
//     so flipping this flag over a transient read costs a device.
//
//   * power_state() must not wake the device. On a hybrid laptop the discrete
//     GPU is in D3cold most of the time; the whole point of asking is to avoid
//     resuming it, so answer from a source that does not touch the hardware
//     (sysfs runtime_status, a cached driver query) or answer kUnknown.
//
//   * static_info() is queried once and cached by the driver. It is called on
//     every frame by the UI and must be free.
//
// See ROADMAP.md task 2.1.
//
#include <string_view>

#include "core/types.hpp"

namespace gtop::driver {

class IGpuDriver {
public:
    IGpuDriver() = default;
    virtual ~IGpuDriver() = default;

    IGpuDriver(const IGpuDriver&) = delete;
    IGpuDriver& operator=(const IGpuDriver&) = delete;
    IGpuDriver(IGpuDriver&&) = delete;
    IGpuDriver& operator=(IGpuDriver&&) = delete;

    // Which backend this came from: "nvml", "amdgpu-sysfs", "level-zero".
    // Diagnostics only — never branch on it.
    [[nodiscard]] virtual std::string_view backend_name() const noexcept = 0;

    // Cached. Must not perform I/O.
    [[nodiscard]] virtual const core::GpuStaticInfo& static_info() const noexcept = 0;

    // One reading. Never throws; unreadable metrics come back unset.
    [[nodiscard]] virtual core::DeviceSample sample() = 0;

    // False means retire me. See the note above about what it does not mean.
    [[nodiscard]] virtual bool healthy() const noexcept = 0;

    // Defaults to kUnknown so a backend on hardware without runtime PM — a
    // desktop card, an integrated GPU — does not have to implement it. Backends
    // that do override it are promising the answer costs no wakeup.
    [[nodiscard]] virtual core::PowerState power_state() const noexcept {
        return core::PowerState::kUnknown;
    }
};

}  // namespace gtop::driver
