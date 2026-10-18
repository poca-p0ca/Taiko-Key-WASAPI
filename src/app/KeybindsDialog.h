#pragma once
#include "app/Controller.h"

namespace taiko {
// Modal editor for KeyBind.ini: a sound list and a key list. Keys are assigned by pressing them.
void showKeybinds(HWND owner, Controller& app);
} // namespace taiko
