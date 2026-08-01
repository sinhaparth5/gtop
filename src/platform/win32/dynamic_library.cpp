// DynamicLibrary — Win32 (LoadLibraryExW / GetProcAddress / FreeLibrary).

#include "platform/dynamic_library.hpp"

#include <windows.h>

#include <string>

namespace gtop::platform {

namespace {

// Candidate names are ASCII sonames and DLL filenames, so a widening copy is
// enough — no code page conversion is involved.
std::wstring widen_ascii(const char* text) {
    std::wstring wide;
    for (const char* p = text; *p != '\0'; ++p) {
        wide.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*p)));
    }
    return wide;
}

}  // namespace

std::optional<DynamicLibrary> DynamicLibrary::open(
    std::span<const char* const> candidates) noexcept {
    for (const char* name : candidates) {
        if (name == nullptr) {
            continue;
        }

        // LOAD_LIBRARY_SEARCH_* restricts the search to the system directory
        // and the DLL's own dependency directory. Vendor DLLs live under
        // System32; searching the current working directory as well would let
        // a file dropped next to the binary impersonate nvml.dll.
        HMODULE module = ::LoadLibraryExW(
            widen_ascii(name).c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (module != nullptr) {
            return DynamicLibrary(static_cast<void*>(module), name);
        }
    }
    return std::nullopt;
}

SymbolAddress DynamicLibrary::raw_symbol(const char* name) const noexcept {
    if (handle_ == nullptr || name == nullptr) {
        return nullptr;
    }
    // GetProcAddress already returns a function pointer (FARPROC), so unlike
    // the POSIX path there is no data-to-function conversion to launder.
    FARPROC address = ::GetProcAddress(static_cast<HMODULE>(handle_), name);
    if (address == nullptr) {
        return nullptr;  // absent entry point: normal on an older driver
    }
    return reinterpret_cast<SymbolAddress>(address);
}

void DynamicLibrary::close() noexcept {
    if (handle_ != nullptr) {
        ::FreeLibrary(static_cast<HMODULE>(handle_));
        handle_ = nullptr;
    }
}

}  // namespace gtop::platform
