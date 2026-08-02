// Which backends exist on Windows. See driver/posix/backends.cpp for why this
// is a second file rather than a second branch.

#include "driver/driver_registry.hpp"

#include "driver/amd/adlx_driver.hpp"
#include "driver/intel/level_zero_driver.hpp"
#include "driver/nvml/nvml_driver.hpp"

namespace gtop::driver {

std::vector<Backend> builtin_backends() {
    // NVML is the same implementation as on Linux — only the library filename
    // differs, and that is a candidate list inside nvml_api.cpp.
    //
    // Level Zero is the whole Intel story here: Windows has no sysfs, so unlike
    // Linux there is no dependency-free fallback beneath it. A machine with an
    // Intel GPU but no compute runtime installed therefore reports no Intel
    // device, which is a real gap rather than an oversight.
    //
    // ADLX has no Linux counterpart in this file's twin, where amdgpu sysfs
    // covers the vendor without a library at all.
    return {
        {core::Vendor::kNvidia, "nvml", &nvml::probe},
        {core::Vendor::kAmd, "adlx", &amd::adlx_probe},
        {core::Vendor::kIntel, "level-zero", &intel::level_zero_probe},
    };
}

}  // namespace gtop::driver
