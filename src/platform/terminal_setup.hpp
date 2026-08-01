#pragma once
//
// Terminal capability detection and console setup.
//
// On Linux this is almost entirely detection. On Windows it is setup, and
// skipping it is not subtle: without ENABLE_VIRTUAL_TERMINAL_PROCESSING every
// TrueColor escape prints as literal garbage like "[38;2;136;192;208m", and
// without SetConsoleOutputCP(CP_UTF8) every Braille glyph prints as "?".
//
// TerminalSession is RAII because both of those are process-wide console
// settings that outlive the program if it exits without restoring them,
// leaving the user's shell in a state they did not choose.
//
// Note what is *not* here: raw mode, SIGWINCH, and the input loop belong to
// FTXUI, which owns the screen from Phase 6 onward. This type only arranges
// for what FTXUI writes to be rendered correctly.
//
#include <cstdint>

namespace gtop::platform {

struct TerminalCapabilities {
    bool is_tty{false};        // false when piped — --dump-json's normal case
    bool truecolor{false};     // 24-bit escapes are honoured
    bool utf8{false};          // Braille and box-drawing will render
    bool color_disabled{false};  // NO_COLOR is set; honour it over everything
};

class TerminalSession {
public:
    // Applies whatever setup the platform needs and reports what the terminal
    // can do. Never fails: a terminal that supports nothing yields an
    // all-false capability set, and rendering degrades accordingly.
    [[nodiscard]] static TerminalSession initialise() noexcept;

    TerminalSession(TerminalSession&& other) noexcept;
    TerminalSession& operator=(TerminalSession&& other) noexcept;
    TerminalSession(const TerminalSession&) = delete;
    TerminalSession& operator=(const TerminalSession&) = delete;
    ~TerminalSession();

    [[nodiscard]] const TerminalCapabilities& capabilities() const noexcept {
        return capabilities_;
    }

private:
    TerminalSession() = default;

    void restore() noexcept;

    TerminalCapabilities capabilities_{};

    // Saved console state. Plain integers so no Windows type reaches a header
    // that portable code includes; unused on POSIX.
    std::uint32_t previous_output_mode_{0};
    std::uint32_t previous_code_page_{0};
    bool restore_output_mode_{false};
    bool restore_code_page_{false};
};

}  // namespace gtop::platform
