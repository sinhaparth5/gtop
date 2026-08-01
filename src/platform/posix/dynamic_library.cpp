// DynamicLibrary — POSIX (dlopen / dlsym / dlclose).

#include "platform/dynamic_library.hpp"

#include <dlfcn.h>

#include <cstring>

namespace gtop::platform {

namespace {

// POSIX guarantees dlsym's void* can be converted to a function pointer; ISO
// C++ has no cast that expresses it. memcpy between two same-sized pointer
// objects does, and every compiler folds it away.
static_assert(sizeof(void*) == sizeof(SymbolAddress),
              "platform does not support converting a data pointer to a function pointer");

}  // namespace

std::optional<DynamicLibrary> DynamicLibrary::open(
    std::span<const char* const> candidates) noexcept {
    for (const char* name : candidates) {
        if (name == nullptr) {
            continue;
        }

        // RTLD_LOCAL matters on hybrid machines: NVML, ROCm SMI and Level Zero
        // may all be loaded at once, and they do not have disjoint symbol
        // names. Keeping each one out of the global namespace stops one
        // vendor's library from satisfying another's relocations.
        //
        // RTLD_LAZY because a vendor library resolves plenty of symbols gtop
        // will never call, and some of those would fail on a partial driver
        // install that is otherwise perfectly usable.
        void* handle = ::dlopen(name, RTLD_LAZY | RTLD_LOCAL);
        if (handle != nullptr) {
            return DynamicLibrary(handle, name);
        }

        // Drain the error even though we ignore it. dlerror() latches, so a
        // message left behind here would surface at the next unrelated call.
        static_cast<void>(::dlerror());
    }
    return std::nullopt;
}

SymbolAddress DynamicLibrary::raw_symbol(const char* name) const noexcept {
    if (handle_ == nullptr || name == nullptr) {
        return nullptr;
    }

    static_cast<void>(::dlerror());  // clear, so the null check below is meaningful
    void* address = ::dlsym(handle_, name);
    if (address == nullptr) {
        static_cast<void>(::dlerror());
        return nullptr;  // absent entry point: normal on an older driver
    }

    SymbolAddress fn{};
    std::memcpy(&fn, &address, sizeof fn);
    return fn;
}

void DynamicLibrary::close() noexcept {
    if (handle_ != nullptr) {
        ::dlclose(handle_);
        handle_ = nullptr;
    }
}

}  // namespace gtop::platform
