// DynamicLibrary — the parts that carry no OS calls.
//
// Lifetime and the single-name overload live here so posix/ and win32/ only
// have to supply the three operations that genuinely differ: load, resolve,
// unload.

#include "platform/dynamic_library.hpp"

#include <utility>

namespace gtop::platform {

DynamicLibrary::DynamicLibrary(void* handle, std::string name) noexcept
    : handle_(handle), loaded_name_(std::move(name)) {}

DynamicLibrary::DynamicLibrary(DynamicLibrary&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr)),
      loaded_name_(std::move(other.loaded_name_)) {
    other.loaded_name_.clear();
}

DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = std::exchange(other.handle_, nullptr);
        loaded_name_ = std::move(other.loaded_name_);
        other.loaded_name_.clear();
    }
    return *this;
}

DynamicLibrary::~DynamicLibrary() { close(); }

std::optional<DynamicLibrary> DynamicLibrary::open(const char* name) noexcept {
    if (name == nullptr) {
        return std::nullopt;
    }
    const char* const candidates[] = {name};
    return open(std::span<const char* const>(candidates));
}

}  // namespace gtop::platform
