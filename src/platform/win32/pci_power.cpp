// PCI runtime power state — Windows.
//
// Windows has no cheap equivalent of sysfs runtime_status. CM_Get_DevNode_Status
// reports whether a device node is started, not whether it is in D3; the actual
// D-state lives behind WMI or a driver IOCTL, both of which cost far more than
// the sample they would let us skip, and at least one of which can resume the
// device to answer.
//
// So this answers kUnknown, which makes the sampler poll as it would on any
// desktop card. That is correct rather than merely acceptable: the state exists
// to protect a hybrid laptop's battery, and on Windows that job belongs to the
// vendor's own driver-side power management, which NVML queries do not defeat
// the way an unconditional sysfs poll would on Linux.

#include "platform/pci_power.hpp"

#include <string_view>

namespace gtop::platform {

DevicePowerState pci_power_state(std::string_view /*pci_bus_id*/) noexcept {
    return DevicePowerState::kUnknown;
}

}  // namespace gtop::platform
