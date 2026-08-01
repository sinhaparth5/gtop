// TerminalSession — Win32.
//
// Two console settings have to be changed, and both are process-wide state
// belonging to the console the shell handed us. Leaving either one modified on
// exit would affect the user's next command, so both are saved and restored.

#include "platform/terminal_setup.hpp"

#include <windows.h>

#include <cstdlib>
#include <string_view>

namespace gtop::platform {

namespace {

std::string_view environment(const char* name) {
    const char* value = std::getenv(name);
    return value == nullptr ? std::string_view{} : std::string_view(value);
}

}  // namespace

TerminalSession TerminalSession::initialise() noexcept {
    TerminalSession session;

    HANDLE out = ::GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    // GetConsoleMode fails on a redirected handle, which is exactly how
    // --dump-json runs. That is not an error; it means there is no console to
    // configure and no capabilities to claim.
    if (out != INVALID_HANDLE_VALUE && ::GetConsoleMode(out, &mode) != 0) {
        session.capabilities_.is_tty = true;
        session.previous_output_mode_ = static_cast<std::uint32_t>(mode);

        // Without this, every TrueColor escape is printed literally rather
        // than interpreted. It exists from Windows 10 1511 onward; on anything
        // older the call fails and rendering falls back to no colour.
        if (::SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0) {
            session.restore_output_mode_ = true;
            session.capabilities_.truecolor = true;
        }
    }

    // Without CP_UTF8 the Braille glyphs and box-drawing characters arrive as
    // "?" — the canvas silently renders nothing recognisable.
    const UINT previous_code_page = ::GetConsoleOutputCP();
    if (previous_code_page != 0 && ::SetConsoleOutputCP(CP_UTF8) != 0) {
        session.previous_code_page_ = static_cast<std::uint32_t>(previous_code_page);
        session.restore_code_page_ = true;
        session.capabilities_.utf8 = true;
    }

    session.capabilities_.color_disabled = !environment("NO_COLOR").empty();
    return session;
}

void TerminalSession::restore() noexcept {
    if (restore_output_mode_) {
        HANDLE out = ::GetStdHandle(STD_OUTPUT_HANDLE);
        if (out != INVALID_HANDLE_VALUE) {
            ::SetConsoleMode(out, static_cast<DWORD>(previous_output_mode_));
        }
        restore_output_mode_ = false;
    }
    if (restore_code_page_) {
        ::SetConsoleOutputCP(static_cast<UINT>(previous_code_page_));
        restore_code_page_ = false;
    }
}

}  // namespace gtop::platform
