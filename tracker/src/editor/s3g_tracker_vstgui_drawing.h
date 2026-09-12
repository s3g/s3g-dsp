#pragma once

#include "s3g/tracker/editor_drawing.h"
#include "vstgui/lib/cfont.h"

namespace VSTGUI { class CDrawContext; }

namespace s3g::tracker::editor {
using FontFactory = VSTGUI::SharedPointer<VSTGUI::CFontDesc> (*)(const std::string&, double);
void drawDisplayList(VSTGUI::CDrawContext& context, const DisplayList& list,
    FontFactory fontFactory = nullptr);
}
