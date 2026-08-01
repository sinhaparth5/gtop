// Device discovery and lifecycle. See driver_registry.hpp for the rules.

#include "driver/driver_registry.hpp"

#include <algorithm>
#include <cassert>
#include <iterator>
#include <utility>

namespace gtop::driver {

std::vector<Backend> builtin_backends() {
    // Empty until Phase 3. The registry, the ordering, the de-duplication and
    // the retry logic are all exercised by tests/mock/mock_driver.hpp in the
    // meantime, so this list grows by one line per vendor rather than by a
    // redesign:
    //
    //     {core::Vendor::kNvidia, "nvml", &nvml::probe},
    //
    // Order is significant — see the header.
    return {};
}

DriverRegistry::DriverRegistry(std::vector<Backend> backends) {
    backends_.reserve(backends.size());
    for (Backend& backend : backends) {
        backends_.push_back(BackendState{std::move(backend), core::TimePoint{}, 0});
    }
}

const IGpuDriver& DriverRegistry::device(std::size_t index) const {
    assert(index < devices_.size() && "device index out of range");
    return *devices_[index].driver;
}

bool DriverRegistry::adopt(std::unique_ptr<IGpuDriver> driver, std::size_t backend_index) {
    if (!driver) {
        return false;
    }

    const core::GpuStaticInfo& info = driver->static_info();
    std::string key = core::normalise_pci_bus_id(info.pci_bus_id);
    if (key.empty() && !info.uuid.empty()) {
        key = "uuid:" + info.uuid;
    }

    if (key.empty()) {
        // No address and no uuid — a backend that cannot say what it found.
        // Adopt it anyway, because refusing would hide a real GPU, but give it
        // a key that collides with nothing, including a later copy of itself.
        key = "anon:" + std::to_string(anonymous_++);
    } else {
        const auto known = std::find_if(devices_.begin(), devices_.end(),
                                        [&key](const Entry& e) { return e.key == key; });
        if (known != devices_.end()) {
            return false;
        }
    }

    devices_.push_back(Entry{std::move(driver), backend_index, std::move(key)});
    ++backends_[backend_index].live;
    return true;
}

void DriverRegistry::sort_devices() {
    std::sort(devices_.begin(), devices_.end(),
              [](const Entry& a, const Entry& b) { return a.key < b.key; });
}

void DriverRegistry::probe(core::TimePoint now) {
    bool adopted = false;

    for (std::size_t i = 0; i < backends_.size(); ++i) {
        BackendState& state = backends_[i];
        if (now < state.next_probe) {
            continue;
        }

        for (std::unique_ptr<IGpuDriver>& found : state.backend.probe()) {
            adopted |= adopt(std::move(found), i);
        }

        // A backend holding devices is left alone: re-probing it would rebuild
        // drivers it already has, and building one means re-running the
        // expensive static queries just to have the result dropped by
        // de-duplication. A backend holding none is asked again — that is the
        // suspended-dGPU case, and it does resolve itself.
        state.next_probe =
            state.live == 0 ? now + kRetryInterval : core::TimePoint::max();
    }

    if (adopted) {
        sort_devices();
    }
}

std::vector<core::DeviceReading> DriverRegistry::sample_all(core::TimePoint now) {
    std::vector<core::DeviceReading> readings;
    readings.reserve(devices_.size());

    for (std::size_t i = 0; i < devices_.size();) {
        IGpuDriver& driver = *devices_[i].driver;

        core::DeviceReading reading;
        bool alive = driver.healthy();

        if (alive) {
            reading.backend = driver.backend_name();
            reading.info = driver.static_info();

            const core::PowerState state = driver.power_state();
            if (state == core::PowerState::kSuspended) {
                // Reported, not polled. The device stays asleep.
                reading.sample.timestamp = now;
                reading.sample.power_state = state;
            } else {
                reading.sample = driver.sample();
                if (reading.sample.timestamp == core::TimePoint{}) {
                    reading.sample.timestamp = now;
                }
                if (reading.sample.power_state == core::PowerState::kUnknown) {
                    reading.sample.power_state = state;
                }
            }

            // Loss is usually discovered by the read that hit it.
            alive = driver.healthy();
        }

        if (!alive) {
            BackendState& owner = backends_[devices_[i].backend];
            --owner.live;
            // Ask once more, soon. If the device comes back it is adopted
            // again; if it does not and the backend still holds others, the
            // probe finds nothing new and scheduling stops there.
            owner.next_probe = now + kRetryInterval;

            devices_.erase(devices_.begin() + static_cast<std::ptrdiff_t>(i));
            ++retired_;
            continue;
        }

        readings.push_back(std::move(reading));
        ++i;
    }

    return readings;
}

}  // namespace gtop::driver
