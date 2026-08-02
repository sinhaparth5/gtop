// Which backends exist on Linux, and in what order they are asked.
//
// The Windows list lives beside this one in driver/win32/backends.cpp. Two
// files rather than two branches: NVML is genuinely the same implementation on
// both, but AMD and Intel are not, and expressing that as a preprocessor
// conditional would put the first `#ifdef` outside src/platform/ (ROADMAP §0
// rule 2).

#include "driver/driver_registry.hpp"

#include "driver/amd/amdgpu_sysfs_driver.hpp"
#include "driver/intel/intel_sysfs_driver.hpp"
#include "driver/intel/level_zero_driver.hpp"
#include "driver/nvml/nvml_driver.hpp"

namespace gtop::driver {

std::vector<Backend> builtin_backends() {
    // Order decides ownership. Two backends can see the same card — the hybrid
    // laptop this was developed on has an NVIDIA GPU that NVML enumerates and
    // that also appears as a DRM node — and the registry keeps whichever claims
    // its PCI address first. NVML is far richer for NVIDIA hardware, so it
    // leads; the same reasoning would put a vendor's own library ahead of any
    // generic reader for its own cards.
    return {
        {core::Vendor::kNvidia, "nvml", &nvml::probe},
        {core::Vendor::kAmd, "amdgpu-sysfs", &amd::probe},
        {core::Vendor::kIntel, "i915-sysfs", &intel::probe},

        // Level Zero works on Linux too, and it reports more than sysfs does —
        // per-engine media activity, real memory-module state. It goes *after*
        // the sysfs backend rather than instead of it because it needs Intel's
        // compute runtime installed, and gtop must report an Intel GPU on a
        // machine that has none of that. Where both are present sysfs keeps the
        // card and this adds only devices sysfs could not see.
        {core::Vendor::kIntel, "level-zero", &intel::level_zero_probe},
    };
}

}  // namespace gtop::driver
