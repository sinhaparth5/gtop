#include "platform/win32/wide_string.hpp"

#include <windows.h>

namespace gtop::platform::win32 {

std::string to_utf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }

    const int wide_length = static_cast<int>(text.size());
    const int needed = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), wide_length,
                                             nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return {};
    }

    std::string out(static_cast<std::size_t>(needed), '\0');
    const int written = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), wide_length,
                                              out.data(), needed, nullptr, nullptr);
    if (written <= 0) {
        return {};
    }
    out.resize(static_cast<std::size_t>(written));
    return out;
}

}  // namespace gtop::platform::win32
