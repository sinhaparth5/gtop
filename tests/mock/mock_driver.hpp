#pragma once
//
// A fake IGpuDriver, so the registry can be tested on a machine with no GPU —
// including the cases real hardware will not reproduce on demand: a card that
// disappears mid-run, one physical GPU that two backends both claim, a discrete
// GPU asleep in D3cold.
//
// Test-only. The user-facing `--driver=mock` backend is ROADMAP.md Phase 9; this
// header is not it, and deliberately lives outside src/ so it cannot be linked
// into the shipping binary by accident.
//
// Drivers are configured at construction rather than mutated afterwards. The
// registry takes ownership and may destroy a driver at any point — dedup drops
// one on the spot, retirement deletes another — so a test holding a pointer to
// one would be holding a dangling pointer half the time. Read state back
// through DriverRegistry::device() instead.
//
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/types.hpp"
#include "driver/driver_registry.hpp"
#include "driver/gpu_driver.hpp"

namespace gtop::test {

struct MockSpec {
    core::Vendor vendor{core::Vendor::kUnknown};
    std::string pci_bus_id;
    std::string name{"Mock GPU"};
    std::string uuid;
    core::PowerState power_state{core::PowerState::kUnknown};

    // Report loss on the Nth sample, the way a real driver discovers a card is
    // gone: the read that hits it is the one that fails. Negative means never.
    int fail_after{-1};

    // Born already lost — a driver that probed successfully and then found the
    // device missing before the first read.
    bool healthy{true};
};

// The common case, spelled without a brace-init that has to name every field.
[[nodiscard]] inline MockSpec gpu(core::Vendor vendor, std::string pci_bus_id,
                                  std::string name = "Mock GPU") {
    MockSpec spec;
    spec.vendor = vendor;
    spec.pci_bus_id = std::move(pci_bus_id);
    spec.name = std::move(name);
    return spec;
}

class MockDriver final : public driver::IGpuDriver {
public:
    MockDriver(std::string_view backend, MockSpec spec) : backend_(backend), spec_(std::move(spec)) {
        info_.vendor = spec_.vendor;
        info_.name = spec_.name;
        info_.pci_bus_id = spec_.pci_bus_id;
        info_.uuid = spec_.uuid;
        healthy_ = spec_.healthy;
    }

    // How many times sample() was actually called. "Do not poll a suspended
    // device" is only provable by showing this stays at zero.
    [[nodiscard]] int sample_count() const noexcept { return samples_; }

    [[nodiscard]] std::string_view backend_name() const noexcept override { return backend_; }
    [[nodiscard]] const core::GpuStaticInfo& static_info() const noexcept override {
        return info_;
    }
    [[nodiscard]] bool healthy() const noexcept override { return healthy_; }
    [[nodiscard]] core::PowerState power_state() const noexcept override {
        return spec_.power_state;
    }

    [[nodiscard]] core::DeviceSample sample() override {
        ++samples_;
        if (spec_.fail_after >= 0 && samples_ >= spec_.fail_after) {
            healthy_ = false;
        }
        core::DeviceSample reading;
        reading.timestamp = core::Clock::now();
        reading.power_state = core::PowerState::kActive;
        reading.core_util = 42.5F;
        return reading;
    }

private:
    std::string_view backend_;
    MockSpec spec_;
    core::GpuStaticInfo info_;
    bool healthy_{true};
    int samples_{0};
};

// A probe returning a fixed set of devices, rebuilt on every call so the
// registry's re-probe path constructs new objects exactly as a real backend
// would.
class MockBackend {
public:
    MockBackend(std::string_view name, std::vector<MockSpec> devices)
        : name_(name), devices_(std::move(devices)) {}

    // How many times the registry has asked. Retry policy is tested by watching
    // this, rather than by sleeping.
    [[nodiscard]] int probe_count() const noexcept { return probes_; }

    void set_devices(std::vector<MockSpec> devices) { devices_ = std::move(devices); }

    [[nodiscard]] driver::ProbeFn probe_fn() {
        return [this] {
            ++probes_;
            std::vector<std::unique_ptr<driver::IGpuDriver>> made;
            made.reserve(devices_.size());
            for (const MockSpec& spec : devices_) {
                made.push_back(std::make_unique<MockDriver>(name_, spec));
            }
            return made;
        };
    }

private:
    std::string_view name_;
    std::vector<MockSpec> devices_;
    int probes_{0};
};

// The registry hands back the base interface; tests know better.
[[nodiscard]] inline const MockDriver& as_mock(const driver::IGpuDriver& driver) {
    return static_cast<const MockDriver&>(driver);
}

}  // namespace gtop::test
