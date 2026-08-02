// Binding ADLX at runtime. See adlx_api.hpp.

#include "driver/amd/adlx_api.hpp"

#include <array>
#include <span>
#include <utility>

namespace gtop::driver::amd {
namespace {

// amdadlx64.dll ships with the AMD display driver and lives in System32, so the
// default search order finds it. atiadlxx.dll is the older ADL library, kept
// here because some driver branches install it alongside and it exports the
// same three entry points; a build that has neither is a machine with no AMD
// driver, which is not an error.
//
// The roadmap flags ADLX entry-point names as more volatile than NVML's. They
// are not, in fact — these three have been stable since ADLX shipped. What is
// volatile is everything *behind* them, which is why the version is checked.
constexpr std::array<const char*, 2> kCandidates{
    "amdadlx64.dll",
    "atiadlxx.dll",
};

}  // namespace

AdlxApi::~AdlxApi() {
    if (initialised_ && terminate != nullptr) {
        (void)terminate();
    }
}

std::shared_ptr<AdlxApi> AdlxApi::load() {
    std::optional<platform::DynamicLibrary> library =
        platform::DynamicLibrary::open(std::span<const char* const>(kCandidates));
    if (!library) {
        return nullptr;
    }

    auto api = std::make_shared<AdlxApi>();

    api->query_version =
        library->symbol<QueryFullVersionFn>(ADLX_QUERY_FULL_VERSION_FUNCTION_NAME);
    api->initialize = library->symbol<InitializeFn>(ADLX_INIT_FUNCTION_NAME);
    api->terminate = library->symbol<TerminateFn>(ADLX_TERMINATE_FUNCTION_NAME);

    if (api->query_version == nullptr || api->initialize == nullptr ||
        api->terminate == nullptr) {
        // An older atiadlxx.dll with the ADL entry points but not the ADLX
        // ones. Nothing here can use it.
        return nullptr;
    }

    // Asked before initialising, as a check that this really is ADLX rather
    // than a DLL that happens to export the three names. A zero version means
    // it is not.
    adlx_uint64 version = 0;
    if (api->query_version(&version) != ADLX_OK || version == 0) {
        return nullptr;
    }

    // ADLXInitialize takes the version the *caller* was compiled against, not
    // the one the library reports, and the library uses it to decide which
    // interface revisions to hand back. Passing the version of the vendored
    // headers is what makes the vtable layouts this code was compiled against
    // the ones it actually receives — passing the runtime's version instead
    // would ask a newer library for newer interfaces than these headers
    // describe.
    if (api->initialize(ADLX_FULL_VERSION, &api->system) != ADLX_OK ||
        api->system == nullptr) {
        return nullptr;
    }

    api->library_ = std::move(library);
    api->initialised_ = true;
    return api;
}

}  // namespace gtop::driver::amd
