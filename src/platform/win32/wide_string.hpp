#pragma once
//
// UTF-16 to UTF-8, for the Win32 implementations only.
//
// Every Win32 API this layer touches is the -W variant, and everything above
// the platform layer speaks UTF-8 std::string. This is the seam. It is a
// private header: nothing outside src/platform/win32/ may include it.
//
#include <string>
#include <string_view>

namespace gtop::platform::win32 {

// Returns an empty string on conversion failure rather than signalling — every
// caller here is filling a best-effort display field.
[[nodiscard]] std::string to_utf8(std::wstring_view text);

}  // namespace gtop::platform::win32
