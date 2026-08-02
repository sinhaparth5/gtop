#pragma once
//
// Runtime power state of a PCI device, answered without touching the device.
//
// This lives in the platform layer for two reasons. It is genuinely OS-specific
// — Linux publishes it in sysfs, Windows does not publish it at all — and every
// vendor backend needs it, so putting it here is what keeps `#ifdef` out of
// driver code entirely.
//
// The contract is narrow and load-bearing: **asking must not wake the device.**
// On an Optimus laptop the discrete GPU sits in D3cold whenever nothing is
// using it, and the entire point of this call is to let the sampler skip it.
// A query that resumed the card to answer "it was asleep" would burn the
// battery it exists to save, so an implementation that cannot answer for free
// answers kUnknown.
//
// kUnknown is the honest, useful default: the caller samples as normal, which
// is exactly right on a desktop card with no runtime PM.
//
#include <string_view>

namespace gtop::platform {

enum class DevicePowerState {
    kUnknown,    // no runtime PM, or the OS does not expose it
    kActive,     // awake
    kSuspended,  // runtime-suspended; do not poll
};

// `pci_bus_id` is a PCI address in any common spelling — "0000:01:00.0",
// "00000000:01:00.0", or "01:00.0". Anything unparseable yields kUnknown
// rather than a guess.
[[nodiscard]] DevicePowerState pci_power_state(std::string_view pci_bus_id) noexcept;

}  // namespace gtop::platform
