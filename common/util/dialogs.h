#pragma once

#include <string>

// tinyfiledialogs has no Switch backend (no native OS dialogs, no zenity/kdialog, and even its
// text-console fallback needs a TTY via termios, which Switch doesn't have either).
#ifndef __SWITCH__
#include "third-party/libtinyfiledialogs/tinyfiledialogs.h"
#endif

namespace dialogs {
void create_error_message_dialog(const std::string& title, const std::string& message);
}
