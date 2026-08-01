// TerminalSession — the parts that carry no OS calls.
//
// Ownership transfer clears the moved-from object's restore flags, so console
// state is put back exactly once no matter how the session is passed around.

#include "platform/terminal_setup.hpp"

#include <utility>

namespace gtop::platform {

TerminalSession::TerminalSession(TerminalSession&& other) noexcept
    : capabilities_(other.capabilities_),
      previous_output_mode_(other.previous_output_mode_),
      previous_code_page_(other.previous_code_page_),
      restore_output_mode_(std::exchange(other.restore_output_mode_, false)),
      restore_code_page_(std::exchange(other.restore_code_page_, false)) {}

TerminalSession& TerminalSession::operator=(TerminalSession&& other) noexcept {
    if (this != &other) {
        restore();
        capabilities_ = other.capabilities_;
        previous_output_mode_ = other.previous_output_mode_;
        previous_code_page_ = other.previous_code_page_;
        restore_output_mode_ = std::exchange(other.restore_output_mode_, false);
        restore_code_page_ = std::exchange(other.restore_code_page_, false);
    }
    return *this;
}

TerminalSession::~TerminalSession() { restore(); }

}  // namespace gtop::platform
