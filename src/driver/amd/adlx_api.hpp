#pragma once
//
// ADLX loaded at runtime, plus the ownership helper the rest of the backend
// depends on.
//
// ADLX exposes exactly three entry points worth binding — query the version,
// initialise, terminate — and everything else is reached through vtables on the
// IADLXSystem it hands back. That makes the dynamic-loading surface here far
// smaller than NVML's, and moves the risk somewhere else: lifetime.
//
// Vendored ADLX headers supply the interface declarations. Nothing is linked;
// amdadlx64.dll is opened through platform::DynamicLibrary, so a machine with
// no AMD driver simply gets no devices.
//
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

#include <amd/ADLX.h>
#include <amd/IPerformanceMonitoring.h>
#include <amd/ISystem.h>

#include "platform/dynamic_library.hpp"

namespace gtop::driver::amd {

// Owning pointer for an ADLX interface.
//
// ADLX objects are reference counted, and every method that returns one has
// already taken a reference on the caller's behalf — so the caller owes a
// Release, on every path including the error ones. Doing that by hand in a
// function with six early returns is how a monitor that runs for days ends up
// holding thousands of driver objects. This makes the release automatic and
// gives the backend one place where it can go wrong instead of thirty.
template <typename T>
class AdlxPtr {
public:
    AdlxPtr() = default;

    // Adopts a reference that has already been taken. There is deliberately no
    // constructor that takes a raw pointer and *adds* a reference: every ADLX
    // out-parameter arrives pre-referenced, so adopting is the only case that
    // occurs, and offering both would make the wrong one available.
    explicit AdlxPtr(T* adopted) noexcept : pointer_(adopted) {}

    ~AdlxPtr() { reset(); }

    AdlxPtr(AdlxPtr&& other) noexcept : pointer_(std::exchange(other.pointer_, nullptr)) {}
    AdlxPtr& operator=(AdlxPtr&& other) noexcept {
        if (this != &other) {
            reset();
            pointer_ = std::exchange(other.pointer_, nullptr);
        }
        return *this;
    }

    AdlxPtr(const AdlxPtr&) = delete;
    AdlxPtr& operator=(const AdlxPtr&) = delete;

    void reset() noexcept {
        if (pointer_ != nullptr) {
            pointer_->Release();
            pointer_ = nullptr;
        }
    }

    [[nodiscard]] T* get() const noexcept { return pointer_; }
    T* operator->() const noexcept { return pointer_; }
    explicit operator bool() const noexcept { return pointer_ != nullptr; }

    // For passing to an ADLX out-parameter. Asserts nothing is already held,
    // because overwriting a live pointer is precisely the leak this exists to
    // prevent.
    [[nodiscard]] T** receive() noexcept {
        reset();
        return &pointer_;
    }

private:
    T* pointer_{nullptr};
};

struct AdlxApi {
    using QueryFullVersionFn = ADLX_RESULT (*)(adlx_uint64*);
    using InitializeFn = ADLX_RESULT (*)(adlx_uint64, adlx::IADLXSystem**);
    using TerminateFn = ADLX_RESULT (*)();

    QueryFullVersionFn query_version{};
    InitializeFn initialize{};
    TerminateFn terminate{};

    // Not owned: IADLXSystem is a singleton belonging to the library and must
    // not be Released. ADLXTerminate is what tears it down.
    adlx::IADLXSystem* system{nullptr};

    [[nodiscard]] std::string_view library_name() const noexcept {
        return library_ ? library_->loaded_name() : std::string_view{};
    }

    // Opens ADLX, resolves the three entry points and initialises. Null when
    // the DLL is absent, when it is a build without these exports, or when
    // initialisation fails.
    [[nodiscard]] static std::shared_ptr<AdlxApi> load();

    AdlxApi() = default;
    ~AdlxApi();

    AdlxApi(const AdlxApi&) = delete;
    AdlxApi& operator=(const AdlxApi&) = delete;
    AdlxApi(AdlxApi&&) = delete;
    AdlxApi& operator=(AdlxApi&&) = delete;

private:
    std::optional<platform::DynamicLibrary> library_;
    bool initialised_{false};
};

}  // namespace gtop::driver::amd
