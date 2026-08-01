// Driver registry invariants.
//
// Everything here is about behaviour that real hardware will not reproduce on
// request: a card vanishing mid-run, two backends claiming one PCI address, a
// dGPU asleep in D3cold. The mock backend can produce all three on demand, and
// time is passed in explicitly, so nothing in this file sleeps.
//
// ROADMAP.md task 2.2.

#include <cassert>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "core/types.hpp"
#include "driver/driver_registry.hpp"
#include "mock/mock_driver.hpp"

namespace core = gtop::core;
namespace driver = gtop::driver;
using gtop::test::MockBackend;
using gtop::test::gpu;
using gtop::test::MockSpec;

namespace {

// Well clear of the epoch, so "never probed" (a default-constructed TimePoint)
// is unambiguously in the past.
const core::TimePoint kT0 = core::TimePoint{} + std::chrono::hours(1);

core::TimePoint after(std::chrono::seconds seconds) { return kT0 + seconds; }

driver::Backend backend_of(core::Vendor vendor, std::string_view name, MockBackend& mock) {
    return driver::Backend{vendor, name, mock.probe_fn()};
}

// -- PCI address canonicalisation --------------------------------------------

void test_pci_normalisation() {
    std::puts("pci bus id normalisation");

    // The three spellings of one card. NVML pads the domain to eight digits,
    // sysfs to four, lspci drops it. De-duplication depends on all three
    // collapsing to the same key.
    const std::string canonical = "0000:01:00.0";
    assert(core::normalise_pci_bus_id("00000000:01:00.0") == canonical);
    assert(core::normalise_pci_bus_id("0000:01:00.0") == canonical);
    assert(core::normalise_pci_bus_id("01:00.0") == canonical);
    assert(core::normalise_pci_bus_id("0000:1:0.0") == canonical);

    // Case and whitespace: NVML returns uppercase hex, sysfs reads arrive with
    // a trailing newline.
    assert(core::normalise_pci_bus_id("0000:0A:00.0") == "0000:0a:00.0");
    assert(core::normalise_pci_bus_id("  0000:01:00.0\n") == canonical);

    // A non-zero domain survives, in either spelling.
    assert(core::normalise_pci_bus_id("00000001:01:00.0") == "0001:01:00.0");
    assert(core::normalise_pci_bus_id("0001:01:00.0") == "0001:01:00.0");

    // Unparseable input comes back lowercased rather than empty: an
    // unrecognised key still has to compare equal to itself.
    assert(core::normalise_pci_bus_id("NOT-A-BUS-ID") == "not-a-bus-id");
    assert(core::normalise_pci_bus_id("0000:zz:00.0") == "0000:zz:00.0");
    assert(core::normalise_pci_bus_id("").empty());
    assert(core::normalise_pci_bus_id("   ").empty());
}

// -- enumeration --------------------------------------------------------------

void test_no_backends_is_not_an_error() {
    std::puts("empty registry");

    driver::DriverRegistry registry{driver::builtin_backends()};
    registry.probe(kT0);
    assert(registry.empty());
    assert(registry.sample_all(kT0).empty());
    assert(registry.retired_count() == 0);
}

void test_every_backend_contributes() {
    std::puts("hybrid enumeration");

    // This laptop: an NVIDIA dGPU and an Intel iGPU. Stopping at the first
    // backend that answers would hide half the hardware.
    MockBackend nvidia{"nvml", {gpu(core::Vendor::kNvidia, "0000:01:00.0", "RTX 3060")}};
    MockBackend intel{"i915", {gpu(core::Vendor::kIntel, "0000:00:02.0", "Iris Xe")}};

    std::vector<driver::Backend> backends;
    backends.push_back(backend_of(core::Vendor::kNvidia, "nvml", nvidia));
    backends.push_back(backend_of(core::Vendor::kIntel, "i915", intel));

    driver::DriverRegistry registry{std::move(backends)};
    registry.probe(kT0);
    assert(registry.size() == 2);

    // Ordered by PCI address, not by probe order: the iGPU at 00:02.0 sorts
    // ahead of the dGPU at 01:00.0 even though NVIDIA was probed first.
    assert(registry.device(0).static_info().name == "Iris Xe");
    assert(registry.device(1).static_info().name == "RTX 3060");
}

void test_one_gpu_appears_once() {
    std::puts("de-duplication");

    // amdgpu sysfs and ROCm SMI both see the same card, spelled differently.
    MockBackend rocm{"rocm-smi", {gpu(core::Vendor::kAmd, "00000000:03:00.0", "via ROCm")}};
    MockBackend sysfs{"amdgpu-sysfs", {gpu(core::Vendor::kAmd, "0000:03:00.0", "via sysfs")}};

    std::vector<driver::Backend> backends;
    backends.push_back(backend_of(core::Vendor::kAmd, "rocm-smi", rocm));
    backends.push_back(backend_of(core::Vendor::kAmd, "amdgpu-sysfs", sysfs));

    driver::DriverRegistry registry{std::move(backends)};
    registry.probe(kT0);

    assert(registry.size() == 1);
    // Probe order is the tie-break, so the richer backend keeps the device.
    assert(registry.device(0).static_info().name == "via ROCm");
}

void test_uuid_identifies_a_device_without_an_address() {
    std::puts("uuid fallback identity");

    MockSpec first = gpu(core::Vendor::kNvidia, "", "first");
    first.uuid = "GPU-1234";
    MockSpec second = gpu(core::Vendor::kNvidia, "", "second");
    second.uuid = "GPU-1234";

    MockBackend a{"a", {first}};
    MockBackend b{"b", {second}};

    std::vector<driver::Backend> backends;
    backends.push_back(backend_of(core::Vendor::kNvidia, "a", a));
    backends.push_back(backend_of(core::Vendor::kNvidia, "b", b));

    driver::DriverRegistry registry{std::move(backends)};
    registry.probe(kT0);
    assert(registry.size() == 1);
    assert(registry.device(0).static_info().name == "first");
}

void test_unidentifiable_devices_are_kept() {
    std::puts("unidentifiable devices");

    // No address and no uuid. Collapsing these into one would hide a real GPU,
    // which is worse than showing two rows that are hard to tell apart.
    MockBackend anonymous{"mystery",
                          {gpu(core::Vendor::kUnknown, "", "one"),
                           gpu(core::Vendor::kUnknown, "", "two")}};

    std::vector<driver::Backend> backends;
    backends.push_back(backend_of(core::Vendor::kUnknown, "mystery", anonymous));

    driver::DriverRegistry registry{std::move(backends)};
    registry.probe(kT0);
    assert(registry.size() == 2);
}

// -- power state --------------------------------------------------------------

void test_suspended_devices_are_reported_not_polled() {
    std::puts("suspended dGPU");

    MockSpec sleeping = gpu(core::Vendor::kNvidia, "0000:01:00.0", "RTX 3060");
    sleeping.power_state = core::PowerState::kSuspended;

    MockBackend nvidia{"nvml", {sleeping}};
    std::vector<driver::Backend> backends;
    backends.push_back(backend_of(core::Vendor::kNvidia, "nvml", nvidia));

    driver::DriverRegistry registry{std::move(backends)};
    registry.probe(kT0);
    assert(registry.size() == 1);

    const std::vector<core::DeviceReading> readings = registry.sample_all(kT0);
    assert(readings.size() == 1);
    assert(readings[0].sample.power_state == core::PowerState::kSuspended);

    // The device is present in the output, with no metrics, and was never
    // touched. Waking a sleeping dGPU once a second costs real battery life.
    assert(!readings[0].sample.core_util.has_value());
    assert(gtop::test::as_mock(registry.device(0)).sample_count() == 0);
    assert(readings[0].sample.timestamp == kT0);
}

void test_active_devices_are_sampled() {
    std::puts("active sampling");

    MockBackend nvidia{"nvml", {gpu(core::Vendor::kNvidia, "0000:01:00.0", "RTX 3060")}};
    std::vector<driver::Backend> backends;
    backends.push_back(backend_of(core::Vendor::kNvidia, "nvml", nvidia));

    driver::DriverRegistry registry{std::move(backends)};
    registry.probe(kT0);

    const std::vector<core::DeviceReading> readings = registry.sample_all(kT0);
    assert(readings.size() == 1);
    assert(readings[0].backend == "nvml");
    assert(readings[0].info.vendor == core::Vendor::kNvidia);
    assert(readings[0].sample.core_util.has_value());
    assert(readings[0].sample.power_state == core::PowerState::kActive);
    assert(gtop::test::as_mock(registry.device(0)).sample_count() == 1);
}

// -- lifecycle ----------------------------------------------------------------

void test_lost_device_is_retired_and_reacquired() {
    std::puts("retirement and recovery");

    MockSpec doomed = gpu(core::Vendor::kNvidia, "0000:01:00.0", "RTX 3060");
    doomed.fail_after = 1;  // the first read discovers the loss

    MockBackend nvidia{"nvml", {doomed}};
    std::vector<driver::Backend> backends;
    backends.push_back(backend_of(core::Vendor::kNvidia, "nvml", nvidia));

    driver::DriverRegistry registry{std::move(backends)};
    registry.probe(kT0);
    assert(registry.size() == 1);

    // The reading that discovered the loss is discarded along with the driver.
    assert(registry.sample_all(kT0).empty());
    assert(registry.empty());
    assert(registry.retired_count() == 1);

    // Losing a device schedules the backend to be asked again, and the app
    // keeps running in the meantime.
    nvidia.set_devices({gpu(core::Vendor::kNvidia, "0000:01:00.0", "RTX 3060")});
    const int probes_before = nvidia.probe_count();
    registry.probe(after(std::chrono::seconds(1)));
    assert(nvidia.probe_count() == probes_before && "too early");

    registry.probe(after(driver::DriverRegistry::kRetryInterval + std::chrono::seconds(1)));
    assert(nvidia.probe_count() == probes_before + 1);
    assert(registry.size() == 1);
}

void test_a_backend_already_holding_devices_is_left_alone() {
    std::puts("probe scheduling");

    // Re-probing a healthy backend rebuilds drivers it already has, and
    // building one means re-running the expensive static queries just to have
    // the result dropped by de-duplication.
    MockBackend nvidia{"nvml", {gpu(core::Vendor::kNvidia, "0000:01:00.0", "RTX 3060")}};
    std::vector<driver::Backend> backends;
    backends.push_back(backend_of(core::Vendor::kNvidia, "nvml", nvidia));

    driver::DriverRegistry registry{std::move(backends)};
    registry.probe(kT0);
    assert(nvidia.probe_count() == 1);

    registry.probe(after(std::chrono::hours(1)));
    assert(nvidia.probe_count() == 1);
    assert(registry.size() == 1);
}

void test_an_empty_backend_is_asked_again() {
    std::puts("Optimus re-probe");

    // nvmlInit() succeeding with a device count of zero is what a D3cold
    // discrete GPU looks like. That is a temporary state, not an absent card,
    // so the backend must not be written off.
    MockBackend nvidia{"nvml", {}};
    std::vector<driver::Backend> backends;
    backends.push_back(backend_of(core::Vendor::kNvidia, "nvml", nvidia));

    driver::DriverRegistry registry{std::move(backends)};
    registry.probe(kT0);
    assert(registry.empty());
    assert(nvidia.probe_count() == 1);

    registry.probe(after(std::chrono::seconds(1)));
    assert(nvidia.probe_count() == 1 && "retry interval not honoured");

    nvidia.set_devices({gpu(core::Vendor::kNvidia, "0000:01:00.0", "RTX 3060")});
    registry.probe(after(driver::DriverRegistry::kRetryInterval));
    assert(nvidia.probe_count() == 2);
    assert(registry.size() == 1);
}

void test_a_dead_driver_is_never_sampled() {
    std::puts("born-unhealthy driver");

    MockSpec stillborn = gpu(core::Vendor::kAmd, "0000:03:00.0", "gone already");
    stillborn.healthy = false;

    MockBackend amd{"amdgpu-sysfs", {stillborn}};
    std::vector<driver::Backend> backends;
    backends.push_back(backend_of(core::Vendor::kAmd, "amdgpu-sysfs", amd));

    driver::DriverRegistry registry{std::move(backends)};
    registry.probe(kT0);
    assert(registry.size() == 1);

    assert(registry.sample_all(kT0).empty());
    assert(registry.empty());
    assert(registry.retired_count() == 1);
}

}  // namespace

int main() {
    std::puts("driver registry");

    test_pci_normalisation();
    test_no_backends_is_not_an_error();
    test_every_backend_contributes();
    test_one_gpu_appears_once();
    test_uuid_identifies_a_device_without_an_address();
    test_unidentifiable_devices_are_kept();
    test_suspended_devices_are_reported_not_polled();
    test_active_devices_are_sampled();
    test_lost_device_is_retired_and_reacquired();
    test_a_backend_already_holding_devices_is_left_alone();
    test_an_empty_backend_is_asked_again();
    test_a_dead_driver_is_never_sampled();

    std::puts("ok");
    return 0;
}
