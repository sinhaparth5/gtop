#pragma once
//
// Runtime library loading — the abstraction the whole vendor layer stands on.
//
// gtop never links NVML, ADLX, ROCm SMI or Level Zero. It opens whichever of
// them happen to exist on the machine it is running on, resolves the entry
// points it recognises, and does without the ones it does not. That is what
// makes one binary work on a machine with three GPUs, one GPU, or none.
//
// Two behaviours here are deliberate and load-bearing:
//
//   * open() takes a *list* of candidate names and returns nullopt if none of
//     them loads. A missing library is a fact about the machine, not a failure.
//   * symbol() returns nullptr for an entry point the loaded library does not
//     export. Older drivers lack newer functions; that disables one metric,
//     never a whole backend.
//
// Neither case throws, logs, or aborts. See ROADMAP.md task 1.2.
//
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

namespace gtop::platform {

// A type-erased function pointer.
//
// dlsym() hands back a void*, and ISO C++ does not permit converting an object
// pointer to a function pointer — POSIX requires the conversion to work, but
// the cast itself is outside the standard and -Wpedantic says so. The POSIX
// implementation launders the value through std::memcpy and returns it as this
// type, so the only cast appearing in a header is function-pointer to
// function-pointer, which is well-defined.
using SymbolAddress = void (*)();

class DynamicLibrary {
public:
    // Tries each candidate in order; the first that loads wins. Returns
    // nullopt when none of them is present. Never throws.
    //
    // The ordering is how version fallback is expressed: put the soname you
    // prefer first and the older one after it.
    [[nodiscard]] static std::optional<DynamicLibrary> open(
        std::span<const char* const> candidates) noexcept;

    // Single-name convenience for the common case.
    [[nodiscard]] static std::optional<DynamicLibrary> open(const char* name) noexcept;

    DynamicLibrary(DynamicLibrary&& other) noexcept;
    DynamicLibrary& operator=(DynamicLibrary&& other) noexcept;
    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;
    ~DynamicLibrary();

    // Resolves an entry point, or returns nullptr when the loaded library does
    // not export it.
    //
    // A null return is expected, not exceptional. Resolve once at init, store
    // the pointer, and let a null one disable exactly the metric that needed
    // it — never re-resolve per sample.
    template <typename Fn>
    [[nodiscard]] Fn symbol(const char* name) const noexcept {
        static_assert(std::is_pointer_v<Fn> && std::is_function_v<std::remove_pointer_t<Fn>>,
                      "symbol<Fn>() requires a function pointer type");
        return reinterpret_cast<Fn>(raw_symbol(name));
    }

    // The candidate that actually loaded. Worth surfacing in diagnostics: on a
    // fallback chain, which name won tells you which driver generation is
    // installed.
    [[nodiscard]] std::string_view loaded_name() const noexcept { return loaded_name_; }

    [[nodiscard]] bool valid() const noexcept { return handle_ != nullptr; }

private:
    DynamicLibrary(void* handle, std::string name) noexcept;

    [[nodiscard]] SymbolAddress raw_symbol(const char* name) const noexcept;
    void close() noexcept;

    void* handle_{nullptr};
    std::string loaded_name_;
};

}  // namespace gtop::platform
