// TerminalSession — POSIX.
//
// Nothing needs enabling here: a POSIX terminal that understands the escapes
// honours them without being asked. This is detection only, and restore() is a
// no-op because no global console state was changed.

#include "platform/terminal_setup.hpp"

#include <unistd.h>

#include <cstdlib>
#include <string_view>

namespace gtop::platform {

namespace {

std::string_view environment(const char* name) {
    const char* value = std::getenv(name);
    return value == nullptr ? std::string_view{} : std::string_view(value);
}

bool contains_ignoring_case(std::string_view haystack, std::string_view needle) {
    if (needle.size() > haystack.size()) {
        return false;
    }
    const auto lower = [](char c) {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    };
    for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        std::size_t j = 0;
        while (j < needle.size() && lower(haystack[i + j]) == lower(needle[j])) {
            ++j;
        }
        if (j == needle.size()) {
            return true;
        }
    }
    return false;
}

bool detect_truecolor() {
    const std::string_view colorterm = environment("COLORTERM");
    if (contains_ignoring_case(colorterm, "truecolor") ||
        contains_ignoring_case(colorterm, "24bit")) {
        return true;
    }
    // Some multiplexers and terminals advertise through TERM instead. Anything
    // not matched here falls back to the 256-colour path, which is a downgrade
    // in fidelity, not a failure.
    return contains_ignoring_case(environment("TERM"), "direct");
}

bool detect_utf8() {
    // Most specific wins, matching the locale precedence in POSIX itself.
    for (const char* name : {"LC_ALL", "LC_CTYPE", "LANG"}) {
        const std::string_view value = environment(name);
        if (value.empty()) {
            continue;
        }
        return contains_ignoring_case(value, "utf-8") || contains_ignoring_case(value, "utf8");
    }
    return false;
}

}  // namespace

TerminalSession TerminalSession::initialise() noexcept {
    TerminalSession session;
    session.capabilities_.is_tty = ::isatty(STDOUT_FILENO) == 1;
    session.capabilities_.truecolor = detect_truecolor();
    session.capabilities_.utf8 = detect_utf8();
    // The NO_COLOR convention: set and non-empty means disable colour. An
    // empty value is explicitly *not* a request to disable it.
    session.capabilities_.color_disabled = !environment("NO_COLOR").empty();
    return session;
}

// No global console state was changed, so there is nothing to put back.
void TerminalSession::restore() noexcept {}

}  // namespace gtop::platform
