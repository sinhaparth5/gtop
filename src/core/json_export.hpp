#pragma once
//
// JSON serialisation of one sampling pass — what `gtop --dump-json` prints.
//
// This exists in Phase 2 rather than Phase 9 on purpose. It is the CI harness
// (a headless binary that needs no terminal), the mechanism for comparing gtop
// against nvidia-smi/rocm-smi/Task Manager metric by metric, and the debugging
// tool for every vendor backend that comes after it. Building it last would
// mean debugging five backends without it.
//
// Two properties are contractual, because scripts parse this:
//
//   * **Key names are stable.** Renaming one breaks somebody's comparison
//     script silently. Add keys; do not rename them.
//   * **An absent metric is `null`, never `0`.** Same rule as the type layer,
//     for the same reason: "the sensor could not be read" and "the value is
//     zero" are different facts, and JSON has a way to say both.
//
// Output is pretty-printed. It is read by humans at least as often as by jq.
//
#include <string>
#include <vector>

#include "core/types.hpp"

namespace gtop::core {

// Host identity, so a captured dump says which machine it came from. Supplied
// by the caller because core performs no I/O and knows nothing about the OS.
struct DumpMetadata {
    std::string host;
    std::string os;
};

[[nodiscard]] std::string to_json(const std::vector<DeviceReading>& devices,
                                  const DumpMetadata& metadata = {});

}  // namespace gtop::core
