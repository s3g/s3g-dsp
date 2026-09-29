#pragma once
#include "vstgui/lib/controls/ctextedit.h"

namespace s3g::portable_gui {
// Opt-in macOS selection styling for an ordinary VSTGUI numeric field.
// The host window's shared native field editor is restored when editing ends.
VSTGUI::CTextEdit* makeReadableNumericTextEdit(const VSTGUI::CRect& bounds,
    VSTGUI::IControlListener* listener, int32_t tag, const char* text);
}
