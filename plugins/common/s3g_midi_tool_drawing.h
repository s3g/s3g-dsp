#pragma once
#include "s3g_feedback_processor_drawing.h"

namespace s3g::portable_gui::midi_drawing {
using namespace feedback_drawing;
using Style = feedback_drawing::Style;
inline Style softTextStyle() { return {}; }
inline Path roundedPath(Rect rect, double radius) {
  Path path;
  if(path.path)path.path->addRoundRect(foundation::rect(rect.origin.x,rect.origin.y,rect.size.width,rect.size.height),radius);
  return path;
}
inline void strokeRoundPath(Path& path) {
  auto* c=current->context;c->saveGlobalState();
  c->setFrameColor(current->stroke);c->setLineWidth(path.width);
  c->setLineStyle(VSTGUI::CLineStyle(VSTGUI::CLineStyle::kLineCapRound,VSTGUI::CLineStyle::kLineJoinRound));
  if(path.path)c->drawGraphicsPath(path.path,VSTGUI::CDrawContext::kPathStroked);
  c->restoreGlobalState();
}
}
