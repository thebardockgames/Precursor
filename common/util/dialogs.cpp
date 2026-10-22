#include "dialogs.h"

#ifdef __SWITCH__
#include "common/log/log.h"
#endif

namespace dialogs {
// char const * const aDialogType , /* "ok" "okcancel" "yesno" "yesnocancel" */
// char const * const aIconType , /* "info" "warning" "error" "question" */
// int const aDefaultButton ) /* 0 for cancel/no , 1 for ok/yes , 2 for no in yesnocancel */
void create_error_message_dialog(const std::string& title, const std::string& message) {
#ifdef __SWITCH__
  // No native dialog concept on Switch. libnx's "error" applet could show a real system dialog
  // here, but that's unimplemented for now - just log it instead of losing the message.
  lg::error("[{}] {}", title, message);
#else
  tinyfd_messageBox(title.data(), message.data(), "ok", "error", 1);
#endif
}
}  // namespace dialogs
