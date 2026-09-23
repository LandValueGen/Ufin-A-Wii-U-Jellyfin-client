// Wii U software keyboard (nn::swkbd) as a single blocking call.
//
// swkbd draws itself with GX2, so -- exactly like video playback -- the
// caller must shut OSScreen down first and bring it back afterwards
// (OSScreen and GX2 can't drive the display at the same time).
#pragma once
#include <string>

namespace ui {

// Shows the keyboard with `hint` as the placeholder text. Returns true
// and fills `out` (UTF-8, trimmed) when the user confirms a non-empty
// entry; false on cancel, empty input, or if the keyboard couldn't start
// (then `error` says why).
bool promptKeyboard(const char16_t* hint, std::string& out, std::string& error);

} // namespace ui
